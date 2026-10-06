# ps5-time-sync

`time-sync.elf` is a small payload for a **jailbroken** PS5 that sets the
console clock from an ordinary internet time server (NTP, UDP port 123).

It's meant for consoles kept away from Sony's servers (blocked at DNS), whose
clock drifts because it never syncs. It does not contact Sony.

There's also a second payload, **`time-unsync.elf`**. It sets the clock *back*
to a fixed date, so that payloads which expire after a certain date (such as
the etaHEN beta builds that some firmwares need) can still start. See
[Expired etaHEN beta / autoload lists](#expired-etahen-beta--autoload-lists).

## What it does

1. Reads its settings from `/data/timesyncer/config.ini`. If that file doesn't
   exist, it creates it with default settings.
2. Tries each NTP server from the config in order, until one answers. If none
   answers, it keeps retrying for up to `retry_for` seconds (30 by default),
   for example while the network is still coming up at boot.
3. Sets the system clock with `settimeofday()` (UTC).
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

Some firmwares are only supported by a **beta** build of etaHEN, and beta
builds stop working after a certain date. To keep using one, the clock has
to be set back before etaHEN starts, and corrected again afterwards.

**Run the payloads in this exact order, for example in your autoload list:**

1. **`time-unsync.elf`**: sets the clock back to `unsync_date`.
2. *Wait a few seconds* (see below).
3. **The etaHEN beta**: starts because the console thinks it's still before
   the expiry date.
4. *Wait a few seconds.*
5. **`time-sync.elf`**: sets the clock back to the real time from the
   internet.

**Add a delay of about 3–5 seconds between the payloads.** Each one takes a
couple of seconds to actually change the clock. If the next payload starts
too early, the etaHEN beta may still see the real date, or etaHEN may still
be starting up when time-sync runs. Many autoloaders support a wait or sleep
line for this; check your autoloader's documentation for the exact syntax
(for example, some use a line like `!3000` to wait 3000 ms). If yours has
none, start the payloads by hand in this order and wait for each
notification before sending the next.

Before you start, set `unsync_date` in `config.ini` to a date when your
etaHEN beta was still valid, for example the day it was released.

Things to know:

- **Always run `time-sync.elf` last.** The clock change made by
  `time-unsync.elf` survives a reboot too. Until time-sync runs, the console
  stays on the old date.
- **Wait for the network.** In an autoload list, time-sync can start before
  the network is up. It keeps retrying for `retry_for` seconds (30 by
  default). If it still fails in your setup, raise `retry_for`.
- If time-sync can't reach any server at all, the clock stays on
  `unsync_date`. Fix the network or `servers` setting, then run it again.

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
; time-sync keeps retrying for this many seconds if no server answers.
retry_for = 30
; On-screen notifications: all, errors, off
notify = all
; Check GitHub (at most once a day) for a newer release: on, off
update_check = on

[unsync]
; Date time-unsync.elf sets the clock to, in UTC:
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
- It doesn't download or install anything: you update by grabbing the new
  `.elf` from the Releases page.
- Turn it off with `update_check = off` in `config.ini`.

Builds made outside a release (version `dev`) never check.

## Building locally

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make                  # version "dev", update check disabled
make VERSION=v1.2     # what the release workflow does
```

This needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and
clang/lld 18.

## Does the time stick after a reboot?

Yes. On FreeBSD-based kernels, `settimeofday()` also writes the hardware
clock (RTC). This has been confirmed on a real PS5: after a successful sync,
the time stayed correct through a reboot. You only need to run the payload
again when the clock has drifted.

The same goes for `time-unsync.elf`: its old date also survives a reboot,
which is why `time-sync.elf` must always run after it.
