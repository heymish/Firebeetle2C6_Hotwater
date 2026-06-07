# Firebeetle2C6_Hotwater
This project is to measure the Water entering a Wetback on a fire and the water exiting. There is a third temperature probe to measure the Hot water tank temperature.
It is meant to connect via Zigbee2mqtt -> HomeAssistant.

A Project using the Arduino IDE to program an ESP32-C6 based Firebeetle https://wiki.dfrobot.com/dfr1075/

In the Arduino IDE, 

Tools: 
Partition Scheme: Zigbee 4MB with spiffs
Zigbee Mode: Zigbee ED (end device)

Tools -> Manage Libraries:
OneWire (2.3.8) <- Has issue with ESP32 3.X.X -> https://forum.arduino.cc/t/onewire-library-esp32/1266351/4
DallasTemperature (4.0.6)

Tools -> Board -> Boards Manager:
ESP32 (3.3.8)

3 x DS18B20 Temperature sensors to measure the fireplace Wetback performance. Connect the Temperature sensors via GPIO pin 4. This is set via ONE_WIRE_GPIO.

