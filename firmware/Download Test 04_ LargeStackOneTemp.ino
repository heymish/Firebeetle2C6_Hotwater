#include <Arduino.h>
#include <Zigbee.h>

// HotWater Zigbee isolation test 04
// Purpose:
//   Same as official-style one-temperature endpoint test, but with a larger
//   Arduino loop task stack. Recent arduino-esp32 Zigbee examples increased
//   stack memory, and a hang/watchdog during Zigbee.begin() with endpoints
//   can be caused by insufficient loop task stack.
//
// Required Arduino IDE settings:
//   Tools -> Board -> ESP32C6 Dev Module or DFRobot FireBeetle 2 ESP32-C6
//   Tools -> Zigbee Mode -> Zigbee ED
//   Tools -> Partition Scheme -> Zigbee 4MB with spiffs
//   Tools -> Erase All Flash -> Enabled for first upload

// Important: must be before setup() and before the loop task is created.
// Your main project used 16 KB. This test uses 64 KB to prove/disprove stack pressure.
SET_LOOP_TASK_STACK_SIZE(64 * 1024);

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> Zigbee ED (End Device)"
#endif

#define TEMP_SENSOR_ENDPOINT_NUMBER 10

ZigbeeTempSensor zbTempSensor(TEMP_SENSOR_ENDPOINT_NUMBER);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Boot Test 04 large loop stack temp endpoint");
  Serial.print("Reset reason=");
  Serial.println((int)esp_reset_reason());
  Serial.print("Wakeup cause=");
  Serial.println((int)esp_sleep_get_wakeup_cause());
  Serial.print("Board=");
  Serial.println(ARDUINO_BOARD);
  Serial.print("SDK=");
  Serial.println(ESP.getSdkVersion());
  Serial.print("Free heap=");
  Serial.println(ESP.getFreeHeap());
  Serial.print("Min heap=");
  Serial.println(ESP.getMinFreeHeap());

  Serial.println("Before setManufacturerAndModel");
  Serial.print("setManufacturerAndModel=");
  Serial.println(zbTempSensor.setManufacturerAndModel("DIY", "FB2C6T1_STACK64") ? 1 : 0);

  Serial.println("Before setMinMaxValue");
  Serial.print("setMinMaxValue=");
  Serial.println(zbTempSensor.setMinMaxValue(-20, 120) ? 1 : 0);

  Serial.println("Before setDefaultValue");
  Serial.print("setDefaultValue=");
  Serial.println(zbTempSensor.setDefaultValue(20.0) ? 1 : 0);

  Serial.println("Before setTolerance");
  Serial.print("setTolerance=");
  Serial.println(zbTempSensor.setTolerance(0.5) ? 1 : 0);

  Serial.println("Before addEndpoint");
  Zigbee.addEndpoint(&zbTempSensor);
  Serial.println("After addEndpoint");

  Zigbee.setDebugMode(true);

  Serial.println("Before Zigbee.begin");
  Serial.flush();

  bool ok = Zigbee.begin();

  Serial.println("After Zigbee.begin");
  Serial.print("Result=");
  Serial.println(ok ? 1 : 0);
  Serial.flush();

  if (!ok) {
    Serial.println("Zigbee.begin returned false");
    while (true) delay(1000);
  }

  Serial.println("Waiting for Zigbee.connected(), max 120 seconds");
  uint32_t connectedWait = millis();
  while (!Zigbee.connected()) {
    delay(250);
    Serial.print(".");
    if (millis() - connectedWait > 120000UL) {
      Serial.println();
      Serial.println("Timeout waiting for Zigbee.connected()");
      break;
    }
  }

  if (Zigbee.connected()) {
    Serial.println();
    Serial.println("Zigbee connected");
    Serial.print("setReporting=");
    Serial.println(zbTempSensor.setReporting(1, 0, 1) ? 1 : 0);
    Serial.print("setTemperature=");
    Serial.println(zbTempSensor.setTemperature(20.0) ? 1 : 0);
    Serial.print("reportTemperature=");
    Serial.println(zbTempSensor.reportTemperature() ? 1 : 0);
  }

  Serial.println("Setup complete. Staying awake.");
}

void loop() {
  static uint32_t lastReport = 0;

  if (Zigbee.connected() && millis() - lastReport > 30000UL) {
    lastReport = millis();
    Serial.println("Periodic test report");
    zbTempSensor.setTemperature(20.0);
    zbTempSensor.reportTemperature();
  }

  delay(1000);
}
