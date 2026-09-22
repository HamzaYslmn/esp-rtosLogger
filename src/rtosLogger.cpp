// The rings and the task: one of each per program, so not in the header. The LOG_ flags below
// are build flags
#include "rtosLogger.h"

#if defined(ARDUINO_ARCH_ESP32)
#define logCore() xPortGetCoreID()
#else
#define IRAM_ATTR
#define logCore() 0
#endif

#ifndef LOG_DRAIN_MS
#define LOG_DRAIN_MS 25
#endif
#ifndef LOG_BATCH
#define LOG_BATCH 512  // the most one logTo call carries, and the longest line
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

static const uint32_t N = LOG_RING / 4;
LogRing logRings[LOG_CORES];  // zeroed, no constructor: usable before any other one runs
std::atomic<uint32_t> logDrops(0);

// Masks this core's interrupts until logClose, so nothing on this core can write between the two,
// and reserves a record that never wraps: one that does not fit before the end leaves a null fmt
// word and starts again at 0. Strictly short of the reader, or a full ring reads as empty
uint32_t *IRAM_ATTR logOpen(const char *fmt, LogFormat format, uint32_t words) {
  UBaseType_t ps = portSET_INTERRUPT_MASK_FROM_ISR();
  LogRing &g = logRings[logCore()];  // read inside the mask: nothing can move this task now
  uint32_t w = g.w.load(std::memory_order_relaxed), r = g.r.load(std::memory_order_acquire), need = 3 + words, at = N;
  if (w < r) {
    if (r - w > need) at = w;
  } else if (N - w >= need) at = w;
  else if (r > need) {
    if (w < N) g.buf[w] = 0;
    at = 0;
  }
  if (at == N) {
    g.drops++;
    portCLEAR_INTERRUPT_MASK_FROM_ISR(ps);
    return nullptr;
  }
  g.buf[at] = (uint32_t)fmt, g.buf[at + 1] = (uint32_t)format, g.buf[at + 2] = need;
  g.end = at + need, g.ps = ps;
  return g.buf + at + 3;
}

void IRAM_ATTR logClose() {
  LogRing &g = logRings[logCore()];
  g.w.store(g.end, std::memory_order_release);
  portCLEAR_INTERRUPT_MASK_FROM_ISR(g.ps);
}

// the default. A USB CDC write takes what fits its FIFO and returns, so it is retried, bounded;
// what never went is counted by the line
static void serialOut(const char *text, size_t n) {
  size_t at = 0;
  for (int tries = 0; at < n && tries < 40; tries++) {
    at += Serial.write((const uint8_t *)text + at, n - at);
    if (at < n) vTaskDelay(1);
  }
  uint32_t lost = 0;
  for (; at < n; at++) lost += text[at] == '\n';
  if (lost) logDrops.fetch_add(lost, std::memory_order_relaxed);
}

static volatile LogOut logOut = serialOut;
void logTo(LogOut out) { logOut = out ? out : serialOut; }

// polls: waking a blocked reader measured 3 us of the caller. Formats in place, straight into
// the batch; each ring's write index is read once a pass, so one busy core cannot starve the other
static void logTask(void *) {
  static char out[LOG_BATCH];
  static uint32_t seen[LOG_CORES];  // drops already folded into logDrops
  const TickType_t period = pdMS_TO_TICKS(LOG_DRAIN_MS);
  for (TickType_t spent = 0;;) {
    // a fixed period, the pass's own time taken out of the sleep: a sleep after it measured ~3000
    // drops in 10 s at 2000 lines a second. Never under a tick, so idle keeps its turn when behind
    vTaskDelay(spent < period ? period - spent : 1);
    TickType_t t0 = xTaskGetTickCount();
    LogOut send = logOut;
    bool nobody = send == serialOut && !Serial;  // a USB port with no host: nothing formatted for nobody
    size_t n = 0;
    for (int c = 0; c < LOG_CORES; c++) {
      LogRing &g = logRings[c];
      uint32_t w = g.w.load(std::memory_order_acquire), r = nobody ? w : g.r.load(std::memory_order_relaxed);
      while (r != w) {
        if (r == N || !g.buf[r]) {
          r = 0;
          continue;
        }
        const uint32_t *h = g.buf + r;
        const char *fmt = (const char *)h[0];
        LogFormat format = (LogFormat)h[1];
        int len = format(h + 3, fmt, out + n, sizeof out - n);  // one byte is kept for the newline
        if (len >= (int)(sizeof out - n - 1) && n) send(out, n), n = 0, len = format(h + 3, fmt, out, sizeof out);
        n += len < 0 ? 0 : len > (int)sizeof out - 1 ? sizeof out - 1 : len;  // longer than a batch: cut
        out[n++] = '\n';
        g.r.store(r += h[2], std::memory_order_release);  // only now may a writer reuse it
      }
      g.r.store(r, std::memory_order_release);
      uint32_t d = g.drops;
      if (d != seen[c]) logDrops.fetch_add(d - seen[c], std::memory_order_relaxed), seen[c] = d;
    }
    if (n) send(out, n);
    spent = xTaskGetTickCount() - t0;
  }
}

// made before setup(): a task created before the scheduler runs when it starts
#if defined(ARDUINO_ARCH_ESP32)
static const bool logUp = xTaskCreatePinnedToCore(logTask, "log", LOG_STACK, nullptr, LOG_PRIO, nullptr, LOG_CORE) == pdPASS;
#else
static const bool logUp = xTaskCreate(logTask, "log", LOG_STACK, nullptr, LOG_PRIO, nullptr) == pdPASS;
#endif
