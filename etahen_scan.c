/*
 * etahen_scan.c - find the expiry date built into etaHEN beta payloads.
 *
 * etaHEN payloads (2.0 and later) are a small unpacker carrying the real
 * bootstrapper as an LZMA-alone stream. Beta builds call
 *
 *   if (isPastBetaDate(YEAR, MONTH, DAY))
 *     notify("This etaHEN Beta version expired on %d-%d-%d", YEAR, MONTH, DAY);
 *
 * near the start of the bootstrapper, which compiles to
 *
 *   BF <year> BE <month> BA <day> [67] E8 ...   isPastBetaDate(y, m, d)
 *   ... BE <year> BA <month> B9 <day> ...      notify(fmt, y, m, d)
 *
 * We decode only the first part of the stream and look for that pair. Both
 * halves must agree, so other payloads cannot match by accident. Payloads
 * stored unpacked (like /data/etaHEN/etaHEN.bin) are scanned directly.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "LzmaDec.h"
#include "etahen_scan.h"

#define READ_MAX   (1024 * 1024) /* stream starts within the first 256 KiB */
#define DECODE_MAX (512 * 1024)  /* the check sits within the first 64 KiB */
#define CONFIRM_WINDOW 0x60
#define MIN_SIZE   (64 * 1024)
#define MAX_SIZE   (64 * 1024 * 1024)

static const uint8_t lzma_hdr[13] = {0x5d, 0x00, 0x00, 0x00, 0x04, 0xff, 0xff,
                                     0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static void *sz_alloc(ISzAllocPtr p, size_t size) {
  (void)p;
  return malloc(size);
}

static void sz_free(ISzAllocPtr p, void *addr) {
  (void)p;
  free(addr);
}

static const ISzAlloc g_alloc = {sz_alloc, sz_free};

static uint32_t le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int plausible(uint32_t y, uint32_t m, uint32_t d) {
  return y >= 2020 && y <= 2099 && m >= 1 && m <= 12 && d >= 1 && d <= 31;
}

/* Earliest confirmed isPastBetaDate(y, m, d) in buf, or 0 if none. */
static int find_expiry(const uint8_t *buf, size_t len, etahen_date_t *out) {
  int found = 0;

  for (size_t i = 0; i + 21 <= len; i++) {
    if (buf[i] != 0xbf || buf[i + 5] != 0xbe || buf[i + 10] != 0xba)
      continue;
    size_t call = i + 15;
    if (buf[call] == 0x67) call++;
    if (buf[call] != 0xe8) continue;

    uint32_t y = le32(buf + i + 1), m = le32(buf + i + 6), d = le32(buf + i + 11);
    if (!plausible(y, m, d)) continue;

    /* The notification repeats the same date: BE y BA m B9 d. */
    int confirmed = 0;
    for (size_t j = call + 5; j + 15 <= len && j < call + 5 + CONFIRM_WINDOW;
         j++) {
      if (buf[j] == 0xbe && buf[j + 5] == 0xba && buf[j + 10] == 0xb9 &&
          le32(buf + j + 1) == y && le32(buf + j + 6) == m &&
          le32(buf + j + 11) == d) {
        confirmed = 1;
        break;
      }
    }
    if (!confirmed) continue;

    if (!found || y < (uint32_t)out->year ||
        (y == (uint32_t)out->year &&
         (m < (uint32_t)out->month ||
          (m == (uint32_t)out->month && d < (uint32_t)out->day)))) {
      out->year = (int)y;
      out->month = (int)m;
      out->day = (int)d;
    }
    found = 1;
  }
  return found;
}

static int contains(const uint8_t *buf, size_t len, const char *s) {
  size_t n = strlen(s);
  for (size_t i = 0; i + n <= len; i++)
    if (!memcmp(buf + i, s, n)) return 1;
  return 0;
}

int etahen_scan_file(const char *path, etahen_date_t *expiry) {
  struct stat st;
  uint8_t *in = NULL, *out = NULL;
  ssize_t got = 0;
  int fd, rc = ETAHEN_NOT_FOUND;

  if ((fd = open(path, O_RDONLY)) < 0) return ETAHEN_NOT_FOUND;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < MIN_SIZE ||
      st.st_size > MAX_SIZE)
    goto out;
  if (!(in = malloc(READ_MAX))) goto out;
  while (got < READ_MAX) {
    ssize_t n = read(fd, in + got, READ_MAX - got);
    if (n <= 0) break;
    got += n;
  }

  /* Unpacked bootstrapper: scan as is. */
  if (find_expiry(in, (size_t)got, expiry)) {
    rc = ETAHEN_EXPIRES;
    goto out;
  }

  size_t off;
  for (off = 0; off + sizeof(lzma_hdr) <= (size_t)got; off++)
    if (in[off] == 0x5d && !memcmp(in + off, lzma_hdr, sizeof(lzma_hdr)))
      break;
  if (off + sizeof(lzma_hdr) > (size_t)got) {
    /* Old unpacked builds (1.x) carry a plain banner and never expire. */
    if (contains(in, (size_t)got, "etaHEN")) rc = ETAHEN_NO_EXPIRY;
    goto out;
  }

  if (!(out = malloc(DECODE_MAX))) goto out;
  SizeT out_len = DECODE_MAX;
  SizeT in_len = (SizeT)got - off - sizeof(lzma_hdr);
  ELzmaStatus status;
  SRes res = LzmaDecode(out, &out_len, in + off + sizeof(lzma_hdr), &in_len,
                        in + off, LZMA_PROPS_SIZE, LZMA_FINISH_ANY, &status,
                        &g_alloc);
  /* Running out of input or output is expected: we decode a prefix only. */
  if (res != SZ_OK && res != SZ_ERROR_INPUT_EOF) goto out;

  if (find_expiry(out, out_len, expiry))
    rc = ETAHEN_EXPIRES;
  else if (contains(out, out_len, "etaHEN"))
    rc = ETAHEN_NO_EXPIRY;

out:
  free(out);
  free(in);
  close(fd);
  return rc;
}
