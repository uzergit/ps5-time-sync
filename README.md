# ps5-time-sync

`time-sync.elf` is a small payload for a **jailbroken** PS5 that sets the
console clock from an ordinary internet time server (NTP, UDP port 123).

It's meant for consoles kept away from Sony's servers (blocked at DNS), whose
clock drifts because it never syncs. It does not contact Sony.

There's also a second payload, **`time-unsync.elf`**. It sets the clock *back*
so that etaHEN beta builds, which refuse to start after their expiry date,
still run. It finds your etaHEN payload, reads the expiry date built into it
and sets the clock to just before that date. See
[Expired etaHEN beta / autoload lists](#expired-etahen-beta--autoload-lists).

## What it does

1. Reads its settings from `/data/timesyncer/config.ini`. If that file doesn't
   exist, it creates it with default settings.
2. Tries each NTP server from the config in order, until one answers. If none
   answers, it keeps retrying for up to `retry_for` seconds (30 by default),
   for example while the network is still coming up at boot.
3. Measures how far off the console clock is, using several samples (see
   [How accurate is it?](#how-accurate-is-it)), and corrects it with
   `settimeofday()` (UTC).
4. Shows a notification on the PS5: `time-sync: OK <date> UTC via <server>` or
   `time-sync: FAILED: <reason>`.

The console's own time zone setting still decides the local time shown.

**It takes a couple of seconds.** From pressing "run" (or the payload being
started by an autoloader) to the clock being set and the notification
appearing, expect roughly 2–3 seconds. Most of that is the loader starting the
payload and the PS5 showing the notification, not the time lookup itself. If a
server doesn't answer, time-sync waits `timeout` seconds (2 by default) before
trying the next one.

## Getting the .elf (no PC needed)

The easiest way: open the repo's **Releases** page and download
`time-sync.elf` (and `time-unsync.elf` if you need it) from the newest
release.

GitHub Actions also builds it automatically on every push to `main`. You can
start a build by hand too: **Actions → build-elf → Run workflow**. To
download one of those builds:

1. Open the repo's **Actions** tab.
2. Tap the latest successful **build-elf** run.
3. Under **Artifacts**, download **time-sync**. It's a zip containing
   `time-sync.elf`, `time-unsync.elf` and an example `config.ini`.

To publish a new release from your phone, go to **Actions → build-elf → Run
workflow**, type a version such as `v1.1` in the "Release tag" box and run it.
The workflow builds both payloads and creates a Release with `time-sync.elf`,
`time-unsync.elf` and `config.ini` attached. Pushing a tag that starts with `v` does the same.

## Sending it to the PS5

Run your ELF loader on the PS5 (for example etaHEN or elfldr), then send the
file to the loader's port, which is usually **9021** (sometimes 9020):

```sh
nc -w1 <PS5-IP> 9021 < time-sync.elf
```

On a phone, a payload sender app works too: point it at the PS5's IP and the
loader port, then pick `time-sync.elf`.

## Expired etaHEN beta / autoload lists

Some firmwares are only supported by a **beta** (test) build of etaHEN, and
beta builds stop working after a date built into them. When that date has
passed, etaHEN shows *"This etaHEN Beta version expired on …"* and doesn't
start. Full releases (like 2.5B from GitHub) never expire.

The check only looks at the console clock when etaHEN starts, so the fix is
to set the clock back before etaHEN starts and put it right again afterwards.

**Run the payloads in this order, for example in your autoload list:**

1. **`time-unsync.elf`**: moves the clock to before the beta's expiry date.
2. *Wait about 3 seconds.*
3. **The etaHEN beta**: starts, because the console thinks it's before the
   expiry date.
4. **`time-sync.elf`**: waits until etaHEN has started (up to `etahen_wait`
   seconds, 60 by default), then sets the real time from the internet.

For the common autoloaders ([ps5-y2jb-autoloader] and
[ps5-unified-autoloader]), `autoload.txt` would look like this. `!3000` waits
3000 ms:

```
time-unsync.elf
!3000
etaHEN.elf
time-sync.elf
```

If your loader has no wait command, send the payloads by hand in this order
and wait for each notification before sending the next.

[ps5-y2jb-autoloader]: https://github.com/itsPLK/ps5-y2jb-autoloader
[ps5-unified-autoloader]: https://github.com/itsPLK/ps5-unified-autoloader

### How time-unsync picks the date

With the default `unsync_mode = auto`, time-unsync looks for etaHEN payload
files (`.elf` or `.bin`, any file name) in these places, in this order:

1. Anything listed in `etahen_path` in `config.ini`.
2. Autoloader folders: `ps5_autoloader` and `ps5_autoloader_<TITLE_ID>` on
   USB drives (`/mnt/usb0`–`/mnt/usb7`), then in `/data`.
3. Payload Manager folders: `pldmgr` and `pldmgr/payloads` on USB drives,
   then `/data/pldmgr` and `/data/pldmgr/payloads`.
4. `/data/etaHEN`, the top level of `/data`, and the top level of each USB
   drive.

It reads each file without running it. etaHEN payloads carry their real code
LZMA-compressed inside; time-unsync unpacks the first part and looks for the
built-in expiry check, which contains the date. Then:

| What it finds | What it does |
|---|---|
| An etaHEN **beta** | Sets the clock to **2 days before** its expiry date (the margin covers time zones). If several betas are found, it uses the earliest date. If the clock is already earlier than that, it leaves it alone. |
| Only etaHEN **full releases** | Leaves the clock alone, because they never expire. |
| No etaHEN at all | Falls back to `unsync_date` (2025-01-01 by default). |

The notification says which of these happened, for example
`time-unsync: clock set to 2026-09-29 (etaHEN beta expires 2026-10-01)`.

Tested against every etaHEN build available to us: the public 1.7B–2.5B
releases (no expiry found), a 2.4B file that GitHub later replaced with one
expiring 2025-12-25 (found), and the 2.6b test build for firmware up to 12.70
(found: 2026-10-01). A future etaHEN that stores its date differently
wouldn't be recognised; it would be treated as having no expiry, or as no
etaHEN at all (`unsync_date`). If that happens, set `unsync_mode = fixed`
and choose `unsync_date` yourself.

### Things to know

- **Always run `time-sync.elf` last.** The clock change made by
  `time-unsync.elf` survives a reboot too. Until time-sync runs, the console
  stays on the old date.
- **time-sync only waits after time-unsync.** time-unsync leaves a marker
  file (`/data/timesyncer/unsynced`). time-sync waits for etaHEN only when
  the marker is there, and deletes it after a successful sync. etaHEN counts
  as started once its service socket (`/system_tmp/etaHEN_crit_service`)
  exists. If it never appears, time-sync syncs anyway after `etahen_wait`
  seconds.
- **Wait for the network.** In an autoload list, time-sync can start before
  the network is up. It keeps retrying for `retry_for` seconds (30 by
  default). If it still fails in your setup, raise `retry_for`.
- If time-sync can't reach any server at all, the clock stays set back. Fix
  the network or `servers` setting, then run it again.

## Configuration: `/data/timesyncer/config.ini`

The first run creates this file. Edit it over FTP, or upload your own copy
(an example is in this repo and attached to each release):

```ini
[timesyncer]
; NTP servers to try in order, separated by commas (hostnames or IPv4).
servers = time.apple.com, time.cloudflare.com, pool.ntp.org
; UDP port of the NTP server.
port = 123
; Seconds to wait for each server before trying the next.
timeout = 2
; Measurements per server (1-8); the one with the least network delay is used
samples = 4
; time-sync keeps retrying for this many seconds if no server answers.
retry_for = 30
; On-screen notifications: all, errors, off
notify = all
; Check GitHub (at most once a day) for a newer release: on, off
update_check = on
; After time-unsync, wait up to this many seconds for etaHEN to start
etahen_wait = 60

[unsync]
; auto: read the expiry date from your etaHEN payload; fixed: use unsync_date
unsync_mode = auto
; Extra files or folders to check for etaHEN first, separated by commas
etahen_path =
; Date for fixed mode, or when auto finds no etaHEN. UTC:
; YYYY-MM-DD or YYYY-MM-DD HH:MM:SS
unsync_date = 2025-01-01 00:00:00
```

If DNS is blocked on your network (for example by the same DNS setup that
blocks Sony), hostnames won't resolve. Put an IP address first instead, for
example `servers = 162.159.200.1` (Cloudflare's time server).

Lines starting with `;` or `#` are comments. Unknown keys and invalid values
are ignored, and the defaults are used for them.

You can also change the built-in default servers at build time:
`make NTP_SERVER="192.168.1.1"`.

## How accurate is it?

time-sync uses the standard NTP calculation. Every reply carries four
timestamps: when the PS5 sent the request, when the server received it,
when the server answered, and when the PS5 got the answer. From those it
works out the clock offset with the server's own processing time cancelled
out and the network delay split evenly between both directions.

The remaining error comes from the network being faster in one direction
than the other, and it can be at most half the round-trip delay. That delay
changes from packet to packet, so time-sync takes `samples` measurements
(4 by default, 0.25 s apart) and uses the one with the shortest delay. It
then adds the measured offset to the console clock at the moment of setting
it, so the time spent measuring doesn't count either.

The log (`/data/timesyncer/time-sync.log`) shows every sample and the
result, for example:

```
  sample 1: offset +1532.481 ms, delay 48.210 ms
  sample 2: offset +1510.902 ms, delay 22.731 ms
corrected by +1510.902 ms (best of 4 samples, delay 22 ms, accurate to about +-11 ms)
```

On a typical home connection that means the clock ends up within a few
milliseconds of the server. Absolute zero isn't possible over a network,
and the PS5's hardware clock probably only stores whole seconds, so after a
reboot the sub-second part may be lost until the next sync.

## Update notifications

After a successful sync, `time-sync.elf` asks GitHub for the newest release
of this project. If there's a newer version than the one you're running,
the PS5 shows a notification like:

```
time-sync: update v1.3 available (you have v1.2) - github.com/uzergit/ps5-time-sync/releases
```

- It checks **at most once a day** (the time of the last check is saved in
  `/data/timesyncer/last_update_check`). If the check fails, for example
  because GitHub is unreachable, it tries again on the next run.
- It only runs **after** the clock has been set, because HTTPS needs a
  correct clock. `time-unsync.elf` never checks.
- It uses the PS5's own HTTPS libraries and only talks to `api.github.com`.
  If those libraries can't be loaded, the check is skipped and the time sync
  still works.
- Some firmwares (seen on 12.40) don't recognise the authority behind
  GitHub's certificate. In that case the check retries without that
  certificate check. That's acceptable here because it only reads a version
  number; a forged reply could at worst show a wrong "update available"
  notification.
- It doesn't download or install anything: you update by grabbing the new
  `.elf` from the Releases page.
- Turn it off with `update_check = off` in `config.ini`.

Builds made outside a release (version `dev`) never check.

## Logs

Both payloads write what they did to `/data/timesyncer/time-sync.log` and
`/data/timesyncer/time-unsync.log` (replaced on every run): servers tried,
samples, the etaHEN files found, and each step of the update check with its
error code. Fetch them over FTP when something doesn't work, for example:

```sh
curl ftp://<PS5-IP>:1337/data/timesyncer/time-sync.log
```

## Building locally

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make                  # version "dev", update check disabled
make VERSION=v1.2     # what the release workflow does
```

This needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and
clang/lld 18. `lzma/` holds the LZMA decoder from the
[LZMA SDK](https://www.7-zip.org/sdk.html) by Igor Pavlov (public domain),
which time-unsync uses to read etaHEN payloads.

## Does the time stick after a reboot?

Yes. On FreeBSD-based kernels, `settimeofday()` also writes the hardware
clock (RTC). This has been confirmed on a real PS5: after a successful sync,
the time stayed correct through a reboot. You only need to run the payload
again when the clock has drifted.

The same goes for `time-unsync.elf`: its old date also survives a reboot,
which is why `time-sync.elf` must always run after it.

## Credits

Made by [uzer](https://github.com/uzergit), with help from [Claude](https://claude.ai) by Anthropic.
