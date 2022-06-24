#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>

#include <TaskScheduler.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include <OneButton.h>

#include "config.h"

#define FW "1.5.0"

#if defined SHELLY1
  #define HW "Shelly 1"
  #define HWPREFIX "1"

  #define PIN_RELAY1 4
  #define PIN_BTN1 5
  #define PIN_BTN2 0

  OneButton btn1 = OneButton(PIN_BTN1, false, false);
  OneButton btn2 = OneButton(PIN_BTN2, true, true);

#elif defined SHELLYI3
  #define HW "Shelly i3"
  #define HWPREFIX "i3"

  #define PIN_BTN1 14
  #define PIN_BTN2 12
  #define PIN_BTN3 13

  OneButton btn1 = OneButton(PIN_BTN1, false, false);
  OneButton btn2 = OneButton(PIN_BTN2, false, false);
  OneButton btn3 = OneButton(PIN_BTN3, false, false);

#elif defined SHELLY25
  #define HW "Shelly 25"
  #define HWPREFIX "25"

  #define PIN_RELAY1 4
  #define PIN_RELAY2 15
  #define PIN_BTN1 13
  #define PIN_BTN2 5

  OneButton btn1 = OneButton(PIN_BTN1, false, false);
  OneButton btn2 = OneButton(PIN_BTN2, false, false);
#endif

String chipId = String(ESP.getChipId(), HEX);
String mqttPrefix = MQTT_PREFIX + chipId;

ESP8266WiFiMulti wifiMulti;
WiFiClient wifiClient;
MQTTClient mqttClient(1024);
Scheduler ts;

void setupWiFi() {
  if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.hostname(String(OTA_NAME) + String(HWPREFIX) + "-" + chipId);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASS);
}

uint32_t uptime;
void uptimeHandle();
Task taskUptime(1000L, TASK_FOREVER, &uptimeHandle, &ts);

void uptimeHandle() {
  uptime ++;
}

void setupOTA() {
  char name[50];
  String(String(OTA_NAME) + String(HWPREFIX) + "-" + chipId).toCharArray(name, sizeof(name));

  MDNS.setInstanceName(name);  
  ArduinoOTA.setHostname(name);
  ArduinoOTA.begin();
}

uint32_t ntpGetWalltime();
void ntpSyncStartCb();
void ntpTickCb();
void ntpSyncFinishCb();

Task taskNtpSync(1000L, TASK_FOREVER, &ntpSyncStartCb, &ts);
Task taskNtpTick(1000L, TASK_FOREVER, &ntpTickCb, &ts);

WiFiUDP ntpUdp;
IPAddress ntpServerIP;

#define TIME_UNKNOWN 0xffffffffUL
uint32_t currentTime;
uint32_t lastTimeSync;

#define NTP_PACKET_SIZE 48
byte ntpBuffer[NTP_PACKET_SIZE];

void setupNtp() {
  currentTime = TIME_UNKNOWN;
  lastTimeSync = 0;
  ntpUdp.begin(2390);

  taskNtpSync.enable();
  taskNtpTick.enable();
}

void ntpTickCb() {
  if (currentTime == TIME_UNKNOWN) return;

  currentTime++;

  setTime(ntpGetWalltime());
}

uint32_t ntpGetWalltime() {
  if (currentTime == TIME_UNKNOWN) return TIME_UNKNOWN;

  return currentTime;
}

void ntpSyncStartCb() {
  if (WiFi.status() != WL_CONNECTED) {
    taskNtpSync.setCallback(&ntpSyncStartCb);
    taskNtpSync.setInterval(1000L);
    taskNtpSync.enableDelayed(0); // clears run counter
    return;
  }
  if (!WiFi.hostByName(NTP_SERVER, ntpServerIP)) {
    return;
  }

  // set all bytes in the buffer to 0
  memset(ntpBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  ntpBuffer[0] = 0b11100011;   // LI, Version, Mode
  ntpBuffer[1] = 0;     // Stratum, or type of clock
  ntpBuffer[2] = 6;     // Polling Interval
  ntpBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  ntpBuffer[12]  = 49;
  ntpBuffer[13]  = 0x4E;
  ntpBuffer[14]  = 49;
  ntpBuffer[15]  = 52;

  ntpUdp.beginPacket(ntpServerIP, 123);
  ntpUdp.write(ntpBuffer, NTP_PACKET_SIZE);
  ntpUdp.endPacket();

  taskNtpSync.setCallback(&ntpSyncFinishCb);
  taskNtpSync.setInterval(1000L);
  taskNtpSync.enableDelayed(0); // clears run counter
}

void ntpSyncFinishCb() {
  int success = ntpUdp.parsePacket() >= NTP_PACKET_SIZE;
  if (success) {
    ntpUdp.read(ntpBuffer, NTP_PACKET_SIZE);

    unsigned long highWord = word(ntpBuffer[40], ntpBuffer[41]);
    unsigned long lowWord = word(ntpBuffer[42], ntpBuffer[43]);
    // Unix time starts on Jan 1 1970. NTP starts in 1900, difference in seconds is 2208988800:
    currentTime = ((highWord << 16) | lowWord) - 2208988800UL;
    lastTimeSync = millis();
  }

  // wait for ntp response five seconds and if this fails, wait one minute and try sync again
  if (success || taskNtpSync.getRunCounter() >= 5) {
    taskNtpSync.setCallback(&ntpSyncStartCb);
    taskNtpSync.setInterval(600000L);
    taskNtpSync.enableDelayed(0); // clears run counter
  }
}

void connectMQTT();
void mqttPing();

Task taskMqttPing(1000L, TASK_FOREVER, &mqttPing, &ts);
Task taskMqttHandle(50L, TASK_FOREVER, &connectMQTT, &ts);

DynamicJsonDocument storage(1024);

void mqttMessage(String &topic, String &payload) {
//  DynamicJsonDocument doc(4096);
//  deserializeJson(doc, payload);

#if defined SHELLY1 || defined SHELLY25
  if (topic == (mqttPrefix + "/cmd/relay1")) {
    if (payload == "on") {
      digitalWrite(PIN_RELAY1, 1);
    } else if (payload == "off") {
      digitalWrite(PIN_RELAY1, 0);
    } else if (payload == "toggle") {
      digitalWrite(PIN_RELAY1, !digitalRead(PIN_RELAY1));
    } else {
      digitalWrite(PIN_RELAY1, 0);
    }

    mqttClient.publish(mqttPrefix + "/relay1", "{\"power\":\"" + String(digitalRead(PIN_RELAY1)?"on":"off") + "\"}");
  }
#endif

#if defined SHELLY25
  if (topic == (mqttPrefix + "/cmd/relay2")) {
    if (payload == "on") {
      digitalWrite(PIN_RELAY2, 1);
    } else if (payload == "off") {
      digitalWrite(PIN_RELAY2, 0);
    } else if (payload == "toggle") {
      digitalWrite(PIN_RELAY2, !digitalRead(PIN_RELAY2));
    } else {
      digitalWrite(PIN_RELAY2, 0);
    }

    mqttClient.publish(mqttPrefix + "/relay2", "{\"power\":\"" + String(digitalRead(PIN_RELAY2)?"on":"off") + "\"}");
  }
#endif
  
  if (topic == (mqttPrefix + "/cmd/store")) {
    deserializeJson(storage, payload);
  }

  if (topic == (mqttPrefix + "/cmd/reconnect")) {
    WiFi.reconnect();
  }
}

void systemStatus() {
  DynamicJsonDocument doc(256);

  JsonObject net = doc.createNestedObject("net");
  net["ip"] = WiFi.localIP().toString();
  net["mask"] = WiFi.subnetMask().toString();
  net["gateway"] = WiFi.gatewayIP().toString();
  net["dns"] = WiFi.dnsIP().toString();
  net["mac"] = WiFi.macAddress();
  JsonObject wifi = doc.createNestedObject("wifi");
  wifi["ssid"] = WiFi.SSID();
  wifi["rssi"] = WiFi.RSSI();

  String output;
  serializeJson(doc, output);
  mqttClient.publish(mqttPrefix + "/connection", output, true, 0);

  DynamicJsonDocument doc2(256);
  if (ntpGetWalltime() != TIME_UNKNOWN) {
    doc2["time"] = ntpGetWalltime();
  } else {
    doc2["time"] = 0;
  }
  doc2["uptime"] = uptime;
  doc2["hw"] = HW;
  doc2["fw"] = FW;
  if (storage.size() > 0)
    doc2["storage"] = storage;

  output = "";
  serializeJson(doc2, output);
  mqttClient.publish(mqttPrefix + "/status", output, true, 0);

#if defined SHELLY1 || defined SHELLY25
  mqttClient.publish(mqttPrefix + "/relay1", "{\"power\":\"" + String(digitalRead(PIN_RELAY1)?"on":"off") + "\"}");
#endif
#if defined SHELLY25
  mqttClient.publish(mqttPrefix + "/relay2", "{\"power\":\"" + String(digitalRead(PIN_RELAY2)?"on":"off") + "\"}");
#endif
}

void mqttPing() {
  if (second() == 0 || second() == 10 || second() == 20 || second() == 30 || second() == 40 || second() == 50) {
    systemStatus();
  }
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!mqttClient.connected()) {
    String clientId = String(MQTT_CLIENT_ID) + "-" + chipId;
    char cClientId[50];
    clientId.toCharArray(cClientId, sizeof(cClientId));
    if (mqttClient.connect(cClientId, MQTT_USER, MQTT_PASS)) {
#if defined SHELLY1 || defined SHELLY25
      mqttClient.subscribe(mqttPrefix + "/cmd/relay1");
#endif
#if defined SHELLY25
      mqttClient.subscribe(mqttPrefix + "/cmd/relay2");
#endif
      mqttClient.subscribe(mqttPrefix + "/cmd/store");
      mqttClient.publish(mqttPrefix, "online", true, 0);  
      mqttClient.publish(mqttPrefix + "/announcement", "{\"hello\":\"world\"}", false, 0);
      systemStatus();    
    }
  }
}

void setupMQTT() {
  char mqttPrefixChr[50];
  mqttPrefix.toCharArray(mqttPrefixChr, sizeof(mqttPrefixChr));
  mqttClient.setWill(mqttPrefixChr, "offline", true, 0);
  mqttClient.begin(MQTT_NAME, MQTT_PORT, wifiClient);
  mqttClient.onMessage(mqttMessage);

  taskMqttHandle.enable();
  taskMqttPing.enable();
}

#if defined SHELLY1 || defined SHELLY25 || defined SHELLYI3
void handleBtn1Click() {
  mqttClient.publish(mqttPrefix + "/button1", "{\"clicked\":\"single\"}");
  #if defined SHELLY1 || defined SHELLY25
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) {
    digitalWrite(PIN_RELAY1, !digitalRead(PIN_RELAY1));
  }
  #endif
}

void handleBtn1DoubleClick() {
  mqttClient.publish(mqttPrefix + "/button1", "{\"clicked\":\"double\"}");
}

void handleBtn1LongPress() {
  mqttClient.publish(mqttPrefix + "/button1", "{\"clicked\":\"long\"}");
}

void handleBtn1MultiClick() {
  if (btn1.getNumberClicks() == 3) {
    mqttClient.publish(mqttPrefix + "/button1", "{\"clicked\":\"triple\"}");
  } else if (btn1.getNumberClicks() == 4) {
    mqttClient.publish(mqttPrefix + "/button1", "{\"clicked\":\"quadruple\"}");
  } else {
    mqttClient.publish(mqttPrefix + "/button1", "{\"clicked\":\"many\"}");
  }
}
#endif

#if defined SHELLY25 || defined SHELLYI3
void handleBtn2Click() {
  mqttClient.publish(mqttPrefix + "/button2", "{\"clicked\":\"single\"}");
  #if defined SHELLY25
  if (WiFi.status() != WL_CONNECTED || !mqttClient.connected()) {
    digitalWrite(PIN_RELAY2, !digitalRead(PIN_RELAY2));
  }
  #endif
}

void handleBtn2DoubleClick() {
  mqttClient.publish(mqttPrefix + "/button2", "{\"clicked\":\"double\"}");
}

void handleBtn2LongPress() {
  mqttClient.publish(mqttPrefix + "/button2", "{\"clicked\":\"long\"}");
}

void handleBtn2MultiClick() {
  if (btn2.getNumberClicks() == 3) {
    mqttClient.publish(mqttPrefix + "/button2", "{\"clicked\":\"triple\"}");
  } else if (btn2.getNumberClicks() == 4) {
    mqttClient.publish(mqttPrefix + "/button2", "{\"clicked\":\"quadruple\"}");
  } else {
    mqttClient.publish(mqttPrefix + "/button2", "{\"clicked\":\"many\"}");
  }
}
#endif

#if defined SHELLYI3
void handleBtn3Click() {
  mqttClient.publish(mqttPrefix + "/button3", "{\"clicked\":\"single\"}");
}

void handleBtn3DoubleClick() {
  mqttClient.publish(mqttPrefix + "/button3", "{\"clicked\":\"double\"}");
}

void handleBtn3LongPress() {
  mqttClient.publish(mqttPrefix + "/button3", "{\"clicked\":\"long\"}");
}

void handleBtn3MultiClick() {
  if (btn2.getNumberClicks() == 3) {
    mqttClient.publish(mqttPrefix + "/button3", "{\"clicked\":\"triple\"}");
  } else if (btn2.getNumberClicks() == 4) {
    mqttClient.publish(mqttPrefix + "/button3", "{\"clicked\":\"quadruple\"}");
  } else {
    mqttClient.publish(mqttPrefix + "/button3", "{\"clicked\":\"many\"}");
  }
}
#endif

void setup() {
  delay(100);

#if defined SHELLY1 || defined SHELLY25
  pinMode(PIN_RELAY1, OUTPUT);
#endif
#if defined SHELLY25
  pinMode(PIN_RELAY2, OUTPUT);
#endif

  taskUptime.enable();

  setupWiFi();
  setupOTA();
  setupNtp();
  setupMQTT();

#if defined SHELLY1 || defined SHELLY25 || defined SHELLYI3
  btn1.setClickTicks(300);
  btn1.attachClick(handleBtn1Click);
  btn1.attachDoubleClick(handleBtn1DoubleClick);
  btn1.attachLongPressStart(handleBtn1LongPress);
  btn1.attachMultiClick(handleBtn1MultiClick);
#endif

#if defined SHELLY25 || defined SHELLYI3
  btn2.setClickTicks(300);
  btn2.attachClick(handleBtn2Click);
  btn2.attachDoubleClick(handleBtn2DoubleClick);
  btn2.attachLongPressStart(handleBtn2LongPress);
  btn2.attachMultiClick(handleBtn2MultiClick);
#endif

#if defined SHELLYI3
  btn3.setClickTicks(300);
  btn3.attachClick(handleBtn3Click);
  btn3.attachDoubleClick(handleBtn3DoubleClick);
  btn3.attachLongPressStart(handleBtn3LongPress);
  btn3.attachMultiClick(handleBtn3MultiClick);
#endif
}

void loop() {
  wifiMulti.run();
  ArduinoOTA.handle();
  ts.execute();
  mqttClient.loop();
#if defined SHELLY1 || defined SHELLY25 || defined SHELLYI3
  btn1.tick();
#endif
#if defined SHELLY25 || defined SHELLYI3
  btn2.tick();
#endif
#if defined SHELLYI3
  btn3.tick();
#endif
}