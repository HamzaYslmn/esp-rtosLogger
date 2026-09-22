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

`src/rtosLogger.h` holds the templates and the macros, so the caller's side inlines. The queue
and the task are in `src/rtosLogger.cpp`, because a header only queue would give a sketch with
two translation units two queues and two tasks.

`logReady()` and `logSink()` are weak. A sketch that defines either one replaces the Serial
default. That is the only extension point, and it stays that way.

`LOG_LEVEL` is a sketch `#define`. Everything else is a build flag, because `LOG_BODY` and the
rest also size things inside the .cpp, which a sketch define never reaches.

## Always

- Keep it small. This library exists because the alternatives are bloated.
- Comments are one line and say why.
- No em dashes. Everything in English.
- Ask before every commit and every push.
- Never add AI attribution to a commit.
- Numbers in the README are measured on real hardware. Do not add one that was not.
