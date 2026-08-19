/* IntegrityCheck confirmation step, reusing engine/'s already-ported,
 * oracle-validated GetTimeXXX duration precomputes (migration_debt.yaml
 * MIG-0023b) instead of re-deriving them independently. Mirrors every
 * FoundXXX's own tail (Players.pas, e.g. FoundSTC:5159-5171):
 *   if IntegrityCheck then
 *     if not LoadTrackerModule(...) then exit;
 *     GetTimeXXX(M, TimeLength[, LoopPoint]);
 *     if TimeLength = 0 then exit;
 * i.e. a structural match alone is not enough - Pascal only accepts a
 * candidate once its computed duration is also nonzero. Covers the 12
 * formats engine/ currently has a GetTimeXXX port for (PT3 via MIG-0101,
 * SQT via MIG-0104, the other 10 via MIG-0103), PLUS ST1/ST3/STF (see
 * st_convert.h): those three have no engine/ port of their own at all,
 * but LoadTrackerModule converts them to STC's/STP's own layout at load
 * time in the real program too, so integrity_check_st1/st3/stf reuse the
 * exact same stc_file_load/stp_file_load via a conversion step first -
 * IMPORTANT: unlike every other function here, those three have NOT been
 * oracle-diff-validated against a real file (none exists anywhere in
 * this repo) - see st_convert.h's file comment and migration_debt.yaml
 * for the full caveat. FXM also has a GetTimeXXX port (MIG-0104) but is
 * deliberately NOT exposed here: Players.pas's Module_Detector never
 * calls FoundFXM (FXM is Tier-A-only in the real program, see
 * dispatch.c's top comment), so there is no structural match for it to
 * confirm in the first place - matches PSM's existing precedent (PSM
 * also has a GetTimePSM port, MIG-0103, unused by this tool for the same
 * reason). */
#ifndef IDENTIFY_INTEGRITY_CHECK_H
#define IDENTIFY_INTEGRITY_CHECK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool integrity_check_gtr(const uint8_t* data, size_t size);
bool integrity_check_stc(const uint8_t* data, size_t size);
bool integrity_check_psm(const uint8_t* data, size_t size);
bool integrity_check_asc(const uint8_t* data, size_t size, bool is_asc0);
bool integrity_check_stp(const uint8_t* data, size_t size);
bool integrity_check_fls(const uint8_t* data, size_t size);
bool integrity_check_pt2(const uint8_t* data, size_t size);
bool integrity_check_pt1(const uint8_t* data, size_t size);
bool integrity_check_psc(const uint8_t* data, size_t size);
bool integrity_check_ftc(const uint8_t* data, size_t size);
bool integrity_check_pt3(const uint8_t* data, size_t size);
bool integrity_check_sqt(const uint8_t* data, size_t size);
bool integrity_check_st1(const uint8_t* data, size_t size);
bool integrity_check_st3(const uint8_t* data, size_t size);
bool integrity_check_stf(const uint8_t* data, size_t size);

#endif /* IDENTIFY_INTEGRITY_CHECK_H */
