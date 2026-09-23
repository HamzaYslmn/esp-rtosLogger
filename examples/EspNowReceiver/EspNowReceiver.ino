// Receiver for SerialAndEspNow: prints its lines and its newest struct. Needs no address: it hears
// every broadcast in range
#include <WiFi.h>
#include <esp_now.h>
#include <rtosLogger.h>

struct Sample {  // the sender's struct, the same name and fields
  uint32_t ms;
  int16_t rpm, amps;
};

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_recv_cb([](const esp_now_recv_info_t *, const uint8_t *data, int n) { logFrom(data, n); });
}

void loop() {
  static Sample s;  // keeps the newest
  if (logBinRead(s)) Serial.printf("sample %lu ms: %d rpm, %d A\n", (unsigned long)s.ms, s.rpm, s.amps);
  delay(10);
}
