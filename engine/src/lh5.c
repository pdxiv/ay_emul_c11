#include "ay_engine/lh5.h"

#include <stdlib.h>
#include <string.h>

#define BITBUFSIZ 16
#define UCHARMAX 255
#define DICBIT 13
#define DICSIZ (1 << DICBIT) /* 8192 */
#define MATCHBIT 8
#define MAXMATCH (1 << MATCHBIT) /* 256 */
#define THRESHOLD 3
#define NC (UCHARMAX + MAXMATCH + 2 - THRESHOLD) /* 510 */
#define CBIT 9
#define CODEBIT 16
#define NP (DICBIT + 1)  /* 14 */
#define NT (CODEBIT + 3) /* 19 */
#define PBIT 4
#define TBIT 5
#define NPT NT

typedef struct lh5_ctx {
  const uint8_t* in;
  int32_t in_size;
  int32_t in_pos;
  bool bad;

  uint16_t bit_buf;
  uint16_t sub_bit_buf;
  int16_t bit_count;

  uint8_t buffer[DICSIZ]; /* dictionary ring buffer */
  uint16_t r;

  uint16_t block_size;
  uint16_t pt_table[256];
  uint8_t pt_len[NPT];
  uint16_t c_table[4096];
  uint8_t c_len[NC];
  uint16_t left[2 * (NC - 1)];
  uint16_t right[2 * (NC - 1)];

  uint16_t decode_i;
  int16_t decode_j;
} lh5_ctx;

static uint8_t get_c(lh5_ctx* c) {
  if (c->in_pos < c->in_size) return c->in[c->in_pos++];
  return 0;
}

static void fill_buf(lh5_ctx* c, int16_t n) {
  c->bit_buf = (uint16_t)(c->bit_buf << n);
  while (n > c->bit_count) {
    n = (int16_t)(n - c->bit_count);
    c->bit_buf = (uint16_t)(c->bit_buf | (c->sub_bit_buf << n));
    if (c->in_pos < c->in_size) {
      c->sub_bit_buf = get_c(c);
    } else {
      c->sub_bit_buf = 0;
    }
    c->bit_count = 8;
  }
  c->bit_count = (int16_t)(c->bit_count - n);
  c->bit_buf = (uint16_t)(c->bit_buf | (c->sub_bit_buf >> c->bit_count));
}

static uint16_t get_bits(lh5_ctx* c, int16_t n) {
  uint16_t v = (uint16_t)(c->bit_buf >> (BITBUFSIZ - n));
  fill_buf(c, n);
  return v;
}

static void init_get_bits(lh5_ctx* c) {
  c->bit_buf = 0;
  c->sub_bit_buf = 0;
  c->bit_count = 0;
  fill_buf(c, BITBUFSIZ);
}

/* lh5.pas:217-283 MakeTable. */
static bool make_table(lh5_ctx* c, int16_t nchar, const uint8_t* bit_len,
                        int16_t table_bits, uint16_t* table) {
  uint16_t count[17], weight[17], start[18];
  int16_t i, len, ch, jutbits, avail, mask;
  uint16_t k, next_code;

  memset(count, 0, sizeof(count));
  for (i = 0; i < nchar; i++) count[bit_len[i]]++;

  start[1] = 0;
  for (i = 1; i <= 16; i++) start[i + 1] = (uint16_t)(start[i] + (count[i] << (16 - i)));
  if (start[17] != 0) return false; /* InvalidLZH */

  jutbits = (int16_t)(16 - table_bits);
  for (i = 1; i <= table_bits; i++) {
    start[i] = (uint16_t)(start[i] >> jutbits);
    weight[i] = (uint16_t)(1 << (table_bits - i));
  }
  i = (int16_t)(table_bits + 1);
  while (i <= 16) {
    weight[i] = (uint16_t)(1 << (16 - i));
    i++;
  }

  i = (int16_t)(start[table_bits + 1] >> jutbits);
  if (i != 0) {
    k = (uint16_t)(1 << table_bits);
    while (i != k) {
      table[i] = 0;
      i++;
    }
  }

  avail = nchar;
  mask = (int16_t)(1 << (15 - table_bits));
  for (ch = 0; ch < nchar; ch++) {
    len = bit_len[ch];
    if (len == 0) continue;
    k = start[len];
    next_code = (uint16_t)(k + weight[len]);
    if (len <= table_bits) {
      uint16_t ii;
      for (ii = k; ii < next_code; ii++) table[ii] = (uint16_t)ch;
    } else {
      /* lh5.pas:266-280: p walks from table[k>>jutbits] down into
       * left[]/right[] as a binary-tree pointer chase; ported as an
       * index-based walk (p_in_table selects the first hop's storage,
       * subsequent hops always index left/right). */
      uint16_t* p = &table[k >> jutbits];
      int16_t remain = (int16_t)(len - table_bits);
      while (remain != 0) {
        if (*p == 0) {
          c->right[avail] = 0;
          c->left[avail] = 0;
          *p = (uint16_t)avail;
          avail++;
        }
        if ((k & mask) != 0) {
          p = &c->right[*p];
        } else {
          p = &c->left[*p];
        }
        k = (uint16_t)(k << 1);
        remain--;
      }
      *p = (uint16_t)ch;
    }
    start[len] = next_code;
  }
  return true;
}

/* lh5.pas:285-327 ReadPtLen. */
static bool read_pt_len(lh5_ctx* c, int16_t nn, int16_t n_bit, int16_t ispecial) {
  int16_t i, cc, n;
  uint16_t mask;

  n = (int16_t)get_bits(c, n_bit);
  if (n == 0) {
    cc = (int16_t)get_bits(c, n_bit);
    for (i = 0; i < nn; i++) c->pt_len[i] = 0;
    for (i = 0; i <= 255; i++) c->pt_table[i] = (uint16_t)cc;
  } else {
    i = 0;
    while (i < n) {
      cc = (int16_t)(c->bit_buf >> (BITBUFSIZ - 3));
      if (cc == 7) {
        mask = (uint16_t)(1 << (BITBUFSIZ - 4));
        while ((mask & c->bit_buf) != 0) {
          mask = (uint16_t)(mask >> 1);
          cc++;
        }
      }
      if (cc < 7) {
        fill_buf(c, 3);
      } else {
        fill_buf(c, (int16_t)(cc - 3));
      }
      c->pt_len[i] = (uint8_t)cc;
      i++;
      if (i == ispecial) {
        cc = (int16_t)(get_bits(c, 2) - 1);
        while (cc >= 0) {
          c->pt_len[i] = 0;
          i++;
          cc--;
        }
      }
    }
    while (i < nn) {
      c->pt_len[i] = 0;
      i++;
    }
    if (!make_table(c, nn, c->pt_len, 8, c->pt_table)) return false;
  }
  return true;
}

/* lh5.pas:329-377 ReadCLen. */
static bool read_c_len(lh5_ctx* c) {
  int16_t i, cc, n;
  uint16_t mask;

  n = (int16_t)get_bits(c, CBIT);
  if (n == 0) {
    cc = (int16_t)get_bits(c, CBIT);
    for (i = 0; i < NC; i++) c->c_len[i] = 0;
    for (i = 0; i <= 4095; i++) c->c_table[i] = (uint16_t)cc;
  } else {
    i = 0;
    while (i < n) {
      cc = (int16_t)c->pt_table[c->bit_buf >> (BITBUFSIZ - 8)];
      if (cc >= NT) {
        mask = (uint16_t)(1 << (BITBUFSIZ - 9));
        do {
          if ((c->bit_buf & mask) != 0) {
            cc = (int16_t)c->right[cc];
          } else {
            cc = (int16_t)c->left[cc];
          }
          mask = (uint16_t)(mask >> 1);
        } while (cc >= NT);
      }
      fill_buf(c, c->pt_len[cc]);
      if (cc <= 2) {
        if (cc == 1) {
          cc = (int16_t)(2 + get_bits(c, 4));
        } else if (cc == 2) {
          cc = (int16_t)(19 + get_bits(c, CBIT));
        }
        while (cc >= 0) {
          c->c_len[i] = 0;
          i++;
          cc--;
        }
      } else {
        c->c_len[i] = (uint8_t)(cc - 2);
        i++;
      }
    }
    while (i < NC) {
      c->c_len[i] = 0;
      i++;
    }
    if (!make_table(c, NC, c->c_len, 12, c->c_table)) return false;
  }
  return true;
}

/* lh5.pas:379-405 DecodeC. */
static bool decode_c(lh5_ctx* c, uint16_t* out) {
  uint16_t j, mask;

  if (c->block_size == 0) {
    c->block_size = get_bits(c, 16);
    if (!read_pt_len(c, NT, TBIT, 3)) return false;
    if (!read_c_len(c)) return false;
    if (!read_pt_len(c, NP, PBIT, -1)) return false;
  }
  c->block_size--;
  j = c->c_table[c->bit_buf >> (BITBUFSIZ - 12)];
  if (j >= NC) {
    mask = (uint16_t)(1 << (BITBUFSIZ - 13));
    do {
      if ((c->bit_buf & mask) != 0) {
        j = c->right[j];
      } else {
        j = c->left[j];
      }
      mask = (uint16_t)(mask >> 1);
    } while (j >= NC);
  }
  fill_buf(c, c->c_len[j]);
  *out = j;
  return true;
}

/* lh5.pas:407-429 DecodeP. */
static uint16_t decode_p(lh5_ctx* c) {
  uint16_t j, mask;

  j = c->pt_table[c->bit_buf >> (BITBUFSIZ - 8)];
  if (j >= NP) {
    mask = (uint16_t)(1 << (BITBUFSIZ - 9));
    do {
      if ((c->bit_buf & mask) != 0) {
        j = c->right[j];
      } else {
        j = c->left[j];
      }
      mask = (uint16_t)(mask >> 1);
    } while (j >= NP);
  }
  fill_buf(c, c->pt_len[j]);
  if (j != 0) {
    j--;
    j = (uint16_t)((1 << j) + get_bits(c, (int16_t)j));
  }
  return j;
}

/* lh5.pas:436-469 DecodeBuffer - decodes until c->r reaches `count`. */
static bool decode_buffer(lh5_ctx* c, uint16_t count) {
  c->decode_j--;
  while (c->decode_j >= 0) {
    c->buffer[c->r] = c->buffer[c->decode_i];
    c->decode_i = (uint16_t)((c->decode_i + 1) & (DICSIZ - 1));
    c->r++;
    if (c->r == count) return true;
    c->decode_j--;
  }
  for (;;) {
    uint16_t cc;
    if (!decode_c(c, &cc)) return false;
    if (cc <= UCHARMAX) {
      c->buffer[c->r] = (uint8_t)cc;
      c->r++;
      if (c->r == count) return true;
    } else {
      c->decode_j = (int16_t)(cc - (UCHARMAX + 1 - THRESHOLD));
      c->decode_i = (uint16_t)((c->r - decode_p(c) - 1) & (DICSIZ - 1));
      c->decode_j--;
      while (c->decode_j >= 0) {
        c->buffer[c->r] = c->buffer[c->decode_i];
        c->decode_i = (uint16_t)((c->decode_i + 1) & (DICSIZ - 1));
        c->r++;
        if (c->r == count) return true;
        c->decode_j--;
      }
    }
  }
}

bool lh5_decompress(const uint8_t* comp, int32_t comp_size, uint8_t* out,
                     int32_t out_size) {
  lh5_ctx* c;
  int32_t out_pos;
  bool ok = true;

  c = (lh5_ctx*)calloc(1, sizeof(lh5_ctx));
  if (!c) return false;

  c->in = comp;
  c->in_size = comp_size;
  c->in_pos = 0;
  c->r = 0;
  c->block_size = 0;
  c->decode_j = 0;
  init_get_bits(c);

  out_pos = 0;
  while (out_pos < out_size) {
    uint16_t a = (uint16_t)(DICSIZ - c->r);
    int32_t remaining = out_size - out_pos;
    if (remaining < a) a = (uint16_t)remaining;
    if (!decode_buffer(c, (uint16_t)(a + c->r))) {
      ok = false;
      break;
    }
    memcpy(out + out_pos, &c->buffer[c->r - a], a);
    c->r = (uint16_t)(c->r & (DICSIZ - 1));
    out_pos += a;
  }

  free(c);
  return ok;
}
