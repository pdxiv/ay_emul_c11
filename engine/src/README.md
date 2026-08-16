# engine/src/

Top-level implementation for the shared engine layer: unified format detection/dispatch used by both the CLI player and the GUI. (Subdirectories `formats/`, `hw/`, `util/` have their own README.md files.)

## player.c

Implements the dispatcher declared in `ay_engine/player.h`: byte-pattern/string matching helpers (`has_at`, `str_at`) and per-format content-signature and extension-based detection logic, plus multi-song helpers (`player_song_count`, `player_load_song`). Originally the CLI player's own private implementation, promoted verbatim into the shared engine layer at the Phase 5 GUI kickoff (see `migration_debt.yaml`).

Ported from: Not a direct file-for-file port. It is new dispatch/glue code, but the format signatures it checks are re-derived from `ay_emul/Ay_Emul.fmt` and `ay_emul/Players.pas` rather than translated from one specific Pascal unit.
