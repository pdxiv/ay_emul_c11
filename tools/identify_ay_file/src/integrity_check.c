#include "identify/integrity_check.h"

#include "identify/st_convert.h"

#include "ay_engine/formats/asc_file.h"
#include "ay_engine/formats/fls_file.h"
#include "ay_engine/formats/ftc_file.h"
#include "ay_engine/formats/gtr_file.h"
#include "ay_engine/formats/psc_file.h"
#include "ay_engine/formats/psm_file.h"
#include "ay_engine/formats/pt1_file.h"
#include "ay_engine/formats/pt2_file.h"
#include "ay_engine/formats/pt3_file.h"
#include "ay_engine/formats/sqt_file.h"
#include "ay_engine/formats/stc_file.h"
#include "ay_engine/formats/stp_file.h"

/* sample_rate is irrelevant to global_tick_max/loop_tick (a pure
 * pattern-walk duration precompute, no audio synthesis happens here) -
 * any valid rate works; ay_player's own default is reused for
 * consistency. */
#define IC_SAMPLE_RATE 44100

bool integrity_check_gtr(const uint8_t* data, size_t size) {
  gtr_file f;
  if (gtr_file_load(&f, data, size, IC_SAMPLE_RATE) != GTR_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_stc(const uint8_t* data, size_t size) {
  stc_file f;
  if (stc_file_load(&f, data, size, IC_SAMPLE_RATE) != STC_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_psm(const uint8_t* data, size_t size) {
  psm_file f;
  if (psm_file_load(&f, data, size, IC_SAMPLE_RATE) != PSM_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_asc(const uint8_t* data, size_t size, bool is_asc0) {
  asc_file f;
  if (asc_file_load(&f, data, size, is_asc0, IC_SAMPLE_RATE) != ASC_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_stp(const uint8_t* data, size_t size) {
  stp_file f;
  if (stp_file_load(&f, data, size, IC_SAMPLE_RATE) != STP_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_fls(const uint8_t* data, size_t size) {
  fls_file f;
  if (fls_file_load(&f, data, size, IC_SAMPLE_RATE) != FLS_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_pt2(const uint8_t* data, size_t size) {
  pt2_file f;
  if (pt2_file_load(&f, data, size, IC_SAMPLE_RATE) != PT2_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_pt1(const uint8_t* data, size_t size) {
  pt1_file f;
  if (pt1_file_load(&f, data, size, IC_SAMPLE_RATE) != PT1_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_psc(const uint8_t* data, size_t size) {
  psc_file f;
  if (psc_file_load(&f, data, size, IC_SAMPLE_RATE) != PSC_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_ftc(const uint8_t* data, size_t size) {
  ftc_file f;
  if (ftc_file_load(&f, data, size, IC_SAMPLE_RATE) != FTC_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_pt3(const uint8_t* data, size_t size) {
  pt3_file f;
  if (pt3_file_load(&f, data, size, IC_SAMPLE_RATE) != PT3_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_sqt(const uint8_t* data, size_t size) {
  sqt_file f;
  if (sqt_file_load(&f, data, size, IC_SAMPLE_RATE) != SQT_FILE_OK) return false;
  return f.global_tick_max > 0;
}

/* ST1/ST3/STF have no engine/ port of their own at all (LoadTrackerModule
 * converts them to STC's/STP's own layout at load time - see
 * st_convert.h) - these three reuse the same already-validated
 * stc_file_load/stp_file_load as detect_stc_structural's own
 * integrity_check_stc/integrity_check_stp above, just with a conversion
 * step first. See st_convert.h's file comment for the important caveat:
 * no real .st1/.st3/.stf sample file exists anywhere in this repo, so
 * unlike every other integrity_check_* here, these three have not been
 * oracle-diff-validated - migration_debt.yaml records this explicitly. */
bool integrity_check_st1(const uint8_t* data, size_t size) {
  uint8_t buf[65536];
  size_t out_size;
  if (!st1_to_stc(data, size, buf, &out_size)) return false;
  stc_file f;
  if (stc_file_load(&f, buf, out_size, IC_SAMPLE_RATE) != STC_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_st3(const uint8_t* data, size_t size) {
  uint8_t buf[65536];
  size_t out_size;
  if (!st3_to_stc(data, size, buf, &out_size)) return false;
  stc_file f;
  if (stc_file_load(&f, buf, out_size, IC_SAMPLE_RATE) != STC_FILE_OK) return false;
  return f.global_tick_max > 0;
}

bool integrity_check_stf(const uint8_t* data, size_t size) {
  uint8_t buf[65536];
  size_t out_size;
  if (!stf_to_stp(data, size, buf, &out_size)) return false;
  stp_file f;
  if (stp_file_load(&f, buf, out_size, IC_SAMPLE_RATE) != STP_FILE_OK) return false;
  return f.global_tick_max > 0;
}
