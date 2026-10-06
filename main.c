/*
 * time-sync.elf - minimal SNTP client payload for the PS5 payload SDK.
 *
 * Queries an NTP server over UDP and sets the system clock with
 * settimeofday(). On FreeBSD-derived kernels settimeofday() also
 * writes the time-of-day (RTC) hardware via resettodr(); this has been
 * confirmed to survive a reboot on a real PS5. The payload reads the clock back
 * and reports what the kernel actually accepted.
 *
 * Built with -DUNSYNC it becomes time-unsync.elf instead, which sets the
 * clock back to a fixed date (unsync_date) so that time-limited payloads,
 * such as etaHEN beta builds, can start. Run time-sync.elf afterwards.
 *
 * Settings are read from /data/timesyncer/config.ini, which is created
 * with defaults on first run if it does not exist.
 */

#include <arpa/inet.h>
#include <ctype.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef NTP_SERVER
#define NTP_SERVER "time.apple.com, time.cloudflare.com, pool.ntp.org"
#endif

#define CONF_DIR        "/data/timesyncer"
#define CONF_PATH       CONF_DIR "/config.ini"
#define NTP_UNIX_DELTA  2208988800ULL /* 1900-01-01 -> 1970-01-01 */
#define MIN_SANE_UNIX   1577836800LL  /* 2020-01-01: reject garbage */
#define MAX_SERVERS     8
#define RETRY_PAUSE_SEC 3

#ifdef UNSYNC
#define TAG "time-unsync"
#else
#define TAG "time-sync"
#endif

typedef struct {
  char servers[512]; /* comma-separated, tried in order */
  int port;
  int timeout;       /* seconds per server */
  int notify;        /* 0 = off, 1 = errors only, 2 = all */
  int retry_for;     /* keep retrying for this many seconds (sync) */
  char unsync_date[32]; /* "YYYY-MM-DD[ HH:MM[:SS]]" UTC (unsync) */
} config_t;

/* Notification toast (same layout used by ps5-payload-sdk samples). */
typedef struct {
  char pad[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static config_t cfg = {.servers = NTP_SERVER,
                       .port = 123,
                       .timeout = 5,
                       .notify = 2,
                       .retry_for = 30,
                       .unsync_date = "2025-01-01 00:00:00"};

static const char default_ini[] =
    "; time-sync payload settings\n"
    "[timesyncer]\n"
    "\n"
    "; NTP servers to try in order, separated by commas.\n"
    "; Hostnames or IPv4 addresses. Use an IP if DNS is blocked,\n"
    "; e.g. 162.159.200.1 (Cloudflare).\n"
    "servers = " NTP_SERVER "\n"
    "\n"
    "; UDP port of the NTP server.\n"
    "port = 123\n"
    "\n"
    "; Seconds to wait for each server before trying the next.\n"
    "timeout = 5\n"
    "\n"
    "; time-sync keeps retrying for this many seconds if no server\n"
    "; answers, e.g. while the network is still coming up at boot.\n"
    "retry_for = 30\n"
    "\n"
    "; On-screen notifications: all, errors, off\n"
    "notify = all\n"
    "\n"
    "[unsync]\n"
    "; Date time-unsync.elf sets the clock to, in UTC:\n"
    "; YYYY-MM-DD or YYYY-MM-DD HH:MM:SS. Pick a date when your\n"
    "; time-limited payload (e.g. an etaHEN beta) was still valid.\n"
    "unsync_date = 2025-01-01 00:00:00\n";

static void vsay(int is_error, const char *fmt, va_list ap) {
  char buf[256];
  notify_request_t req;

  vsnprintf(buf, sizeof(buf), fmt, ap);
  printf("[" TAG "] %s\n", buf);
  fflush(stdout);

  if (cfg.notify == 0 || (cfg.notify == 1 && !is_error))
    return;
  memset(&req, 0, sizeof(req));
  snprintf(req.message, sizeof(req.message), TAG ": %s", buf);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void say(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay(0, fmt, ap);
  va_end(ap);
}

static void fail(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsay(1, fmt, ap);
  va_end(ap);
}

static char *trim(char *s) {
  char *e;
  while (isspace((unsigned char)*s)) s++;
  e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

static int parse_int(const char *v, int lo, int hi, int def) {
  char *end;
  long n = strtol(v, &end, 10);
  if (end == v || *trim(end) || n < lo || n > hi) return def;
  return (int)n;
}

static void write_default_config(void) {
  FILE *f;
  mkdir(CONF_DIR, 0777);
  if ((f = fopen(CONF_PATH, "w"))) {
    fputs(default_ini, f);
    fclose(f);
    say("created " CONF_PATH);
  }
}

/* Minimal INI reader: "key = value" lines, ';' or '#' comments,
 * section headers ignored. Unknown keys and bad values are skipped. */
static void load_config(void) {
  char line[600];
  FILE *f = fopen(CONF_PATH, "r");

  if (!f) {
    write_default_config();
    return;
  }
  while (fgets(line, sizeof(line), f)) {
    char *k = trim(line), *v, *eq;
    if (!*k || *k == ';' || *k == '#' || *k == '[') continue;
    if (!(eq = strchr(k, '='))) continue;
    *eq = 0;
    k = trim(k);
    v = trim(eq + 1);

    if (!strcasecmp(k, "servers") || !strcasecmp(k, "server")) {
      if (*v) snprintf(cfg.servers, sizeof(cfg.servers), "%s", v);
    } else if (!strcasecmp(k, "port")) {
      cfg.port = parse_int(v, 1, 65535, cfg.port);
    } else if (!strcasecmp(k, "retry_for")) {
      cfg.retry_for = parse_int(v, 0, 600, cfg.retry_for);
    } else if (!strcasecmp(k, "unsync_date")) {
      if (*v) snprintf(cfg.unsync_date, sizeof(cfg.unsync_date), "%s", v);
    } else if (!strcasecmp(k, "timeout")) {
      cfg.timeout = parse_int(v, 1, 60, cfg.timeout);
    } else if (!strcasecmp(k, "notify")) {
      if (!strcasecmp(v, "off") || !strcmp(v, "0")) cfg.notify = 0;
      else if (!strcasecmp(v, "errors")) cfg.notify = 1;
      else if (!strcasecmp(v, "all") || !strcmp(v, "1")) cfg.notify = 2;
    }
  }
  fclose(f);
}

#ifndef UNSYNC
static void put_be32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static uint32_t get_be32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static int64_t usec_of(const struct timeval *tv) {
  return (int64_t)tv->tv_sec * 1000000LL + tv->tv_usec;
}

/* Query one server. On success fills *out and *rtt_us and returns NULL,
 * otherwise returns a short reason. */
static const char *sntp_query(const char *server, struct timeval *out,
                              int64_t *rtt_us) {
  char port[8];
  struct addrinfo hints, *res = NULL;
  struct timeval tmo, t1, t4;
  uint8_t req[48], rsp[48];
  ssize_t n;
  int fd;

  snprintf(port, sizeof(port), "%d", cfg.port);
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(server, port, &hints, &res) != 0 || !res)
    return "cannot resolve (DNS blocked? use an IP)";

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    freeaddrinfo(res);
    return "socket() failed";
  }

  tmo.tv_sec = cfg.timeout;
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
    close(fd);
    freeaddrinfo(res);
    return "sendto() failed";
  }

  n = recvfrom(fd, rsp, sizeof(rsp), 0, NULL, NULL);
  gettimeofday(&t4, NULL);
  close(fd);
  freeaddrinfo(res);

  if (n < (ssize_t)sizeof(rsp))
    return "no reply (timeout)";
  if (memcmp(&rsp[24], &req[40], 8) != 0)
    return "reply does not match request";
  if ((rsp[0] & 7) != 4 || (rsp[0] >> 6) == 3 || rsp[1] == 0)
    return "server unsynchronized or kiss-o'-death";

  uint32_t secs = get_be32(&rsp[40]);
  uint32_t frac = get_be32(&rsp[44]);
  if (secs == 0)
    return "empty transmit timestamp";

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

  if (unix_s < MIN_SANE_UNIX)
    return "implausible time from server";

  out->tv_sec = (time_t)unix_s;
  out->tv_usec = (suseconds_t)us;
  *rtt_us = rtt;
  return NULL;
}
#endif

#ifdef UNSYNC
/* Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant). */
static int64_t days_from_civil(int64_t y, int m, int d) {
  y -= m <= 2;
  int64_t era = (y >= 0 ? y : y - 399) / 400;
  int64_t yoe = y - era * 400;
  int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

/* Parse "YYYY-MM-DD[ HH:MM[:SS]]" (or with 'T') as UTC. */
static int parse_date(const char *s, time_t *out) {
  static const int mdays[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int y, mo, d, h = 0, mi = 0, se = 0, n;
  char sep;

  n = sscanf(s, "%d-%d-%d%c%d:%d:%d", &y, &mo, &d, &sep, &h, &mi, &se);
  if (n != 3 && n < 6) return -1;
  if (n > 3 && sep != ' ' && sep != 'T') return -1;
  if (y < 2000 || y > 2099 || mo < 1 || mo > 12 || d < 1 || d > mdays[mo - 1] ||
      h < 0 || h > 23 || mi < 0 || mi > 59 || se < 0 || se > 59)
    return -1;
  if (mo == 2 && d == 29 && (y % 4 != 0 || (y % 100 == 0 && y % 400 != 0)))
    return -1;
  *out = (time_t)(days_from_civil(y, mo, d) * 86400 + h * 3600 + mi * 60 + se);
  return 0;
}
#endif

static int set_clock(const struct timeval *tv) {
  if (settimeofday(tv, NULL) == 0) return 0;
  struct timespec ts = {.tv_sec = tv->tv_sec, .tv_nsec = tv->tv_usec * 1000};
  return clock_settime(CLOCK_REALTIME, &ts);
}

static void report_clock(const char *suffix) {
  struct timeval now;
  struct tm tmv;
  time_t t;

  gettimeofday(&now, NULL);
  t = now.tv_sec;
  gmtime_r(&t, &tmv);
  say("OK %04d-%02d-%02d %02d:%02d:%02d UTC%s", tmv.tm_year + 1900,
      tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
      suffix);
}

#ifdef UNSYNC
int main(void) {
  struct timeval set = {0};

  load_config();
  if (parse_date(cfg.unsync_date, &set.tv_sec) != 0) {
    fail("FAILED: bad unsync_date \"%s\" (edit " CONF_PATH ")",
         cfg.unsync_date);
    return 1;
  }
  if (set_clock(&set) != 0) {
    fail("FAILED: kernel refused to set the clock (privileges?)");
    return 1;
  }
  report_clock(" - run time-sync.elf afterwards");
  return 0;
}
#else
/* Try each configured server once. Returns the one that answered. */
static const char *try_servers(char *list, size_t len, struct timeval *set,
                               int64_t *rtt, const char **err) {
  char *tok, *save = NULL;
  int tried = 0;

  snprintf(list, len, "%s", cfg.servers);
  *err = "no servers configured";
  for (tok = strtok_r(list, ",", &save); tok && tried < MAX_SERVERS;
       tok = strtok_r(NULL, ",", &save)) {
    char *server = trim(tok);
    if (!*server) continue;
    tried++;
    printf("[" TAG "] querying %s\n", server);
    if (!(*err = sntp_query(server, set, rtt)))
      return server;
    printf("[" TAG "] %s: %s\n", server, *err);
  }
  return NULL;
}

static int sync_main(void) {
  char list[sizeof(cfg.servers)], suffix[160];
  struct timeval set;
  const char *err, *used;
  int64_t rtt = 0;
  int waited = 0;

  load_config();

  /* The clock may have just been moved by time-unsync, so measure the
   * retry window with sleep() counts rather than wall-clock time. */
  while (!(used = try_servers(list, sizeof(list), &set, &rtt, &err)) &&
         waited + RETRY_PAUSE_SEC <= cfg.retry_for) {
    printf("[" TAG "] retrying in %ds\n", RETRY_PAUSE_SEC);
    sleep(RETRY_PAUSE_SEC);
    waited += RETRY_PAUSE_SEC;
  }

  if (!used) {
    fail("FAILED: %s (edit " CONF_PATH ")", err);
    return 1;
  }
  if (set_clock(&set) != 0) {
    fail("FAILED: kernel refused to set the clock (privileges?)");
    return 1;
  }
  snprintf(suffix, sizeof(suffix), " via %s (rtt %lld ms)", used,
           (long long)(rtt / 1000));
  report_clock(suffix);
  return 0;
}

int main(void) {
  return sync_main();
}
#endif
