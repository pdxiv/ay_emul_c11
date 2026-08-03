#include "player/format.h"

#include <string.h>
#include <strings.h>

static bool has_at(const uint8_t* data, size_t size, size_t offset,
                    const void* pat, size_t patlen) {
  if (offset > size || patlen > size - offset) return false;
  return memcmp(data + offset, pat, patlen) == 0;
}

static bool str_at(const uint8_t* data, size_t size, size_t offset,
                    const char* s) {
  return has_at(data, size, offset, s, strlen(s));
}

static player_format detect(const uint8_t* data, size_t size) {
  if (str_at(data, size, 0, "ZXAY") && str_at(data, size, 4, "EMUL"))
    return PLAYER_FORMAT_AY;

  if (str_at(data, size, 0, "YM2!") || str_at(data, size, 0, "YM3!") ||
      str_at(data, size, 0, "YM3b") || str_at(data, size, 0, "YM5!LeOnArD!") ||
      str_at(data, size, 0, "YM6!LeOnArD!") || str_at(data, size, 2, "-lh5-"))
    return PLAYER_FORMAT_YM;

  if ((str_at(data, size, 0, "ProTracker 3.") &&
       str_at(data, size, 14, " compilation of ") &&
       str_at(data, size, 62, " by ")) ||
      (str_at(data, size, 0, "Vortex Tracker II 1.0 module: ") &&
       str_at(data, size, 62, " by ")))
    return PLAYER_FORMAT_PT3;

  if (str_at(data, size, 0, "ICE!") || str_at(data, size, 12, "SNDH"))
    return PLAYER_FORMAT_SNDH;

  if (size >= 3) {
    char c0 = (char)data[0], c1 = (char)data[1];
    bool ay_lc = c0 == 'a' && c1 == 'y', ym_lc = c0 == 'y' && c1 == 'm';
    bool ay_uc = c0 == 'A' && c1 == 'Y', ym_uc = c0 == 'Y' && c1 == 'M';
    if ((ay_lc || ym_lc || ay_uc || ym_uc) && data[2] <= 6)
      return PLAYER_FORMAT_VTX;
  }

  return PLAYER_FORMAT_UNKNOWN;
}

/* Extension-based fallback for formats with no Ay_Emul.fmt byte signature,
 * plus PT3 (whose signature IS a byte-position title string, but real
 * Pascal's actual detector, FoundPT3 (Players.pas:5880-5967), never checks
 * that text at all - it validates structural pointer/table offsets from
 * byte 202 onward. A PT3 file with a corrupted title byte (e.g. this
 * corpus's DIABOLIS_IN_MUSICA.pt3) still loads fine in real Pascal, so
 * falling back to extension here matches that behavior instead of
 * rejecting on cosmetic title corruption).
 * (Players.pas:8100-8103's Tier A - a recognised extension is trusted
 * outright with no content check). `path` may be NULL (e.g. data read
 * from stdin) - callers with no filename simply never reach this. */
static player_format detect_by_extension(const char* path) {
  if (!path) return PLAYER_FORMAT_UNKNOWN;
  const char* dot = strrchr(path, '.');
  if (!dot) return PLAYER_FORMAT_UNKNOWN;
  if (strcasecmp(dot, ".pt3") == 0) return PLAYER_FORMAT_PT3;
  if (strcasecmp(dot, ".pt1") == 0) return PLAYER_FORMAT_PT1;
  if (strcasecmp(dot, ".gtr") == 0) return PLAYER_FORMAT_GTR;
  if (strcasecmp(dot, ".fls") == 0) return PLAYER_FORMAT_FLS;
  if (strcasecmp(dot, ".stc") == 0) return PLAYER_FORMAT_STC;
  if (strcasecmp(dot, ".stp") == 0) return PLAYER_FORMAT_STP;
  if (strcasecmp(dot, ".pt2") == 0) return PLAYER_FORMAT_PT2;
  if (strcasecmp(dot, ".fxm") == 0) return PLAYER_FORMAT_FXM;
  if (strcasecmp(dot, ".psm") == 0) return PLAYER_FORMAT_PSM;
  if (strcasecmp(dot, ".asc") == 0) return PLAYER_FORMAT_ASC;
  if (strcasecmp(dot, ".as0") == 0) return PLAYER_FORMAT_ASC0;
  if (strcasecmp(dot, ".ftc") == 0) return PLAYER_FORMAT_FTC;
  if (strcasecmp(dot, ".psc") == 0) return PLAYER_FORMAT_PSC;
  if (strcasecmp(dot, ".sqt") == 0) return PLAYER_FORMAT_SQT;
  return PLAYER_FORMAT_UNKNOWN;
}

player_status player_load(player* p, const char* path, const uint8_t* data,
                           size_t size, int sample_rate) {
  p->format = detect(data, size);
  if (p->format == PLAYER_FORMAT_UNKNOWN)
    p->format = detect_by_extension(path);
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      if (ay_file_load(&p->as.ay, data, size, 0, AY_FILE_AY_FREQ_DEF,
                        AY_FILE_FRQ_Z80_DEF, sample_rate) != AY_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_YM:
      if (ym_file_load(&p->as.ym, data, size, sample_rate) != YM_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_PT3:
      if (pt3_file_load(&p->as.pt3, data, size, sample_rate) != PT3_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_SNDH:
      if (sndh_file_load(&p->as.sndh, data, size, sample_rate) != SNDH_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_VTX:
      if (vtx_file_load(&p->as.vtx, data, size, sample_rate) != VTX_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_PT1:
      if (pt1_file_load(&p->as.pt1, data, size, sample_rate) != PT1_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_GTR:
      if (gtr_file_load(&p->as.gtr, data, size, sample_rate) != GTR_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_FLS:
      if (fls_file_load(&p->as.fls, data, size, sample_rate) != FLS_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_STC:
      if (stc_file_load(&p->as.stc, data, size, sample_rate) != STC_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_STP:
      if (stp_file_load(&p->as.stp, data, size, sample_rate) != STP_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_PT2:
      if (pt2_file_load(&p->as.pt2, data, size, sample_rate) != PT2_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_FXM:
      if (fxm_file_load(&p->as.fxm, data, size, sample_rate) != FXM_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_PSM:
      if (psm_file_load(&p->as.psm, data, size, sample_rate) != PSM_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_ASC:
      if (asc_file_load(&p->as.asc, data, size, false, sample_rate) != ASC_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_ASC0:
      if (asc_file_load(&p->as.asc, data, size, true, sample_rate) != ASC_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_FTC:
      if (ftc_file_load(&p->as.ftc, data, size, sample_rate) != FTC_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_PSC:
      if (psc_file_load(&p->as.psc, data, size, sample_rate) != PSC_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_SQT:
      if (sqt_file_load(&p->as.sqt, data, size, sample_rate) != SQT_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return PLAYER_ERR_UNRECOGNIZED;
  }
}

int player_make_buffer(player* p, int16_t* buf, int buffer_length) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      return ay_file_make_buffer(&p->as.ay, buf, buffer_length);
    case PLAYER_FORMAT_YM:
      return ym_file_make_buffer(&p->as.ym, buf, buffer_length);
    case PLAYER_FORMAT_PT3:
      return pt3_file_make_buffer(&p->as.pt3, buf, buffer_length);
    case PLAYER_FORMAT_SNDH:
      return sndh_file_make_buffer(&p->as.sndh, buf, buffer_length);
    case PLAYER_FORMAT_VTX:
      return vtx_file_make_buffer(&p->as.vtx, buf, buffer_length);
    case PLAYER_FORMAT_PT1:
      return pt1_file_make_buffer(&p->as.pt1, buf, buffer_length);
    case PLAYER_FORMAT_GTR:
      return gtr_file_make_buffer(&p->as.gtr, buf, buffer_length);
    case PLAYER_FORMAT_FLS:
      return fls_file_make_buffer(&p->as.fls, buf, buffer_length);
    case PLAYER_FORMAT_STC:
      return stc_file_make_buffer(&p->as.stc, buf, buffer_length);
    case PLAYER_FORMAT_STP:
      return stp_file_make_buffer(&p->as.stp, buf, buffer_length);
    case PLAYER_FORMAT_PT2:
      return pt2_file_make_buffer(&p->as.pt2, buf, buffer_length);
    case PLAYER_FORMAT_FXM:
      return fxm_file_make_buffer(&p->as.fxm, buf, buffer_length);
    case PLAYER_FORMAT_PSM:
      return psm_file_make_buffer(&p->as.psm, buf, buffer_length);
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return asc_file_make_buffer(&p->as.asc, buf, buffer_length);
    case PLAYER_FORMAT_FTC:
      return ftc_file_make_buffer(&p->as.ftc, buf, buffer_length);
    case PLAYER_FORMAT_PSC:
      return psc_file_make_buffer(&p->as.psc, buf, buffer_length);
    case PLAYER_FORMAT_SQT:
      return sqt_file_make_buffer(&p->as.sqt, buf, buffer_length);
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return 0;
  }
}

bool player_real_end_all(const player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      return p->as.ay.real_end_all;
    case PLAYER_FORMAT_YM:
      return p->as.ym.real_end_all;
    case PLAYER_FORMAT_PT3:
      return false; /* pt3_file has no real_end_all concept - see pt3_file.h */
    case PLAYER_FORMAT_SNDH:
      return p->as.sndh.real_end_all;
    case PLAYER_FORMAT_VTX:
      return p->as.vtx.real_end_all;
    case PLAYER_FORMAT_PT1:
      return false; /* pt1_file has no real_end_all concept - see pt1_file.h */
    case PLAYER_FORMAT_GTR:
      return false; /* gtr_file has no real_end_all concept - see gtr_file.h */
    case PLAYER_FORMAT_FLS:
      return false; /* fls_file has no real_end_all concept - see fls_file.h */
    case PLAYER_FORMAT_STC:
      return false; /* stc_file has no real_end_all concept - see stc_file.h */
    case PLAYER_FORMAT_STP:
      return false; /* stp_file has no real_end_all concept - see stp_file.h */
    case PLAYER_FORMAT_PT2:
      return false; /* pt2_file has no real_end_all concept - see pt2_file.h */
    case PLAYER_FORMAT_FXM:
      return false; /* fxm_file has no real_end_all concept - see fxm_file.h */
    case PLAYER_FORMAT_PSM:
      return p->as.psm.finished; /* PSM_Parameters.Finished - a real natural-
                                   * end signal, unlike every other tracker
                                   * format wired in so far */
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return false; /* asc_file has no real_end_all concept - see asc_file.h */
    case PLAYER_FORMAT_FTC:
      return false; /* ftc_file has no real_end_all concept - see ftc_file.h */
    case PLAYER_FORMAT_PSC:
      return false; /* psc_file has no real_end_all concept - see psc_file.h */
    case PLAYER_FORMAT_SQT:
      return false; /* sqt_file has no real_end_all concept - see sqt_file.h */
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return true;
  }
}

void player_free(player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_YM:
      ym_file_free(&p->as.ym);
      break;
    case PLAYER_FORMAT_SNDH:
      sndh_file_free(&p->as.sndh);
      break;
    case PLAYER_FORMAT_VTX:
      vtx_file_free(&p->as.vtx);
      break;
    case PLAYER_FORMAT_AY:
    case PLAYER_FORMAT_PT3:
    case PLAYER_FORMAT_PT1:
    case PLAYER_FORMAT_GTR:
    case PLAYER_FORMAT_FLS:
    case PLAYER_FORMAT_STC:
    case PLAYER_FORMAT_STP:
    case PLAYER_FORMAT_PT2:
    case PLAYER_FORMAT_FXM:
    case PLAYER_FORMAT_PSM:
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
    case PLAYER_FORMAT_FTC:
    case PLAYER_FORMAT_PSC:
    case PLAYER_FORMAT_SQT:
    case PLAYER_FORMAT_UNKNOWN:
    default:
      break;
  }
}

const char* player_format_name(player_format fmt) {
  switch (fmt) {
    case PLAYER_FORMAT_AY:
      return "AY";
    case PLAYER_FORMAT_YM:
      return "YM";
    case PLAYER_FORMAT_PT3:
      return "PT3";
    case PLAYER_FORMAT_SNDH:
      return "SNDH";
    case PLAYER_FORMAT_VTX:
      return "VTX";
    case PLAYER_FORMAT_PT1:
      return "PT1";
    case PLAYER_FORMAT_GTR:
      return "GTR";
    case PLAYER_FORMAT_FLS:
      return "FLS";
    case PLAYER_FORMAT_STC:
      return "STC";
    case PLAYER_FORMAT_STP:
      return "STP";
    case PLAYER_FORMAT_PT2:
      return "PT2";
    case PLAYER_FORMAT_FXM:
      return "FXM";
    case PLAYER_FORMAT_PSM:
      return "PSM";
    case PLAYER_FORMAT_ASC:
      return "ASC";
    case PLAYER_FORMAT_ASC0:
      return "ASC0";
    case PLAYER_FORMAT_FTC:
      return "FTC";
    case PLAYER_FORMAT_PSC:
      return "PSC";
    case PLAYER_FORMAT_SQT:
      return "SQT";
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return "unknown";
  }
}
