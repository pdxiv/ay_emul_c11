# engine/include/ay_engine/

Top-level public header for the shared engine layer: unified dispatch/detection over the file formats the engine can load and play. (Subdirectories `formats/`, `hw/`, `util/` have their own README.md files.)

## player.h

Declares the format-detection dispatcher used by both the CLI player and the GUI: content-signature-based and extension-based detection across all supported tracker/register-dump/SNDH formats, plus `player_song_count()` / `player_load_song()` helpers for multi-song files. It was originally the CLI player's private header (`tools/ay_player/include/player/format.h`) and was promoted here unchanged (mechanical move, no dispatch-logic change) at the Phase 5 GUI kickoff so `gui/` could share it, per `migration_debt.yaml`'s "Phase 5 kickoff" entry.

Ported from: Not a direct file-for-file port. It is new dispatch/glue code, but its format signatures and detection order are re-derived from `ay_emul/Ay_Emul.fmt` and `ay_emul/Players.pas` (cited per-format in the file's own header comment) rather than being a translation of one specific Pascal unit.
