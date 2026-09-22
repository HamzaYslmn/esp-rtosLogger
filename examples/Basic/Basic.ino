// Lines from the loop and from a timer interrupt. Nothing to start. Open the serial monitor:
// with no host on the USB port nothing is formatted
#define LOG_LEVEL 4  // debug and up; 5 would show logV too
#include <rtosLogger.h>

static hw_timer_t *timer;
static volatile uint32_t ticks;

void IRAM_ATTR onTimer() {
  ticks++;
  if (ticks % 1000 == 0) logI("isr tick %lu", (unsigned long)ticks);
}

void setup() {
  Serial.begin(115200);
  logI("boot, chip %s at %u MHz", ESP.getChipModel(), (unsigned)ESP.getCpuFreqMHz());
  timer = timerBegin(1000000);  // Arduino-ESP32 3.x
  timerAttachInterrupt(timer, onTimer);
  timerAlarm(timer, 1000, true, 0);  // every 1 ms
}

void loop() {
  static int n;
  float t = temperatureRead();
  logI("loop %d temp=%.1f", n++, t);
  logD("free=%u ticks=%lu", (unsigned)ESP.getFreeHeap(), (unsigned long)ticks);  // gone with LOG_LEVEL 3
  if (t > 60) logW("warm: %.1f", t);
  if (logDrops.load()) logE("dropped %lu so far", (unsigned long)logDrops.load());
  delay(500);
}
