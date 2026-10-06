# ps5-time-sync

`time-sync.elf` is a small payload for a **jailbroken** PS5 that sets the
console clock from an ordinary internet time server (NTP, UDP port 123).

It's meant for consoles kept away from Sony's servers (blocked at DNS), whose
clock drifts because it never syncs. It does not contact Sony.

## What it does

1. Reads its settings from `/data/timesyncer/config.ini`. If that file doesn't
   exist, it creates it with default settings.
2. Tries each NTP server from the config in order, until one answers.
3. Sets the system clock with `settimeofday()` (UTC).
4. Shows a notification on the PS5: `time-sync: OK <date> UTC via <server>` or
   `time-sync: FAILED: <reason>`.

The console's own time zone setting still decides the local time shown.

## Getting the .elf (no PC needed)

The easiest way: open the repo's **Releases** page and download
`time-sync.elf` from the newest release.

GitHub Actions also builds it automatically on every push to `main`. You can
start a build by hand too: **Actions → build-elf → Run workflow**. To
download one of those builds:

1. Open the repo's **Actions** tab.
2. Tap the latest successful **build-elf** run.
3. Under **Artifacts**, download **time-sync**. It's a zip containing
   `time-sync.elf` and an example `config.ini`.

To publish a new release, push a tag that starts with `v` (for example
`v1.1`). The workflow builds the payload and creates a Release with
`time-sync.elf` and `config.ini` attached.

## Sending it to the PS5

Run your ELF loader on the PS5 (for example etaHEN or elfldr), then send the
file to the loader's port, which is usually **9021** (sometimes 9020):

```sh
nc -w1 <PS5-IP> 9021 < time-sync.elf
```

On a phone, a payload sender app works too: point it at the PS5's IP and the
loader port, then pick `time-sync.elf`.

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
timeout = 5
; On-screen notifications: all, errors, off
notify = all
```

If DNS is blocked on your network (for example by the same DNS setup that
blocks Sony), hostnames won't resolve. Put an IP address first instead, for
example `servers = 162.159.200.1` (Cloudflare's time server).

Lines starting with `;` or `#` are comments. Unknown keys and invalid values
are ignored, and the defaults are used for them.

You can also change the built-in default servers at build time:
`make NTP_SERVER="192.168.1.1"`.

## Building locally

```sh
export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make
```

This needs [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) and
clang/lld 18.

## Caveat (please read)

On FreeBSD-based kernels, `settimeofday()` also writes the hardware clock
(RTC). **That hasn't been verified on the PS5's kernel.** After a successful
run, reboot the console and check whether the time stuck. If it didn't, just
run the payload again after each boot.
