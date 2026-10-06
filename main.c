/*
 * time-sync.elf - minimal SNTP client payload for the PS5 payload SDK.
 *
 * Queries an NTP server over UDP/123 and sets the system clock with
 * settimeofday(). On FreeBSD-derived kernels settimeofday() also
 * writes the time-of-day (RTC) hardware via resettodr(); this is
 * not verified on Orbis/Prospero, so the payload reads the clock back
 * and reports what the kernel actually accepted.
 *
 * Server selection (first match wins):
 *   1. first line of /data/time-sync.conf  (hostname or IPv4)
 *   2. -DNTP_SERVER="..." at build time
 *   3. "time.apple.com"
 */

#include <arpa/inet.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef NTP_SERVER
#define NTP_SERVER "time.apple.com"
#endif

#define NTP_PORT        "123"
#define NTP_TIMEOUT_SEC 5
#define NTP_UNIX_DELTA  2208988800ULL /* 1900-01-01 -> 1970-01-01 */
#define CONF_PATH       "/data/time-sync.conf"
#define MIN_SANE_UNIX   1577836800LL  /* 2020-01-01: reject garbage */

/* Notification toast (same layout used by ps5-payload-sdk samples). */
typedef struct {
  char pad[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void say(const char *fmt, ...) {
  char buf[256];
  va_list ap;
  notify_request_t req;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  printf("[time-sync] %s\n", buf);
  fflush(stdout);

  memset(&req, 0, sizeof(req));
  snprintf(req.message, sizeof(req.message), "time-sync: %s", buf);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void put_be32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static uint32_t get_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void load_server(char *out, size_t len) {
  FILE *f = fopen(CONF_PATH, "r");
  snprintf(out, len, "%s", NTP_SERVER);
  if (f) {
    char line[128];
    if (fgets(line, sizeof(line), f)) {
      line[strcspn(line, "\r\n \t")] = 0;
      if (line[0]) snprintf(out, len, "%s", line);
    }
    fclose(f);
  }
}

static int64_t usec_of(const struct timeval *tv) {
  return (int64_t)tv->tv_sec * 1000000LL + tv->tv_usec;
}

int main(void) {
  char server[128];
  struct addrinfo hints, *res = NULL;
  struct timeval tmo, t1, t4, now;
  uint8_t req[48], rsp[48];
  int fd, rc;
  ssize_t n;

  load_server(server, sizeof(server));
  say("querying %s", server);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if ((rc = getaddrinfo(server, NTP_PORT, &hints, &res)) != 0 || !res) {
    say("FAILED: cannot resolve %s (use an IP in " CONF_PATH ")", server);
    return 1;
  }

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    say("FAILED: socket()");
    freeaddrinfo(res);
    return 1;
  }

  tmo.tv_sec = NTP_TIMEOUT_SEC;
  tmo.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tmo, sizeof(tmo));

  /* LI=0, VN=4, Mode=3 (client). The transmit timestamp doubles as a
   * nonce: the server must echo it back in its originate field. */
  memset(req, 0, sizeof(req));
  req[0] = (0 << 6) | (4 << 3) | 3;
  gettimeofday(&t1, NULL);
  put_be32(&req[40], (uint32_t)(t1.tv_sec + NTP_UNIX_DELTA));
  put_be32(&req[44], (uint32_t)(((uint64_t)t1.tv_usec << 32) / 1000000ULL));

  if (sendto(fd, req, sizeof(req), 0, res->ai_addr, res->ai_addrlen) !=
      (ssize_t)sizeof(req)) {
    say("FAILED: sendto()");
    close(fd);
    freeaddrinfo(res);
    return 1;
  }

  n = recvfrom(fd, rsp, sizeof(rsp), 0, NULL, NULL);
  gettimeofday(&t4, NULL);
  close(fd);
  freeaddrinfo(res);

  if (n < (ssize_t)sizeof(rsp)) {
    say("FAILED: no/short reply (timeout %ds)", NTP_TIMEOUT_SEC);
    return 1;
  }
  if (memcmp(&rsp[24], &req[40], 8) != 0) {
    say("FAILED: reply does not match request");
    return 1;
  }
  if ((rsp[0] & 7) != 4 || (rsp[0] >> 6) == 3 || rsp[1] == 0) {
    say("FAILED: server unsynchronized or kiss-o'-death");
    return 1;
  }

  uint32_t secs = get_be32(&rsp[40]);
  uint32_t frac = get_be32(&rsp[44]);
  if (secs == 0) {
    say("FAILED: empty transmit timestamp");
    return 1;
  }

  /* NTP era 0 ends in 2036; values below the 1970 offset mean era 1. */
  int64_t unix_s = (secs >= NTP_UNIX_DELTA)
                       ? (int64_t)secs - (int64_t)NTP_UNIX_DELTA
                       : (int64_t)secs + (1LL << 32) - (int64_t)NTP_UNIX_DELTA;
  int64_t us = (int64_t)(((uint64_t)frac * 1000000ULL) >> 32);

  /* Compensate for network delay: assume symmetric path. */
  int64_t rtt = usec_of(&t4) - usec_of(&t1);
  us += rtt / 2;
  unix_s += us / 1000000;
  us %= 1000000;

  if (unix_s < MIN_SANE_UNIX) {
    say("FAILED: implausible time from server");
    return 1;
  }

  struct timeval set = {.tv_sec = (time_t)unix_s, .tv_usec = (suseconds_t)us};
  if (settimeofday(&set, NULL) != 0) {
    struct timespec ts = {.tv_sec = set.tv_sec, .tv_nsec = set.tv_usec * 1000};
    if (clock_settime(CLOCK_REALTIME, &ts) != 0) {
      say("FAILED: kernel refused to set the clock (privileges?)");
      return 1;
    }
  }

  gettimeofday(&now, NULL);
  time_t t = now.tv_sec;
  struct tm tmv;
  gmtime_r(&t, &tmv);
  say("OK %04d-%02d-%02d %02d:%02d:%02d UTC (rtt %lld ms)",
      tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour,
      tmv.tm_min, tmv.tm_sec, (long long)(rtt / 1000));
  return 0;
}
