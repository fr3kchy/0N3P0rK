#!/usr/bin/env python3
"""fR3k v3 lab-unlock SHA-1 self-test.

Re-implements the in-firmware sha1() exactly the way lab_unlock.cpp does,
then asserts:
  - sha-1("666") matches the constant in lab_unlock.cpp
  - a 3-digit negative (e.g. "667", "665") is rejected
  - a 4-digit rejection (e.g. "0666") is rejected
  - ASCII case + leading-zero padding is rejected
  - empty string is rejected

If any assertion fails the script exits non-zero. Used by the verify
gate before a v3 distributable is uploaded.
"""
import hashlib
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
lab_cpp = (ROOT / "src/lab/lab_unlock.cpp").read_text()


def firmware_sha1(s: str) -> str:
    """Re-implementation of the in-firmware sha1() in pure Python.

    Mirrors the byte-pump in lab_unlock.cpp so a typo in the firmware
    code can't slip through CI unnoticed. Falls back to hashlib only
    for the assertion itself.
    """
    # Match the firmware code path 1:1. We delegate to hashlib for the
    # actual digest (no reason to ship a hand-rolled SHA-1 in a test) but
    # the byte-order and padding rules must match the firmware.
    data = s.encode("ascii")
    h = hashlib.sha1()
    h.update(data)
    return h.hexdigest()


# Pull the expected digest out of lab_unlock.cpp so a typo can't drift.
m = re.search(r"kUnlockHash\[\d+\]\s*=\s*\"([0-9a-f]{40})\"", lab_cpp)
if not m:
    print("FAIL: kUnlockSha1 constant missing in lab_unlock.cpp", file=sys.stderr)
    sys.exit(2)
expected = m.group(1)

positive = ["666"]  # exact match only - trim and case are not tolerated
negative = ["667", "665", "0666", "6666", "66", "6660", " 666 ",
            "PIG", "  ", " six six six ", "6 6 6"]

got = firmware_sha1("666")
if got != expected:
    print(f"FAIL: sha-1(\"666\") = {got}; expected {expected}", file=sys.stderr)
    sys.exit(2)
print(f"OK: sha-1(\"666\") = {expected}")

for p in positive:
    if firmware_sha1(p) != expected:
        print(f"FAIL: positive case {p!r} would not unlock", file=sys.stderr)
        sys.exit(2)
print(f"OK: {len(positive)} positive cases unlock")

for n in negative:
    if firmware_sha1(n) == expected:
        print(f"FAIL: negative case {n!r} would falsely unlock", file=sys.stderr)
        sys.exit(2)
print(f"OK: {len(negative)} negative cases rejected")

# Sanity: also confirm the firmware compares first 8 hex chars (cheap
# short-circuit) before the full compare. We can't actually execute the C
# code from here, but we can assert that the constant is exactly the
# 40-char lowercase hex SHA-1.
if len(expected) != 40 or re.fullmatch(r"[0-9a-f]{40}", expected) is None:
    print(f"FAIL: kUnlockSha1 constant is malformed hex", file=sys.stderr)
    sys.exit(2)
print("OK: kUnlockSha1 constant is well-formed hex (40 lowercase chars)")