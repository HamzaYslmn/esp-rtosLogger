// Every line to Serial and broadcast over ESP-NOW: any ESP32 in range can listen, no pairing
#include <WiFi.h>
#include <esp_now.h>
#include <rtosLogger.h>

static const uint8_t EVERYONE[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void setupEspNow() {
  WiFi.mode(WIFI_STA);  // ESP-NOW rides the WiFi radio, no network needed
  esp_now_init();
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, EVERYONE, 6);
  esp_now_add_peer(&peer);
}

// a packet carries 250 bytes: cut the batch there, at a newline, so no line is split
void espNowSend(const char *text, size_t n) {
  while (n) {
    size_t k = n;
    if (k > ESP_NOW_MAX_DATA_LEN) for (k = ESP_NOW_MAX_DATA_LEN; k > 1 && text[k - 1] != '\n'; k--) {}
    esp_now_send(EVERYONE, (const uint8_t *)text, k);
    text += k, n -= k;
  }
}

void setup() {
  Serial.begin(115200);
  setupEspNow();
  logTo([](const char *text, size_t n) {
    if (Serial) Serial.write(text, n);  // a USB port with no host would wait out its timeout
    espNowSend(text, n);
  });
}

void loop() {
  logI("uptime %lu ms", millis());
  delay(200);
}
