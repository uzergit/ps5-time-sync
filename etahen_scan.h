#ifndef ETAHEN_SCAN_H
#define ETAHEN_SCAN_H

typedef struct {
  int year, month, day;
} etahen_date_t;

enum {
  ETAHEN_NOT_FOUND = 0, /* not an etaHEN payload (or unreadable) */
  ETAHEN_NO_EXPIRY = 1, /* etaHEN, but no beta expiry check found */
  ETAHEN_EXPIRES = 2,   /* beta build: *expiry is filled in */
};

/* Inspect one payload file without running it. */
int etahen_scan_file(const char *path, etahen_date_t *expiry);

#endif
