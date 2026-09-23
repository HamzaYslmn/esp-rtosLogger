# esp-rtosLogger

`printf` style logging that does not make your code wait, for ESP32 and nRF52.

`Serial.printf` formats the text and pushes it out the port before it returns: 85 us on an
ESP32-S3, and 270 us when the port is busy. That is too slow for a motor loop or an interrupt.
`logI` only stores the numbers you pass and returns, in about a microsecond. A background task
turns them into text and prints them a few milliseconds later.

## Quick start

```cpp
#include <rtosLogger.h>

void setup() { Serial.begin(115200); }  // nothing else to start

void loop() {
  logI("rpm=%d temp=%.2f", rpm, temp);  // prints: [I] rpm=1200 temp=31.50
  logW("over %d A", amps);              // [W] over 12 A
  logE("trip: %s", why);                // [E] trip: stall
  logD("pwm=%u", duty);                 // [D] pwm=812
  logV("adc raw %d", raw);              // [V] adc raw 2048
}
```

It works the same from any task, on either core, and from inside an interrupt.

`#define LOG_LEVEL 3` before the include keeps errors, warnings and info, and removes the other two
from the program entirely. The default, 5, keeps everything.

## How fast

What the line of code costs the code that calls it, measured on real boards while they ran a
motor drive (SimpleFOC at 45 000 rpm with its interrupts) and Bluetooth:

| | `Serial.printf` | `logI` |
|---|---|---|
| ESP32-S3, typical | 85 us | **0.9 us** |
| ESP32-S3, worst | 273 us | **1.4 us** |
| ESP32-S3, inside an interrupt | not safe | **0.5 us**, worst 0.7 |
| nRF52840, typical / worst | 48 / 64 us | **4.4 / 6.4 us** |
| nRF52840, inside an interrupt | not safe | **2.0 us**, worst 2.2 |

**The ESP32 numbers need your loop in RAM.** Normal Arduino code runs from flash, through a small
cache the two cores share. A `logI` that has not run for a while must be fetched into that cache
first, and then it costs 4 to 6 us, up to 17 us at worst. Mark the function that cannot wait
with `IRAM_ATTR` and it runs from RAM, at the numbers above:

```cpp
void IRAM_ATTR motorLoop() { ... logI("trip at %d rpm", rpm); ... }
```

A sketch with more than a few lines in IRAM also needs a file named `build_opt.h` next to it,
holding one line: `-mtext-section-literals`.

The work does not vanish, it moves to the background task on the other core: about 140 us a line
of formatting, 250 us with USB's own work. At 2000 lines a second nothing was lost.

## Send lines somewhere else

`logTo` says where the text goes. Until you call it, it goes to `Serial`. This sends every line to
the serial port and over Bluetooth:

```cpp
logTo([](const uint8_t *data, size_t n, bool text) {  // several lines, each ending in a newline
  if (text && Serial) Serial.write(data, n);
  if (linked) out->setValue((uint8_t *)data, n), out->notify();
});
```

Add a second argument to cap the packet size for your transport: `logTo(fn, 250)` for ESP-NOW,
the MTU less 3 for a Bluetooth notify. The default is 512. A line is never split between packets.

The examples are whole sketches: `SerialAndBle` for Bluetooth, `SerialAndEspNow` to broadcast to
every ESP32 nearby.

## Send structs instead of text

For data you send often, such as telemetry, send the struct itself. Nothing is formatted, and it is
smaller on the air.

```cpp
struct Sample { uint32_t ms; int16_t rpm, amps; };  // in a header both boards include

logBin(Sample{millis(), rpm, amps});                // on the sender, from anywhere logI works
```

On the receiving board, for every packet that arrives, however it came:

```cpp
logBinRead<Sample>(data, n, [](const Sample &s) { ... });  // other packets are ignored
```

- Any plain struct up to 508 bytes. Declare it once, in a header both boards share.
- You never number it. Its id comes from its name and size, so both boards agree on their own, and
  a changed struct gets a new id that an old receiver ignores instead of misreading.
- Structs reach your `logTo` function with `text` false, as a 4 byte id and then the structs. The
  default `Serial` output skips them.
- `LOG_LEVEL` does not remove them: they are data, not messages.

Measured with a 32 byte packet of 16 fields, 1000 a second, against the same data as a `logI` line:

| | `logI` line | `logBin` |
|---|---|---|
| the call, ESP32-S3 in RAM | 0.7 us | **0.7 us** |
| the call, ESP32-S3 in flash, worst | 19 us | **9 us** |
| the call, nRF52840 | 4.9 us | **2.4 us** |
| the background task, per packet | about 430 us | **19 to 65 us** |
| bytes on the air | 77 | **32** |

Use `logI` for messages people read, `logBin` for data a program reads.

To read structs in an app that is not C++, work the id out from the struct's name and size:

```js
function logBinId(name, size) {  // matches the packet's first 4 bytes, little endian
  let h = 0x811c9dc5;
  for (const b of [...new TextEncoder().encode(name), size & 255, (size >> 8) & 255])
    h = Math.imul(h ^ b, 0x01000193) >>> 0;
  return h;
}
```

The name is as C++ spells it: `Sample`, or `ns::Sample` inside a namespace. Over a cable, which has
no packet edges, send each packet's length first. Structs are not hidden from anyone with the app:
pair Bluetooth with encryption if that matters.

## Limits

- The format must be a string literal. The compiler checks it against the arguments.
- A `%s` string is copied at the call, so its buffer can change right after. Its cost grows with its
  length, and past 500 bytes it is cut.
- If the buffer is full, the line is dropped and counted in `logDrops`. A call never waits.
- A line longer than a packet loses its end.
- Lines stay in order within a core, not across the two cores.
- On the ESP32, not from an interrupt above level 3 or the NMI, the same rule as FreeRTOS.

## Settings

`LOG_LEVEL` is a `#define` before the include. The others are build flags: in arduino-cli
`--build-property "compiler.cpp.extra_flags=-DLOG_RING=8192"`, in PlatformIO `build_flags`.

| flag | default | what |
|---|---|---|
| `LOG_LEVEL` | 5 | 1 error, 2 warn, 3 info, 4 debug, 5 verbose |
| `LOG_RING` | 4096 | buffer bytes per core, about 170 short lines |
| `LOG_BODY` | 500 | the longest `%s` kept |
| `LOG_DRAIN_MS` | 25 | how often the background task empties the buffers |
| `LOG_BATCH` | 512 | the largest packet |
| `LOG_CORE` | 0 | ESP32: the core the background task runs on. Keep it off your busy one |
| `LOG_PRIO` | 1 | the background task's priority |
| `LOG_STACK` | 4096 | its stack, bytes on the ESP32, words on the nRF52 |

## How it works

Each core has its own buffer. A call pauses its own core's interrupts for the few stores it takes,
so tasks and interrupts on one core never collide, and the two cores never wait for each other.
The background task wakes every 25 ms, formats what is waiting, and hands it to `logTo`.

## Boards

ESP32 with Arduino-ESP32 3.x, nRF52 with the Adafruit core.
