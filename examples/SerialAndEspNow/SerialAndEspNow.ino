// Sender: lines to Serial, lines and structs broadcast over ESP-NOW. EspNowReceiver on a second ESP32
// prints them; any number of boards in range can listen, no pairing
#include <WiFi.h>
#include <esp_now.h>
#include <rtosLogger.h>

struct Sample {  // the receiver declares the same struct; in a real project, one header both include
  uint32_t ms;
  int16_t rpm, amps;
};

static const uint8_t EVERYONE[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static hw_timer_t *timer;
static volatile int rpm, amps;

void airSend(const uint8_t *data, size_t n, bool) { esp_now_send(EVERYONE, data, n); }  // an error until setup starts it

LogOutput serial(logSerial);  // making any output drops the Serial default, so it is made too
LogOutput air(airSend, ESP_NOW_MAX_DATA_LEN);  // 250: no packet for the air is bigger, no line split

void IRAM_ATTR onTimer() {  // an interrupt: logI is safe here, Serial.printf is not
  static uint32_t ticks;
  if (++ticks % 1000 == 0) logI(serial, "isr: %lu ticks", (unsigned long)ticks);  // Serial only
}

void IRAM_ATTR control() {  // the hot path, in RAM: about 1 us a call, never a flash fetch
  if (amps > 35) logW("over %d A at %d rpm", amps, rpm);  // every output
  logBin(air, Sample{millis(), (int16_t)rpm, (int16_t)amps});  // telemetry: a struct, nothing formatted, air only
}

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
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, onTimer);
  timerAlarm(timer, 1000, true, 0);  // every 1 ms
}

void loop() {  // plain code runs from flash: fine where a few us more does not matter
  rpm = 1200 + random(-50, 50);
  amps = random(20, 40);
  control();
  static uint32_t last;
  if (millis() - last >= 1000) last = millis(), logI("uptime %lu ms, %lu dropped", last, (unsigned long)logDrops.load());
  delay(20);
}
