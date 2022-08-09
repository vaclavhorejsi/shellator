# **Shellator**

Custom Shelly firmware

**!!!Use at your own risk!!! If you are not sure how to use it, it is definitely not for you!!!**

## **Prerequisities**
* Shelly 1, Shelly i3 or Shelly 2.5
* MQTT broker

## **Configuration**
* Copy ```src/config.h.example``` to ```src/config.h```
* Edit ```src/config.h``` for your needs

## **Instalation**
* Compile in VSC with Platformio or Arduino IDE for ESP8266
* Upload over cable or OTA from VSC

or

* ```docker build -t shellator .```
* ```docker run -ti --volume $PWD:/app shellator```

## **OTA Upload**
* TODO

## **MQTT Topic structure**
* TODO

## **TODO**
* Blink
* Status message random start
* Anouncement every 24 hours?
* Enhance S25 with power consumtion meter
* Add REST API along MQTT
* Add fallback script what to do when there is no MQTT connection


## **Changelog**
* 1.6.0
  * Added bssid into status message
* 1.5.0
  * Build in docker container
* 1.4.0
  * Merged S1, Si3 and S25 code into one
  * Added /cmd/reconnect - restart wifi connection