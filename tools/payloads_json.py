#!/usr/bin/env python3
"""Write payloads.json: a PS5 Payload Manager repository for one release.

Payload Manager (github.com/itsPLK/ps5-payload-manager) offers an "Update"
button when the repository lists a file whose name differs from the
installed one only by its version suffix (time-sync.elf vs
time-sync_v1.4.elf), so each entry gets a versioned filename.

usage: payloads_json.py TAG OWNER/REPO OUTPUT
       (reads time-sync.elf and time-unsync.elf from the current folder)
"""
import hashlib
import json
import sys

PAYLOADS = [
    ("time-sync", "time-sync.elf",
     "Sets the PS5 clock from an internet time server (NTP), accurate to a "
     "few milliseconds. Settings: /data/timesyncer/config.ini"),
    ("time-unsync", "time-unsync.elf",
     "Sets the clock back to just before your etaHEN beta's expiry date so "
     "it still starts. Run time-unsync, then etaHEN, then time-sync."),
]


def main():
    tag, repo, output = sys.argv[1:4]
    items = []
    for name, asset, description in PAYLOADS:
        with open(asset, "rb") as f:
            checksum = hashlib.sha256(f.read()).hexdigest()
        items.append({
            "name": name,
            "filename": f"{name}_{tag}.elf",
            "url": f"https://github.com/{repo}/releases/download/{tag}/{asset}",
            "description": description,
            "version": tag,
            "category": "Utilities",
            "checksum": checksum,
        })
    # Payload Manager needs "name" before "payloads".
    repository = {"name": "ps5-time-sync", "payloads": items}
    with open(output, "w") as f:
        json.dump(repository, f, indent=2)
        f.write("\n")


if __name__ == "__main__":
    main()
