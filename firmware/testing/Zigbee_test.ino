#include <Arduino.h>
#include <Zigbee.h>

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Boot");

  esp_zb_cfg_t cfg = ZIGBEE_DEFAULT_ED_CONFIG();

  Serial.println("Before begin");

  bool ok = Zigbee.begin(&cfg, false);

  Serial.println("After begin");

  Serial.print("Result=");
  Serial.println(ok);
}

void loop() {
  delay(1000);
}
