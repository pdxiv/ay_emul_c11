# tools/ay_player/src/

Source for `ay_player`, a minimal CLI player/WAV exporter for the five
chiptune formats `engine/libayengine.a` supports (AY/YM/PT3/SNDH/VTX). This
is Phase 4 tool infrastructure built on top of the ported playback engine, not
a port of a Pascal GUI window.

## alsa_output.c

Implements the `alsa_output` API declared in `ay_player/alsa_output.h`:
opens the "default" ALSA PCM device for interleaved S16_LE stereo output at a
given sample rate, writes frames with one-shot `-EPIPE`/underrun recovery via
`snd_pcm_prepare`/`snd_pcm_recover`, and drains/closes the device. Defines
`_GNU_SOURCE` before any include (even its own headers) as a documented
workaround for a `<alsa/asoundlib.h>` `struct timespec` redeclaration issue
under strict `-std=c11`.

Not a port — new CLI/tool infrastructure, a thin wrapper directly over ALSA's
`asoundlib.h`. Replaces the original's large hand-maintained `fpalsa` Pascal
binding rather than porting it (see the file's own header comment and
`ay_player/alsa_output.h`).

## wav.c

Implements the `wav_writer` API: builds a 44-byte little-endian RIFF/WAVE/fmt
/data header, writes a placeholder copy on open, streams raw interleaved
int16 PCM frames on write, and rewrites the header with the real data length
on close.

Ported from: `Convs.pas` — matches `Convs.pas:88-105`'s `TWaveFileHeader`
layout field-for-field (`rId`/`rLen`/`wId`/`fId`/`fLen`/`wFormatTag`/
`nChannels`/`nSamplesPerSec`/`nAvgBytesPerSec`/`nBlockAlign`/
`FormatSpecific`/`dId`/`dLen`) and its seek-write-seek-rewrite pattern from
`Convs.pas:525,551-555`, per the file's own header comment.

## main.c

CLI entry point: parses `<file> [--wav=<path>] [--seconds=N] [--frames=N]
[--ignore-end]`, reads the input file, loads it via `engine`'s
`player_load`, and either renders to a WAV file (`render_to_wav`, using
`wav_writer`) or plays it live over ALSA (`play_via_alsa`, using
`alsa_output`), pumping fixed-size buffers from `player_make_buffer` until
the requested frame/second bound or the player's own natural end
(`player_real_end_all`) is reached. `--ignore-end` sets `player_set_do_loop`
so playback always renders the full requested length instead of stopping
early, used for byte-for-byte oracle comparison tests.

Not a port — new CLI/tool infrastructure (Phase 4 of
`PORTING_TO_C11_LINUX.md`'s phased porting plan, per its own header comment).
It drives the ported `engine` library (`ay_engine/player.h`) but has no
single corresponding Pascal source file; it plays the same functional role as
`MainWin.pas`'s playback-invocation logic, but as a headless CLI rather than
a GUI window, so it is not treated as a port of that file.
