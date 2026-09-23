// Lines from the loop and from a pin interrupt, on an ESP32 or an nRF52. Nothing to start. Open
// the serial monitor: with no host on the USB port nothing is formatted
#define LOG_LEVEL 4  // debug and up; 5 would show logV too
#include <rtosLogger.h>

const int PIN = 0;  // a button to ground: the BOOT button on most ESP32 boards
static std::atomic<uint32_t> presses(0);

void IRAM_ATTR onPress() { logI("press %lu", (unsigned long)++presses); }  // straight from the interrupt

void setup() {
  Serial.begin(115200);
  pinMode(PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN), onPress, FALLING);
  logI("boot");
}

void loop() {
  static int n;
  logI("loop %d, up %.1f s", n++, millis() / 1000.0f);
  logD("presses=%lu", (unsigned long)presses.load());  // gone with LOG_LEVEL 3
  if (logDrops.load()) logE("dropped %lu so far", (unsigned long)logDrops.load());
  delay(500);
}
