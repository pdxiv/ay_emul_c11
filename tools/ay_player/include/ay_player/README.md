# tools/ay_player/include/ay_player/

Public headers for `ay_player`'s two platform-facing helpers: live ALSA audio output and WAV file writing. Both are consumed by `tools/ay_player/src/main.c`'s playback/render loop; neither wraps a single Pascal unit one-to-one, since the original ay_emul was a Windows/Lazarus GUI application with no standalone CLI player.

## alsa_output.h

Declares the opaque `alsa_output` handle and `alsa_output_open`/`alsa_output_write`/`alsa_output_close`, a thin wrapper around `<alsa/asoundlib.h>` for blocking, stereo16, interleaved PCM playback to the default device, with distinct error codes for "no device" vs "bad params" vs "write failure" so the caller can fail gracefully rather than crash.

Ported from: no Pascal equivalent. The original used a Windows-only DirectSound/waveOut-style output backend (not present in this repo's Pascal sources); this is new Linux/ALSA-native code written for the C11 port, per PORTING_TO_C11_LINUX.md §4.2 (see migration_debt.yaml/migration_debt_validated.yaml MIG-0025, closed as validated).

## wav.h

Declares `wav_writer` and `wav_writer_open`/`wav_writer_write`/`wav_writer_close`, a hand-rolled 44-byte PCM RIFF/WAVE header writer using the placeholder-then-rewrite pattern (write a zero-length header, stream PCM, seek back and patch in the real frame count once known).

Ported from: ay_emul/Convs.pas (the `WaveFileHeader` record, Convs.pas:88-105, and `WAV_Converter`'s header-populate / bounded `MakeBuffer` loop / seek-back-and-rewrite structure, Convs.pas:500-561). `WAV_Converter` itself is GUI-entangled (`FrmPLst.GetVarsForSave`, `ShowProgress`, `Application.ProcessMessages`) and not callable headlessly, so only its core header/streaming logic is carried over, matching byte-for-byte against Convs.pas's field layout (see migration_debt_validated.yaml MIG-0026, closed as validated for AY/YM/PT3/VTX; SNDH WAV export is tracked separately as incomplete under MIG-0021).
