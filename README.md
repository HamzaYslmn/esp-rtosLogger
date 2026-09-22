# esp-rtosLogger

Logging for boards where the caller cannot wait. `Serial.printf` formats on the spot and one
`%f` costs about 170 us on an ESP32-S3. Here the call just writes its arguments into a ring of
its own core, about a microsecond, and a background task formats and prints them later.

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

Works from an interrupt too, and from any number of tasks on either core at once. Lines go to
`Serial`.

`#define LOG_LEVEL 3` before the include keeps error, warn and info and compiles the rest to
nothing. Default is 5, everything.

## Speed

Measured on the boards under a motor drive's load: SimpleFOC at 45k rpm with its Hall
interrupts, BLE to a pedal and a PC, 100 to 2000 lines a second. The cost of the call itself:

| | mean | worst |
|---|---|---|
| ESP32-S3, 240 MHz, a loop in IRAM | 0.9 us | 1.4 us |
| ESP32-S3, from an interrupt | 0.5 us | 0.7 us |
| ESP32-S3, a loop in flash | 4 to 6 us | 15 to 17 us, the cache misses |
| nRF52840, 64 MHz | 4.4 us | 6.4 us |
| nRF52840, from an interrupt | 2.0 us | 2.2 us |
| `Serial.printf`, for scale | 85 us on the S3, 48 on the nRF52 | 273 us, 64 us |

The task pays about 25 us a line on the other core, most of it `snprintf`. Nothing was lost at
2000 lines a second.

## Limits

- The format has to be a literal. The compiler checks it against the arguments.
- A `%s` is copied at the call, so the buffer can die right after. It costs by its length, and a
  `%s` past 500 bytes is cut.
- A full ring drops the line and counts it in `logDrops`. Never blocks.
- A line past 512 bytes loses its tail.
- Lines keep their order within a core, not across the two.
- Not from an interrupt above level 3 on the ESP32 or from the NMI, the same rule as FreeRTOS's
  `FromISR` calls.
- With the calling loop in flash, a call that has not run lately pays for its cache misses, up to
  about 15 us on an S3. Put a loop that cannot wait in IRAM (`IRAM_ATTR`) and it is the table's
  numbers.

## Somewhere else than Serial

`logTo` says where lines go, Serial until it is called:

```cpp
logTo([](const char *text, size_t n) {  // several lines, each ends in a newline
  if (Serial) Serial.write(text, n);
  if (linked) out->setValue((uint8_t *)text, n), out->notify();  // one BLE packet, the whole batch
});
```

`examples/SerialAndBle` is the whole sketch.

## Flags

`LOG_LEVEL` is a `#define` before the include. The rest are build flags, arduino-cli
`--build-property "compiler.cpp.extra_flags=-DLOG_CORE=1"` or PlatformIO `build_flags`:

| flag | default | what |
|---|---|---|
| `LOG_LEVEL` | 5 | 1 error, 2 warn, 3 info, 4 debug, 5 verbose |
| `LOG_RING` | 4096 | bytes of ring per core, about 170 short lines |
| `LOG_BODY` | 500 | the longest `%s` kept |
| `LOG_DRAIN_MS` | 25 | how often the task looks |
| `LOG_BATCH` | 512 | the most one `logTo` call carries |
| `LOG_CORE` | 0 | ESP32: the task's core. Keep it off the one that cannot wait |
| `LOG_PRIO` | 1 | the task's priority |
| `LOG_STACK` | 4096 | bytes on the ESP32, words on the nRF52 |

## Boards

ESP32 on Arduino-ESP32 3.x, nRF52 on the Adafruit core.
