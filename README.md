# esp-rtosLogger

Logging for boards where the caller cannot wait. `Serial.printf` formats on the spot and one
`%f` costs about 170 us on an ESP32-S3. Here the call just copies its arguments into a queue,
about 3 us, and a background task formats and prints them later.

## Use

```cpp
#include <rtosLogger.h>

void setup() { Serial.begin(115200); }   // nothing else to start

void loop() {
  logI("rpm=%d temp=%.2f", rpm, temp);   // -> [I] rpm=1200 temp=31.50
  logW("over %d A", amps);               // -> [W] over 12 A
  logE("trip: %s", why);                 // -> [E] trip: stall
  logD("pwm=%u", duty);                  // -> [D] pwm=812
  logV("adc raw %d", raw);               // -> [V] adc raw 2048
}
```

Works from an interrupt too. Lines go to `Serial`.

`#define LOG_LEVEL 3` before the include keeps error, warn and info and compiles the rest to
nothing. Default is 5, everything.

## Speed

ESP32-S3 at 240 MHz, called from a 20 kHz loop on core 1, the task on core 0:

| | mean | p50 | p99 |
|---|---|---|---|
| `logI` with two arguments | 3.7 us | 3.2 us | 7.5 us |
| the same from a 30 kHz interrupt | 2.7 us | 2.7 us | 2.7 us |
| `Serial.printf` | 50 to 90 us | | |

nRF52840 at 64 MHz: about 48 us, all of it the queue send itself.

## Limits

- The format has to be a literal. The compiler checks it against the arguments.
- A `%s` is copied at the call, so the buffer can die right after.
- Arguments past 500 bytes, or more than 12 lines waiting, drop the line and count it in
  `logDrops`. Never blocks.
- A line past 512 bytes loses its tail.

## Somewhere else than Serial

Define either one in the sketch and the default steps aside:

```cpp
bool logReady() { return connected; }        // false: nothing is even formatted
void logSink(const char *batch, size_t n) {  // several lines, each ends in a newline
  characteristic->setValue((uint8_t *)batch, n);
  characteristic->notify();                  // one packet carries the whole batch
}
```

`examples/CustomSink` sends them over BLE.

## Flags

`LOG_LEVEL` is a `#define` before the include. The rest are build flags, arduino-cli
`--build-property "compiler.cpp.extra_flags=-DLOG_CORE=1"` or PlatformIO `build_flags`:

| flag | default | what |
|---|---|---|
| `LOG_LEVEL` | 5 | 1 error, 2 warn, 3 info, 4 debug, 5 verbose |
| `LOG_BODY` | 500 | bytes of arguments per line |
| `LOG_DEPTH` | 12 | lines the queue holds |
| `LOG_DRAIN_MS` | 25 | how often the task looks |
| `LOG_BATCH` | 512 | the most one `logSink` call carries |
| `LOG_CORE` | 0 | ESP32: the task's core. Keep it off the one that cannot wait |
| `LOG_PRIO` | 1 | the task's priority |
| `LOG_STACK` | 4096 | bytes on the ESP32, words on the nRF52 |

## Boards

ESP32 on Arduino-ESP32 3.x, nRF52 on the Adafruit core.
