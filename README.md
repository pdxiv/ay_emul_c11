# ay_emul_c11

A from-scratch C11 port of [Ay_Emul](https://sourceforge.net/projects/ay-emul/), originally written in Object Pascal (FPC/Lazarus, GTK2), targeting **Linux only**.

## Status

Planning stage. No code has been ported yet — this repository currently contains only the feasibility assessment in [PORTING_TO_C11_LINUX.md](PORTING_TO_C11_LINUX.md), which should be read before any porting work begins.

## Source

The full original Object Pascal program is included as the [`ay_emul`](ay_emul/) git submodule (`git@github.com:pdxiv/ay_emul.git`). Clone with `git submodule update --init` (or `git clone --recurse-submodules`) to fetch it — it's the source of truth referenced throughout PORTING_TO_C11_LINUX.md.

## Scope

This port covers **chiptune playback and WAV export only**: the AY/YM/SNDH/Atari-ST/etc. formats emulated by the original `Players.pas`, plus exporting rendered audio to WAV. Playback of general audio formats (MP3/OGG/FLAC/etc.) and tracker modules via BASS, non-WAV export, internet-radio streaming, MIDI, and physical Audio CD playback are all out of scope — see PORTING_TO_C11_LINUX.md §4 for the rationale.

CPU cores are decided: Z80 emulation will use [superzazu/z80](https://github.com/superzazu/z80) (single-file C, MIT-licensed), and the 68000 core (Atari ST) will use [Musashi](https://github.com/kstenerud/Musashi). Both still need a fidelity gate (ZEXALL/ZEXDOC plus undocumented-flag verification for the Z80 core) before integration — see PORTING_TO_C11_LINUX.md §7.1.

The recommended build order (§8 of that document):

0. Run the Z80 fidelity gate (ZEXALL/ZEXDOC, undocumented-flag checks) before writing engine code
1. Core CPU/sound emulation as a standalone C static library
2. Format parsers (hand-ported, no library equivalent exists)
3. ALSA output
4. A minimal CLI/headless player + WAV export
5. GUI (GTK3/4), as a separate, later effort

## Porting conventions

Per this workspace's global LLM-porting invariants, incomplete or approximated behavior must be tracked as explicit migration debt (`MIG-####` entries in a machine-checkable ledger such as `migration_debt.yaml`), not silently omitted. No such ledger exists yet — it should be created once porting work starts (see §9 of PORTING_TO_C11_LINUX.md).
