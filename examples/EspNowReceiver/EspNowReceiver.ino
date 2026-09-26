// Receiver for SerialAndEspNow: prints its lines and its newest struct. Needs no address: it hears
// every broadcast in range
#include <WiFi.h>
#include <esp_now.h>
#include <rtosLogger.h>

struct Sample {  // the sender's struct, the same name and fields
  uint32_t ms;
  int16_t rpm, amps;
};

// Serial and the radio here, not in setup(): an interrupt runs on the core that starts it, and
// setup() is core 1. Nothing here is busy, so core 1 is left free
static void core0Task(void *) {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  esp_now_init();
  esp_now_register_recv_cb([](const esp_now_recv_info_t *, const uint8_t *data, int n) { logFrom(data, n); });
  for (;;) {
    static Sample s;  // keeps the newest
    if (logBinRead(s)) Serial.printf("sample %lu ms: %d rpm, %d A\n", (unsigned long)s.ms, s.rpm, s.amps);
    delay(10);
  }
}

void setup() { xTaskCreatePinnedToCore(core0Task, "core0Task", 8192, nullptr, 1, nullptr, 0); }

void loop() { vTaskDelete(nullptr); }  // the Arduino task is done after setup()
