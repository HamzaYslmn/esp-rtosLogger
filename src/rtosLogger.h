// rtosLogger: printf style logging that costs the caller a queue send.
//
//   logI("rpm=%d temp=%.2f", rpm, temp);   ->   [I] rpm=1200 temp=31.50
//
// The caller copies the raw arguments into a record and leaves. A low priority task reads them
// back as the same types and calls the real snprintf later, so every specifier works and no
// float formatting runs at the call. The format must be a literal; a %s is copied at the call.
// C++11 only: the nRF52 core compiles at gnu++11.
#pragma once
#include <Arduino.h>
#include <atomic>

#ifndef LOG_BODY
#define LOG_BODY 500  // build flag, the .cpp sizes the queue by it. 512 byte record with the header
#endif

#define LOG_INLINE inline __attribute__((always_inline))  // a call out to flash measured 3 us

struct LogRec {
  const char *fmt;                                // a literal, level included
  int (*format)(const LogRec &, char *, size_t);  // picked per call site by the compiler
  uint16_t len;                                   // body in use, or OVER
  uint8_t body[LOG_BODY];
  static const uint16_t OVER = 0xFFFF;
};

// one pair per type: how the caller stores it, how the task reads it back
template <class T>
struct LogArg {
  static LOG_INLINE void put(LogRec &r, T v) {
    if (r.len > sizeof r.body - sizeof v) return (void)(r.len = LogRec::OVER);
    const uint8_t *b = (const uint8_t *)&v;
    for (size_t i = 0; i < sizeof v; i++) r.body[r.len + i] = b[i];  // memcpy is a ROM call
    r.len += sizeof v;
  }
  static T get(const uint8_t *&p) {
    T v;
    memcpy(&v, p, sizeof v);
    p += sizeof v;
    return v;
  }
};

template <>
struct LogArg<const char *> {
  static LOG_INLINE void put(LogRec &r, const char *s) {
    if (r.len >= sizeof r.body - 1) return (void)(r.len = LogRec::OVER);
    if (!s) s = "(null)";
    size_t n = 0;
    for (; s[n] && r.len + n < sizeof r.body - 1; n++) r.body[r.len + n] = s[n];  // a long string is cut
    r.len += n;
    r.body[r.len++] = 0;
  }
  static const char *get(const uint8_t *&p) {
    const char *s = (const char *)p;
    p += strlen(s) + 1;
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
  static int call(char *out, size_t cap, const char *fmt, const uint8_t *, Got... got) {
    return snprintf(out, cap, fmt, got...);
  }
};

template <class T, class... Rest>
struct LogUnpack<T, Rest...> {
  template <class... Got>
  static int call(char *out, size_t cap, const char *fmt, const uint8_t *p, Got... got) {
    auto v = LogArg<T>::get(p);  // own statement: read before the recursion
    return LogUnpack<Rest...>::call(out, cap, fmt, p, got..., v);
  }
};

template <class... A>
int logFormat(const LogRec &r, char *out, size_t cap) {
  const uint8_t *p = r.body;
  return LogUnpack<A...>::call(out, cap, r.fmt, p);
}

// in rtosLogger.cpp
void logPost(const LogRec &r);          // safe from a task or an interrupt
extern std::atomic<uint32_t> logDrops;  // lines refused, cut, or never taken by the sink

// Serial by default. Define either in the sketch to send lines elsewhere
bool logReady();                            // false: nothing is formatted
void logSink(const char *batch, size_t n);  // several lines, each ending in a newline

template <class... A>
LOG_INLINE void logBuild(const char *fmt, A... args) {
  LogRec r;
  r.fmt = fmt, r.format = &logFormat<A...>, r.len = 0;
  int ordered[] = {0, (LogArg<A>::put(r, args), 0)...};  // a braced list is evaluated in order
  (void)ordered;
  logPost(r);
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
