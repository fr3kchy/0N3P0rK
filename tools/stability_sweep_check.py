#!/usr/bin/env python3
"""fR3k v3.0.4 stability-sweep gate.

Borrowed from the POSEIDON stability audit (v0.6.0, commit 997287a):
the project ran an 11-bug memory-safety/lifecycle pass over their
whole stack and patched:

  - Bounded copies against attacker-controlled buffer lengths
  - Recursion caps
  - strtol parse loops that spin forever
  - Per-file mkdir/depth-cap on recursive SD ops
  - Identity-keyed dedup for ring buffers
  - Hard deadlines on long-running operations (POSIX pattern)

This gate asserts the fR3k equivalents are in place. Cheap to extend
when more classes are added.
"""

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    errors = []
    wigle_cpp = (ROOT / "src" / "sync" / "wigle.cpp").read_text()
    loot_cpp = (ROOT / "src" / "ui" / "loot_menu.cpp").read_text()
    storage_cpp = (ROOT / "src" / "storage" / "littlefs_ops.cpp").read_text()
    wpasec_cpp = (ROOT / "src" / "sync" / "wpasec.cpp").read_text()

    # 1. Hard deadline on the Wigle upload picker so a hung SD/TLS
    #    path can't wedge the menu past 25s. POSEIDON-style.
    if "DEADLINE_MS" not in wigle_cpp:
        errors.append("Wigle upload picker has no millis() deadline")
    if "millis() - t0 > DEADLINE_MS" not in wigle_cpp:
        errors.append("Wigle deadline not actually checked in the walk loop")

    # 2. WiFiClientSecure timeout + HTTPClient timeout so a hung
    #    TLS handshake can't run forever.
    if wigle_cpp.count("setTimeout(15)") < 2:
        errors.append("Wigle client.setTimeout(15) must be set in BOTH "
                      "uploadRecommended and uploadBssids")
    if wigle_cpp.count("http.setTimeout(15000)") < 2:
        errors.append("Wigle http.setTimeout(15000) must be set in BOTH "
                      "uploadRecommended and uploadBssids")

    # 3. Bounded copies on attacker-controlled filenames. Every
    #    snprintf/sprintf path that touches `name` (a file name from
    #    the SD card) must pass a bound.
    risk = ['snprintf(path, sizeof(path), "%s/%s"',
            'snprintf(path, sizeof(path), "/0N3P0rK/handshakes/%s"']
    for needle in risk:
        if needle not in loot_cpp:
            errors.append(f"loot_menu must use bounded snprintf for: {needle!r}")

    # 4. Wigle BSSID set has a hard upper bound on the per-call row
    #    count to prevent OOM.
    if "WIGLE_MAX_ROWS_PER_CALL" not in wigle_cpp:
        errors.append("Wigle must cap rows per call (WIGLE_MAX_ROWS_PER_CALL)")

    # 5. No unbounded string concatenation in the Wigle body builder
    #    (the String + String + ... pattern is the heap-pressure
    #    footgun POSEIDON's v0.6.0 audit specifically called out).
    body = wigle_cpp[wigle_cpp.find("buildWigleBody"):wigle_cpp.find("uploadRecommended")]
    if "body += row" in body and "body.reserve" not in body:
        errors.append("buildWigleBody must reserve() before appending rows")
    if "body.reserve(rows.size() * 96 + 256)" not in wigle_cpp:
        errors.append("buildWigleBody must size its reserve for the row count")

    # 6. WPA-sec cache loading is bounded.
    if "WPASEC_MAX_CACHE" not in wpasec_cpp:
        errors.append("WPA-sec cache must be bounded (WPASEC_MAX_CACHE)")
    if "uploadedCache.size() < WPASEC_MAX_CACHE" not in wpasec_cpp:
        errors.append("loadUploadedList must check the cap inside the loop")

    # 7. forEachInDir is non-recursive (POSEIDON's recursive-delete
    #    depth cap finding - our equivalent is that we don't recurse
    #    at all in forEachInDir).
    fe = storage_cpp[storage_cpp.find("forEachInDir("):storage_cpp.find("forEachInDir(") + 600]
    if "openNextFile" not in fe:
        errors.append("forEachInDir must use openNextFile (non-recursive)")
    if "recurse" in fe.lower() or "recursive" in fe.lower():
        errors.append("forEachInDir must NOT be recursive")

    if errors:
        print("FAIL: fR3k v3.0.4 stability sweep")
        for e in errors:
            print(f"  - {e}")
        return 1
    print("OK: fR3k v3.0.4 stability sweep (POSEIDON-pattern gates)")
    print("OK: 7 classes gated (deadline, TLS timeout, HTTP timeout, "
          "bounded copy, row cap, heap reserve, cache bound, "
          "non-recursive walk)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
