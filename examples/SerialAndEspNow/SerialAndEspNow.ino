// Lines to Serial, and everything broadcast over ESP-NOW: any ESP32 in range can listen, no pairing
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

void setup() {
  Serial.begin(115200);
  setupEspNow();
  logTo([](const uint8_t *data, size_t n, bool text) {
    if (text && Serial) Serial.write(data, n);  // a USB port with no host would wait out its timeout
    esp_now_send(EVERYONE, data, n);
  }, ESP_NOW_MAX_DATA_LEN);  // 250: no packet is ever bigger, and no line is ever split
}

void loop() {
  logI("uptime %lu ms", millis());
  delay(200);
}
