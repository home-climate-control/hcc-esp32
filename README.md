# Home Climate Control ESP32 based edge device firmware

Coming up. For now take a look at [hcc-esp8266](https://github.com/home-climate-control/hcc-esp8266).

(Work in progress - the application is being documented *before* it is built.Having a roadmap helps)_

This application connects to WiFi, then to the MQTT broker, introduces itself and starts sending sensor data on MQTT publish topic, and listening to commands on MQTT subscribe topic,  according to [DZ edge device MQTT protocol](https://github.com/home-climate-control/dz/issues/113).

All configuration parameteres are selected using `idf.py menuconfiig`.

## How to use this application

### Prerequisites

[Install ESP-IDF Development Framework](https://github.com/espressif/esp-idf#setting-up-esp-idf).

### Hardware Required

This application can be executed on any ESP32 board, the only required interface is WiFi and connection to internet (sensor and actuator hardware requirements coming soon; you can count on 1-Wire being the first).

### Configure the project

* Open the project configuration menu (`idf.py menuconfig`)
* Configure Wi-Fi under "Connectivity Configuration" menu.
* Configure MQTT under "MQTT Configuration" menu.
* When using Make build system, set `Default serial port` under `Serial flasher config`.

### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(To exit the serial monitor, type ``Ctrl-]``.)

## Example MQTT Output

The output is duplicated on the serial console and in MQTT output stream.

```
/hcc/edge {"entity_type":"sensor","device_id":"ESP32-246F28A7C53C","sources":["D90301A2792B0528","E40300A27970F728"]}
/hcc/sensor/D90301A2792B0528 {"entity_type":"sensor","name":"D90301A2792B0528","signature":"TD90301A2792B0528","signal":24.625,"device_id":"ESP32-246F28A7C53C"}
/hcc/sensor/E40300A27970F728 {"entity_type":"sensor","name":"E40300A27970F728","signature":"TE40300A27970F728","signal":26.1875,"device_id":"ESP32-246F28A7C53C"}
```
