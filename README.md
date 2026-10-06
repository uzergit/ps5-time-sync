# ps5-time-sync

`time-sync.elf` is a small payload for a **jailbroken** PS5 that sets the
console clock from an ordinary internet time server (NTP, UDP port 123).

It's meant for consoles kept away from Sony's servers (blocked at DNS), whose
clock drifts because it never syncs. It does not contact Sony.

## What it does

1. Picks a time server: the first line of `/data/time-sync.conf` if that file
   exists, otherwise `time.apple.com`.
2. Sends one NTP request and checks the reply.
3. Sets the system clock with `settimeofday()` (UTC).
4. Shows a notification on the PS5: `time-sync: OK <date> UTC` or
   `time-sync: FAILED: <reason>`.

The console's own time zone setting still decides the local time shown.

## Getting the .elf (no PC needed)

GitHub Actions builds it automatically on every push to `main`. You can also
start a build by hand: **Actions → build-elf → Run workflow**.

To download it:

1. Open the repo's **Actions** tab.
2. Tap the latest successful **build-elf** run.
3. Under **Artifacts**, download **time-sync**. It's a zip containing
   `time-sync.elf`.

## Sending it to the PS5

Run your ELF loader on the PS5 (for example etaHEN or elfldr), then send the
file to the loader's port, which is usually **9021** (sometimes 9020):

```sh
nc -w1 <PS5-IP> 9021 < time-sync.elf
```

On a phone, a payload sender app works too: point it at the PS5's IP and the
loader port, then pick `time-sync.elf`.

## Optional: choosing the time server

If DNS for `time.apple.com` is blocked on your network (for example by the
same DNS setup that blocks Sony), create a text file at
`/data/time-sync.conf` on the PS5. Its first line is the server to use, as a
hostname or an IPv4 address, for example:

```
162.159.200.1
```

Another option is to build with a different default:
`make NTP_SERVER=192.168.1.1`.

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
