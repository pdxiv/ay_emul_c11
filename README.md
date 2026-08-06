# ay_emul_c11

A from-scratch C11 port of [Ay_Emul](https://sourceforge.net/projects/ay-emul/), originally written in Object Pascal (FPC/Lazarus, GTK2), targeting **Linux only**.

## Status

Phases 0–4 of the plan below are done and oracle-validated (see `migration_debt.yaml` for still-open MIG-#### entries and `migration_debt_validated.yaml` for closed-out ones — split across two files to keep each one workable in size; see "Porting conventions" below). Phase 5 (GUI)'s kickoff milestone is done: a working GTK2 skinned playback window plus a `.lfm`-to-C11-skeleton generator proof of concept — see MIG-0063 through MIG-0067 for exactly what landed and what remains deferred. Concretely:

- **Engine** (`engine/`): Z80 (superzazu/z80) and 68000 (Musashi) CPU cores, the AY-3-8910/12 sound chip, Atari ST hardware (MFP/DMA-sound/scheduling), and format loaders for AY/YM/PT3/VTX (byte-identical against the real binary) and SNDH (loads and runs, but see MIG-0021 for its one known-incomplete gap: no audible output yet).
- **`tools/identify_ay_file/`**: a standalone AY/YM format identification utility (not part of playback), covering ~19 formats, oracle-diff-verified against the real binary for the ~9 it can fully verify this way.
- **`tools/ay_player/`**: a minimal CLI player/WAV exporter (`ay_player <file> [--wav=<path>] [--seconds=N]`) driving the engine through ALSA or a hand-rolled WAV writer, oracle-diff-verified byte-identical against the real binary's WAV export.
- **`gui/`**: a GTK2 skinned playback window reproducing `MainWin.pas`'s exact `.ays` skin format, window silhouette, and button/LED/slider hit-testing (see MIG-0063–MIG-0065) — Play/Pause/Stop/Open/volume all wired to real ALSA playback. `tools/lfm_gen/`: the `.lfm`-to-C11-skeleton generator, proven against two dialogs (MIG-0066). Everything else in Phase 5's scope (tray icon, drag-and-drop, visualizer, playlist, seeking, remaining dialogs, etc.) is explicitly deferred — see MIG-0067.

Read [PORTING_TO_C11_LINUX.md](PORTING_TO_C11_LINUX.md) before starting Phase 5 or touching anything foundational — it's the feasibility assessment and design-decision record this whole port follows. Phase 5's ongoing, feature-by-feature implementation status is tracked separately in [PHASE5_GUI_PROGRESS.md](PHASE5_GUI_PROGRESS.md), updated as work lands rather than once at the end.

## Source

The full original Object Pascal program is included as the [`ay_emul`](ay_emul/) git submodule (`git@github.com:pdxiv/ay_emul.git`). Clone with `git submodule update --init` (or `git clone --recurse-submodules`) to fetch it — it's the source of truth referenced throughout PORTING_TO_C11_LINUX.md, and the oracle every differential test in this port checks against.

The submodule may be extended with optional, additive oracle-harness diagnostic code (e.g. a new unit that drives the original engine directly with synthetic inputs and dumps state for comparison against the C11 port, gated behind an environment variable so normal use of the program is unaffected) — this is legitimate porting-support work. It must never be edited to change what the original code *computes* in order to make it agree with the C11 port; see PORTING_TO_C11_LINUX.md §8.1 for the full policy. If the two disagree, the presumption is a bug in the port, not the oracle.

## Scope

This port covers **chiptune playback and WAV export only**: the AY/YM/SNDH/Atari-ST/etc. formats emulated by the original `Players.pas`, plus exporting rendered audio to WAV. Playback of general audio formats (MP3/OGG/FLAC/etc.) and tracker modules via BASS, non-WAV export, internet-radio streaming, MIDI, and physical Audio CD playback are all out of scope — see PORTING_TO_C11_LINUX.md §4 for the rationale.

CPU cores are decided: Z80 emulation will use [superzazu/z80](https://github.com/superzazu/z80) (single-file C, MIT-licensed), and the 68000 core (Atari ST) will use [Musashi](https://github.com/kstenerud/Musashi). Both still need a fidelity gate (ZEXALL/ZEXDOC plus undocumented-flag verification for the Z80 core) before integration — see PORTING_TO_C11_LINUX.md §7.1.

The recommended build order (§8 of that document):

0. Run the Z80 fidelity gate (ZEXALL/ZEXDOC, undocumented-flag checks) before writing engine code — **done**
1. Core CPU/sound emulation as a standalone C static library — **done**
2. Format parsers (hand-ported, no library equivalent exists) — **done** (SNDH partially — MIG-0021)
3. ALSA output — **done**
4. A minimal CLI/headless player + WAV export — **done**
5. GUI (GTK2, matching the original), as a separate, later effort — **kickoff milestone done** (skinned playback window + `.lfm` generator proof of concept; see MIG-0063–MIG-0067 for landed vs. deferred scope)

## Porting conventions

Per this workspace's global LLM-porting invariants, incomplete or approximated behavior must be tracked as explicit migration debt (`MIG-####` entries in the machine-checkable ledger), not silently omitted — see §9 of PORTING_TO_C11_LINUX.md for the states used (`translated`/`behaviorally_incomplete`/`validated`). The ledger is split across two files to keep each one a manageable size: **`migration_debt.yaml`** holds every entry still in the `translated`/`behaviorally_incomplete` state (i.e. anything not yet closed out), and **`migration_debt_validated.yaml`** holds entries once they reach `state: validated`. When closing out an entry, move it from the former to the latter rather than just flipping its `state` in place. When auditing open debt or deciding what's safe to build on, check `migration_debt.yaml`; when looking for how a past area was validated, check `migration_debt_validated.yaml`.

**No C11 source file should exceed 600 lines.** If a file grows past that, split it into several smaller files along appropriate semantic boundaries (e.g. one file per format/subsystem/detector family, with shared types/helpers factored into their own file) rather than letting a single file accumulate unrelated concerns. See PORTING_TO_C11_LINUX.md §8.2 for the full rationale.
