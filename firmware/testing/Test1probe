#include <Arduino.h>
#include <Zigbee.h>

// Next Zigbee isolation test for FireBeetle 2 ESP32-C6
// Purpose:
//   Starts Zigbee with three temperature endpoints only.
//   No DS18B20, no battery, no Preferences, no Analog endpoints, no deep sleep.
//
// Expected serial output if this test passes:
//   Boot
//   Reset reason=<n>
//   Board=<board name>
//   SDK=<sdk version>
//   Free heap=<bytes>
//   Configuring temp endpoints
//   Adding endpoints
//   Creating Zigbee config
//   Before begin
//   After begin
//   Result=1
//   Waiting for Zigbee.started()
//   Zigbee stack started
//
// If it hangs at "Before begin", then one of the temperature endpoints is enough
// to trigger the Zigbee.begin() watchdog issue.

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> Zigbee ED (End Device)"
#endif

#define DEBUG_LOG 1

#if DEBUG_LOG
  #define DBG_BEGIN(baud)      do { Serial.begin(baud); delay(1000); } while (0)
  #define DBG_PRINT(x)         Serial.print(x)
  #define DBG_PRINTLN(x)       Serial.println(x)
  #define DBG_PRINTF(...)      Serial.printf(__VA_ARGS__)
  #define DBG_FLUSH()          Serial.flush()
#else
  #define DBG_BEGIN(baud)      do {} while (0)
  #define DBG_PRINT(x)         do {} while (0)
  #define DBG_PRINTLN(x)       do {} while (0)
  #define DBG_PRINTF(...)      do {} while (0)
  #define DBG_FLUSH()          do {} while (0)
#endif

static const char* ZB_MANUFACTURER = "DIY";
static const char* ZB_MODEL        = "FB2C6T3_TEST";

static const uint8_t EP_TEMP1 = 1;
static const uint8_t EP_TEMP2 = 2;
static const uint8_t EP_TEMP3 = 3;

ZigbeeTempSensor zbTemp1(EP_TEMP1);
ZigbeeTempSensor zbTemp2(EP_TEMP2);
ZigbeeTempSensor zbTemp3(EP_TEMP3);

void setup() {
  DBG_BEGIN(115200);

  DBG_PRINTLN("Boot");
  DBG_PRINT("Reset reason=");
  DBG_PRINTLN((int)esp_reset_reason());
  DBG_PRINT("Wakeup cause=");
  DBG_PRINTLN((int)esp_sleep_get_wakeup_cause());
  DBG_PRINT("Board=");
  DBG_PRINTLN(ARDUINO_BOARD);
  DBG_PRINT("SDK=");
  DBG_PRINTLN(ESP.getSdkVersion());
  DBG_PRINT("Free heap=");
  DBG_PRINTLN(ESP.getFreeHeap());
  DBG_PRINT("Min heap=");
  DBG_PRINTLN(ESP.getMinFreeHeap());

  DBG_PRINTLN("Configuring temp endpoints");

  if (!zbTemp1.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) {
    DBG_PRINTLN("zbTemp1 setManufacturerAndModel failed");
  }
  if (!zbTemp2.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) {
    DBG_PRINTLN("zbTemp2 setManufacturerAndModel failed");
  }
  if (!zbTemp3.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) {
    DBG_PRINTLN("zbTemp3 setManufacturerAndModel failed");
  }

  zbTemp1.setDefaultValue(20.0f);
  zbTemp2.setDefaultValue(21.0f);
  zbTemp3.setDefaultValue(22.0f);

  DBG_PRINTLN("Adding endpoints");
  Zigbee.addEndpoint(&zbTemp1);
  DBG_PRINTLN("Added temp1");
  Zigbee.addEndpoint(&zbTemp2);
  DBG_PRINTLN("Added temp2");
  Zigbee.addEndpoint(&zbTemp3);
  DBG_PRINTLN("Added temp3");

  Zigbee.setDebugMode(true);

  DBG_PRINTLN("Creating Zigbee config");
  esp_zb_cfg_t cfg = ZIGBEE_DEFAULT_ED_CONFIG();

  DBG_PRINTLN("Before begin");
  DBG_FLUSH();

  bool ok = Zigbee.begin(&cfg, false);

  DBG_PRINTLN("After begin");
  DBG_PRINT("Result=");
  DBG_PRINTLN(ok ? 1 : 0);
  DBG_FLUSH();

  if (!ok) {
    DBG_PRINTLN("Zigbee.begin returned false");
    while (true) {
      delay(1000);
    }
  }

  DBG_PRINTLN("Waiting for Zigbee.started()");
  uint32_t startWaitMs = millis();
  while (!Zigbee.started()) {
    delay(50);
    if ((millis() - startWaitMs) > 30000UL) {
      DBG_PRINTLN("Timeout waiting for Zigbee.started()");
      break;
    }
  }

  if (Zigbee.started()) {
    DBG_PRINTLN("Zigbee stack started");
  }

  DBG_PRINTLN("Waiting for Zigbee.connected() for up to 120 seconds");
  uint32_t joinWaitMs = millis();
  while (!Zigbee.connected()) {
    delay(250);
    if ((millis() - joinWaitMs) > 120000UL) {
      DBG_PRINTLN("Timeout waiting for Zigbee.connected()");
      break;
    }
  }

  if (Zigbee.connected()) {
    DBG_PRINTLN("Zigbee connected");

    DBG_PRINTLN("Publishing test temperatures");
    zbTemp1.setTemperature(20.0f);
    zbTemp1.reportTemperature();

    zbTemp2.setTemperature(21.0f);
    zbTemp2.reportTemperature();

    zbTemp3.setTemperature(22.0f);
    zbTemp3.reportTemperature();
  }

  DBG_PRINTLN("Setup complete. Staying awake for debug.");
}

void loop() {
  static uint32_t lastReport = 0;

  if (Zigbee.connected() && millis() - lastReport > 30000UL) {
    lastReport = millis();

    DBG_PRINTLN("Periodic test report");
    zbTemp1.setTemperature(20.0f);
    zbTemp1.reportTemperature();

    zbTemp2.setTemperature(21.0f);
    zbTemp2.reportTemperature();

    zbTemp3.setTemperature(22.0f);
    zbTemp3.reportTemperature();
  }

  delay(1000);
}
