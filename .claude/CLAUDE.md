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

`logTo(fn)` says where lines go, Serial until it is called. That is the only extension point, and
it stays that way. With the Serial default and no USB host, nothing is formatted.

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
