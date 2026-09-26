# esp-rtosLogger

`printf` style logging that does not make your code wait, for ESP32 and nRF52.

`Serial.printf` takes 85 us or more, too slow for a motor loop or an interrupt. `logI` just saves
your numbers and returns in about 1 us. A background task prints them a moment later.

## Quick start

```cpp
#include <rtosLogger.h>

void setup() { Serial.begin(115200); }

void loop() {
  logI("rpm=%d temp=%.2f", rpm, temp);  // [I] rpm=1200 temp=31.50
  logW("over %d A", amps);              // [W] over 12 A
  logE("trip: %s", why);                // [E] trip: stall
  logD("pwm=%u", duty);                 // [D] pwm=812
  logV("adc raw %d", raw);              // [V] adc raw 2048
}
```

It works from any task, either core, and inside an interrupt. `#define LOG_LEVEL 3` before the
include keeps only errors, warnings and info.

## How fast

| | `Serial.printf` | `logI` |
|---|---|---|
| ESP32-S3 | 85 us, worst 273 | **0.9 us**, worst 1.4 |
| ESP32-S3, in an interrupt | not safe | **0.5 us** |
| nRF52840 | 48 us, worst 64 | **4.4 us**, worst 6.4 |

Measured while a motor drive and Bluetooth ran.

## Keeping your busy core free (ESP32)

1. **Put the busy function in RAM** with `IRAM_ATTR`. From flash a `logI` can cost up to 17 us.
   With many lines in RAM, add a file `build_opt.h` next to your sketch holding
   `-mtext-section-literals`.
2. **Start Serial on core 0.** A port's interrupt runs on the core that called `begin`, and
   `setup()` is core 1. The examples all do it this way:

```cpp
void core0Task(void *) { Serial.begin(115200); vTaskDelete(nullptr); }
void core1Task(void *) { for (;;) motorLoop(); }

void setup() {
  xTaskCreatePinnedToCore(core0Task, "core0Task", 8192, nullptr, 1, nullptr, 0);
  xTaskCreatePinnedToCore(core1Task, "core1Task", 8192, nullptr, 2, nullptr, 1);
}
void loop() { vTaskDelete(nullptr); }
```

3. **On an S3 USB port in TinyUSB mode**, also add these to `core0Task`. USB has a task of its own
   that would otherwise run on core 1:

```cpp
vTaskPrioritySet(xTaskGetHandle("usbd"), 1);
Serial.setTxTimeoutMs(0);
```

On an S3 at 2000 lines a second, `begin` in `setup()` cost core 1 2.4% (hardware CDC) to 5.1%
(TinyUSB). Done as above, 0.45%, and no line was lost.

## Send lines somewhere else

Lines go to `Serial` until you make an output. Once you make any, they go to the ones you made:

```cpp
void bleSend(const uint8_t *data, size_t n, bool text) {
  if (linked) out->setValue((uint8_t *)data, n), out->notify();
}

LogOutput serial(logSerial);  // the built in Serial writer
LogOutput ble(bleSend);

logI("to both");
logI(ble, "BLE only");
logW(serial | ble, "any combination");
```

A second argument caps the packet size: `LogOutput air(airSend, 250)` for ESP-NOW. Eight outputs at
most. For Bluetooth, raise the MTU (`BLEDevice::setMTU(517)`): at the default a packet arrives in
20 byte pieces, and `logFrom` ignores them.

## Send structs instead of text

For telemetry, send the struct itself. Nothing is formatted, and it is smaller.

```cpp
struct Sample { uint32_t ms; int16_t rpm, amps; };  // in a header both boards include

logBin(Sample{millis(), rpm, amps});  // sender

logFrom(data, n);                     // receiver, where packets arrive
static Sample s;
if (logBinRead(s)) show(s);           // true when a newer one came
```

Any plain struct up to 508 bytes. Both boards match it by its name and size, so there is nothing to
number.

## Examples

| example | what |
|---|---|
| `Basic` | Serial only, from a loop and a pin interrupt. ESP32 or nRF52 |
| `Loopback` | one board, shows both kinds of packet byte by byte |
| `SerialAndBle` + `BleReceiver` | two ESP32s over a Bluetooth notify |
| `SerialAndEspNow` + `EspNowReceiver` | two ESP32s over ESP-NOW, no pairing |

`uv run python/programmer.py` flashes an example and opens a serial monitor, or watches
`SerialAndBle` over Bluetooth.

## Limits

- The format must be a string literal.
- A full buffer drops the line and counts it in `logDrops`. A call never waits.
- Lines keep their order within a core, not across both.
- `%s` strings past 500 bytes are cut.

## Settings

Build flags, for example `--build-property "compiler.cpp.extra_flags=-DLOG_RING=8192"`.

| flag | default | what |
|---|---|---|
| `LOG_LEVEL` | 5 | 1 error ... 5 verbose. A `#define`, not a flag |
| `LOG_RING` | 4096 | buffer bytes per core |
| `LOG_DRAIN_MS` | 25 | how often lines are printed |
| `LOG_BATCH` | 512 | largest packet |
| `LOG_CORE` | 0 | ESP32: the background task's core |
| `LOG_PRIO` | 1 | its priority |
| `LOG_STACK` | 4096 | its stack |

## Boards

ESP32 with Arduino-ESP32 3.x, nRF52 with the Adafruit core.
