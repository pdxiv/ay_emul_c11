#include "ay_engine/player.h"

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

  /* MIG-0010 update: Players.pas:7830-7843's real content-signature
   * check ("EPSG"+$1A) - FT.OUT has no content signature at all in the
   * original (extension-only, see detect_by_extension below). */
  if (str_at(data, size, 0, "EPSG") && size >= 5 && data[4] == 0x1A)
    return PLAYER_FORMAT_EPSG;

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
  if (strcasecmp(dot, ".out") == 0) return PLAYER_FORMAT_OUT;
  return PLAYER_FORMAT_UNKNOWN;
}

player_status player_load(player* p, const char* path, const uint8_t* data,
                           size_t size, int sample_rate) {
  return player_load_song(p, path, data, size, sample_rate, 0, true);
}

player_status player_load_song(player* p, const char* path,
                                const uint8_t* data, size_t size,
                                int sample_rate, int song_index,
                                bool is_ste) {
  p->format = detect(data, size);
  if (p->format == PLAYER_FORMAT_UNKNOWN)
    p->format = detect_by_extension(path);
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      if (ay_file_load(&p->as.ay, data, size, song_index, AY_FILE_AY_FREQ_DEF,
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
      if (sndh_file_load(&p->as.sndh, data, size, sample_rate, is_ste) !=
          SNDH_FILE_OK)
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
    case PLAYER_FORMAT_OUT:
      if (out_file_load(&p->as.out, data, size, OUT_FILE_AY_FREQ_DEF,
                         OUT_FILE_FRQ_Z80_DEF, OUT_FILE_MAX_TSTATES_DEF,
                         sample_rate) != OUT_FILE_OK)
        return PLAYER_ERR_LOAD_FAILED;
      return PLAYER_OK;
    case PLAYER_FORMAT_EPSG:
      if (epsg_file_load(&p->as.epsg, data, size, EPSG_FILE_AY_FREQ_DEF,
                          EPSG_FILE_FRQ_Z80_DEF,
                          sample_rate) != EPSG_FILE_OK)
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
    case PLAYER_FORMAT_OUT:
      return out_file_make_buffer(&p->as.out, buf, buffer_length);
    case PLAYER_FORMAT_EPSG:
      return epsg_file_make_buffer(&p->as.epsg, buf, buffer_length);
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
      return p->as.pt3.real_end_all; /* MIG-0101 */
    case PLAYER_FORMAT_SNDH:
      return p->as.sndh.real_end_all;
    case PLAYER_FORMAT_VTX:
      return p->as.vtx.real_end_all;
    case PLAYER_FORMAT_PT1:
      return p->as.pt1.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_GTR:
      return p->as.gtr.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_FLS:
      return p->as.fls.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_STC:
      return p->as.stc.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_STP:
      return p->as.stp.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_PT2:
      return p->as.pt2.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_FXM:
      return p->as.fxm.real_end_all; /* MIG-0108 */
    /* MIG-0108: PSM_Get_Registers calls CheckLoopAndStop too (Players.
     * pas:12213-12214), same as every other tracker format - real_end_all
     * (this format's own CheckLoopAndStop-driven field, matching all the
     * others above) is now the single source of truth here, correcting
     * the PREVIOUS wiring which returned p->as.psm.finished instead. That
     * was a real, if reasonable-at-the-time, divergence: PSM_Parameters.
     * Finished is a genuinely separate, PSM-native "ran off the position
     * list" flag that only freezes PSM_Get_Registers's own pattern-walk
     * state internally - real Pascal's MakeBufferTracker loop (Players.
     * pas:12301-12317) only ever consults Real_End_All (set exclusively
     * by CheckLoopAndStop), never Finished directly, so a PSM song
     * reaching Finished does NOT by itself end playback in the original -
     * it just goes silent (amplitudes already zeroed just above the
     * Finished check) until global_tick_counter separately catches up to
     * global_tick_max. See psm_file.h's own `finished` field comment. */
    case PLAYER_FORMAT_PSM:
      return p->as.psm.real_end_all;
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return p->as.asc.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_FTC:
      return p->as.ftc.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_PSC:
      return p->as.psc.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_SQT:
      return p->as.sqt.real_end_all; /* MIG-0108 */
    case PLAYER_FORMAT_OUT:
      return p->as.out.real_end_all;
    case PLAYER_FORMAT_EPSG:
      return p->as.epsg.real_end_all;
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return true;
  }
}

void player_set_do_loop(player* p, bool do_loop) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      p->as.ay.do_loop = do_loop;
      break;
    case PLAYER_FORMAT_YM:
      p->as.ym.do_loop = do_loop;
      break;
    case PLAYER_FORMAT_VTX:
      p->as.vtx.do_loop = do_loop;
      break;
    case PLAYER_FORMAT_SNDH:
      p->as.sndh.atari.do_loop = do_loop;
      break;
    case PLAYER_FORMAT_PT3:
      p->as.pt3.do_loop = do_loop; /* MIG-0101 */
      break;
    case PLAYER_FORMAT_PT1:
      p->as.pt1.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_GTR:
      p->as.gtr.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_FLS:
      p->as.fls.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_STC:
      p->as.stc.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_STP:
      p->as.stp.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_PT2:
      p->as.pt2.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_FXM:
      p->as.fxm.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_PSM:
      p->as.psm.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      p->as.asc.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_FTC:
      p->as.ftc.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_PSC:
      p->as.psc.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_SQT:
      p->as.sqt.do_loop = do_loop; /* MIG-0108 */
      break;
    case PLAYER_FORMAT_OUT:
      p->as.out.do_loop = do_loop;
      break;
    case PLAYER_FORMAT_EPSG:
      p->as.epsg.do_loop = do_loop;
      break;
    default:
      break; /* no do_loop concept for this format - see player_real_end_all */
  }
}

/* MIG-0114: scoped to exactly the 14 CheckLoopAndStop-driven tracker
 * formats (player_supports_pairing's own list) - Force_Loop is only ever
 * consulted from within that one Pascal function, never by AY/YM/VTX/
 * SNDH's own, entirely different natural-end logic. */
void player_set_force_loop(player* p, bool force_loop) {
  switch (p->format) {
    case PLAYER_FORMAT_PT3:
      p->as.pt3.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_PT1:
      p->as.pt1.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_GTR:
      p->as.gtr.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_FLS:
      p->as.fls.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_STC:
      p->as.stc.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_STP:
      p->as.stp.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_PT2:
      p->as.pt2.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_FXM:
      p->as.fxm.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_PSM:
      p->as.psm.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      p->as.asc.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_FTC:
      p->as.ftc.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_PSC:
      p->as.psc.force_loop = force_loop;
      break;
    case PLAYER_FORMAT_SQT:
      p->as.sqt.force_loop = force_loop;
      break;
    default:
      break; /* no force_loop concept for this format - see
              * player_supports_pairing */
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
    case PLAYER_FORMAT_OUT:
      out_file_free(&p->as.out);
      break;
    case PLAYER_FORMAT_EPSG:
      epsg_file_free(&p->as.epsg);
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
    case PLAYER_FORMAT_OUT:
      return "OUT";
    case PLAYER_FORMAT_EPSG:
      return "EPSG";
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return "unknown";
  }
}

int player_song_count(const player* p) {
  if (p->format == PLAYER_FORMAT_AY) return p->as.ay.song_count;
  return 1; /* every other format: no subsong-selection concept yet */
}

ay_chip_type player_chip_type(const player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      return p->as.ay.ay.chip_type;
    case PLAYER_FORMAT_YM:
      return p->as.ym.ay.chip_type;
    case PLAYER_FORMAT_PT3:
      return p->as.pt3.ay.chip_type;
    case PLAYER_FORMAT_SNDH:
      return p->as.sndh.ay.chip_type;
    case PLAYER_FORMAT_VTX:
      return p->as.vtx.ay.chip_type;
    case PLAYER_FORMAT_PT1:
      return p->as.pt1.ay.chip_type;
    case PLAYER_FORMAT_GTR:
      return p->as.gtr.ay.chip_type;
    case PLAYER_FORMAT_FLS:
      return p->as.fls.ay.chip_type;
    case PLAYER_FORMAT_STC:
      return p->as.stc.ay.chip_type;
    case PLAYER_FORMAT_STP:
      return p->as.stp.ay.chip_type;
    case PLAYER_FORMAT_PT2:
      return p->as.pt2.ay.chip_type;
    case PLAYER_FORMAT_FXM:
      return p->as.fxm.ay.chip_type;
    case PLAYER_FORMAT_PSM:
      return p->as.psm.ay.chip_type;
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return p->as.asc.ay.chip_type;
    case PLAYER_FORMAT_FTC:
      return p->as.ftc.ay.chip_type;
    case PLAYER_FORMAT_PSC:
      return p->as.psc.ay.chip_type;
    case PLAYER_FORMAT_SQT:
      return p->as.sqt.ay.chip_type;
    /* Neither FT.OUT nor FT.EPSG's own Players.pas loader ever assigns
     * ChType explicitly (confirmed by direct search) - the real global
     * just retains whatever the PREVIOUSLY loaded item left it at. This
     * port's own ay_engine_init gives every freshly-loaded format the
     * SAME starting value ChType would have (AY.pas's own default,
     * AY_CHIP_TYPE_YM - see ay_engine_init's own comment), so reading it
     * straight from out_file/epsg_file's own ay_engine here is the
     * faithful "unset, defaults like the original's global" behavior,
     * not a fabricated choice. */
    case PLAYER_FORMAT_OUT:
      return p->as.out.ay.chip_type;
    case PLAYER_FORMAT_EPSG:
      return p->as.epsg.ay.chip_type;
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return AY_CHIP_TYPE_AY;
  }
}

static void copy_or_empty(const char* src, char* out, size_t cap) {
  if (!out || cap == 0) return;
  if (src) {
    strncpy(out, src, cap - 1);
    out[cap - 1] = '\0';
  } else {
    out[0] = '\0';
  }
}

void player_get_metadata_raw(const player* p, char* author,
                              size_t author_cap, char* title,
                              size_t title_cap, char* comment,
                              size_t comment_cap) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      copy_or_empty(p->as.ay.author, author, author_cap);
      copy_or_empty(p->as.ay.title, title, title_cap);
      copy_or_empty(p->as.ay.comment, comment, comment_cap);
      break;
    case PLAYER_FORMAT_YM:
      copy_or_empty(p->as.ym.author, author, author_cap);
      copy_or_empty(p->as.ym.title, title, title_cap);
      copy_or_empty(p->as.ym.comment, comment, comment_cap);
      break;
    /* GTR/FTC/PT1/PT2 (MIG-0082) only ever had a Title field in
     * Players.pas's own AddTrackerModule (7345-7402) - no Author, no
     * Comment. */
    case PLAYER_FORMAT_GTR:
      copy_or_empty(NULL, author, author_cap);
      copy_or_empty(p->as.gtr.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    case PLAYER_FORMAT_FTC:
      copy_or_empty(NULL, author, author_cap);
      copy_or_empty(p->as.ftc.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    case PLAYER_FORMAT_PT1:
      copy_or_empty(NULL, author, author_cap);
      copy_or_empty(p->as.pt1.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    case PLAYER_FORMAT_PT2:
      copy_or_empty(NULL, author, author_cap);
      copy_or_empty(p->as.pt2.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    case PLAYER_FORMAT_PT3:
      copy_or_empty(p->as.pt3.author, author, author_cap);
      copy_or_empty(p->as.pt3.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    case PLAYER_FORMAT_PSC:
      copy_or_empty(p->as.psc.author, author, author_cap);
      copy_or_empty(p->as.psc.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    /* ASC/ASC0/STP/PSM (MIG-0083): title only (STP/PSM never had an
     * Author field in Players.pas's own title-extraction branches for
     * them; ASC/ASC0 DO have one). */
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      copy_or_empty(p->as.asc.author, author, author_cap);
      copy_or_empty(p->as.asc.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    case PLAYER_FORMAT_STP:
      copy_or_empty(NULL, author, author_cap);
      copy_or_empty(p->as.stp.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    case PLAYER_FORMAT_PSM:
      copy_or_empty(NULL, author, author_cap);
      copy_or_empty(p->as.psm.title, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
    default:
      copy_or_empty(NULL, author, author_cap);
      copy_or_empty(NULL, title, title_cap);
      copy_or_empty(NULL, comment, comment_cap);
      break;
  }
}

ay_engine* player_ay_engine(player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      return &p->as.ay.ay;
    case PLAYER_FORMAT_YM:
      return &p->as.ym.ay;
    case PLAYER_FORMAT_PT3:
      return &p->as.pt3.ay;
    case PLAYER_FORMAT_SNDH:
      return &p->as.sndh.ay;
    case PLAYER_FORMAT_VTX:
      return &p->as.vtx.ay;
    case PLAYER_FORMAT_PT1:
      return &p->as.pt1.ay;
    case PLAYER_FORMAT_GTR:
      return &p->as.gtr.ay;
    case PLAYER_FORMAT_FLS:
      return &p->as.fls.ay;
    case PLAYER_FORMAT_STC:
      return &p->as.stc.ay;
    case PLAYER_FORMAT_STP:
      return &p->as.stp.ay;
    case PLAYER_FORMAT_PT2:
      return &p->as.pt2.ay;
    case PLAYER_FORMAT_FXM:
      return &p->as.fxm.ay;
    case PLAYER_FORMAT_PSM:
      return &p->as.psm.ay;
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return &p->as.asc.ay;
    case PLAYER_FORMAT_FTC:
      return &p->as.ftc.ay;
    case PLAYER_FORMAT_PSC:
      return &p->as.psc.ay;
    case PLAYER_FORMAT_SQT:
      return &p->as.sqt.ay;
    case PLAYER_FORMAT_OUT:
      return &p->as.out.ay;
    case PLAYER_FORMAT_EPSG:
      return &p->as.epsg.ay;
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return &p->as.ay.ay; /* unreachable after a successful load - see
                             * this function's own header comment */
  }
}

bool player_get_tick_position(const player* p, int64_t* counter,
                               int64_t* max) {
  *counter = 0;
  *max = 0;
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      *counter = p->as.ay.global_tick_counter;
      *max = p->as.ay.global_tick_max;
      return true;
    case PLAYER_FORMAT_YM:
      *counter = p->as.ym.global_tick_counter;
      *max = p->as.ym.global_tick_max;
      return true;
    case PLAYER_FORMAT_VTX:
      *counter = p->as.vtx.global_tick_counter;
      *max = p->as.vtx.global_tick_max;
      return true;
    /* MIG-0100: SNDH's TIME tag (sndh.pas: GetTunesTime) gives a real
     * declared duration in VBL ticks, same role as AY/YM/VTX's own
     * Global_Tick_Max - wired through atari_emulate's tick_count/
     * tick_count_max (see sndh_file_load's own comment for the exact
     * seconds*PlayFreq/5-minute-fallback derivation). Previously
     * returned false here on the mistaken assumption ("SNDH's separate
     * Atari-VBL position model") that no comparable total existed. */
    case PLAYER_FORMAT_SNDH:
      *counter = p->as.sndh.atari.tick_count;
      *max = p->as.sndh.atari.tick_count_max;
      return true;
    /* MIG-0101: PT3 is registered `type=AY` in the original's own
     * FILETYPES resource (confirmed by extracting Ay_Emul.res's text),
     * meaning RerollMusic seeks it through IsAYNativeFileType's branch
     * exactly like true .ay files - Global_Tick_Max is real, computed
     * by GetTimePT3 (a pattern-opcode-only duration simulation, ported
     * as pt3_get_time in pt3_file.c). The earlier framing here ("no
     * Global_Tick_Max field... UI-only") was itself an unverified
     * assumption - see migration_debt.yaml MIG-0101 for the full trace
     * and why this affects real playback correctness (CheckLoopAndStop
     * natural-end), not just seeking. PT1/PT2/GTR/FLS/STC/STP/FXM/PSM/
     * ASC/ASC0/FTC/PSC/SQT are ALSO `type=AY` in the same resource and
     * so ALSO have real Global_Tick_Max in the original via their own
     * GetTimeXXX routines (GetTimePT1/GetTimePT2/GetTimeSTC/etc,
     * Players.pas:17001-17037) - MIG-0103/MIG-0104 already ported those
     * GetTimeXXX computations into each format's own global_tick_max/
     * global_tick_counter fields (e.g. stc_file.h, pt1_file.h); this
     * switch just wasn't updated to expose them until now. */
    case PLAYER_FORMAT_PT3:
      *counter = p->as.pt3.global_tick_counter;
      *max = p->as.pt3.global_tick_max;
      return true;
    case PLAYER_FORMAT_PT1:
      *counter = p->as.pt1.global_tick_counter;
      *max = p->as.pt1.global_tick_max;
      return true;
    case PLAYER_FORMAT_PT2:
      *counter = p->as.pt2.global_tick_counter;
      *max = p->as.pt2.global_tick_max;
      return true;
    case PLAYER_FORMAT_GTR:
      *counter = p->as.gtr.global_tick_counter;
      *max = p->as.gtr.global_tick_max;
      return true;
    case PLAYER_FORMAT_FLS:
      *counter = p->as.fls.global_tick_counter;
      *max = p->as.fls.global_tick_max;
      return true;
    case PLAYER_FORMAT_STC:
      *counter = p->as.stc.global_tick_counter;
      *max = p->as.stc.global_tick_max;
      return true;
    case PLAYER_FORMAT_STP:
      *counter = p->as.stp.global_tick_counter;
      *max = p->as.stp.global_tick_max;
      return true;
    case PLAYER_FORMAT_FXM:
      *counter = p->as.fxm.global_tick_counter;
      *max = p->as.fxm.global_tick_max;
      return true;
    case PLAYER_FORMAT_PSM:
      *counter = p->as.psm.global_tick_counter;
      *max = p->as.psm.global_tick_max;
      return true;
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      *counter = p->as.asc.global_tick_counter;
      *max = p->as.asc.global_tick_max;
      return true;
    case PLAYER_FORMAT_FTC:
      *counter = p->as.ftc.global_tick_counter;
      *max = p->as.ftc.global_tick_max;
      return true;
    case PLAYER_FORMAT_PSC:
      *counter = p->as.psc.global_tick_counter;
      *max = p->as.psc.global_tick_max;
      return true;
    case PLAYER_FORMAT_SQT:
      *counter = p->as.sqt.global_tick_counter;
      *max = p->as.sqt.global_tick_max;
      return true;
    /* MIG-0010 update: OUT/EPSG have no native Global_Tick_Counter/Max
     * concept in the original (natural end is "ran out of file bytes")
     * - global_tick_max here is a derived value (see out_file.h/
     * epsg_file.h's own comments) computed once at load time so these
     * two formats can participate in this generic per-tick contract
     * exactly like every other format. */
    case PLAYER_FORMAT_OUT:
      *counter = p->as.out.global_tick_counter;
      *max = p->as.out.global_tick_max;
      return true;
    case PLAYER_FORMAT_EPSG:
      *counter = p->as.epsg.global_tick_counter;
      *max = p->as.epsg.global_tick_max;
      return true;
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return false;
  }
}

double player_get_seconds_per_tick(const player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      if (p->as.ay.frq_z80 <= 0) return 0.0;
      return (double)p->as.ay.bus.max_tstates / (double)p->as.ay.frq_z80;
    /* interrupt_freq is stored pre-scaled by 1000 (ym_file.c/vtx_file.c:
     * `interrupt_freq = inter_frq * 1000.0`, mirroring Players.pas's own
     * Interrupt_Freq := InterFrq*1000) - RerollMusic's own BaseSample
     * formula (`Global_Tick_Counter * 1000 / Interrupt_Freq * SampleRate`)
     * divides by Interrupt_Freq with an explicit *1000 in the numerator,
     * i.e. seconds-per-tick = 1000/Interrupt_Freq, NOT 1/Interrupt_Freq -
     * confirmed by testing: without the *1000, a typical 50Hz VTX/YM
     * tick rate came out as 0.00002s/tick (50000Hz) instead of the
     * correct 0.02s/tick (50Hz). */
    case PLAYER_FORMAT_YM:
      if (p->as.ym.interrupt_freq <= 0.0) return 0.0;
      return 1000.0 / p->as.ym.interrupt_freq;
    case PLAYER_FORMAT_VTX:
      if (p->as.vtx.interrupt_freq <= 0.0) return 0.0;
      return 1000.0 / p->as.vtx.interrupt_freq;
    /* MIG-0100: SNDH ticks are VBL interrupts at play_freq Hz (no *1000
     * pre-scaling like YM/VTX's interrupt_freq - play_freq is a plain
     * Hz value, sndh.pas's own PlayFreq). */
    case PLAYER_FORMAT_SNDH:
      if (p->as.sndh.play_freq <= 0) return 0.0;
      return 1.0 / p->as.sndh.play_freq;
    /* MIG-0101: PT3 always plays at a fixed 50Hz interrupt rate (no
     * per-file override, unlike YM/VTX/SNDH's own stored rate) -
     * PT3_FILE_INTERRUPT_FREQ_DEF is a plain #define, not a file field,
     * so this is a constant, same *1000-prescaled convention as YM/VTX. */
    case PLAYER_FORMAT_PT3:
      return 1000.0 / PT3_FILE_INTERRUPT_FREQ_DEF;
    /* Same fixed-50Hz-interrupt convention as PT3 above - each of these
     * formats' own <FMT>_FILE_INTERRUPT_FREQ_DEF is 50000 (Players.pas:
     * settings.pas's Interrupt_FreqDef, *1000-prescaled). */
    case PLAYER_FORMAT_PT1:
      return 1000.0 / PT1_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_PT2:
      return 1000.0 / PT2_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_GTR:
      return 1000.0 / GTR_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_FLS:
      return 1000.0 / FLS_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_STC:
      return 1000.0 / STC_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_STP:
      return 1000.0 / STP_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_FXM:
      return 1000.0 / FXM_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_PSM:
      return 1000.0 / PSM_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return 1000.0 / ASC_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_FTC:
      return 1000.0 / FTC_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_PSC:
      return 1000.0 / PSC_FILE_INTERRUPT_FREQ_DEF;
    case PLAYER_FORMAT_SQT:
      return 1000.0 / SQT_FILE_INTERRUPT_FREQ_DEF;
    /* MIG-0010 update: one OUT/EPSG "tick" (per out_file_step_registers/
     * epsg_file_step_registers) is one max_tstates/epsg_tstate_max
     * worth of Z80 time, same formula as AY's own frq_z80-based case
     * above. */
    case PLAYER_FORMAT_OUT:
      if (p->as.out.frq_z80 <= 0) return 0.0;
      return (double)p->as.out.max_tstates / (double)p->as.out.frq_z80;
    case PLAYER_FORMAT_EPSG:
      if (p->as.epsg.frq_z80 <= 0) return 0.0;
      return (double)p->as.epsg.epsg_tstate_max / (double)p->as.epsg.frq_z80;
    default:
      return 0.0;
  }
}

bool player_seek_fast_forward(player* p, int64_t target_tick) {
  if (p->format != PLAYER_FORMAT_SNDH) return false;
  if (target_tick < p->as.sndh.atari.tick_count) return false;
  sndh_file_seek_fast_forward(&p->as.sndh, target_tick);
  return true;
}

void player_set_chip_freq(player* p, int ay_freq, int sample_rate) {
  if (sample_rate <= 0) return;
  ay_engine* e = player_ay_engine(p);
  e->delay_in_tiks = (uint32_t)(8192.0 / sample_rate * ay_freq + 0.5);
  e->tik_re = e->delay_in_tiks;

  /* MainWin.pas:1549: SetFilter(FilterQuality), right after the
   * Delay_In_Tiks/Tik.Re recompute above and before the format-specific
   * cases below (Set_Chip_Frq's own call order) - recomputes the FIR
   * filter's coefficients for the new AY-clock/sample-rate pair, or
   * disables filtering entirely if filter_quality is 0 or the sample
   * rate is too high relative to ay_freq (see ay_engine_set_filter's own
   * comment). */
  ay_engine_set_filter(e, e->filter_quality, ay_freq, sample_rate);

  switch (p->format) {
    case PLAYER_FORMAT_AY:
      if (p->as.ay.frq_z80 > 0) {
        p->as.ay.ay.frq_ay_by_frq_z80 = (int64_t)(
            (double)ay_freq / p->as.ay.frq_z80 / 8.0 * 4294967296.0 + 0.5);
      }
      break;
    case PLAYER_FORMAT_VTX:
      p->as.vtx.ay_freq = (double)ay_freq;
      if (p->as.vtx.interrupt_freq > 0.0) {
        p->as.vtx.ay_tiks_in_interrupt = (int64_t)(
            (double)ay_freq / (p->as.vtx.interrupt_freq / 1000.0 * 8.0) +
            0.5);
      }
      break;
    case PLAYER_FORMAT_YM:
      p->as.ym.ay_freq = (double)ay_freq;
      if (p->as.ym.interrupt_freq > 0.0) {
        p->as.ym.ym6_tiks_on_int =
            (double)ay_freq / (p->as.ym.interrupt_freq / 1000.0 * 8.0);
      }
      break;
    /* MIG-0010 update: OUT/EPSG's own SynthesizerOUT/SynthesizerEPSG use
     * the same frq_ay_by_frq_z80/Number_Of_Tiks accumulator SynthesizerAY
     * does (see out_file.h/epsg_file.h) - same recompute as the AY case
     * above. */
    case PLAYER_FORMAT_OUT:
      if (p->as.out.frq_z80 > 0) {
        p->as.out.ay.frq_ay_by_frq_z80 = (int64_t)(
            (double)ay_freq / p->as.out.frq_z80 / 8.0 * 4294967296.0 + 0.5);
      }
      break;
    case PLAYER_FORMAT_EPSG:
      if (p->as.epsg.frq_z80 > 0) {
        p->as.epsg.ay.frq_ay_by_frq_z80 = (int64_t)(
            (double)ay_freq / p->as.epsg.frq_z80 / 8.0 * 4294967296.0 + 0.5);
      }
      break;
    default:
      break; /* delay_in_tiks/tik_re above still apply universally,
              * matching Set_Chip_Frq's own unconditional recompute -
              * see this function's own header comment */
  }
}

int player_get_ay_freq(const player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_VTX:
      return (int)p->as.vtx.ay_freq;
    case PLAYER_FORMAT_YM:
      return (int)p->as.ym.ay_freq;
    default:
      return PLAYER_AY_FREQ_DEF;
  }
}

int player_get_int_freq(const player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_VTX:
      return (int)p->as.vtx.interrupt_freq;
    case PLAYER_FORMAT_YM:
      return (int)p->as.ym.interrupt_freq;
    default:
      return PLAYER_INTERRUPT_FREQ_DEF;
  }
}

void player_set_player_freq(player* p, int freq_hz_x1000) {
  switch (p->format) {
    case PLAYER_FORMAT_VTX:
      p->as.vtx.interrupt_freq = (double)freq_hz_x1000;
      if (p->as.vtx.ay_freq > 0.0) {
        p->as.vtx.ay_tiks_in_interrupt =
            (int64_t)(p->as.vtx.ay_freq /
                          ((double)freq_hz_x1000 / 1000.0 * 8.0) +
                      0.5);
      }
      break;
    case PLAYER_FORMAT_YM:
      p->as.ym.interrupt_freq = (double)freq_hz_x1000;
      if (p->as.ym.ay_freq > 0.0) {
        p->as.ym.ym6_tiks_on_int =
            p->as.ym.ay_freq / ((double)freq_hz_x1000 / 1000.0 * 8.0);
      }
      break;
    default:
      break; /* AY has no interrupt_freq concept (MaxTStates/FrqZ80
              * instead) - a documented no-op, see this function's own
              * header comment */
  }
}

void player_set_frq_z80(player* p, int frq_z80) {
  if (frq_z80 < 1000000 || frq_z80 > 8000000) return; /* MainWin.pas:1605 */
  if (p->format != PLAYER_FORMAT_AY) return;
  p->as.ay.frq_z80 = frq_z80;
  /* MainWin.pas:1627, same formula as player_set_chip_freq's own AY
   * case - reuses the file's CURRENT AY clock (player_get_ay_freq,
   * PLAYER_AY_FREQ_DEF for AY since it has no per-file ay_freq concept
   * of its own). */
  p->as.ay.ay.frq_ay_by_frq_z80 = (int64_t)(
      (double)player_get_ay_freq(p) / frq_z80 / 8.0 * 4294967296.0 + 0.5);
}

/* MainWin.pas:1636-1650 - see player.h's own comment. */
void player_set_mc68000_freq(player* p, double freq) {
  if (p->format != PLAYER_FORMAT_SNDH) return;
  atari_emulate_set_mc68000_freq(&p->as.sndh.atari, freq);
}

/* MainWin.pas:1563-1585 - see player.h's own comment. */
void player_set_mfp_freq(player* p, int mode, int freq) {
  if (p->format != PLAYER_FORMAT_SNDH) return;
  double effective_freq = (double)freq;
  if (mode == 0) {
    /* MainWin.pas:1570 - Auto: derived from THIS player's own AY_Freq,
     * `freq` is ignored (matches Set_MFP_Frq's own Md=0 branch, which
     * never reads its Fr parameter). */
    effective_freq = (double)player_get_ay_freq(p) * 16.0 / 13.0 + 0.5;
    effective_freq = (double)(int64_t)effective_freq; /* Trunc */
  }
  atari_emulate_set_mfp_freq(&p->as.sndh.atari, mode, effective_freq);
}

int player_get_frq_z80(const player* p) {
  return p->format == PLAYER_FORMAT_AY ? p->as.ay.frq_z80 : 0;
}

double player_get_mc68000_freq(const player* p) {
  return p->format == PLAYER_FORMAT_SNDH ? p->as.sndh.atari.mc68000_freq
                                          : 0.0;
}

double player_get_mfp_freq(const player* p) {
  return p->format == PLAYER_FORMAT_SNDH ? p->as.sndh.atari.mfp_timer_freq
                                          : 0.0;
}

/* MainWin.pas:1670-1694's arithmetic core only - a LIVE override (no
 * IsPlaying guard in the original), see player.h's own comment. */
void player_set_n_tact(player* p, int n_tact) {
  if (n_tact <= 9999 || n_tact > 200000) return; /* MainWin.pas:1672 */
  if (p->format != PLAYER_FORMAT_AY) return;
  p->as.ay.bus.max_tstates = n_tact;
}

int player_get_n_tact(const player* p) {
  return p->format == PLAYER_FORMAT_AY ? (int)p->as.ay.bus.max_tstates : 0;
}

void player_set_number_of_channels(player* p, int channels) {
  if (channels != 1 && channels != 2) return; /* MainWin.pas:1795 */
  ay_engine* e = player_ay_engine(p);
  e->number_of_channels = channels;
  ay_engine_calculate_level_tables(e); /* SetSynthesizer -> Calculate_
                                         * Level_Tables2, MainWin.pas:1759 */
}

void player_set_sample_bits(player* p, int bits) {
  if (bits != 8 && bits != 16) return; /* MainWin.pas:1776's implicit
                                         * range (only ever called with
                                         * 8 or 16 from RBBt8/RBBt16) */
  ay_engine* e = player_ay_engine(p);
  e->sample_bits = bits;
  ay_engine_calculate_level_tables(e);
}

/* MIG-0112: playlist-level Turbosound pairing - see player.h's own,
 * much longer comment on player_supports_pairing/player_step_registers/
 * player_pair for the full design rationale. */
bool player_supports_pairing(const player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_PT1:
    case PLAYER_FORMAT_PT2:
    case PLAYER_FORMAT_PT3:
    case PLAYER_FORMAT_STC:
    case PLAYER_FORMAT_STP:
    case PLAYER_FORMAT_PSC:
    case PLAYER_FORMAT_FLS:
    case PLAYER_FORMAT_FTC:
    case PLAYER_FORMAT_SQT:
    case PLAYER_FORMAT_GTR:
    case PLAYER_FORMAT_FXM:
    case PLAYER_FORMAT_PSM:
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return true;
    case PLAYER_FORMAT_AY:
    case PLAYER_FORMAT_YM:
    case PLAYER_FORMAT_SNDH:
    case PLAYER_FORMAT_VTX:
    case PLAYER_FORMAT_OUT:
    case PLAYER_FORMAT_EPSG:
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return false;
  }
}

bool player_step_registers(player* p, ay_chip* target) {
  switch (p->format) {
    case PLAYER_FORMAT_PT1:
      return pt1_file_step_registers(&p->as.pt1, target);
    case PLAYER_FORMAT_PT2:
      return pt2_file_step_registers(&p->as.pt2, target);
    case PLAYER_FORMAT_PT3:
      return pt3_file_step_registers(&p->as.pt3, target);
    case PLAYER_FORMAT_STC:
      return stc_file_step_registers(&p->as.stc, target);
    case PLAYER_FORMAT_STP:
      return stp_file_step_registers(&p->as.stp, target);
    case PLAYER_FORMAT_PSC:
      return psc_file_step_registers(&p->as.psc, target);
    case PLAYER_FORMAT_FLS:
      return fls_file_step_registers(&p->as.fls, target);
    case PLAYER_FORMAT_FTC:
      return ftc_file_step_registers(&p->as.ftc, target);
    case PLAYER_FORMAT_SQT:
      return sqt_file_step_registers(&p->as.sqt, target);
    case PLAYER_FORMAT_GTR:
      return gtr_file_step_registers(&p->as.gtr, target);
    case PLAYER_FORMAT_FXM:
      return fxm_file_step_registers(&p->as.fxm, target);
    case PLAYER_FORMAT_PSM:
      return psm_file_step_registers(&p->as.psm, target);
    case PLAYER_FORMAT_ASC:
    case PLAYER_FORMAT_ASC0:
      return asc_file_step_registers(&p->as.asc, target);
    case PLAYER_FORMAT_AY:
    case PLAYER_FORMAT_YM:
    case PLAYER_FORMAT_SNDH:
    case PLAYER_FORMAT_VTX:
    case PLAYER_FORMAT_OUT:
    case PLAYER_FORMAT_EPSG:
    case PLAYER_FORMAT_UNKNOWN:
    default:
      return false;
  }
}

/* MIG-0010 update: generalizes player_step_registers to all 18 formats
 * (not just the 14 player_supports_pairing covers) - AY/YM/VTX/SNDH each
 * genuinely have their own All_GetRegisters[CNum] entry in the real
 * Pascal too (Players.pas:2760-2948/2913, confirmed by direct trace -
 * see migration_debt.yaml MIG-0010/MIG-0017), reached by Convs.pas's
 * VBL2PSG/VBL2VTX generic "else" branch exactly like the 14 tracker
 * formats - they were excluded from player_step_registers specifically
 * because TSMode/Turbosound PAIRING is structurally unreachable for
 * them (player_supports_pairing's own comment), which is a completely
 * separate question from "does this format have a per-tick register
 * generator at all" (yes, for all 18). Unlike player_step_registers,
 * this steps the format's OWN internal chip in place (p->as.<fmt>.ay.
 * chip) rather than an explicit external target - none of AY/YM/VTX/
 * SNDH's own per-tick functions were built to redirect into a foreign
 * ay_chip* (they were never used for pairing in the original either),
 * so there is no real target-redirection contract to generalize here;
 * callers needing the current register state read player_ay_engine(p)
 * ->chip directly after calling this (exactly what engine/src/
 * psg_export.c and tools/ay_export/src/vtx_export.c both do). */
bool player_step_registers_any(player* p) {
  switch (p->format) {
    case PLAYER_FORMAT_AY:
      return ay_file_step_registers(&p->as.ay);
    case PLAYER_FORMAT_YM:
      return ym_file_step_registers(&p->as.ym);
    case PLAYER_FORMAT_VTX:
      return vtx_file_step_registers(&p->as.vtx);
    case PLAYER_FORMAT_SNDH:
      return sndh_file_step_registers(&p->as.sndh);
    /* MIG-0010 update: FT.OUT/FT.EPSG's own OUT_Get_Registers/EPSG_Get_
     * Registers (Players.pas:8801-8840/8907-8922) - see out_file.h/
     * epsg_file.h for the full citation. */
    case PLAYER_FORMAT_OUT:
      return out_file_step_registers(&p->as.out);
    case PLAYER_FORMAT_EPSG:
      return epsg_file_step_registers(&p->as.epsg);
    case PLAYER_FORMAT_UNKNOWN:
      return false;
    default:
      return player_step_registers(p, &player_ay_engine(p)->chip);
  }
}

player_status player_pair_load_song(player_pair* pair, const char* primary_path,
                                     const uint8_t* primary_data,
                                     size_t primary_size,
                                     const char* secondary_path,
                                     const uint8_t* secondary_data,
                                     size_t secondary_size, int sample_rate,
                                     int song_index, bool is_ste) {
  memset(pair, 0, sizeof(*pair));
  player_status st = player_load_song(&pair->primary, primary_path,
                                       primary_data, primary_size,
                                       sample_rate, song_index, is_ste);
  if (st != PLAYER_OK) return st;

  if (secondary_data == NULL || secondary_size == 0) return PLAYER_OK;

  /* Players.pas: TrModLoaded's `if (TSMode = False) and
   * (PlayListItems[Index]^.Next <> nil) then if LoadTrackerModule(...)
   * then TSMode := True` - the second file always gets LOADED (needed
   * to even know its own format/eligibility), but pairing only
   * ACTIVATES under the conditions below; a secondary that fails to
   * load or isn't itself an eligible tracker format just means no
   * pairing, never a load failure for the whole pair (matching the
   * original's own graceful non-fatal handling here). */
  player_status sec_st = player_load_song(&pair->secondary, secondary_path,
                                           secondary_data, secondary_size,
                                           sample_rate, 0, true);
  if (sec_st != PLAYER_OK) return PLAYER_OK;
  pair->secondary_loaded = true;

  if (!player_supports_pairing(&pair->primary) ||
      !player_supports_pairing(&pair->secondary)) {
    return PLAYER_OK;
  }
  /* TrModLoaded only ever consults Next `if TSMode = False` - a primary
   * that already self-paired (PT3's own byte-98 tag, MIG-0109) always
   * wins over any playlist-level Next chain. */
  if (player_ay_engine(&pair->primary)->ts_mode) return PLAYER_OK;

  pair->active = true;
  /* Reuses primary's OWN chip2 (already present on every ay_engine,
   * MIG-0109) as the shared second-chip target - exactly like PT3's own
   * self-pairing does, just fed by a second player's register-
   * generation instead of a second voice within the same struct. */
  player_ay_engine(&pair->primary)->ts_mode = true;
  return PLAYER_OK;
}

/* AY.pas:2070's AY_Tiks_In_Interrupt formula (trunc(AY_Freq/(Interrupt_
 * Freq/1000*8)+0.5)), evaluated at the fixed 1773400Hz/50000(*1000)Hz
 * defaults every one of the 14 pairing-eligible tracker formats' own
 * <fmt>_file_make_buffer already uses as a `static const` (confirmed:
 * grepping every one of their headers shows byte-identical
 * <FMT>_FILE_AY_FREQ_DEF/<FMT>_FILE_INTERRUPT_FREQ_DEF values) - unlike
 * VTX/YM, none of these 14 formats have a LIVE, player_set_chip_freq-
 * recomputable ay_tiks_in_interrupt field (see that function's own
 * comment on why only VTX/YM do), so this is safe to hardcode once here
 * rather than needing a per-format accessor. */
#define PLAYER_PAIR_AY_TIKS_IN_INTERRUPT 4434

int player_pair_make_buffer(player_pair* pair, int16_t* buf,
                             int buffer_length) {
  if (!pair->active) return player_make_buffer(&pair->primary, buf, buffer_length);

  ay_engine* ay = player_ay_engine(&pair->primary);
  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  ay->sample_bits = 16;

  if (ay->int_flag) {
    ay->int_flag = false;
    ay_synthesizer_dispatch(ay);
  }
  if (ay->int_flag) return ay->buf_len;

  /* Players.pas: MakeBufferTracker (12301-12317) - see player.h's own
   * comment on player_pair_make_buffer for the full citation. Both
   * sides are stepped UNCONDITIONALLY every frame, exactly like
   * `All_GetRegisters[0](0)`/`All_GetRegisters[1](1)` - player_step_
   * registers is an idempotent no-op once a side has already ended
   * (see its own comment), matching CheckLoopAndStop's own persistent
   * Real_End[CNum] flag. */
  while (ay->buf_len < buffer_length) {
    player_step_registers(&pair->primary, &ay->chip);
    player_step_registers(&pair->secondary, &ay->chip2);
    if (player_pair_real_end_all(pair)) break;
    /* Mirrors every <fmt>_file_make_buffer's own per-tick `ay->number_
     * of_tiks = ay_tiks_in_interrupt << 32` - without this,
     * ay_synthesizer_dispatch's inner tick budget never advances. */
    if (!ay->int_flag) {
      ay->number_of_tiks = ((int64_t)PLAYER_PAIR_AY_TIKS_IN_INTERRUPT) << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay);
  }
  return ay->buf_len;
}

bool player_pair_real_end_all(const player_pair* pair) {
  bool p_end = player_real_end_all(&pair->primary);
  if (!pair->active) return p_end;
  return p_end && player_real_end_all(&pair->secondary);
}

void player_pair_set_do_loop(player_pair* pair, bool do_loop) {
  player_set_do_loop(&pair->primary, do_loop);
  if (pair->secondary_loaded) player_set_do_loop(&pair->secondary, do_loop);
}

void player_pair_set_force_loop(player_pair* pair, bool force_loop) {
  player_set_force_loop(&pair->primary, force_loop);
  if (pair->secondary_loaded) player_set_force_loop(&pair->secondary, force_loop);
}

void player_pair_free(player_pair* pair) {
  player_free(&pair->primary);
  if (pair->secondary_loaded) player_free(&pair->secondary);
}
