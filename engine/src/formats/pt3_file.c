#include "ay_engine/formats/pt3_file.h"

#include <string.h>

/* Players.pas:1063-1071, PT3NoteTable_ST (TonTableId=1, version-
 * independent). */
static const uint16_t PT3_NOTE_TABLE_ST[96] = {
    0x0EF8, 0x0E10, 0x0D60, 0x0C80, 0x0BD8, 0x0B28, 0x0A88, 0x09F0, 0x0960,
    0x08E0, 0x0858, 0x07E0, 0x077C, 0x0708, 0x06B0, 0x0640, 0x05EC, 0x0594,
    0x0544, 0x04F8, 0x04B0, 0x0470, 0x042C, 0x03FD, 0x03BE, 0x0384, 0x0358,
    0x0320, 0x02F6, 0x02CA, 0x02A2, 0x027C, 0x0258, 0x0238, 0x0216, 0x01F8,
    0x01DF, 0x01C2, 0x01AC, 0x0190, 0x017B, 0x0165, 0x0151, 0x013E, 0x012C,
    0x011C, 0x010A, 0x00FC, 0x00EF, 0x00E1, 0x00D6, 0x00C8, 0x00BD, 0x00B2,
    0x00A8, 0x009F, 0x0096, 0x008E, 0x0085, 0x007E, 0x0077, 0x0070, 0x006B,
    0x0064, 0x005E, 0x0059, 0x0054, 0x004F, 0x004B, 0x0047, 0x0042, 0x003F,
    0x003B, 0x0038, 0x0035, 0x0032, 0x002F, 0x002C, 0x002A, 0x0027, 0x0025,
    0x0023, 0x0021, 0x001F, 0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016,
    0x0015, 0x0013, 0x0012, 0x0011, 0x0010, 0x000F};

/* Players.pas:1041-1049, PT3NoteTable_PT_33_34r (TonTableId=0, ver<=3). */
static const uint16_t PT3_NOTE_TABLE_PT_33_34R[96] = {
    0x0C21, 0x0B73, 0x0ACE, 0x0A33, 0x09A0, 0x0916, 0x0893, 0x0818, 0x07A4,
    0x0736, 0x06CE, 0x066D, 0x0610, 0x05B9, 0x0567, 0x0519, 0x04D0, 0x048B,
    0x0449, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0336, 0x0308, 0x02DC, 0x02B3,
    0x028C, 0x0268, 0x0245, 0x0224, 0x0206, 0x01E9, 0x01CD, 0x01B3, 0x019B,
    0x0184, 0x016E, 0x0159, 0x0146, 0x0134, 0x0122, 0x0112, 0x0103, 0x00F4,
    0x00E6, 0x00D9, 0x00CD, 0x00C2, 0x00B7, 0x00AC, 0x00A3, 0x009A, 0x0091,
    0x0089, 0x0081, 0x007A, 0x0073, 0x006C, 0x0066, 0x0061, 0x005B, 0x0056,
    0x0051, 0x004D, 0x0048, 0x0044, 0x0040, 0x003D, 0x0039, 0x0036, 0x0033,
    0x0030, 0x002D, 0x002B, 0x0028, 0x0026, 0x0024, 0x0022, 0x0020, 0x001E,
    0x001C, 0x001B, 0x0019, 0x0018, 0x0016, 0x0015, 0x0014, 0x0013, 0x0012,
    0x0011, 0x0010, 0x000F, 0x000E, 0x000D, 0x000C};

/* Players.pas:1052-1060, PT3NoteTable_PT_34_35 (TonTableId=0, ver>=4). */
static const uint16_t PT3_NOTE_TABLE_PT_34_35[96] = {
    0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819, 0x07A4,
    0x0737, 0x06CF, 0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0, 0x048B,
    0x044A, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0337, 0x0308, 0x02DD, 0x02B4,
    0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4, 0x019B,
    0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0112, 0x0103, 0x00F5,
    0x00E7, 0x00DA, 0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A, 0x0091,
    0x0089, 0x0082, 0x007A, 0x0073, 0x006D, 0x0067, 0x0061, 0x005C, 0x0056,
    0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036, 0x0033,
    0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020, 0x001F,
    0x001D, 0x001B, 0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013, 0x0012,
    0x0011, 0x0010, 0x000F, 0x000E, 0x000D, 0x000C};

/* Players.pas:1074-1082, PT3NoteTable_ASM_34r (TonTableId=2, ver<=3). */
static const uint16_t PT3_NOTE_TABLE_ASM_34R[96] = {
    0x0D3E, 0x0C80, 0x0BCC, 0x0B22, 0x0A82, 0x09EC, 0x095C, 0x08D6, 0x0858,
    0x07E0, 0x076E, 0x0704, 0x069F, 0x0640, 0x05E6, 0x0591, 0x0541, 0x04F6,
    0x04AE, 0x046B, 0x042C, 0x03F0, 0x03B7, 0x0382, 0x034F, 0x0320, 0x02F3,
    0x02C8, 0x02A1, 0x027B, 0x0257, 0x0236, 0x0216, 0x01F8, 0x01DC, 0x01C1,
    0x01A8, 0x0190, 0x0179, 0x0164, 0x0150, 0x013D, 0x012C, 0x011B, 0x010B,
    0x00FC, 0x00EE, 0x00E0, 0x00D4, 0x00C8, 0x00BD, 0x00B2, 0x00A8, 0x009F,
    0x0096, 0x008D, 0x0085, 0x007E, 0x0077, 0x0070, 0x006A, 0x0064, 0x005E,
    0x0059, 0x0054, 0x0050, 0x004B, 0x0047, 0x0043, 0x003F, 0x003C, 0x0038,
    0x0035, 0x0032, 0x002F, 0x002D, 0x002A, 0x0028, 0x0026, 0x0024, 0x0022,
    0x0020, 0x001E, 0x001D, 0x001B, 0x001A, 0x0019, 0x0018, 0x0015, 0x0014,
    0x0013, 0x0012, 0x0011, 0x0010, 0x000F, 0x000E};

/* Players.pas:1085-1093, PT3NoteTable_ASM_34_35 (TonTableId=2, ver>=4). */
static const uint16_t PT3_NOTE_TABLE_ASM_34_35[96] = {
    0x0D10, 0x0C55, 0x0BA4, 0x0AFC, 0x0A5F, 0x09CA, 0x093D, 0x08B8, 0x083B,
    0x07C5, 0x0755, 0x06EC, 0x0688, 0x062A, 0x05D2, 0x057E, 0x052F, 0x04E5,
    0x049E, 0x045C, 0x041D, 0x03E2, 0x03AB, 0x0376, 0x0344, 0x0315, 0x02E9,
    0x02BF, 0x0298, 0x0272, 0x024F, 0x022E, 0x020F, 0x01F1, 0x01D5, 0x01BB,
    0x01A2, 0x018B, 0x0174, 0x0160, 0x014C, 0x0139, 0x0128, 0x0117, 0x0107,
    0x00F9, 0x00EB, 0x00DD, 0x00D1, 0x00C5, 0x00BA, 0x00B0, 0x00A6, 0x009D,
    0x0094, 0x008C, 0x0084, 0x007C, 0x0075, 0x006F, 0x0069, 0x0063, 0x005D,
    0x0058, 0x0053, 0x004E, 0x004A, 0x0046, 0x0042, 0x003E, 0x003B, 0x0037,
    0x0034, 0x0031, 0x002F, 0x002C, 0x0029, 0x0027, 0x0025, 0x0023, 0x0021,
    0x001F, 0x001D, 0x001C, 0x001A, 0x0019, 0x0017, 0x0016, 0x0015, 0x0014,
    0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D};

/* Players.pas:1096-1104, PT3NoteTable_REAL_34r (TonTableId else, ver<=3). */
static const uint16_t PT3_NOTE_TABLE_REAL_34R[96] = {
    0x0CDA, 0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819,
    0x07A4, 0x0737, 0x06CF, 0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0,
    0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0337, 0x0308, 0x02DD,
    0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4,
    0x019B, 0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0113, 0x0103,
    0x00F5, 0x00E7, 0x00DA, 0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A,
    0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D, 0x0067, 0x0061, 0x005C,
    0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036,
    0x0033, 0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020,
    0x001F, 0x001D, 0x001B, 0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013,
    0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D};

/* Players.pas:1107-1115, PT3NoteTable_REAL_34_35 (TonTableId else,
 * ver>=4) - differs from REAL_34r at just entry 43 (0x0112 vs 0x0113),
 * confirmed by direct comparison, not assumed identical. */
static const uint16_t PT3_NOTE_TABLE_REAL_34_35[96] = {
    0x0CDA, 0x0C22, 0x0B73, 0x0ACF, 0x0A33, 0x09A1, 0x0917, 0x0894, 0x0819,
    0x07A4, 0x0737, 0x06CF, 0x066D, 0x0611, 0x05BA, 0x0567, 0x051A, 0x04D0,
    0x048B, 0x044A, 0x040C, 0x03D2, 0x039B, 0x0367, 0x0337, 0x0308, 0x02DD,
    0x02B4, 0x028D, 0x0268, 0x0246, 0x0225, 0x0206, 0x01E9, 0x01CE, 0x01B4,
    0x019B, 0x0184, 0x016E, 0x015A, 0x0146, 0x0134, 0x0123, 0x0112, 0x0103,
    0x00F5, 0x00E7, 0x00DA, 0x00CE, 0x00C2, 0x00B7, 0x00AD, 0x00A3, 0x009A,
    0x0091, 0x0089, 0x0082, 0x007A, 0x0073, 0x006D, 0x0067, 0x0061, 0x005C,
    0x0056, 0x0052, 0x004D, 0x0049, 0x0045, 0x0041, 0x003D, 0x003A, 0x0036,
    0x0033, 0x0031, 0x002E, 0x002B, 0x0029, 0x0027, 0x0024, 0x0022, 0x0020,
    0x001F, 0x001D, 0x001B, 0x001A, 0x0018, 0x0017, 0x0016, 0x0014, 0x0013,
    0x0012, 0x0011, 0x0010, 0x000F, 0x000E, 0x000D};

/* Players.pas:1118-1134, PT3VolumeTable_33_34 (version <= 4). */
static const uint8_t PT3_VOL_33_34[16][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02},
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03},
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04},
    {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
    {0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09},
    {0x00, 0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x09, 0x0A, 0x0B},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x08, 0x08, 0x09, 0x0A, 0x0B, 0x0C},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D},
    {0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}};

/* Players.pas:1137-1154, PT3VolumeTable_35 (version >= 5). */
static const uint8_t PT3_VOL_35[16][16] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
    {0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02},
    {0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03},
    {0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 0x04, 0x04},
    {0x00, 0x00, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06},
    {0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07},
    {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08},
    {0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x04, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x08, 0x09},
    {0x00, 0x01, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x09, 0x0A},
    {0x00, 0x01, 0x01, 0x02, 0x03, 0x04, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B},
    {0x00, 0x01, 0x02, 0x02, 0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C},
    {0x00, 0x01, 0x02, 0x03, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0A, 0x0B, 0x0C, 0x0D},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E},
    {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F}};

static uint16_t rd16(const uint8_t* d, uint32_t addr) {
  addr &= 0xFFFF;
  uint32_t a2 = (addr + 1) & 0xFFFF;
  return (uint16_t)(d[addr] | (d[a2] << 8));
}

/* Copies a fixed-width, space-padded (not NUL-terminated) field, then
 * trims leading/trailing bytes <= ' ' - see gtr_file.c's identical helper for
 * the full rationale (duplicated per this project's per-file
 * convention). */
static void copy_fixed_field(const uint8_t* src, size_t src_size,
                              size_t field_offset, size_t field_len,
                              char* out, size_t cap) {
  out[0] = '\0';
  if (cap == 0 || field_offset + field_len > src_size) return;
  size_t n = field_len;
  if (n >= cap) n = cap - 1;
  memcpy(out, src + field_offset, n);
  out[n] = '\0';
  while (n > 0 && (unsigned char)out[n - 1] <= ' ') n--;
  out[n] = '\0';
  size_t start = 0;
  while (start < n && (unsigned char)out[start] <= ' ') start++;
  if (start > 0) memmove(out, out + start, n - start + 1);
}

/* Players.pas:12304-12322, the GetNoteFreq nested inside PT3_Get_Registers.
 * A 4-way case on PT3_TonTableId (0/1/2/else), NOT a 6-value enum - any id
 * >=3 legitimately falls into the else/REAL family (the real corpus has
 * TonTableId values 3,4,5,7,12,16,128,201,240,241,248, all REAL-family). */
static int get_note_freq(pt3_file* f, int j) {
  switch (f->ton_table_id) {
    case 0:
      return (f->version <= 3) ? PT3_NOTE_TABLE_PT_33_34R[j]
                                : PT3_NOTE_TABLE_PT_34_35[j];
    case 1:
      return PT3_NOTE_TABLE_ST[j];
    case 2:
      return (f->version <= 3) ? PT3_NOTE_TABLE_ASM_34R[j]
                                : PT3_NOTE_TABLE_ASM_34_35[j];
    default:
      return (f->version <= 3) ? PT3_NOTE_TABLE_REAL_34R[j]
                                : PT3_NOTE_TABLE_REAL_34_35[j];
  }
}

/* Players.pas:15353-15612, GetTimePT3's PatInt nested function - one
 * channel's per-row scan. `check_end` is true only for channel A: it
 * alone detects "no more pattern data" (a raw 0 byte) and signals the
 * caller to stop the whole row-walk for THIS position (move on to the
 * next position in the outer loop) - matching the original exactly
 * (channels B/C have no such check: PatInt's own source has no `if
 * Index[j2/j3]=0 then exit` for them, only channel A's `if Index[j1]=0
 * then exit` - well-formed pattern data never needs it for B/C since
 * they always terminate via a normal note/rest opcode within the same
 * row A does). PT3_TIME_STEP_ERROR signals a malformed file (a payload
 * read pushed past the 65536-byte data buffer - Players.pas' own `if
 * j1 >= 65536 then RaiseBadFileStructure` bounds check, in the one
 * place it exists; the first opcode-consuming scan below has none
 * either in the original, so none is added here). `b` (shared tempo/
 * delay) is mutated in place exactly as the original's closure over
 * its own outer `b` variable does. */
typedef enum {
  PT3_TIME_STEP_CONTINUE,
  PT3_TIME_STEP_END_OF_POSITION,
  PT3_TIME_STEP_ERROR,
} pt3_time_step_result;

static pt3_time_step_result pt3_time_channel_step(const uint8_t* d,
                                                    uint32_t* j, int* a,
                                                    int* a1x, int* b,
                                                    bool check_end) {
  int jj, c1, c2, c3, c4, c5, c8;

  (*a)--;
  if (*a != 0) return PT3_TIME_STEP_CONTINUE;
  if (check_end && d[*j] == 0) return PT3_TIME_STEP_END_OF_POSITION;

  jj = c1 = c2 = c3 = c4 = c5 = c8 = 0;
  for (;;) {
    uint8_t op = d[*j];
    if (op == 0xd0 || op == 0xc0 || (op >= 0x50 && op <= 0xaf)) {
      *a = *a1x;
      (*j)++;
      break;
    } else if (op == 0x10 || op >= 0xf0) {
      (*j)++;
    } else if (op >= 0xb2 && op <= 0xbf) {
      *j += 2;
    } else if (op == 0xb1) {
      (*j)++;
      *a1x = d[*j];
    } else if (op >= 0x11 && op <= 0x1f) {
      *j += 3;
    } else if (op == 1) {
      jj++;
      c1 = jj;
    } else if (op == 2) {
      jj++;
      c2 = jj;
    } else if (op == 3) {
      jj++;
      c3 = jj;
    } else if (op == 4) {
      jj++;
      c4 = jj;
    } else if (op == 5) {
      jj++;
      c5 = jj;
    } else if (op == 8) {
      jj++;
      c8 = jj;
    } else if (op == 9) {
      jj++;
    }
    (*j)++;
  }
  while (jj > 0) {
    if (jj == c1 || jj == c8) {
      *j += 3;
    } else if (jj == c2) {
      *j += 5;
    } else if (jj == c3 || jj == c4) {
      *j += 1;
    } else if (jj == c5) {
      *j += 2;
    } else {
      *b = d[*j];
      (*j)++;
    }
    if (*j >= 65536) return PT3_TIME_STEP_ERROR;
    jj--;
  }
  return PT3_TIME_STEP_CONTINUE;
}

/* Players.pas:15333-15662, GetTimePT3 - computes the song's total
 * duration and loop-point tick in the SAME "global tick" units
 * global_tick_counter uses: it walks the known position list exactly
 * once, summing each processed row's Delay value, matching one
 * increment of global_tick_counter per row (pt3_get_registers
 * increments it once per interrupt frame, and a row's note-skip
 * counters gate how many frames elapse before the next row - the sum
 * of Delay values across all rows equals the total frame count).
 *
 * MIG-0109: now also walks voice[1]'s own pattern data (GetTimePT3's
 * `vars[1]`) whenever the caller has already determined ts_mode/ts_byte
 * (pt3_file_load computes these before calling this), matching the
 * original's own `if TS <> $20 then ...` gating exactly (Players.pas:
 * 15613-15662) - `TS` there is re-derived from the same file bytes this
 * port's own ts_byte already holds, so passing it in rather than
 * re-reading PT3_MusicName[13]/[98] here is behaviorally identical.
 * Per-row, the walk stops (moves to the next position) as soon as EITHER
 * voice's channel A hits an end-of-pattern marker (`if not PatInt(0) then
 * break; if TS<>$20 then if not PatInt(1) then break;`) - i.e. the
 * computed duration reflects whichever voice's pattern data is SHORTER
 * within each position, not just voice 0's alone. On a malformed file (a
 * bounds violation, or a pathologically long position tripping the same
 * 65536-row DLCatcher safety net the original has), returns 0/0 rather
 * than raising - this port's `pt3_file_load` already succeeded by the
 * time this runs (a well-formed-enough file for the real player to
 * interpret), so a duration-precompute failure degrades to "no known
 * duration" (seeking/natural-end unavailable) rather than failing the
 * whole load, unlike the original's own
 * RaiseBadFileStructure->Error=ErBadFileStructure. */
static void pt3_get_time(const pt3_file* f, bool ts_active, uint8_t ts_byte,
                          int64_t* out_tm, int64_t* out_lp) {
  const uint8_t* d = f->data;
  int64_t tm = 0, lp = 0;
  /* GetTimePT3 reads `b := PT3_Delay` directly - the fixed HEADER byte,
   * not any voice's own runtime-mutable Delay (no voice/session state
   * exists yet at this point in pt3_file_load). */
  int b = f->data[100];
  int pos;
  /* Players.pas:15623-15629: `for i := 0 to 1 do with vars[i] do begin
   * a11 := 1; a22 := 1; a33 := 1; ... end` runs ONCE, before the position
   * loop below, for BOTH voices - a11/a22/a33 (each channel's current
   * note-length "step size", last set by opcode $B1 and otherwise carried
   * forward from whichever note most recently set it) are NOT part of
   * the per-position reset (`with vars[0] do begin a1:=1;a2:=1;a3:=1;
   * end` only touches a1/a2/a3, the row countdown). Declaring a11/a22/a33
   * here, outside the position loop, matches that: a position that
   * doesn't immediately re-issue $B1 on its first note correctly
   * inherits the previous position's step size instead of always
   * restarting at 1. (a11b/a22b/a33b, voice 1's own copies, already had
   * this right.) */
  int a11 = 1, a22 = 1, a33 = 1;
  int a1b = 1, a2b = 1, a3b = 1, a11b = 1, a22b = 1, a33b = 1;
  /* Players.pas:15615: `DLCatcher: integer` is a single scalar declared
   * in GetTimePT3's own var block, initialized ONCE (in the same
   * once-only `for i := 0 to 1 do with vars[i] do DLCatcher := 256*256`
   * loop above - `with vars[i]` doesn't actually reach DLCatcher, since
   * it isn't a field of vars[i]'s record type, so that statement just
   * assigns the outer DLCatcher twice) and never reset again - it's a
   * safety budget spanning the WHOLE song's total row count, not a
   * per-position allowance. Resetting it every position (as this port
   * previously did) silently grants far more budget than the original
   * ever would, so a pathological file that should trip Pascal's
   * RaiseBadFileStructure might never trip this port's equivalent. */
  int dl_catcher = 256 * 256; /* max 256 patterns * 256 lines/pattern */

  for (pos = 0; pos < f->number_of_positions; pos++) {
    int pat, a1 = 1, a2 = 1, a3 = 1;
    uint32_t j1, j2, j3;
    uint32_t j1b = 0, j2b = 0, j3b = 0;

    if (pos == f->loop_position) lp = tm;

    pat = d[f->position_list_offset + (uint32_t)pos];
    j1 = rd16(d, f->patterns_pointer + (uint32_t)pat * 2);
    j2 = rd16(d, f->patterns_pointer + (uint32_t)pat * 2 + 2);
    j3 = rd16(d, f->patterns_pointer + (uint32_t)pat * 2 + 4);

    if (ts_active) {
      /* Players.pas: GetPatPtrs(1, TS*3-3-PT3_PositionList[i]), which
       * bounds-checks its `i` (`if (i<0) or (i>84*3) or (i mod 3<>0) then
       * RaiseBadFileStructure`) - degrading to "no known duration" here
       * instead, matching every other malformed-input path in this
       * function. */
      int pat_b = (int)ts_byte * 3 - 3 - pat;
      if (pat_b < 0 || pat_b > 84 * 3 || pat_b % 3 != 0) {
        *out_tm = 0;
        *out_lp = 0;
        return;
      }
      j1b = rd16(d, f->patterns_pointer + (uint32_t)pat_b * 2);
      j2b = rd16(d, f->patterns_pointer + (uint32_t)pat_b * 2 + 2);
      j3b = rd16(d, f->patterns_pointer + (uint32_t)pat_b * 2 + 4);
      a1b = 1;
      a2b = 1;
      a3b = 1;
    }

    for (;;) {
      pt3_time_step_result ra =
          pt3_time_channel_step(d, &j1, &a1, &a11, &b, true);
      if (ra == PT3_TIME_STEP_ERROR) {
        *out_tm = 0;
        *out_lp = 0;
        return;
      }
      if (ra == PT3_TIME_STEP_END_OF_POSITION) break; /* next position */

      if (pt3_time_channel_step(d, &j2, &a2, &a22, &b, false) ==
          PT3_TIME_STEP_ERROR) {
        *out_tm = 0;
        *out_lp = 0;
        return;
      }
      if (pt3_time_channel_step(d, &j3, &a3, &a33, &b, false) ==
          PT3_TIME_STEP_ERROR) {
        *out_tm = 0;
        *out_lp = 0;
        return;
      }

      if (ts_active) {
        pt3_time_step_result rb =
            pt3_time_channel_step(d, &j1b, &a1b, &a11b, &b, true);
        if (rb == PT3_TIME_STEP_ERROR) {
          *out_tm = 0;
          *out_lp = 0;
          return;
        }
        if (rb == PT3_TIME_STEP_END_OF_POSITION) break;
        if (pt3_time_channel_step(d, &j2b, &a2b, &a22b, &b, false) ==
            PT3_TIME_STEP_ERROR) {
          *out_tm = 0;
          *out_lp = 0;
          return;
        }
        if (pt3_time_channel_step(d, &j3b, &a3b, &a33b, &b, false) ==
            PT3_TIME_STEP_ERROR) {
          *out_tm = 0;
          *out_lp = 0;
          return;
        }
      }

      tm += b;
      if (--dl_catcher < 0) {
        *out_tm = 0;
        *out_lp = 0;
        return;
      }
    }
  }
  *out_tm = tm;
  *out_lp = lp;
}

pt3_file_status pt3_file_load(pt3_file* f, const uint8_t* data, size_t size,
                               int sample_rate) {
  int i;

  memset(f, 0, sizeof(*f));

  if (size < 202) return PT3_FILE_ERR_TRUNCATED;
  if (size > 65536) size = 65536; /* Players.pas:2253: clamped to 65536 */
  memcpy(f->data, data, size);

  /* Players.pas: "else if FType = FT.PT3" (7405-7427). */
  copy_fixed_field(f->data, size, 0x1E, 32, f->title, sizeof(f->title));
  copy_fixed_field(f->data, size, 0x42, 32, f->author, sizeof(f->author));

  f->ton_table_id = f->data[99];

  /* Header PT3_Delay byte (offset 100) - copied into each voice's own
   * mutable v->delay during the per-voice init loop below (Players.pas:
   * InitTrackerModule's `Delay := PT3_Delay;`), not stored flat here. */
  f->number_of_positions = f->data[101];
  f->loop_position = f->data[102];
  f->patterns_pointer = rd16(f->data, 103);
  for (i = 0; i < 32; i++)
    f->samples_pointers[i] = rd16(f->data, 105 + (uint32_t)i * 2);
  for (i = 0; i < 16; i++)
    f->ornaments_pointers[i] = rd16(f->data, 169 + (uint32_t)i * 2);
  f->position_list_offset = 201;

  f->version = 6;
  if (f->data[13] >= '0' && f->data[13] <= '9') f->version = f->data[13] - '0';

  /* Players.pas: TrModLoaded (2643-2693) - `if (CurFileType = FT.PT3) and
   * (PlConsts[0].Version >= 7) then begin TS := Ord(ZRAM.PT3_MusicName[98]);
   * if TS <> $20 then TSMode := True; ...`. See pt3_file.h's own ts_byte
   * comment for the file-offset-98 verification (MIG-0109). */
  f->ts_byte = 0x20;
  f->ts_mode = false;
  if (f->version >= 7) {
    f->ts_byte = f->data[98];
    if (f->ts_byte != 0x20) f->ts_mode = true;
  }

  ay_engine_init(&f->ay);
  f->ay.ts_mode = f->ts_mode; /* AY.pas: TSMode, consumed by
                               * ay_synthesizer_stereo16's mixer (MIG-0109) -
                               * ay_engine_init memsets the whole engine, so
                               * this must be (re)applied after it, not
                               * before. ay_engine_reset_chip below always
                               * resets chip2 too, regardless of ts_mode, so
                               * it's in a clean state either way. */
  /* AY.pas: ChType's own declared global default is YM_Chip (AY.pas:151),
   * and PT3 never overrides it (only VTX's loader sets Chip_Type) - so
   * standalone PT3 playback uses the YM amplitude curve, matching
   * ay_engine_init's own default; no override needed here. */
  f->ay.delay_in_tiks =
      (uint32_t)(8192.0 / sample_rate * PT3_FILE_AY_FREQ_DEF + 0.5);
  f->ay.frq_ay_by_frq_z80 = 0; /* unused - no Z80 core drives this format */
  f->ay.tik_re = f->ay.delay_in_tiks;
  ay_engine_calculate_level_tables(&f->ay);
  ay_engine_reset_chip(&f->ay, true);

  /* Players.pas:3717-3806, InitTrackerModule's FT.PT3 branch, run once for
   * voice[0] and, when ts_mode, once more for voice[1] (Players.pas:4029-
   * 4030's `InitTrackerModule(CurFileType,0); if TSMode then
   * InitTrackerModule(CurFileType1,1);`) - voice_ts_byte is 0x20 for
   * voice[0] (PLConsts[0].TS is unconditionally $20, TrModLoaded:2651) and
   * f->ts_byte for voice[1] (PLConsts[1].TS := TS, only reached when
   * ts_mode, TrModLoaded:2667). */
  for (i = 0; i < (f->ts_mode ? 2 : 1); i++) {
    pt3_voice* v = &f->voice[i];
    uint8_t voice_ts_byte = (i == 0) ? 0x20 : f->ts_byte;
    int pat0 = f->data[f->position_list_offset];

    v->delay = f->data[100];
    v->delay_counter = 1;
    v->current_position = 0;
    v->noise_base = 0;
    v->add_to_noise = 0;
    v->cur_env_slide = 0;
    v->cur_env_delay = 0;
    v->env_base = 0;

    if (voice_ts_byte != 0x20) pat0 = (int)voice_ts_byte * 3 - 3 - pat0;
    v->chan_a.address_in_pattern =
        rd16(f->data, f->patterns_pointer + (uint32_t)pat0 * 2);
    v->chan_b.address_in_pattern =
        rd16(f->data, f->patterns_pointer + (uint32_t)pat0 * 2 + 2);
    v->chan_c.address_in_pattern =
        rd16(f->data, f->patterns_pointer + (uint32_t)pat0 * 2 + 4);

    {
      pt3_channel* a = &v->chan_a;
      a->ornament_pointer = f->ornaments_pointers[0];
      a->loop_ornament_position = f->data[a->ornament_pointer];
      a->ornament_pointer++;
      a->ornament_length = f->data[a->ornament_pointer];
      a->ornament_pointer++;
      a->sample_pointer = f->samples_pointers[1];
      a->loop_sample_position = f->data[a->sample_pointer];
      a->sample_pointer++;
      a->sample_length = f->data[a->sample_pointer];
      a->sample_pointer++;
      a->volume = 15;
      a->note_skip_counter = 1;
    }
    v->chan_b.ornament_pointer = v->chan_a.ornament_pointer;
    v->chan_b.loop_ornament_position = v->chan_a.loop_ornament_position;
    v->chan_b.ornament_length = v->chan_a.ornament_length;
    v->chan_b.sample_pointer = v->chan_a.sample_pointer;
    v->chan_b.loop_sample_position = v->chan_a.loop_sample_position;
    v->chan_b.sample_length = v->chan_a.sample_length;
    v->chan_b.volume = 15;
    v->chan_b.note_skip_counter = 1;
    v->chan_c.ornament_pointer = v->chan_a.ornament_pointer;
    v->chan_c.loop_ornament_position = v->chan_a.loop_ornament_position;
    v->chan_c.ornament_length = v->chan_a.ornament_length;
    v->chan_c.sample_pointer = v->chan_a.sample_pointer;
    v->chan_c.loop_sample_position = v->chan_a.loop_sample_position;
    v->chan_c.sample_length = v->chan_a.sample_length;
    v->chan_c.volume = 15;
    v->chan_c.note_skip_counter = 1;

    v->global_tick_counter = 0;
    v->real_end = false;
  }

  f->global_tick_counter = 0;
  f->do_loop = false;
  f->real_end_all = false;
  /* MIG-0101/MIG-0109 */
  pt3_get_time(f, f->ts_mode, f->ts_byte, &f->global_tick_max, &f->loop_tick);

  return PT3_FILE_OK;
}

/* Players.pas:12324-12583, PatternInterpreter. `chip` is the AY chip this
 * voice writes to (&f->ay.chip for voice 0, &f->ay.chip2 for voice 1,
 * MIG-0109) and `v` is that same voice's own mutable state (f->voice[0]
 * or f->voice[1]) - `f` itself is still needed for shared, header-derived
 * config (f->ornaments_pointers etc) plus, for the Flag9 tempo-change
 * effect below, both voices' state at once. */
static void pattern_interpreter(pt3_file* f, ay_chip* chip, pt3_voice* v,
                                 pt3_channel* chan) {
  bool quit = false;
  uint8_t flag9 = 0, flag8 = 0, flag5 = 0, flag4 = 0, flag3 = 0, flag2 = 0,
          flag1 = 0;
  uint8_t counter = 0;
  int pr_note = chan->note;
  int pr_sliding = chan->current_ton_sliding;
  uint8_t* d = f->data;

  do {
    uint8_t op = d[chan->address_in_pattern];
    if (op >= 0xF0) {
      chan->ornament_pointer = f->ornaments_pointers[op - 0xF0];
      chan->loop_ornament_position = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->ornament_length = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->address_in_pattern++;
      chan->sample_pointer = f->samples_pointers[d[chan->address_in_pattern] / 2];
      chan->loop_sample_position = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->sample_length = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->envelope_enabled = false;
      chan->position_in_ornament = 0;
    } else if (op >= 0xD1) { /* 0xD1..0xEF */
      chan->sample_pointer = f->samples_pointers[op - 0xD0];
      chan->loop_sample_position = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->sample_length = d[chan->sample_pointer];
      chan->sample_pointer++;
    } else if (op == 0xD0) {
      quit = true;
    } else if (op >= 0xC1) { /* 0xC1..0xCF */
      chan->volume = (uint8_t)(op - 0xC0);
    } else if (op == 0xC0) {
      chan->position_in_sample = 0;
      chan->current_amplitude_sliding = 0;
      chan->current_noise_sliding = 0;
      chan->current_envelope_sliding = 0;
      chan->position_in_ornament = 0;
      chan->ton_slide_count = 0;
      chan->current_ton_sliding = 0;
      chan->ton_accumulator = 0;
      chan->current_onoff = 0;
      chan->enabled = false;
      quit = true;
    } else if (op >= 0xB2) { /* 0xB2..0xBF */
      chan->envelope_enabled = true;
      ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(op - 0xB1));
      chan->address_in_pattern++;
      v->env_base = (int16_t)(((uint16_t)d[chan->address_in_pattern]) << 8);
      chan->address_in_pattern++;
      v->env_base = (int16_t)((uint16_t)v->env_base | d[chan->address_in_pattern]);
      chan->position_in_ornament = 0;
      v->cur_env_slide = 0;
      v->cur_env_delay = 0;
    } else if (op == 0xB1) {
      chan->address_in_pattern++;
      chan->number_of_notes_to_skip = d[chan->address_in_pattern];
    } else if (op == 0xB0) {
      chan->envelope_enabled = false;
      chan->position_in_ornament = 0;
    } else if (op >= 0x50) { /* 0x50..0xAF */
      chan->note = (uint8_t)(op - 0x50);
      chan->position_in_sample = 0;
      chan->current_amplitude_sliding = 0;
      chan->current_noise_sliding = 0;
      chan->current_envelope_sliding = 0;
      chan->position_in_ornament = 0;
      chan->ton_slide_count = 0;
      chan->current_ton_sliding = 0;
      chan->ton_accumulator = 0;
      chan->current_onoff = 0;
      chan->enabled = true;
      quit = true;
    } else if (op >= 0x40) { /* 0x40..0x4F */
      chan->ornament_pointer = f->ornaments_pointers[op - 0x40];
      chan->loop_ornament_position = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->ornament_length = d[chan->ornament_pointer];
      chan->ornament_pointer++;
      chan->position_in_ornament = 0;
    } else if (op >= 0x20) { /* 0x20..0x3F */
      v->noise_base = (uint8_t)(op - 0x20);
    } else if (op >= 0x10) { /* 0x10..0x1F */
      if (op == 0x10) {
        chan->envelope_enabled = false;
      } else {
        ay_chip_set_ay_register_fast(chip, 13, (uint8_t)(op - 0x10));
        chan->address_in_pattern++;
        v->env_base = (int16_t)(((uint16_t)d[chan->address_in_pattern]) << 8);
        chan->address_in_pattern++;
        v->env_base = (int16_t)((uint16_t)v->env_base | d[chan->address_in_pattern]);
        chan->envelope_enabled = true;
        v->cur_env_slide = 0;
        v->cur_env_delay = 0;
      }
      chan->address_in_pattern++;
      chan->sample_pointer = f->samples_pointers[d[chan->address_in_pattern] / 2];
      chan->loop_sample_position = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->sample_length = d[chan->sample_pointer];
      chan->sample_pointer++;
      chan->position_in_ornament = 0;
    } else if (op == 9) {
      counter++;
      flag9 = counter;
    } else if (op == 8) {
      counter++;
      flag8 = counter;
    } else if (op == 5) {
      counter++;
      flag5 = counter;
    } else if (op == 4) {
      counter++;
      flag4 = counter;
    } else if (op == 3) {
      counter++;
      flag3 = counter;
    } else if (op == 2) {
      counter++;
      flag2 = counter;
    } else if (op == 1) {
      counter++;
      flag1 = counter;
    }
    chan->address_in_pattern++;
  } while (!quit);

  while (counter > 0) {
    if (counter == flag1) {
      chan->ton_slide_delay = d[chan->address_in_pattern];
      chan->ton_slide_count = chan->ton_slide_delay;
      chan->address_in_pattern++;
      chan->ton_slide_step = (int16_t)rd16(d, chan->address_in_pattern);
      chan->address_in_pattern += 2;
      chan->simple_gliss = true;
      chan->current_onoff = 0;
      if (chan->ton_slide_count == 0 && f->version >= 7) chan->ton_slide_count++;
    } else if (counter == flag2) {
      chan->simple_gliss = false;
      chan->current_onoff = 0;
      chan->ton_slide_delay = d[chan->address_in_pattern];
      chan->ton_slide_count = chan->ton_slide_delay;
      chan->address_in_pattern += 3;
      {
        int16_t raw = (int16_t)rd16(d, chan->address_in_pattern);
        chan->ton_slide_step = (int16_t)(raw < 0 ? -raw : raw);
      }
      chan->address_in_pattern += 2;
      chan->ton_delta = (int16_t)(get_note_freq(f, chan->note) - get_note_freq(f, pr_note));
      chan->slide_to_note = chan->note;
      chan->note = (uint8_t)pr_note;
      if (f->version >= 6) chan->current_ton_sliding = (int16_t)pr_sliding;
      if (chan->ton_delta - chan->current_ton_sliding < 0)
        chan->ton_slide_step = (int16_t)-chan->ton_slide_step;
    } else if (counter == flag3) {
      chan->position_in_sample = d[chan->address_in_pattern];
      chan->address_in_pattern++;
    } else if (counter == flag4) {
      chan->position_in_ornament = d[chan->address_in_pattern];
      chan->address_in_pattern++;
    } else if (counter == flag5) {
      chan->onoff_delay = d[chan->address_in_pattern];
      chan->address_in_pattern++;
      chan->offon_delay = d[chan->address_in_pattern];
      chan->current_onoff = chan->onoff_delay;
      chan->address_in_pattern++;
      chan->ton_slide_count = 0;
      chan->current_ton_sliding = 0;
    } else if (counter == flag8) {
      v->env_delay = (int8_t)d[chan->address_in_pattern];
      v->cur_env_delay = v->env_delay;
      chan->address_in_pattern++;
      v->env_slide_add = (int16_t)rd16(d, chan->address_in_pattern);
      chan->address_in_pattern += 2;
    } else if (counter == flag9) {
      uint8_t b = d[chan->address_in_pattern];
      v->delay = b;
      if (f->ts_mode) {
        /* Players.pas:12584-12594 - `if TSMode and (PLConsts[1].TS <>
         * $20) then begin PlParams[0].PT3.Delay := b;
         * PlParams[0].PT3.DelayCounter := b; PlParams[1].PT3.Delay := b;
         * end;` - a tempo-change effect triggered from EITHER voice's own
         * pattern data re-syncs BOTH voices' tempo when real TS pairing
         * is active (PLConsts[1].TS <> $20 is exactly f->ts_mode here,
         * since ts_mode is only ever true when ts_byte != $20 by
         * construction). Note the asymmetry, faithfully reproduced: only
         * voice 0's DelayCounter is forced to the new value; voice 1's
         * own DelayCounter countdown is left alone regardless of which
         * voice triggered this. MIG-0109 - previously flagged as
         * unported debt (see this port's earlier "TSMode paired-delay
         * sync not ported - MIG-0007" comment, now resolved). */
        f->voice[0].delay = b;
        f->voice[0].delay_counter = b;
        f->voice[1].delay = b;
      }
      chan->address_in_pattern++;
    }
    counter--;
  }
  chan->note_skip_counter = (int8_t)chan->number_of_notes_to_skip;
}

/* Players.pas:12589-12682, ChangeRegisters. */
static void change_registers(pt3_file* f, pt3_voice* v, pt3_channel* chan,
                              uint8_t* temp_mixer, int8_t* add_to_env) {
  uint8_t* d = f->data;

  if (chan->enabled) {
    uint8_t b0 = d[chan->sample_pointer + (uint32_t)chan->position_in_sample * 4];
    uint8_t b1 = d[chan->sample_pointer + (uint32_t)chan->position_in_sample * 4 + 1];
    int j;

    chan->ton = rd16(d, chan->sample_pointer + (uint32_t)chan->position_in_sample * 4 + 2);
    chan->ton = (uint16_t)(chan->ton + chan->ton_accumulator);
    if (b1 & 0x40) chan->ton_accumulator = (int16_t)chan->ton;

    j = chan->note + d[chan->ornament_pointer + chan->position_in_ornament];
    j &= 0xFF;
    if ((int8_t)j < 0) j = 0;
    else if (j > 95) j = 95;

    {
      uint16_t w = (uint16_t)get_note_freq(f, j);
      chan->ton = (uint16_t)((chan->ton + chan->current_ton_sliding + w) & 0xFFF);
    }

    if (chan->ton_slide_count > 0) {
      chan->ton_slide_count--;
      if (chan->ton_slide_count == 0) {
        chan->current_ton_sliding = (int16_t)(chan->current_ton_sliding + chan->ton_slide_step);
        chan->ton_slide_count = chan->ton_slide_delay;
        if (!chan->simple_gliss) {
          if ((chan->ton_slide_step < 0 && chan->current_ton_sliding <= chan->ton_delta) ||
              (chan->ton_slide_step >= 0 && chan->current_ton_sliding >= chan->ton_delta)) {
            chan->note = chan->slide_to_note;
            chan->ton_slide_count = 0;
            chan->current_ton_sliding = 0;
          }
        }
      }
    }

    {
      int amplitude = b1 & 0xF;
      if (b0 & 0x80) {
        if (b0 & 0x40) {
          if (chan->current_amplitude_sliding < 15) chan->current_amplitude_sliding++;
        } else if (chan->current_amplitude_sliding > -15) {
          chan->current_amplitude_sliding--;
        }
      }
      amplitude += chan->current_amplitude_sliding;
      amplitude &= 0xFF;
      if ((int8_t)amplitude < 0) amplitude = 0;
      else if (amplitude > 15) amplitude = 15;

      if (f->version <= 4)
        amplitude = PT3_VOL_33_34[chan->volume][amplitude];
      else
        amplitude = PT3_VOL_35[chan->volume][amplitude];

      if ((b0 & 1) == 0 && chan->envelope_enabled) amplitude |= 16;
      chan->amplitude = (uint8_t)amplitude;
    }

    if (b1 & 0x80) {
      int j2;
      if (b0 & 0x20)
        j2 = (int)(((b0 >> 1) | 0xF0) + chan->current_envelope_sliding);
      else
        j2 = (int)((((b0 >> 1) & 0xF)) + chan->current_envelope_sliding);
      j2 &= 0xFF;
      if (b1 & 0x20) chan->current_envelope_sliding = (uint8_t)j2;
      *add_to_env = (int8_t)(*add_to_env + (int8_t)j2);
    } else {
      v->add_to_noise = (uint8_t)((b0 >> 1) + chan->current_noise_sliding);
      if (b1 & 0x20) chan->current_noise_sliding = v->add_to_noise;
    }

    *temp_mixer = (uint8_t)(((b1 >> 1) & 0x48) | *temp_mixer);

    chan->position_in_sample++;
    if (chan->position_in_sample >= chan->sample_length)
      chan->position_in_sample = chan->loop_sample_position;
    chan->position_in_ornament++;
    if (chan->position_in_ornament >= chan->ornament_length)
      chan->position_in_ornament = chan->loop_ornament_position;
  } else {
    chan->amplitude = 0;
  }

  *temp_mixer = (uint8_t)(*temp_mixer >> 1);
  if (chan->current_onoff > 0) {
    chan->current_onoff--;
    if (chan->current_onoff == 0) {
      chan->enabled = !chan->enabled;
      chan->current_onoff = chan->enabled ? chan->onoff_delay : chan->offon_delay;
    }
  }
}

/* Players.pas:12684-12766, PT3_Get_Registers's main body, parametrized by
 * voice_idx (0 or 1) matching the original's own explicit CNum parameter
 * (MIG-0109 - PT3_Get_Registers already took CNum throughout in the
 * original; this port's earlier single-voice version had simply never
 * been called with anything but CNum=0), PLUS an explicit `chip` target
 * (MIG-0112) - normally f's own &f->ay.chip/chip2 (see
 * pt3_get_registers_voice below, self-pairing's own entry point), but
 * for playlist-level Turbosound pairing (a DIFFERENT player's shared
 * engine's chip2) an external target is needed instead - matching
 * player_step_registers' own ay_chip* generalization across every
 * pairing-eligible format. voice_idx 1 is only ever driven by
 * pt3_file_make_buffer when f->ts_mode is set (self-pairing); playlist
 * pairing only ever drives voice 0 (see pt3_file_step_registers). */
static void pt3_get_registers_into(pt3_file* f, int voice_idx, ay_chip* chip) {
  pt3_voice* v = &f->voice[voice_idx];
  uint8_t temp_mixer = 0;
  int8_t add_to_env = 0;

  v->delay_counter--;
  if (v->delay_counter == 0) {
    v->chan_a.note_skip_counter--;
    if (v->chan_a.note_skip_counter == 0) {
      if (f->data[v->chan_a.address_in_pattern] == 0) {
        v->current_position++;
        if (v->current_position == f->number_of_positions)
          v->current_position = f->loop_position;
        {
          /* Players.pas:12722-12723 - `i := PT3_PositionList[CurrentPosition];
           * b := PLConsts[CNum].TS; if b <> $20 then i := b*3-3-i;` -
           * voice 0's own PLConsts[0].TS is unconditionally $20 (never
           * triggers), voice 1's is f->ts_byte (see pt3_file.h). */
          int i = f->data[f->position_list_offset + v->current_position];
          if (voice_idx == 1 && f->ts_mode)
            i = (int)f->ts_byte * 3 - 3 - i;
          v->chan_a.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 2);
          v->chan_b.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 2 + 2);
          v->chan_c.address_in_pattern =
              rd16(f->data, f->patterns_pointer + (uint32_t)i * 2 + 4);
          v->noise_base = 0;
        }
      }
      pattern_interpreter(f, chip, v, &v->chan_a);
    }
    v->chan_b.note_skip_counter--;
    if (v->chan_b.note_skip_counter == 0)
      pattern_interpreter(f, chip, v, &v->chan_b);
    v->chan_c.note_skip_counter--;
    if (v->chan_c.note_skip_counter == 0)
      pattern_interpreter(f, chip, v, &v->chan_c);
    v->delay_counter = v->delay;
  }

  add_to_env = 0;
  temp_mixer = 0;
  change_registers(f, v, &v->chan_a, &temp_mixer, &add_to_env);
  change_registers(f, v, &v->chan_b, &temp_mixer, &add_to_env);
  change_registers(f, v, &v->chan_c, &temp_mixer, &add_to_env);

  ay_chip_set_ay_register_fast(chip, 7, temp_mixer);

  chip->reg[0] = (uint8_t)(v->chan_a.ton & 0xFF);
  chip->reg[1] = (uint8_t)(v->chan_a.ton >> 8);
  chip->reg[2] = (uint8_t)(v->chan_b.ton & 0xFF);
  chip->reg[3] = (uint8_t)(v->chan_b.ton >> 8);
  chip->reg[4] = (uint8_t)(v->chan_c.ton & 0xFF);
  chip->reg[5] = (uint8_t)(v->chan_c.ton >> 8);

  ay_chip_set_ay_register_fast(chip, 8, v->chan_a.amplitude);
  ay_chip_set_ay_register_fast(chip, 9, v->chan_b.amplitude);
  ay_chip_set_ay_register_fast(chip, 10, v->chan_c.amplitude);

  chip->reg[6] = (uint8_t)((v->noise_base + v->add_to_noise) & 31);

  {
    int16_t env = (int16_t)(v->env_base + add_to_env + v->cur_env_slide);
    chip->reg[11] = (uint8_t)(env & 0xFF);
    chip->reg[12] = (uint8_t)((env >> 8) & 0xFF);
  }

  if (v->cur_env_delay > 0) {
    v->cur_env_delay--;
    if (v->cur_env_delay == 0) {
      v->cur_env_delay = v->env_delay;
      v->cur_env_slide = (int16_t)(v->cur_env_slide + v->env_slide_add);
    }
  }

  v->global_tick_counter++;
}

/* Thin wrapper matching pt3_get_registers_into's own pre-MIG-0112
 * signature - self-pairing's two call sites (pt3_file_make_buffer)
 * always target f's own chip/chip2 by voice index. */
static void pt3_get_registers_voice(pt3_file* f, int voice_idx) {
  ay_chip* chip = (voice_idx == 0) ? &f->ay.chip : &f->ay.chip2;
  pt3_get_registers_into(f, voice_idx, chip);
}

/* Players.pas:8732-8746, CheckLoopAndStop(CNum) - returns true (matching
 * the original's own `Exit(True)`) when this voice's tick budget has been
 * reached and Do_Loop is off AND Force_Loop is off (MIG-0114), meaning
 * pt3_get_registers_voice should be skipped entirely this frame; sets
 * v->real_end permanently once the tick budget is reached regardless of
 * Force_Loop (matching `if not Do_Loop then Real_End[CNum] := True`
 * unconditionally within that branch). With Force_Loop on, global_tick_
 * counter is clamped at global_tick_max (matching `if Do_Loop or
 * Force_Loop then ...Counter := ...Max`) but register generation is NOT
 * skipped (`if not Force_Loop then Exit(True)`), so this voice keeps
 * audibly looping its own pattern data - the "keep playing a shorter
 * TS-pair module" case - even though v->real_end is already true. */
static bool pt3_check_loop_and_stop(pt3_voice* v, int64_t global_tick_max,
                                     bool do_loop, bool force_loop) {
  if (global_tick_max > 0 && v->global_tick_counter >= global_tick_max) {
    if (do_loop || force_loop) {
      v->global_tick_counter = global_tick_max;
    }
    if (!do_loop) {
      v->real_end = true;
      if (!force_loop) return true;
    }
  }
  return false;
}

/* MIG-0112: playlist-level Turbosound pairing's entry point for PT3 -
 * used when a PT3 file is one half of a playlist Next-chain pair
 * (player_pair, player.c), as opposed to PT3's OWN self-pairing
 * (f->ts_mode, MIG-0109, driven by pt3_file_make_buffer directly). Only
 * ever drives voice 0 - player_pair_load_song already refuses to
 * activate playlist pairing for a PT3 file that's already self-pairing
 * (TrModLoaded's own `TSMode = False` guard, Players.pas:2643-2691), so
 * a PT3 participating in a playlist pair never has its own f->ts_mode
 * set, exactly like standalone single-chip PT3 playback. */
bool pt3_file_step_registers(pt3_file* f, ay_chip* chip) {
  if (pt3_check_loop_and_stop(&f->voice[0], f->global_tick_max, f->do_loop, f->force_loop)) {
    f->real_end_all = true;
    return false;
  }
  pt3_get_registers_into(f, 0, chip);
  f->global_tick_counter = f->voice[0].global_tick_counter;
  return true;
}

int pt3_file_make_buffer(pt3_file* f, int16_t* buf, int buffer_length) {
  ay_engine* ay = &f->ay;
  /* AY.pas:2070's AY_Tiks_In_Interrupt: trunc(AY_Freq/(Interrupt_Freq/1000*8)
   * + 0.5) - a fixed per-frame integer tick budget, computed once (not a
   * running accumulation like SynthesizerAY/SynthesizerYM6). */
  static const int64_t ay_tiks_in_interrupt =
      (int64_t)(PT3_FILE_AY_FREQ_DEF /
                    (PT3_FILE_INTERRUPT_FREQ_DEF / 1000.0 * 8.0) +
                0.5);

  ay->buf = buf;
  ay->buf_len = 0;
  ay->buffer_length = buffer_length;
  /* See fxm_file.c's make_buffer for why number_of_channels is not
   * reset here (player_set_number_of_channels's load-time override
   * must persist across buffer-fill calls). */
  ay->sample_bits = 16;

  /* Players.pas:12283-12300 MakeBufferTracker's own buffer-boundary
   * cutoff handling, mirroring MakeBufferAY/MakeBufferYM5. */
  if (ay->int_flag) {
    ay->int_flag = false;
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  if (ay->int_flag) return ay->buf_len;

  while (ay->buf_len < buffer_length) {
    /* Players.pas: MakeBufferTracker's own body (12301-12317, MIG-0109) -
     * `Real_End_All := True; All_GetRegisters[0](0); Real_End_All :=
     * Real_End_All and Real_End[0]; if TSMode then begin
     * All_GetRegisters[1](1); Real_End_All := Real_End_All and
     * Real_End[1]; end; if not Real_End_All then SynthesizerZX50(Buf);` -
     * each voice's own PT3_Get_Registers(CNum) starts with `if
     * CheckLoopAndStop(CNum) then Exit;` (Players.pas:8732-8746,
     * MIG-0101), reproduced here as pt3_check_loop_and_stop gating the
     * pt3_get_registers_voice call rather than being its first statement
     * - equivalent since nothing else touches global_tick_counter between
     * one voice's call ending and the next starting. When ts_mode is set,
     * BOTH voices' own natural-end condition is genuinely required (a
     * real AND, not silently collapsed to voice 0's alone) - matching
     * Real_End_All's own semantics exactly. */
    bool real_end_all = true;
    if (!pt3_check_loop_and_stop(&f->voice[0], f->global_tick_max, f->do_loop, f->force_loop))
      pt3_get_registers_voice(f, 0);
    real_end_all = real_end_all && f->voice[0].real_end;
    if (f->ts_mode) {
      if (!pt3_check_loop_and_stop(&f->voice[1], f->global_tick_max, f->do_loop, f->force_loop))
        pt3_get_registers_voice(f, 1);
      real_end_all = real_end_all && f->voice[1].real_end;
    }
    /* player.c's own progress-display contract only ever reads voice 0's
     * counter (see pt3_file.h's own comment on this field) - kept in
     * sync here every iteration. */
    f->global_tick_counter = f->voice[0].global_tick_counter;
    if (real_end_all) {
      f->real_end_all = true;
      break;
    }
    /* AY.pas:1075-1082 SynthesizerZX50. */
    if (!ay->int_flag) {
      ay->number_of_tiks = ay_tiks_in_interrupt << 32;
    } else {
      ay->int_flag = false;
    }
    ay_synthesizer_dispatch(ay); /* MIG-0107: was hardcoded stereo16 */
  }
  return ay->buf_len;
}
