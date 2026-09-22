// The queue and the task: one of each per program, so not in the header. The LOG_ flags below
// are build flags
#include "rtosLogger.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/queue.h>
#define logInIsr() xPortInIsrContext()
#else
#include <queue.h>
#define IRAM_ATTR
#define logInIsr() (__get_IPSR() != 0)
#endif

#ifndef LOG_DEPTH
#define LOG_DEPTH 12  // lines the queue holds between drains
#endif
#ifndef LOG_DRAIN_MS
#define LOG_DRAIN_MS 25
#endif
#ifndef LOG_BATCH
#define LOG_BATCH 512  // the most one logSink call carries, and the longest line
#endif
#ifndef LOG_CORE
#define LOG_CORE 0  // ESP32: keep the task off the core that cannot wait
#endif
#ifndef LOG_PRIO
#define LOG_PRIO 1
#endif
#ifndef LOG_STACK
#define LOG_STACK 4096  // bytes on the ESP32, words on the nRF52
#endif

// static: no heap, and it exists before setup()
static StaticQueue_t logQueueCb;
static LogRec logSlots[LOG_DEPTH];
static QueueHandle_t logQueue = xQueueCreateStatic(LOG_DEPTH, sizeof(LogRec), (uint8_t *)logSlots, &logQueueCb);
std::atomic<uint32_t> logDrops(0);

// IRAM: the one part every call site shares. Nothing is woken: the task polls
void IRAM_ATTR logPost(const LogRec &r) {
  bool ok = r.len != LogRec::OVER &&
            (logInIsr() ? xQueueSendFromISR(logQueue, &r, nullptr) : xQueueSend(logQueue, &r, 0));
  if (!ok) logDrops.fetch_add(1, std::memory_order_relaxed);
}

// the defaults. A USB port with no host reads false, so nothing is formatted for nobody
__attribute__((weak)) bool logReady() { return Serial; }

// a USB CDC write takes what fits its FIFO and returns, so it is retried, bounded
__attribute__((weak)) void logSink(const char *batch, size_t n) {
  for (int tries = 0; n && tries < 40; tries++) {
    size_t w = Serial.write((const uint8_t *)batch, n);
    batch += w, n -= w;
    if (n) vTaskDelay(1);
  }
  if (n) logDrops.fetch_add(1, std::memory_order_relaxed);
}

// polls: waking a blocked reader measured 3 us of the caller. One pass is one logSink call
static void logTask(void *) {
  static LogRec r;  // static: off the task's stack
  static char line[LOG_BATCH], out[LOG_BATCH];
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(LOG_DRAIN_MS));
    bool ready = logReady();
    size_t n = 0;
    while (xQueueReceive(logQueue, &r, 0) == pdTRUE) {
      if (!ready) continue;
      int len = r.format(r, line, sizeof line);
      if (len < 0) continue;
      if (len > (int)sizeof out - 1) len = sizeof out - 1;  // longer than a batch: cut
      if (n + len + 1 > sizeof out) logSink(out, n), n = 0;  // full: send what is there
      memcpy(out + n, line, len), n += len;
      out[n++] = 10;  // newline
    }
    if (n) logSink(out, n);
  }
}

// made before setup(): a task created before the scheduler runs when it starts
#if defined(ARDUINO_ARCH_ESP32)
static const bool logUp = xTaskCreatePinnedToCore(logTask, "log", LOG_STACK, nullptr, LOG_PRIO, nullptr, LOG_CORE) == pdPASS;
#else
static const bool logUp = xTaskCreate(logTask, "log", LOG_STACK, nullptr, LOG_PRIO, nullptr) == pdPASS;
#endif
