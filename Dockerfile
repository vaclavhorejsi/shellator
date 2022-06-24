FROM python:slim

RUN apt update -y && apt upgrade -y
RUN apt install -y curl

RUN mkdir /app
WORKDIR /app

RUN python3 -c "$(curl -fsSL https://raw.githubusercontent.com/platformio/platformio/master/scripts/get-platformio.py)"

RUN ln -s ~/.platformio/penv/bin/pio /usr/local/bin/pio

ADD platformio.ini /app

RUN pio pkg install -p "espressif8266"

CMD ["/app/entrypoint.sh"]