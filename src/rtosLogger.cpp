// The rings and the task: one of each per program, so not in the header. The LOG_ flags below
// are build flags
#include "rtosLogger.h"

#if defined(ARDUINO_ARCH_ESP32)
#define logCore() xPortGetCoreID()
#else
#define logCore() 0
#endif

#ifndef LOG_DRAIN_MS
#define LOG_DRAIN_MS 25
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
static_assert(N <= 0xFFFF, "LOG_RING: a record's length is 16 bits of words");
LogRing logRings[LOG_CORES];  // zeroed, no constructor: usable before any other one runs
std::atomic<uint32_t> logDrops(0);

// Masks this core's interrupts until logClose, so nothing on this core can write between the two,
// and reserves a record that never wraps: one that does not fit before the end leaves a null fmt
// word and starts again at 0. Strictly short of the reader, or a full ring reads as empty
uint32_t *IRAM_ATTR logOpen(const char *fmt, LogFormat format, uint32_t head) {
  UBaseType_t ps = portSET_INTERRUPT_MASK_FROM_ISR();
  LogRing &g = logRings[logCore()];  // read inside the mask: nothing can move this task now
  uint32_t w = g.w.load(std::memory_order_relaxed), r = g.r.load(std::memory_order_acquire), need = head >> 3 & 0xFFFF, at = N;
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
  g.buf[at] = (uint32_t)fmt, g.buf[at + 1] = (uint32_t)format, g.buf[at + 2] = head;
  g.end = at + need, g.ps = ps;
  return g.buf + at + 3;
}

void IRAM_ATTR logClose() {
  LogRing &g = logRings[logCore()];
  g.w.store(g.end, std::memory_order_release);
  portCLEAR_INTERRUPT_MASK_FROM_ISR(g.ps);
}

// the default: lines only, since raw bytes on a terminal are noise and a byte stream has no packet
// edges. A USB CDC write takes what fits its FIFO and returns, so it is retried, bounded; what
// never went is counted by the line
void logSerial(const uint8_t *data, size_t n, bool text) {
  if (!text) return;
  size_t at = 0;
  for (int tries = 0; at < n && tries < 40; tries++) {
    at += Serial.write(data + at, n - at);
    if (at < n) vTaskDelay(1);
  }
  uint32_t lost = 0;
  for (; at < n; at++) lost += data[at] == '\n';
  if (lost) logDrops.fetch_add(lost, std::memory_order_relaxed);
}

// constants, no constructor: an output made in any file, in any order, finds them ready. Serial
// until the first output is made
static LogSend volatile logSends[LOG_OUTPUTS] = {logSerial};
static volatile uint16_t logMax[LOG_OUTPUTS] = {LOG_BATCH};

LogOutput::LogOutput(LogSend send, size_t max) : bits(0) {
  static bool made;
  if (!made) made = true, logSends[0] = nullptr;
  int i = 0;
  while (i < LOG_OUTPUTS && logSends[i] && logSends[i] != send) i++;
  if (i == LOG_OUTPUTS || !send) return;  // a ninth goes nowhere
  logMax[i] = max < 16 ? 16 : max > LOG_BATCH ? LOG_BATCH : max;
  logSends[i] = send;  // after its max: the drain sees the pair whole
  bits = 1 << i;
}

// the outputs worth formatting for: made, and not Serial with no USB host
static uint8_t logLive() {
  uint8_t m = 0;
  for (int i = 0; i < LOG_OUTPUTS; i++)
    if (logSends[i] && (logSends[i] != logSerial || Serial)) m |= 1 << i;
  return m;
}

// the smallest packet of the outputs one goes to
static size_t logCap(uint8_t to) {
  size_t cap = LOG_BATCH;
  for (int i = 0; i < LOG_OUTPUTS; i++)
    if (to >> i & 1 && logMax[i] && logMax[i] < cap) cap = logMax[i];
  return cap;
}

static void logDeliver(const uint8_t *data, size_t n, bool text, uint8_t to) {
  for (int i = 0; i < LOG_OUTPUTS; i++) {
    LogSend s = logSends[i];
    if (to >> i & 1 && s) s(data, n, text);
  }
}

// FNV-1a 32 over the type's name as the compiler spells it after "T = ", then its size as two
// little endian bytes. The README has the same in JavaScript, for a receiver that is not C++
uint32_t logBinId(const char *pretty, uint32_t size) {
  const char *s = strstr(pretty, "T = ");
  uint32_t h = 2166136261u;
  for (s = s ? s + 4 : pretty; *s && *s != ']' && *s != ';'; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  for (int i = 0; i < 2; i++) h = (h ^ (uint8_t)(size >> 8 * i)) * 16777619u;
  return (h & 0xFF) == '[' ? h ^ 1 : h;  // every line starts with '[', so a packet's first byte says which it is
}

// one slot per type read, each with its newest struct. No lock: logFrom makes seq odd while it
// writes, and a read that saw it odd or changed is thrown away, so a reader never spins
static std::atomic<LogSlot *> logSlots(nullptr);

LogSlot::LogSlot(uint32_t id, uint32_t size, void *data) : next(nullptr), id(id), size(size), data(data), seq(0), seen(0) {
  next = logSlots.load(std::memory_order_relaxed);
  while (!logSlots.compare_exchange_weak(next, this, std::memory_order_release)) {}
}

void logFrom(const void *data, size_t n) {
  const uint8_t *p = (const uint8_t *)data;
  if (n && p[0] == '[') return logDeliver(p, n, true, logLive());  // lines: out the way this board's own go
  if (n <= 4) return;
  uint32_t id;
  memcpy(&id, p, 4);
  for (LogSlot *s = logSlots.load(std::memory_order_acquire); s; s = s->next) {
    if (s->id != id || (n - 4) % s->size) continue;
    uint32_t q = s->seq.load(std::memory_order_relaxed);
    s->seq.store(q + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);  // odd is seen before the bytes change
    memcpy(s->data, p + n - s->size, s->size);  // the packet's last is its newest
    s->seq.store(q + 2, std::memory_order_release);
    return;
  }
}

// in IRAM: a motor loop reads every pass, and from flash its worst read measured 8.8 us, here 0.6
bool IRAM_ATTR logBinTake(LogSlot &s, void *out) {
  uint32_t q = s.seq.load(std::memory_order_acquire);
  if (q == s.seen || (q & 1)) return false;  // nothing new, or being written: the next call gets it
  memcpy(out, s.data, s.size);
  std::atomic_thread_fence(std::memory_order_acquire);
  if (s.seq.load(std::memory_order_relaxed) != q) return false;  // written over while copied
  s.seen = q;
  return true;
}

// polls: waking a blocked reader measured 3 us of the caller. Formats in place, straight into
// the packet; each ring's write index is read once a pass, so one busy core cannot starve the other.
// A packet holds one kind, lines or one type's records, for one set of outputs, so it is sent when
// either changes. A record no live output wants is passed over, never formatted
static void logTask(void *) {
  static const char tags[] = "[?] [E] [W] [I] [D] [V] ";  // by level, 4 bytes each
  static uint8_t out[LOG_BATCH];
  static uint32_t seen[LOG_CORES];  // drops already folded into logDrops
  const TickType_t period = pdMS_TO_TICKS(LOG_DRAIN_MS);
  for (TickType_t spent = 0;;) {
    // a fixed period, the pass's own time taken out of the sleep: a sleep after it measured ~3000
    // drops in 10 s at 2000 lines a second. Never under a tick, so idle keeps its turn when behind
    vTaskDelay(spent < period ? period - spent : 1);
    TickType_t t0 = xTaskGetTickCount();
    uint8_t live = logLive(), to = 0;  // to: the outputs the packet goes to
    size_t cap = LOG_BATCH, n = 0;
    uint32_t kind = 0;  // what the packet holds: 0 lines, else its records' type function
    LogBinType t = {};
    for (int c = 0; c < LOG_CORES; c++) {
      LogRing &g = logRings[c];
      // nobody listening: skipped unread, nothing formatted for nobody
      uint32_t w = g.w.load(std::memory_order_acquire), r = live ? g.r.load(std::memory_order_relaxed) : w;
      while (r != w) {
        if (r == N || !g.buf[r]) {
          r = 0;
          continue;
        }
        const uint32_t *h = g.buf + r;
        uint32_t head = h[2];
        uint8_t dest = ~(head >> 19) & live;
        if (dest) {
          LogFormat format = (LogFormat)h[1];
          uint32_t key = format ? 0 : h[0];
          if (key != kind || dest != to) {
            if (n) logDeliver(out, n, !kind, to), n = 0;
            if (dest != to) cap = logCap(dest), to = dest;
            kind = key;
          }
          if (!format) {  // a record: its id once, at the packet's head, then as many as fit
            if (!n) t = ((LogBinType(*)())key)(), memcpy(out, &t.id, 4), n = 4;
            if (n + t.size > cap && n > 4) logDeliver(out, n, false, to), n = 4;  // the id stays at the head
            if (n + t.size <= cap) memcpy(out + n, h + 3, t.size), n += t.size;
            else logDrops.fetch_add(1, std::memory_order_relaxed), n = 0;  // bigger than the transport's packet
          } else {  // a line: its tag, then its text, one byte kept for the newline
            char *text = (char *)out;
            const char *fmt = (const char *)h[0];
            if (n + 6 > cap) logDeliver(out, n, true, to), n = 0;  // no room for a tag, a byte and a newline
            int len = format(h + 3, fmt, text + n + 4, cap - n - 4);
            if (len >= (int)(cap - n - 5) && n) logDeliver(out, n, true, to), n = 0, len = format(h + 3, fmt, text + 4, cap - 4);
            memcpy(out + n, tags + 4 * (head & 7), 4);
            n += 4 + (len < 0 ? 0 : len > (int)cap - 5 ? cap - 5 : len);  // longer than a packet: cut
            out[n++] = '\n';
          }
        }
        g.r.store(r += head >> 3 & 0xFFFF, std::memory_order_release);  // only now may a writer reuse it
      }
      g.r.store(r, std::memory_order_release);
      uint32_t d = g.drops;
      if (d != seen[c]) logDrops.fetch_add(d - seen[c], std::memory_order_relaxed), seen[c] = d;
    }
    if (n) logDeliver(out, n, !kind, to);
    spent = xTaskGetTickCount() - t0;
  }
}

// made before setup(): a task created before the scheduler runs when it starts
#if defined(ARDUINO_ARCH_ESP32)
static const bool logUp = xTaskCreatePinnedToCore(logTask, "log", LOG_STACK, nullptr, LOG_PRIO, nullptr, LOG_CORE) == pdPASS;
#else
static const bool logUp = xTaskCreate(logTask, "log", LOG_STACK, nullptr, LOG_PRIO, nullptr) == pdPASS;
#endif
