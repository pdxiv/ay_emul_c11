# tools/ay_player/include/ay_player/

Public headers for `ay_player`, the minimal CLI player/WAV exporter tool built
on `engine/libayengine.a`. Each header declares the interface implemented by
the matching file in `tools/ay_player/src/`.

## alsa_output.h

Declares `alsa_output`, an opaque handle for a default ALSA PCM playback
device, plus `alsa_output_open`/`alsa_output_write`/`alsa_output_close` and
the `alsa_output_status` error enum (no device, bad params, unrecoverable
write failure). Documents that failures must degrade gracefully (a stderr
message and a non-OK status) rather than crash, since no ALSA device is
expected to be present in sandboxed/CI environments.

Not a port — new CLI/tool infrastructure. Its own header comment states this
explicitly: it is a thin, hand-written wrapper directly over
`<alsa/asoundlib.h>`, added per `PORTING_TO_C11_LINUX.md` §4.2's decision to
avoid re-implementing the original's ~1854-line hand-maintained `fpalsa`
Pascal/Lazarus ALSA binding. There is no single Pascal source file it mirrors.

## wav.h

Declares `wav_writer` and `wav_writer_open`/`wav_writer_write`/
`wav_writer_close`, a hand-rolled streaming PCM WAV file writer (open with a
placeholder header, stream raw interleaved int16 frames, rewrite the header
with the real frame count on close).

Ported from: `Convs.pas` — the header comment states this matches
`Convs.pas`'s `TWaveFileHeader` layout (`Convs.pas:88-105`) field-for-field,
and mirrors its placeholder-header-then-seek-back-and-rewrite pattern
(`Convs.pas:525,551-555`) exactly, specifically so the WAV output can be
byte-compared against the Pascal oracle. `Convs.pas` exists in `./ay_emul/`.
