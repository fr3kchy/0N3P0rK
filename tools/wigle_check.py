#!/usr/bin/env python3
"""fR3k v3.0.4 Wigle v1.6 CSV encoder self-test.

The v3.0.4 implementation conforms to the WigleWireless CSV format
v1.6 (https://api.wigle.net/csvFormat.html). Required:
  - A pre-header line beginning with "WigleWifi-1.6," listing app
    release, model, device, brand, star=Sol, body=4, subBody=0.
  - A 14-column header in this exact order:
      MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,
      CurrentLatitude,CurrentLongitude,AltitudeMeters,
      AccuracyMeters,RCOIs,MfgrId,Type
  - Each row is 14 fields; the BSSID is the colon form (aa:bb:cc:dd:ee:ff),
    the AuthMode is an Android-style capabilities string
    ([WPA2-PSK-CCMP][ESS]), the FirstSeen is YYYY-MM-DD hh:mm:ss UTC,
    RSSI is signed, AccuracyMeters is decimal metres (or empty),
    Type is always WIFI for wardriving.

This test mirrors the firmware's build_csv() and asserts the format
matches the spec by reading it back with csv.DictReader.
"""

import csv
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PRE_HEADER_PREFIX = "WigleWifi-1.6,"
HEADER = ("MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,"
          "CurrentLatitude,CurrentLongitude,AltitudeMeters,"
          "AccuracyMeters,RCOIs,MfgrId,Type")


def csv_escape(field):
    """Mirror the firmware's csvEscapeAndAppend()."""
    if field is None:
        return ""
    need_quote = any(c in field for c in (',', '"', '\n', '\r'))
    out = []
    if need_quote:
        out.append('"')
    for c in field:
        if c == '"':
            out.append('"')
        out.append(c)
    if need_quote:
        out.append('"')
    return ''.join(out)


def channel_to_frequency(ch):
    """Mirror the firmware's channelToFrequency(). 0 = unknown."""
    if 1 <= ch <= 13:
        return 2412 + (ch - 1) * 5
    if ch == 14:
        return 2484
    table = {36: 5180, 40: 5200, 44: 5220, 48: 5240, 52: 5260, 56: 5280,
             60: 5300, 64: 5320, 100: 5500, 104: 5520, 108: 5540,
             112: 5560, 116: 5580, 120: 5600, 124: 5620, 128: 5640,
             132: 5660, 136: 5680, 140: 5700, 149: 5745, 153: 5765,
             157: 5785, 161: 5805, 165: 5825}
    return table.get(ch, 0)


def bssid_hex_to_colon(hex12):
    """Mirror the firmware's in-place BSSID reformat."""
    h = hex12.upper()
    return ":".join(h[i:i + 2] for i in range(0, 12, 2))


def build_csv(rows, app_release="fR3k 3.0.4-fr3k-lab"):
    """Mirror the firmware's Wigle::uploadRecommended() body build."""
    out = io.StringIO()
    out.write(f"{PRE_HEADER_PREFIX}appRelease={app_release},"
              "model=M5Cardputer-ADV,release=espressif32@6.12.0,"
              "device=fR3k,display=ST7789v2-1.14,board=esp32s3,"
              "brand=Blackwave,star=Sol,body=4,subBody=0\n")
    out.write(HEADER + "\n")
    for r in rows[:2000]:
        out.write(bssid_hex_to_colon(r["bssid_hex"]))
        out.write(",")
        out.write(csv_escape(r.get("ssid", "")))
        out.write(",")
        out.write(r.get("authmode", "[WPA2-PSK-CCMP][ESS]"))
        out.write(",")
        out.write(r.get("first_seen", ""))
        out.write(",")
        if r.get("channel"):
            out.write(str(r["channel"]))
        out.write(",")
        freq = channel_to_frequency(r.get("channel", 0))
        if freq:
            out.write(str(freq))
        out.write(",")
        if r.get("rssi"):
            out.write(str(r["rssi"]))
        out.write(",")
        if r.get("lat") or r.get("lon"):
            out.write(f"{r['lat']:.6f},{r['lon']:.6f}")
        else:
            out.write(",")
        out.write(",")
        if r.get("alt"):
            out.write(f"{r['alt']:.0f}")
        out.write(",")
        if r.get("acc"):
            out.write(f"{r['acc']:.2f}")
        out.write(",")
        out.write(r.get("rcois", ""))
        out.write(",")
        out.write(r.get("mfgrid", ""))
        out.write(",WIFI\n")
    return out.getvalue()


def split_preheader_and_body(text):
    """Return (preheader_line, rest). Both exclude the final \\n."""
    lines = text.splitlines()
    pre = ""
    start = 0
    if lines and lines[0].startswith(PRE_HEADER_PREFIX):
        pre = lines[0]
        start = 1
    return pre, "\n".join(lines[start:])


def main():
    errors = []

    # Case 1: pre-header presence + required fields.
    csv_text = build_csv([])
    pre, body = split_preheader_and_body(csv_text)
    if not pre.startswith(PRE_HEADER_PREFIX):
        errors.append("pre-header missing or wrong prefix")
    for required in ("appRelease=", "model=M5Cardputer-ADV", "device=fR3k",
                     "brand=Blackwave", "star=Sol", "body=4", "subBody=0"):
        if required not in pre:
            errors.append(f"pre-header missing field: {required}")

    # Case 2: header is exact spec, 14 columns.
    header_line = body.splitlines()[0]
    if header_line != HEADER:
        errors.append(f"header mismatch:\n  got: {header_line!r}\n  want: {HEADER!r}")
    if len(HEADER.split(",")) != 14:
        errors.append("header should have 14 columns")

    # Case 3: simple row round-trips through csv.DictReader.
    rows = [{
        "bssid_hex": "AABBCCDDEEFF",
        "ssid": "FreekNet",
        "first_seen": "2026-09-02 12:34:56",
        "channel": 6,
        "rssi": -42,
        "lat": -27.4698, "lon": 153.0251,
        "alt": 25.0, "acc": 3.2,
    }]
    csv_text = build_csv(rows)
    _, body = split_preheader_and_body(csv_text)
    parsed = list(csv.DictReader(io.StringIO(body)))
    if len(parsed) != 1:
        errors.append(f"expected 1 row, got {len(parsed)}")
    else:
        r = parsed[0]
        if r["MAC"] != "AA:BB:CC:DD:EE:FF":
            errors.append(f"BSSID wrong: {r['MAC']!r}")
        if r["SSID"] != "FreekNet":
            errors.append(f"SSID wrong: {r['SSID']!r}")
        if r["AuthMode"] != "[WPA2-PSK-CCMP][ESS]":
            errors.append(f"AuthMode wrong: {r['AuthMode']!r}")
        if r["Channel"] != "6":
            errors.append(f"Channel wrong: {r['Channel']!r}")
        if r["Frequency"] != "2437":
            errors.append(f"Frequency wrong (ch 6 -> 2437): {r['Frequency']!r}")
        if r["RSSI"] != "-42":
            errors.append(f"RSSI wrong: {r['RSSI']!r}")
        if r["CurrentLatitude"] != "-27.469800" or r["CurrentLongitude"] != "153.025100":
            errors.append(f"lat/lon wrong: {r['CurrentLatitude']!r} {r['CurrentLongitude']!r}")
        if r["AltitudeMeters"] != "25":
            errors.append(f"altitude wrong: {r['AltitudeMeters']!r}")
        if r["AccuracyMeters"] != "3.20":
            errors.append(f"accuracy wrong: {r['AccuracyMeters']!r}")
        if r["Type"] != "WIFI":
            errors.append(f"type wrong: {r['Type']!r}")

    # Case 4: SSID with comma + quote (RFC-4180).
    rows = [{
        "bssid_hex": "112233445566",
        "ssid": 'Cafe, "Free" WiFi',
        "first_seen": "2026-09-02 12:00:00",
    }]
    csv_text = build_csv(rows)
    _, body = split_preheader_and_body(csv_text)
    parsed = list(csv.DictReader(io.StringIO(body)))
    if parsed[0]["SSID"] != 'Cafe, "Free" WiFi':
        errors.append(f"SSID escape wrong: {parsed[0]['SSID']!r}")

    # Case 5: 5 GHz channel -> correct frequency.
    rows = [{
        "bssid_hex": "AABBCCDDEEFF",
        "ssid": "x",
        "first_seen": "2026-09-02 12:00:00",
        "channel": 149,
    }]
    csv_text = build_csv(rows)
    _, body = split_preheader_and_body(csv_text)
    parsed = list(csv.DictReader(io.StringIO(body)))
    if parsed[0]["Frequency"] != "5745":
        errors.append(f"5 GHz ch 149 should be 5745 MHz, got {parsed[0]['Frequency']!r}")

    # Case 6: 2000-row cap.
    rows = [{
        "bssid_hex": f"{i:012X}", "ssid": f"AP{i}",
        "first_seen": "2026-09-02 12:00:00",
        "channel": 1, "rssi": -50,
        "lat": -27.0, "lon": 153.0,
    } for i in range(3000)]
    csv_text = build_csv(rows)
    _, body = split_preheader_and_body(csv_text)
    parsed = list(csv.DictReader(io.StringIO(body)))
    if len(parsed) != 2000:
        errors.append(f"2000-row cap broken: {len(parsed)}")

    # Case 7: empty GPS lat/lon leaves two adjacent commas.
    rows = [{
        "bssid_hex": "DEADBEEF0001", "ssid": "Hidden",
        "first_seen": "2026-09-02 12:00:00",
        "channel": 11,
    }]
    csv_text = build_csv(rows)
    _, body = split_preheader_and_body(csv_text)
    parsed = list(csv.DictReader(io.StringIO(body)))
    if parsed[0]["CurrentLatitude"] != "" or parsed[0]["CurrentLongitude"] != "":
        errors.append(f"empty lat/lon not blank: {parsed[0]['CurrentLatitude']!r} / {parsed[0]['CurrentLongitude']!r}")

    if errors:
        print("FAIL: Wigle v1.6 CSV encoder")
        for e in errors:
            print(f"  - {e}")
        return 1

    print("OK: Wigle v1.6 CSV encoder round-trip passed")
    print("OK: 7 cases passed (pre-header, header, round-trip, RFC-4180, "
          "5 GHz, 2000 cap, empty GPS)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
