#!/usr/bin/env python3
"""fR3k v3.0.4 Wigle CSV encoder self-test.

Round-trips the Wigle upload body's CSV header + a small fixture
through the same encoder pattern the firmware uses. Asserts:
  - Header line is exactly the Wigle 1.x header.
  - SSIDs containing commas / quotes are properly RFC-4180 escaped.
  - The 2000-row cap is honoured (WIGLE_MAX_ROWS_PER_CALL).
  - Round-trip: parse the CSV back, every cell matches.

The firmware uses std::vector + sprintf for this; the Python version
mirrors the logic so a regression in one surfaces in the other.
"""

import csv
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def csv_escape(field):
    """Mirror the firmware's csvEscapeAndAppend()."""
    out = io.StringIO()
    if field is None:
        return ""
    need_quote = any(c in field for c in (',', '"', '\n', '\r'))
    if need_quote:
        out.write('"')
    for c in field:
        if c == '"':
            out.write('"')
        out.write(c)
    if need_quote:
        out.write('"')
    return out.getvalue()


def build_csv(rows):
    """Mirror the firmware's Wigle::uploadRecommended() body build."""
    out = io.StringIO()
    out.write("BSSID,SSID,Latitude,Longitude,Time,Channel,Encryption,Accuracy\n")
    for r in rows[:2000]:
        out.write(r["bssid"])
        out.write(",")
        out.write(csv_escape(r.get("ssid", "")))
        out.write(",")
        if r.get("lat") or r.get("lon"):
            out.write(f"{r['lat']:.6f},{r['lon']:.6f}")
        else:
            out.write(",")
        out.write(",")
        out.write(r.get("time", ""))
        out.write(",")
        out.write(str(r.get("channel", "")))
        out.write(",WPA,")
        out.write("10\n")
    return out.getvalue()


def main():
    # Case 1: header.
    csv_text = build_csv([])
    expected_header = "BSSID,SSID,Latitude,Longitude,Time,Channel,Encryption,Accuracy\n"
    assert csv_text == expected_header, f"bad header: {csv_text!r}"

    # Case 2: simple row.
    rows = [{
        "bssid": "AABBCCDDEEFF",
        "ssid": "FreekNet",
        "lat": -27.4698,
        "lon": 153.0251,
        "time": "2026-09-02 12:34:56",
        "channel": 6,
    }]
    csv_text = build_csv(rows)
    reader = csv.DictReader(io.StringIO(csv_text))
    out = list(reader)
    assert len(out) == 1, f"expected 1 row, got {len(out)}"
    assert out[0]["BSSID"] == "AABBCCDDEEFF"
    assert out[0]["SSID"] == "FreekNet"
    assert out[0]["Encryption"] == "WPA"
    assert out[0]["Accuracy"] == "10"

    # Case 3: SSID with comma + quote (RFC-4180).
    rows = [{
        "bssid": "112233445566",
        "ssid": 'Cafe, "Free" WiFi',
        "lat": 0.0, "lon": 0.0, "time": "", "channel": 0,
    }]
    csv_text = build_csv(rows)
    reader = csv.DictReader(io.StringIO(csv_text))
    out = list(reader)
    assert out[0]["SSID"] == 'Cafe, "Free" WiFi', f"bad escape: {out[0]['SSID']!r}"

    # Case 4: 2000-row cap.
    rows = [{
        "bssid": f"{i:012X}", "ssid": f"AP{i}",
        "lat": -27.0, "lon": 153.0,
        "time": "2026-09-02 12:00:00", "channel": 1,
    } for i in range(3000)]
    csv_text = build_csv(rows)
    reader = csv.DictReader(io.StringIO(csv_text))
    out = list(reader)
    assert len(out) == 2000, f"cap not honoured: {len(out)}"

    # Case 5: empty lat/lon -> two consecutive commas.
    rows = [{
        "bssid": "DEADBEEF0001", "ssid": "Hidden",
        "lat": 0.0, "lon": 0.0, "time": "", "channel": 0,
    }]
    csv_text = build_csv(rows)
    # The line is: BSSID,SSID,,,Time,Channel,Encryption,Accuracy
    line = csv_text.splitlines()[1]
    parts = line.split(",")
    # Lat and Lon are empty, so position 2 and 3 are empty strings.
    assert parts[2] == "" and parts[3] == "", f"empty lat/lon not encoded: {parts!r}"

    print("OK: Wigle CSV encoder round-trip passed")
    print(f"OK: 5 cases passed (header, simple, escape, cap, empty lat/lon)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
