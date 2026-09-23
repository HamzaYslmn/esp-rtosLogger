// rtosLogger: printf style logging that costs the caller a few stores.
//
//   logI("rpm=%d temp=%.2f", rpm, temp);   ->   [I] rpm=1200 temp=31.50
//
// The caller writes the raw arguments into a ring of its own core and leaves. A low priority task
// reads them back as the same types and calls the real snprintf later, so every specifier works and
// no float formatting runs at the call. The format must be a literal; a %s is copied at the call.
// C++11 only: the nRF52 core compiles at gnu++11.
#pragma once
#include <Arduino.h>
#include <atomic>
#include <type_traits>

#ifndef LOG_RING
#define LOG_RING 4096  // build flag: bytes of ring per core, the .cpp sizes it
#endif
#ifndef LOG_BODY
#define LOG_BODY 500  // the longest %s kept, in bytes
#endif
#ifndef LOG_BATCH
#define LOG_BATCH 512  // build flag: the largest packet, and the longest line
#endif
#if defined(ARDUINO_ARCH_ESP32)
#define LOG_CORES portNUM_PROCESSORS
#else
#define LOG_CORES 1
#endif

#define LOG_INLINE inline __attribute__((always_inline))  // a call out to flash is a cache miss

// One per core, so no two cores share one. Its writers take turns by masking their own core's
// interrupts, its one reader is the log task. Word indices, and a record never wraps
struct LogRing {
  std::atomic<uint32_t> w, r;
  uint32_t drops, end, ps;  // lines refused; the record being written, and the mask to give back
  uint32_t buf[LOG_RING / 4];
};
// the size in the name: a sketch that defines LOG_RING differently from the .cpp fails to link
#define LOG_CAT2(a, b) a##b
#define LOG_CAT(a, b) LOG_CAT2(a, b)
#define logRings LOG_CAT(logRings, LOG_RING)
extern LogRing logRings[LOG_CORES];

// in rtosLogger.cpp, in IRAM: the part every call site shares, so it is never a cache miss
typedef int (*LogFormat)(const uint32_t *args, const char *fmt, char *out, size_t cap);
uint32_t *logOpen(const char *fmt, LogFormat format, uint32_t words);  // nullptr: full, counted
void logClose();
extern std::atomic<uint32_t> logDrops;  // lines refused, or never taken by the cable

// where packets go, Serial until then. A packet is lines, each ending in a newline (text true), or
// records of one type after its 4 byte id. max is the transport's packet: mtu - 3 for a notify
typedef void (*LogOut)(const uint8_t *data, size_t n, bool text);
void logTo(LogOut out, size_t max = LOG_BATCH);

// one per type: its size in bytes, how the caller stores it, how the task reads it back
template <class T>
struct LogArg {
  static const uint32_t W = (sizeof(T) + 3) / 4;
  static LOG_INLINE uint32_t bytes(T) { return W * 4; }
  static LOG_INLINE uint32_t *put(uint32_t *p, T v, uint32_t) {
    // through a word array: straight into p the compiler cannot see the alignment, and measured
    // 16 byte stores instead of 4 word stores
    uint32_t t[W] = {};
    __builtin_memcpy(t, &v, sizeof v);
    for (uint32_t i = 0; i < W; i++) p[i] = t[i];
    return p + W;
  }
  static T get(const uint32_t *&p) {
    T v;
    memcpy(&v, p, sizeof v);
    p += W;
    return v;
  }
};

// measured before the ring is taken and copied no further than that: a string that changes in
// between gives wrong text, never a read past the record
template <>
struct LogArg<const char *> {
  static LOG_INLINE uint32_t bytes(const char *s) { return strnlen(s ? s : "(null)", LOG_BODY) + 1; }
  static LOG_INLINE uint32_t *put(uint32_t *p, const char *s, uint32_t n) {
    memcpy(p, s ? s : "(null)", n - 1);  // the length already measured: word copies in the ESP32's ROM
    ((char *)p)[n - 1] = 0;
    return p + (n + 3) / 4;
  }
  static const char *get(const uint32_t *&p) {
    const char *s = (const char *)p;
    p += (strlen(s) + 4) / 4;
    return s;
  }
};
template <>
struct LogArg<char *> : LogArg<const char *> {};

// C++11's std::apply: read one argument, recurse with it carried, snprintf at the end
template <class... Rest>
struct LogUnpack;

template <>
struct LogUnpack<> {
  template <class... Got>
  static int call(char *out, size_t cap, const char *fmt, const uint32_t *, Got... got) {
    return snprintf(out, cap, fmt, got...);
  }
};

template <class T, class... Rest>
struct LogUnpack<T, Rest...> {
  template <class... Got>
  static int call(char *out, size_t cap, const char *fmt, const uint32_t *p, Got... got) {
    auto v = LogArg<T>::get(p);  // own statement: read before the recursion
    return LogUnpack<Rest...>::call(out, cap, fmt, p, got..., v);
  }
};

template <class... A>
int logFormat(const uint32_t *args, const char *fmt, char *out, size_t cap) {
  return LogUnpack<A...>::call(out, cap, fmt, args);
}

// each argument's size noted and summed into words in one go: a constant unless there is a %s,
// and one strnlen per %s. A loop over an array of them did not fold at -Os
LOG_INLINE uint32_t logWords(uint32_t *) { return 0; }
template <class T, class... R>
LOG_INLINE uint32_t logWords(uint32_t *b, T v, R... r) {
  return ((*b = LogArg<T>::bytes(v)) + 3) / 4 + logWords(b + 1, r...);
}

template <class... A>
LOG_INLINE void logBuild(const char *fmt, A... args) {
  uint32_t w[sizeof...(A) + 1];  // each argument's bytes
  uint32_t *p = logOpen(fmt, &logFormat<A...>, logWords(w, args...));
  if (!p) return;
  const uint32_t *at = w;
  int ordered[] = {0, (p = LogArg<A>::put(p, args, *at++), 0)...};  // a braced list runs in order
  (void)ordered, (void)at;
  logClose();
}

// logBin(value): a plain struct into the ring as bytes, sent as bytes. It has no number: its id is
// its name and size, hashed, so both ends agree on their own and a changed struct is a new id
struct LogBinType { uint32_t id, size; };
uint32_t logBinId(const char *pretty, uint32_t size);  // in the .cpp: FNV-1a, the README has it

// the drain calls this, never the caller: the id is worked out on the other core
template <class T>
LogBinType logBinType() { return {logBinId(__PRETTY_FUNCTION__, sizeof(T)), sizeof(T)}; }

template <class T>
LOG_INLINE void logBin(const T &v) {
  static_assert(std::is_trivially_copyable<T>::value && sizeof(T) + 4 <= LOG_BATCH,
                "logBin takes a plain struct of at most LOG_BATCH - 4 bytes");
  // the type's function where a line's fmt goes; no format marks a record
  uint32_t *p = logOpen((const char *)(uintptr_t)&logBinType<T>, nullptr, LogArg<T>::W);
  if (!p) return;
  // a word at a time: copied whole, a 32 byte struct measured three memcpy calls at the call site
  const uint8_t *s = (const uint8_t *)&v;
  const size_t whole = sizeof(T) / 4, tail = sizeof(T) % 4;
  for (size_t i = 0; i < whole; i++) {
    uint32_t w;
    __builtin_memcpy(&w, s + 4 * i, 4);
    p[i] = w;
  }
  if (tail) {
    uint32_t w = 0;
    __builtin_memcpy(&w, s + 4 * whole, tail);
    p[whole] = w;
  }
  logClose();
}

// on the receiving end: calls fn for each T in a packet of Ts, and ignores any other packet
template <class T, class F>
void logBinRead(const uint8_t *data, size_t n, F fn) {
  LogBinType t = logBinType<T>();
  if (n < 4 || memcmp(data, &t.id, 4) || (n - 4) % t.size) return;
  for (data += 4, n -= 4; n; data += t.size, n -= t.size) {
    T v;
    memcpy(&v, data, sizeof v);
    fn(v);
  }
}

inline __attribute__((format(printf, 1, 2))) void logFormatCheck(const char *, ...) {}  // never called: the compiler's check

// the concatenation puts the level in the literal and refuses a non literal format
#define logAt(tag, fmt, ...) (false ? logFormatCheck(fmt, ##__VA_ARGS__) : logBuild(tag " " fmt, ##__VA_ARGS__))

// a level above LOG_LEVEL is a constant false branch: the compiler emits nothing for it
#ifndef LOG_LEVEL
#define LOG_LEVEL 5  // 1 error, 2 warn, 3 info, 4 debug, 5 verbose
#endif
#define logE(...) (LOG_LEVEL >= 1 ? logAt("[E]", __VA_ARGS__) : (void)0)
#define logW(...) (LOG_LEVEL >= 2 ? logAt("[W]", __VA_ARGS__) : (void)0)
#define logI(...) (LOG_LEVEL >= 3 ? logAt("[I]", __VA_ARGS__) : (void)0)
#define logD(...) (LOG_LEVEL >= 4 ? logAt("[D]", __VA_ARGS__) : (void)0)
#define logV(...) (LOG_LEVEL >= 5 ? logAt("[V]", __VA_ARGS__) : (void)0)
