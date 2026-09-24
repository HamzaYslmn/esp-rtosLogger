# esp-rtosLogger

An Arduino library: deferred printf logging on a FreeRTOS task. Two files in `src/`, two
examples, and that is the whole thing. README.md is the only doc, written for a stranger.

## Releasing

The Arduino Library Manager indexes a tag only if it matches `version` in `library.properties`.
Mismatch, or no tag at all, and the registry check fails. So a release is always both:

```bash
sed -i 's/^version=.*/version=0.0.2/' library.properties
git commit -am "version 0.0.2" && git tag -a 0.0.2 -m "0.0.2"
git push origin main && git push origin 0.0.2
```

No pull request for a new version. The indexer picks the tag up within a day. Its logs:
http://downloads.arduino.cc/libraries/logs/github.com/HamzaYslmn/esp-rtosLogger/

The library is already in the index, accepted 2026-09-22 as arduino/library-registry#9175. That
was a one time thing. Only the tag matters now.

## The shape of it

`src/rtosLogger.h` holds the templates and the macros, so a call site inlines only its argument
stores. One ring per core, `logOpen` / `logClose` (in IRAM) and the task are in
`src/rtosLogger.cpp`, because a header only ring would give a sketch with two translation units two
of everything. Each ring is written only from its own core, with that core's interrupts masked,
so no lock is ever shared between cores. Keep the call site small: in a sketch whose loop runs from
flash, every cache line the call touches costs about a microsecond, which is most of what a cold
call costs. `sim/` (gitignored) is the rig that measures it.

A `LogOutput` says where packets go: Serial until one is made, then only the ones made. That is
the only extension point, and it stays that way. `logI(out, ...)` names outputs, every one
otherwise; the record carries them, with its level, in its length word, and the drain writes the
`[I] ` tag, since a format that no longer comes first cannot be glued to it. A packet holds one
kind for one set of outputs, cut at the smallest of their `max`. A record no live output wants,
Serial with no USB host included, is never formatted. An output made twice with the same function
is one output, so a header can make one. Anything else, a connection, a rate, is the send
function's.

**Keep a call's constant small.** The word is packed level, length, then the outputs a call skips,
so a line to every output is a constant under 2048, which the call site loads as an immediate. The
first 0.4.0 put the outputs on top, `0xFF03xxxx`: the compiler read it from a literal pool, which
for a call site in flash is a cache miss, and a call from a BLE callback went from 1.7 us to 2.2.

`logBin(struct)` shares the rings: a record carries its type's `logBinType<T>` where a line
carries its format, and a null `format` word marks it. Its id is FNV-1a over the type's name and
size, worked out by the drain, never at the call, so both ends agree without numbering anything.

`LOG_LEVEL` is a sketch `#define`. Everything else is a build flag, because `LOG_RING` and the
rest also size things inside the .cpp, which a sketch define never reaches. `LOG_RING` is in the
ring's symbol name, so a mismatch fails to link instead of writing past the ring.

## Always

- Keep it small. This library exists because the alternatives are bloated.
- Comments are one line and say why.
- No em dashes. Everything in English.
- Ask before every commit and every push.
- Never add AI attribution to a commit.
- Numbers in the README are measured on real hardware. Do not add one that was not.
