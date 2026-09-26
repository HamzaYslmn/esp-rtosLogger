// Lines from a loop and from a pin interrupt, on an ESP32 or an nRF52. Open the serial monitor: with
// no host on the USB port nothing is formatted
#define LOG_LEVEL 4  // debug and up; 5 would show logV too
#include <rtosLogger.h>

#ifndef ARDUINO_ARCH_ESP32  // the nRF52 has one core and counts stack in words
#define xTaskCreatePinnedToCore(fn, name, bytes, arg, prio, handle, core) xTaskCreate(fn, name, (bytes) / 4, arg, prio, handle)
#endif

const int PIN = 0;  // a button to ground: the BOOT button on most ESP32 boards
static std::atomic<uint32_t> presses(0);

void IRAM_ATTR onPress() { logI("press %lu", (unsigned long)++presses); }  // straight from the interrupt

// Serial here, not in setup(): an interrupt runs on the core that starts it, and setup() is core 1
static void core0Task(void *) {
  Serial.begin(115200);
  vTaskDelete(nullptr);
}

// your busy code: it only logs, the background task on core 0 prints
static void core1Task(void *) {
  pinMode(PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN), onPress, FALLING);
  logI("boot");
  for (int n = 0;; n++) {
    logI("loop %d, up %.1f s", n, millis() / 1000.0f);
    logD("presses=%lu", (unsigned long)presses.load());  // gone with LOG_LEVEL 3
    if (logDrops.load()) logE("dropped %lu so far", (unsigned long)logDrops.load());
    delay(500);
  }
}

void setup() {
  xTaskCreatePinnedToCore(core0Task, "core0Task", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(core1Task, "core1Task", 8192, nullptr, 1, nullptr, 1);
}

void loop() { vTaskDelete(nullptr); }  // the Arduino task is done after setup()
