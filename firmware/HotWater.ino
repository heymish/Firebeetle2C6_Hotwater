#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Preferences.h>
#include <Zigbee.h>

// Keep Arduino loopTask comfortable during bring-up
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

#ifndef ZIGBEE_MODE_ED
#error "Select Tools -> Zigbee mode -> Zigbee ED (End Device)"
#endif

// ============================================================
// DEBUG LOGGING CONTROL
// Set to 0 for production / better battery life
// ============================================================
#define DEBUG_LOG 0
#if DEBUG_LOG
  #define DBG_BEGIN(baud)      do { Serial.begin(baud); delay(300); } while (0)
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

// ============================================================
// USER SETTINGS
// ============================================================
static const int ONE_WIRE_GPIO = 4;          // Shared DS18B20 data pin
static const int BAT_ADC_PIN   = 0;          // FireBeetle battery sense ADC0
static const float BAT_DIVIDER = 2.0f;       // FireBeetle battery divider correction

// Sleep interval settings (minutes)
static const uint32_t DEFAULT_SLEEP_MIN = 5;
static const uint32_t MIN_SLEEP_MIN     = 1;
static const uint32_t MAX_SLEEP_MIN     = 360;

// Awake window / reliability tuning
static const uint32_t CONFIG_WINDOW_MS    = 10000; // base awake/config window
static const uint32_t REPORT_FLUSH_MS     = 1500;  // time to allow reports to leave before config window
static const uint32_t MAX_FORCE_AWAKE_SEC = 900;   // 15 minutes max extra awake time

// Keep strings short (Zigbee metadata length limits)
static const char* ZB_MANUFACTURER = "DIY";
static const char* ZB_MODEL        = "FB2C6T3";

// Endpoints
static const uint8_t EP_TEMP1 = 1;
static const uint8_t EP_TEMP2 = 2;
static const uint8_t EP_TEMP3 = 3;
static const uint8_t EP_SLEEP = 4;
static const uint8_t EP_AWAKE = 5;

// Preferences keys (must be short)
static const char* PREF_NS         = "cfg";
static const char* PREF_KEY_SLEEP  = "sleep";
static const char* PREF_KEY_P1     = "p1";
static const char* PREF_KEY_P2     = "p2";
static const char* PREF_KEY_P3     = "p3";

// Optional dev helper: hold BOOT at reset to stay awake and not sleep
static const uint8_t BUTTON_PIN = BOOT_PIN;

// ============================================================
// DS18B20 SETTINGS
// 10-bit gives faster conversion and is usually plenty for ambient sensing
// ============================================================
static const uint8_t DS18_RESOLUTION_BITS = 10;

static uint16_t ds18ConversionMs(uint8_t resolution_bits) {
  switch (resolution_bits) {
    case 9:  return 94;
    case 10: return 188;
    case 11: return 375;
    default: return 750;  // 12-bit
  }
}

static const uint16_t DS18_CONVERSION_MS = 188; // for 10-bit

// ============================================================
// DS18B20 / 1-WIRE
// ============================================================
OneWire oneWire(ONE_WIRE_GPIO);
DallasTemperature dallas(&oneWire);

DeviceAddress probeAddr[3];
bool probeMapped[3] = {false, false, false};

// ============================================================
// Zigbee Endpoints
// ============================================================
ZigbeeTempSensor zbTemp1(EP_TEMP1);
ZigbeeTempSensor zbTemp2(EP_TEMP2);
ZigbeeTempSensor zbTemp3(EP_TEMP3);
ZigbeeAnalog     zbSleep(EP_SLEEP);
ZigbeeAnalog     zbAwake(EP_AWAKE);

// ============================================================
// Persistent / runtime state
// ============================================================
Preferences prefs;
uint32_t sleepMinutes = DEFAULT_SLEEP_MIN;
uint32_t forceAwakeSeconds = 0;
volatile bool sleepValueChanged = false;
volatile bool awakeValueChanged = false;
bool debugNoSleep = false;
uint32_t lastActivityMs = 0;

// ============================================================
// Helpers
// ============================================================
static void printAddress(const uint8_t addr[8]) {
#if DEBUG_LOG
  for (uint8_t i = 0; i < 8; i++) {
    if (addr[i] < 16) Serial.print('0');
    Serial.print(addr[i], HEX);
  }
#else
  (void)addr;
#endif
}

static bool addrEqual(const uint8_t a[8], const uint8_t b[8]) {
  return memcmp(a, b, 8) == 0;
}

static void clearAddress(uint8_t addr[8]) {
  memset(addr, 0, 8);
}

static bool isZeroAddress(const uint8_t addr[8]) {
  for (uint8_t i = 0; i < 8; i++) {
    if (addr[i] != 0) return false;
  }
  return true;
}

static const char* probeKey(uint8_t slot) {
  switch (slot) {
    case 0: return PREF_KEY_P1;
    case 1: return PREF_KEY_P2;
    default: return PREF_KEY_P3;
  }
}

static bool loadProbeAddress(uint8_t slot, uint8_t out[8]) {
  const char* key = probeKey(slot);
  if (!prefs.isKey(key)) {
    clearAddress(out);
    return false;
  }

  size_t len = prefs.getBytesLength(key);
  if (len != 8) {
    clearAddress(out);
    return false;
  }

  prefs.getBytes(key, out, 8);

  if (isZeroAddress(out)) {
    clearAddress(out);
    return false;
  }

  if (!dallas.validAddress(out)) {
    clearAddress(out);
    return false;
  }

  return true;
}

static void saveProbeAddress(uint8_t slot, const uint8_t addr[8]) {
  prefs.putBytes(probeKey(slot), addr, 8);
  memcpy(probeAddr[slot], addr, 8);
  probeMapped[slot] = true;
}

static bool isAddressAlreadyMapped(const uint8_t addr[8]) {
  for (uint8_t i = 0; i < 3; i++) {
    if (probeMapped[i] && addrEqual(probeAddr[i], addr)) {
      return true;
    }
  }
  return false;
}

static void loadProbeMap() {
  for (uint8_t i = 0; i < 3; i++) {
    clearAddress(probeAddr[i]);
    probeMapped[i] = loadProbeAddress(i, probeAddr[i]);
  }
}

// If any probe slot is empty/invalid, learn addresses from the bus and save them.
// Discovery order is used only for initial filling of empty slots.
static void autoMapMissingProbeSlots() {
  bool needMapping = false;
  for (uint8_t i = 0; i < 3; i++) {
    if (!probeMapped[i]) {
      needMapping = true;
      break;
    }
  }

  if (!needMapping) {
    DBG_PRINTLN("Probe map already complete");
    return;
  }

  int count = dallas.getDeviceCount();
  DBG_PRINT("Auto-mapping missing probe slots from ");
  DBG_PRINT(count);
  DBG_PRINTLN(" discovered device(s)");

  for (int discoveredIndex = 0; discoveredIndex < count; discoveredIndex++) {
    uint8_t addr[8];
    if (!dallas.getAddress(addr, discoveredIndex)) continue;
    if (!dallas.validAddress(addr)) continue;
    if (isAddressAlreadyMapped(addr)) continue;

    for (uint8_t slot = 0; slot < 3; slot++) {
      if (!probeMapped[slot]) {
        saveProbeAddress(slot, addr);
        DBG_PRINT("Mapped Probe ");
        DBG_PRINT(slot + 1);
        DBG_PRINT(" -> ");
        printAddress(addr);
        DBG_PRINTLN("");
        break;
      }
    }
  }

  for (uint8_t i = 0; i < 3; i++) {
    if (!probeMapped[i]) {
      DBG_PRINT("Probe ");
      DBG_PRINT(i + 1);
      DBG_PRINTLN(" remains unmapped");
    }
  }
}

static void printDiscoveredSensors() {
  int count = dallas.getDeviceCount();
  DBG_PRINT("DS18B20 devices found: ");
  DBG_PRINTLN(count);

  for (int i = 0; i < count; i++) {
    uint8_t addr[8];
    if (dallas.getAddress(addr, i) && dallas.validAddress(addr)) {
      DBG_PRINT("Discovered[");
      DBG_PRINT(i);
      DBG_PRINT("] = ");
      printAddress(addr);
      DBG_PRINTLN("");
    }
  }
}

static void printMappedProbeSlots() {
  for (uint8_t i = 0; i < 3; i++) {
    DBG_PRINT("Probe ");
    DBG_PRINT(i + 1);
    DBG_PRINT(" mapped = ");
    if (probeMapped[i]) {
      printAddress(probeAddr[i]);
      DBG_PRINT("  present=");
      DBG_PRINTLN(dallas.isConnected(probeAddr[i]) ? "YES" : "NO");
    } else {
      DBG_PRINTLN("<empty>");
    }
  }
}

// Average multiple ADC readings for better stability
static uint32_t readBatteryMilliVolts() {
  const uint8_t samples = 8;
  uint32_t sum = 0;

  for (uint8_t i = 0; i < samples; i++) {
    sum += (uint32_t)analogReadMilliVolts(BAT_ADC_PIN);
    delay(2);
  }

  uint32_t mv = sum / samples;
  return (uint32_t)(mv * BAT_DIVIDER);
}

// More realistic 1S LiPo voltage -> percentage mapping
static uint8_t voltageToPercent(float v) {
  if (v >= 4.20f) return 100;
  if (v <= 3.20f) return 0;
  if (v >= 4.10f) return (uint8_t)lroundf(90 + (v - 4.10f) * (10.0f / 0.10f));
  if (v >= 4.00f) return (uint8_t)lroundf(80 + (v - 4.00f) * (10.0f / 0.10f));
  if (v >= 3.92f) return (uint8_t)lroundf(70 + (v - 3.92f) * (10.0f / 0.08f));
  if (v >= 3.85f) return (uint8_t)lroundf(60 + (v - 3.85f) * (10.0f / 0.07f));
  if (v >= 3.80f) return (uint8_t)lroundf(50 + (v - 3.80f) * (10.0f / 0.05f));
  if (v >= 3.75f) return (uint8_t)lroundf(40 + (v - 3.75f) * (10.0f / 0.05f));
  if (v >= 3.70f) return (uint8_t)lroundf(30 + (v - 3.70f) * (10.0f / 0.05f));
  if (v >= 3.65f) return (uint8_t)lroundf(20 + (v - 3.65f) * (10.0f / 0.05f));
  if (v >= 3.55f) return (uint8_t)lroundf(10 + (v - 3.55f) * (10.0f / 0.10f));
  return (uint8_t)lroundf((v - 3.20f) * (10.0f / 0.35f));
}

static uint32_t effectiveAwakeWindowMs() {
  uint32_t extraMs = forceAwakeSeconds * 1000UL;
  uint32_t maxMs = MAX_FORCE_AWAKE_SEC * 1000UL;
  if (extraMs > maxMs) extraMs = maxMs;
  return CONFIG_WINDOW_MS + extraMs;
}

static void goToDeepSleepMinutes(uint32_t minutes) {
  uint64_t sleepUs = (uint64_t)minutes * 60ULL * 1000000ULL;

  DBG_PRINT("Sleeping for ");
  DBG_PRINT(minutes);
  DBG_PRINTLN(" minute(s)");

  // Stop Zigbee stack before sleeping
  Zigbee.stop();
  delay(100);
  DBG_FLUSH();

  esp_sleep_enable_timer_wakeup(sleepUs);
  esp_deep_sleep_start();
}

static void reportOneSensor(ZigbeeTempSensor &ep, const uint8_t addr[8], uint8_t slot) {
  if (!dallas.isConnected((uint8_t*)addr)) {
    DBG_PRINT("Probe ");
    DBG_PRINT(slot);
    DBG_PRINTLN(" disconnected");
    return;
  }

  float t = dallas.getTempC((uint8_t*)addr);

  DBG_PRINT("Probe ");
  DBG_PRINT(slot);
  DBG_PRINT(" raw temp = ");
  DBG_PRINTLN(t);

  if (t > -100.0f && t < 150.0f) {
    bool ok1 = ep.setTemperature(t);
    bool ok2 = ep.reportTemperature();

    DBG_PRINT("Probe ");
    DBG_PRINT(slot);
    DBG_PRINT(" -> ");
    DBG_PRINT(t);
    DBG_PRINT(" C, set=");
    DBG_PRINT(ok1 ? "true" : "false");
    DBG_PRINT(", report=");
    DBG_PRINTLN(ok2 ? "true" : "false");
  } else {
    DBG_PRINT("Probe ");
    DBG_PRINT(slot);
    DBG_PRINTLN(" invalid");
  }
}

// Zigbee callback for writable sleep value
void onSleepMinutesChange(float value) {
  uint32_t m = (uint32_t)lroundf(value);

  if (m < MIN_SLEEP_MIN) m = MIN_SLEEP_MIN;
  if (m > MAX_SLEEP_MIN) m = MAX_SLEEP_MIN;

  // Ignore duplicate writes of the same value
  if (m == sleepMinutes) {
    DBG_PRINT("Sleep minutes duplicate write ignored -> ");
    DBG_PRINTLN(m);
    return;
  }

  DBG_PRINT("Sleep minutes updated from Zigbee: ");
  DBG_PRINT(sleepMinutes);
  DBG_PRINT(" -> ");
  DBG_PRINTLN(m);

  sleepMinutes = m;
  prefs.putUInt(PREF_KEY_SLEEP, sleepMinutes);
  sleepValueChanged = true;

  // Auto-extend awake window when a command arrives
  lastActivityMs = millis();
}

void onForceAwakeChange(float value) {
  uint32_t s = (uint32_t)lroundf(value);

  if (s > MAX_FORCE_AWAKE_SEC) s = MAX_FORCE_AWAKE_SEC;

  if (s == forceAwakeSeconds) {
    DBG_PRINT("Force-awake duplicate write ignored -> ");
    DBG_PRINTLN(s);
    return;
  }

  DBG_PRINT("Force-awake seconds updated from Zigbee: ");
  DBG_PRINT(forceAwakeSeconds);
  DBG_PRINT(" -> ");
  DBG_PRINTLN(s);

  forceAwakeSeconds = s;
  awakeValueChanged = true;

  // Auto-extend awake window when a command arrives
  lastActivityMs = millis();
}

void setup() {
  DBG_BEGIN(115200);
  delay(500);

  DBG_PRINTLN("Booting 3-sensor auto-mapped + battery + sleep-control Zigbee test");

  // Optional development safeguard: hold BOOT during reset to prevent sleeping
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  debugNoSleep = (digitalRead(BUTTON_PIN) == LOW);
  if (debugNoSleep) {
    DBG_PRINTLN("BOOT held -> debug/no-sleep mode");
  }

  // -------------------------
  // Preferences / persisted sleep interval + probe map
  // -------------------------
  prefs.begin(PREF_NS, false);

  sleepMinutes = prefs.getUInt(PREF_KEY_SLEEP, DEFAULT_SLEEP_MIN);
  if (sleepMinutes < MIN_SLEEP_MIN) sleepMinutes = MIN_SLEEP_MIN;
  if (sleepMinutes > MAX_SLEEP_MIN) sleepMinutes = MAX_SLEEP_MIN;

  DBG_PRINT("Loaded sleepMinutes = ");
  DBG_PRINTLN(sleepMinutes);

  // -------------------------
  // Battery ADC init
  // -------------------------
  analogReadResolution(12);

  // -------------------------
  // DS18B20 init
  // -------------------------
  dallas.begin();

  // Async conversions to reduce awake/blocking time
  dallas.setWaitForConversion(false);

  printDiscoveredSensors();

  // Load saved mapping, then auto-map only empty/invalid slots
  loadProbeMap();
  autoMapMissingProbeSlots();
  printMappedProbeSlots();

  // Set lower conversion resolution for faster reads / less awake time
  for (uint8_t i = 0; i < 3; i++) {
    if (probeMapped[i]) {
      dallas.setResolution(probeAddr[i], DS18_RESOLUTION_BITS);
    }
  }

  // -------------------------
  // Zigbee static endpoint setup
  // -------------------------
  if (!zbTemp1.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) DBG_PRINTLN("zbTemp1 model failed");
  if (!zbTemp2.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) DBG_PRINTLN("zbTemp2 model failed");
  if (!zbTemp3.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) DBG_PRINTLN("zbTemp3 model failed");
  if (!zbSleep.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) DBG_PRINTLN("zbSleep model failed");
  if (!zbAwake.setManufacturerAndModel(ZB_MANUFACTURER, ZB_MODEL)) DBG_PRINTLN("zbAwake model failed");

  // Must be before addEndpoint()
  zbTemp1.setDefaultValue(20.0f);
  zbTemp2.setDefaultValue(20.0f);
  zbTemp3.setDefaultValue(20.0f);

  // Sleep control endpoint: writable Analog Output
  zbSleep.addAnalogOutput();
  if (!zbSleep.setAnalogOutputDescription("SleepMin")) {
    DBG_PRINTLN("zbSleep description failed");
  }
  zbSleep.setAnalogOutputResolution(1.0f);
  zbSleep.setAnalogOutputMinMax((float)MIN_SLEEP_MIN, (float)MAX_SLEEP_MIN);
  zbSleep.onAnalogOutputChange(onSleepMinutesChange);

  // Force-awake endpoint: writable Analog Output (seconds)
  zbAwake.addAnalogOutput();
  if (!zbAwake.setAnalogOutputDescription("AwakeSec")) {
    DBG_PRINTLN("zbAwake description failed");
  }
  zbAwake.setAnalogOutputResolution(1.0f);
  zbAwake.setAnalogOutputMinMax(0.0f, (float)MAX_FORCE_AWAKE_SEC);
  zbAwake.onAnalogOutputChange(onForceAwakeChange);

  // -------------------------
  // Battery metadata init BEFORE Zigbee.begin()
  // -------------------------
  uint32_t batMv0 = readBatteryMilliVolts();
  float batV0 = batMv0 / 1000.0f;
  uint8_t batPct0 = voltageToPercent(batV0);
  uint8_t batV100mV0 = (uint8_t)lroundf(batV0 * 10.0f);

  DBG_PRINT("Initial battery V = ");
  DBG_PRINT(batV0);
  DBG_PRINT(" pct = ");
  DBG_PRINTLN(batPct0);

  if (!zbTemp1.setPowerSource(ZB_POWER_SOURCE_BATTERY, batPct0, batV100mV0)) {
    DBG_PRINTLN("setPowerSource failed");
  } else {
    DBG_PRINTLN("setPowerSource OK");
  }

  Zigbee.addEndpoint(&zbTemp1);
  Zigbee.addEndpoint(&zbTemp2);
  Zigbee.addEndpoint(&zbTemp3);
  Zigbee.addEndpoint(&zbSleep);
  Zigbee.addEndpoint(&zbAwake);

  Zigbee.setDebugMode(DEBUG_LOG);

  // End Device config
  esp_zb_cfg_t zigbeeConfig = ZIGBEE_DEFAULT_ED_CONFIG();
  zigbeeConfig.nwk_cfg.zed_cfg.keep_alive = 10000;

  DBG_PRINTLN("Starting Zigbee...");
  if (!Zigbee.begin(&zigbeeConfig, false)) {
    DBG_PRINTLN("Zigbee begin failed");
    while (true) delay(1000);
  }

  while (!Zigbee.started()) {
    delay(50);
  }
  DBG_PRINTLN("Zigbee stack started");

  while (!Zigbee.connected()) {
    delay(100);
  }
  DBG_PRINTLN("Zigbee connected");

  // Small settle delay after join
  delay(1500);

  // Publish current sleep value so coordinator/frontend can see it
  bool okSleepSet = zbSleep.setAnalogOutput((float)sleepMinutes);
  bool okSleepRpt = zbSleep.reportAnalogOutput();
  DBG_PRINT("Sleep endpoint init set=");
  DBG_PRINT(okSleepSet ? "true" : "false");
  DBG_PRINT(" report=");
  DBG_PRINTLN(okSleepRpt ? "true" : "false");

  // Publish current force-awake value (normally 0)
  bool okAwakeSet = zbAwake.setAnalogOutput((float)forceAwakeSeconds);
  bool okAwakeRpt = zbAwake.reportAnalogOutput();
  DBG_PRINT("Awake endpoint init set=");
  DBG_PRINT(okAwakeSet ? "true" : "false");
  DBG_PRINT(" report=");
  DBG_PRINTLN(okAwakeRpt ? "true" : "false");

  // -------------------------
  // Request DS18B20 conversion asynchronously
  // -------------------------
  dallas.requestTemperatures();
  delay(DS18_CONVERSION_MS);

  // -------------------------
  // Read all temperatures once
  // -------------------------
  if (probeMapped[0] && dallas.isConnected(probeAddr[0])) reportOneSensor(zbTemp1, probeAddr[0], 1);
  if (probeMapped[1] && dallas.isConnected(probeAddr[1])) reportOneSensor(zbTemp2, probeAddr[1], 2);
  if (probeMapped[2] && dallas.isConnected(probeAddr[2])) reportOneSensor(zbTemp3, probeAddr[2], 3);

  // -------------------------
  // Read and report battery once (using endpoint 1)
  // -------------------------
  uint32_t batMv = readBatteryMilliVolts();
  float batV = batMv / 1000.0f;
  uint8_t batPct = voltageToPercent(batV);

  bool ok3 = zbTemp1.setBatteryPercentage(batPct);
  bool ok4 = zbTemp1.reportBatteryPercentage();

  DBG_PRINT("Battery V=");
  DBG_PRINT(batV);
  DBG_PRINT(" pct=");
  DBG_PRINT(batPct);
  DBG_PRINT(" set=");
  DBG_PRINT(ok3 ? "true" : "false");
  DBG_PRINT(" report=");
  DBG_PRINTLN(ok4 ? "true" : "false");

  // Give reports a short moment to flush before opening config window
  delay(REPORT_FLUSH_MS);

  // -------------------------
  // Config / command window with auto-extend on command activity
  // -------------------------
  lastActivityMs = millis();
  uint32_t extraAwakeMs = forceAwakeSeconds * 1000UL;
  if (extraAwakeMs > MAX_FORCE_AWAKE_SEC * 1000UL) {
    extraAwakeMs = MAX_FORCE_AWAKE_SEC * 1000UL;
  }

  while ((millis() - lastActivityMs) < (CONFIG_WINDOW_MS + extraAwakeMs)) {
    if (sleepValueChanged) {
      sleepValueChanged = false;

      bool ok1 = zbSleep.setAnalogOutput((float)sleepMinutes);
      bool ok2 = zbSleep.reportAnalogOutput();

      DBG_PRINT("mirror Sleep set=");
      DBG_PRINT(ok1 ? "true" : "false");
      DBG_PRINT(" report=");
      DBG_PRINTLN(ok2 ? "true" : "false");

      // Auto-extend awake window on command
      lastActivityMs = millis();
    }

    if (awakeValueChanged) {
      awakeValueChanged = false;

      bool ok1 = zbAwake.setAnalogOutput((float)forceAwakeSeconds);
      bool ok2 = zbAwake.reportAnalogOutput();

      DBG_PRINT("mirror AwakeSec set=");
      DBG_PRINT(ok1 ? "true" : "false");
      DBG_PRINT(" report=");
      DBG_PRINTLN(ok2 ? "true" : "false");

      // Recompute extra awake time and extend from "now"
      extraAwakeMs = forceAwakeSeconds * 1000UL;
      if (extraAwakeMs > MAX_FORCE_AWAKE_SEC * 1000UL) {
        extraAwakeMs = MAX_FORCE_AWAKE_SEC * 1000UL;
      }
      lastActivityMs = millis();
    }

    delay(10);  // allow background Zigbee task to run
  }

  if (debugNoSleep) {
    DBG_PRINTLN("Debug mode active: not sleeping");
    while (true) {
      delay(1000);
    }
  }

  // Reset transient force-awake back to 0 so the next cycle returns to normal unless re-commanded
  if (forceAwakeSeconds != 0) {
    forceAwakeSeconds = 0;

    bool ok1 = zbAwake.setAnalogOutput((float)forceAwakeSeconds);
    bool ok2 = zbAwake.reportAnalogOutput();

    DBG_PRINT("reset AwakeSec set=");
    DBG_PRINT(ok1 ? "true" : "false");
    DBG_PRINT(" report=");
    DBG_PRINTLN(ok2 ? "true" : "false");

    delay(500);
  }

  // Final flush before sleeping
  delay(1000);

  // Sleep
  goToDeepSleepMinutes(sleepMinutes);
}

void loop() {
  // not used; device sleeps from setup()
  delay(1000);
}
