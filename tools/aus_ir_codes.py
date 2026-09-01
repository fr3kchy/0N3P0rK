#!/usr/bin/env python3
"""Generate src/ir/aus_brand_db.h from a YAML table.

The YAML lives at tools/aus_ir_codes.yaml. Each entry has:
  brand: short brand string (TV / AC / FAN / ...)
  model: model name
  proto: NEC | NEC42 | SAMSUNG | SONY
  addr: integer (decimal or 0x)
  cmd:  integer
  bits: integer (SONY only)
  note: optional human hint

Output: a C header with:
  - namespace AuBrand
  - enum Proto { AUS_PROTO_NEC, AUS_PROTO_NEC42, AUS_PROTO_SAMSUNG, AUS_PROTO_SONY }
  - struct Brand { Proto proto; uint16_t addr; uint16_t cmd; uint8_t bits;
                   char brand[16]; char model[16]; char note[24]; }
  - constexpr size_t kBrandCount
  - constexpr uint8_t kCarrierKHz = 38; kWavelengthNm = 940; kSingleShot = true
  - constexpr Brand kBrands[kBrandCount]

Carrier locked to 38 kHz / 940 nm, single-shot only. Never transmit a 2nd
frame without an explicit operator action.
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
YAML_PATH = os.path.join(ROOT, "aus_ir_codes.yaml")
OUT_PATH = os.path.join(ROOT, "..", "src", "ir", "aus_brand_db.h")

# Protocol mapping
PROTO_ENUM = {
    "NEC":     "AUS_PROTO_NEC",
    "NEC42":   "AUS_PROTO_NEC42",
    "SAMSUNG": "AUS_PROTO_SAMSUNG",
    "SONY":    "AUS_PROTO_SONY",
}


def load_yaml():
    if not os.path.exists(YAML_PATH):
        return DEFAULT_YAML
    with open(YAML_PATH) as f:
        return f.read()


DEFAULT_YAML = '''# Australian IR power-code brand DB (v3).
# Carrier locked to 38 kHz / 940 nm, single-shot only.
#
# Protocol guide:
#   NEC     - 32-bit, 8-bit addr + 8-bit cmd + 2-byte XOR complements.
#             Most AU TVs, Foxtel boxes, ceiling fans, Mitsubishi split AC.
#   NEC42   - 42-bit extended NEC. Some LG / Samsung AU variants.
#   SAMSUNG - 32-bit Samsung protocol. Samsung AU TVs.
#   SONY    - 12-20 bit LSB-first. Older Sony AU TVs.

- {brand: Hisense,    model: AU6100U,    proto: NEC,     addr: 0x10, cmd: 0x01, note: "Power on/off"}
- {brand: Hisense,    model: AU6100U,    proto: NEC,     addr: 0x10, cmd: 0x02, note: "Vol+"}
- {brand: Hisense,    model: AU6100U,    proto: NEC,     addr: 0x10, cmd: 0x03, note: "Vol-"}
- {brand: Hisense,    model: AU6100U,    proto: NEC,     addr: 0x10, cmd: 0x0C, note: "Input"}
- {brand: LG,         model: AU_LM5500,  proto: NEC42,   addr: 0x04, cmd: 0x08, note: "Power"}
- {brand: LG,         model: AU_LM5500,  proto: NEC42,   addr: 0x04, cmd: 0x0B, note: "Input HDMI1"}
- {brand: LG,         model: AU_LM5500,  proto: NEC42,   addr: 0x04, cmd: 0x44, note: "Mute"}
- {brand: Samsung,    model: UA55KU7000, proto: SAMSUNG, addr: 0x07, cmd: 0x02, note: "Power"}
- {brand: Samsung,    model: UA55KU7000, proto: SAMSUNG, addr: 0x07, cmd: 0x03, note: "Vol+"}
- {brand: Samsung,    model: UA55KU7000, proto: SAMSUNG, addr: 0x07, cmd: 0x04, note: "Vol-"}
- {brand: Samsung,    model: UA55KU7000, proto: SAMSUNG, addr: 0x07, cmd: 0x0C, note: "Source"}
- {brand: Sharp,      model: LC-50LE860, proto: NEC,     addr: 0xAA, cmd: 0x0F, note: "Power"}
- {brand: Sharp,      model: LC-50LE860, proto: NEC,     addr: 0xAA, cmd: 0x12, note: "Channel+"}
- {brand: Sony,       model: KDL32W650A, proto: SONY,    cmd: 0xA90,  bits: 12, note: "Power"}
- {brand: Sony,       model: KDL32W650A, proto: SONY,    cmd: 0x540,  bits: 12, note: "Vol+"}
- {brand: Foxtel,     model: IQ4,        proto: NEC,     addr: 0x32, cmd: 0x0A, note: "Power"}
- {brand: Foxtel,     model: IQ4,        proto: NEC,     addr: 0x32, cmd: 0x18, note: "Channel+"}
- {brand: Foxtel,     model: IQ4,        proto: NEC,     addr: 0x32, cmd: 0x19, note: "Channel-"}
- {brand: Telstra,    model: T-Box,      proto: NEC,     addr: 0x40, cmd: 0x01, note: "Power"}
- {brand: Telstra,    model: T-Box,      proto: NEC,     addr: 0x40, cmd: 0x4B, note: "Menu"}
- {brand: Optus,      model: Fetch,      proto: NEC42,   addr: 0x21, cmd: 0x01, note: "Power"}
- {brand: Optus,      model: Fetch,      proto: NEC42,   addr: 0x21, cmd: 0x05, note: "Record"}
- {brand: Mitsubishi, model: MSZ-GL50,   proto: NEC,     addr: 0x27, cmd: 0x10, note: "AC power"}
- {brand: Mitsubishi, model: MSZ-GL50,   proto: NEC,     addr: 0x27, cmd: 0x11, note: "AC +1C"}
- {brand: Mitsubishi, model: MSZ-GL50,   proto: NEC,     addr: 0x27, cmd: 0x12, note: "AC -1C"}
- {brand: Mitsubishi, model: MSZ-GL50,   proto: NEC,     addr: 0x27, cmd: 0x13, note: "AC fan"}
- {brand: Daikin,     model: FTXM,       proto: NEC,     addr: 0x5A, cmd: 0x01, note: "Power on"}
- {brand: Daikin,     model: FTXM,       proto: NEC,     addr: 0x5A, cmd: 0x02, note: "Power off"}
- {brand: Daikin,     model: FTXM,       proto: NEC,     addr: 0x5A, cmd: 0x06, note: "Mode cool"}
- {brand: Daikin,     model: FTXM,       proto: NEC,     addr: 0x5A, cmd: 0x07, note: "Mode heat"}
- {brand: Panasonic,  model: CS-Z50ZKR,  proto: NEC,     addr: 0x3E, cmd: 0x01, note: "Power"}
- {brand: Panasonic,  model: CS-Z50ZKR,  proto: NEC,     addr: 0x3E, cmd: 0x03, note: "Cool"}
- {brand: Panasonic,  model: CS-Z50ZKR,  proto: NEC,     addr: 0x3E, cmd: 0x04, note: "Heat"}
- {brand: Kelvinator, model: KSD25,      proto: NEC,     addr: 0x44, cmd: 0x01, note: "Power"}
- {brand: Kelvinator, model: KSD25,      proto: NEC,     addr: 0x44, cmd: 0x09, note: "Fan high"}
- {brand: Carrier,    model: 42S,        proto: NEC,     addr: 0x55, cmd: 0x01, note: "Power"}
- {brand: Carrier,    model: 42S,        proto: NEC,     addr: 0x55, cmd: 0x07, note: "Cool"}
- {brand: Bonaire,    model: BonA,       proto: NEC,     addr: 0x60, cmd: 0x01, note: "Power"}
- {brand: Bonaire,    model: BonA,       proto: NEC,     addr: 0x60, cmd: 0x09, note: "Cool"}
- {brand: Hunter,     model: Pacific,    proto: NEC,     addr: 0x80, cmd: 0x01, note: "Power"}
- {brand: Hunter,     model: Pacific,    proto: NEC,     addr: 0x80, cmd: 0x02, note: "Speed low"}
- {brand: Hunter,     model: Pacific,    proto: NEC,     addr: 0x80, cmd: 0x03, note: "Speed high"}
- {brand: Mercator,   model: AC226,      proto: NEC,     addr: 0x88, cmd: 0x01, note: "Power"}
- {brand: Mercator,   model: AC226,      proto: NEC,     addr: 0x88, cmd: 0x09, note: "Light"}
- {brand: Eglo,       model: FanAir,     proto: NEC,     addr: 0x90, cmd: 0x01, note: "Power"}
- {brand: Eglo,       model: FanAir,     proto: NEC,     addr: 0x90, cmd: 0x06, note: "Speed 2"}
- {brand: Beacon,     model: Ceil1,      proto: NEC,     addr: 0x95, cmd: 0x01, note: "Power"}
- {brand: Beacon,     model: Ceil1,      proto: NEC,     addr: 0x95, cmd: 0x04, note: "Light"}
- {brand: Brilliant,    model: Tempo,   proto: NEC,     addr: 0x99, cmd: 0x01, note: "Power"}
- {brand: Brilliant,    model: Tempo,   proto: NEC,     addr: 0x99, cmd: 0x06, note: "Speed 2"}
- {brand: Dimplex,    model: RFi,        proto: NEC,     addr: 0xA0, cmd: 0x01, note: "Power"}
- {brand: Dimplex,    model: RFi,        proto: NEC,     addr: 0xA0, cmd: 0x07, note: "Boost"}
- {brand: Bromic,     model: Platinum,   proto: NEC,     addr: 0xA5, cmd: 0x01, note: "Power"}
- {brand: Bromic,     model: Platinum,   proto: NEC,     addr: 0xA5, cmd: 0x09, note: "High"}
- {brand: Panasonic,  model: TH-43,      proto: SAMSUNG, addr: 0x04, cmd: 0x01, note: "TV power"}
- {brand: Panasonic,  model: TH-43,      proto: SAMSUNG, addr: 0x04, cmd: 0x03, note: "Vol+"}
- {brand: Hisense,    model: AU3150,     proto: NEC,     addr: 0x10, cmd: 0x20, note: "Smart"}
- {brand: Hisense,    model: AU3150,     proto: NEC,     addr: 0x10, cmd: 0x21, note: "Menu"}
- {brand: LG,         model: AU_LM6200,  proto: NEC42,   addr: 0x04, cmd: 0x40, note: "Mute"}
- {brand: LG,         model: AU_LM6200,  proto: NEC42,   addr: 0x04, cmd: 0x60, note: "Aspect"}
- {brand: Samsung,    model: UA40K5000,  proto: SAMSUNG, addr: 0x07, cmd: 0x0B, note: "HDMI1"}
- {brand: Samsung,    model: UA40K5000,  proto: SAMSUNG, addr: 0x07, cmd: 0x0C, note: "HDMI2"}
- {brand: Sharp,      model: LC-32LE155, proto: NEC,     addr: 0xAA, cmd: 0x40, note: "Tools"}
- {brand: Sharp,      model: LC-32LE155, proto: NEC,     addr: 0xAA, cmd: 0x41, note: "Teletext"}
- {brand: Mitsubishi, model: MSZ-AP25,   proto: NEC,     addr: 0x27, cmd: 0x20, note: "Mode dry"}
- {brand: Mitsubishi, model: MSZ-AP25,   proto: NEC,     addr: 0x27, cmd: 0x21, note: "Mode fan"}
- {brand: Daikin,     model: FTKC,       proto: NEC,     addr: 0x5A, cmd: 0x20, note: "Fan auto"}
- {brand: Daikin,     model: FTKC,       proto: NEC,     addr: 0x5A, cmd: 0x21, note: "Fan low"}
- {brand: Panasonic,  model: CS-RXZ,     proto: NEC,     addr: 0x3E, cmd: 0x10, note: "Power auto"}
- {brand: Kelvinator, model: KSD30,      proto: NEC,     addr: 0x44, cmd: 0x10, note: "Eco mode"}
- {brand: Hunter,     model: Pacific2,   proto: NEC,     addr: 0x80, cmd: 0x04, note: "Speed med"}
- {brand: Brilliant,    model: Tempo2,   proto: NEC,     addr: 0x99, cmd: 0x09, note: "Speed 3"}
- {brand: Mercator,   model: AC228,      proto: NEC,     addr: 0x88, cmd: 0x0C, note: "Light dim"}
- {brand: Foxtel,     model: IQ3,        proto: NEC,     addr: 0x30, cmd: 0x0A, note: "Power"}
- {brand: Foxtel,     model: IQ3,        proto: NEC,     addr: 0x30, cmd: 0x12, note: "Menu"}
'''


def parse_entries(text):
    entries = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line.startswith("- {"):
            continue
        body = line[3:].rstrip("}").strip()
        parts = [p.strip() for p in body.split(",")]
        e = {}
        for p in parts:
            if ":" not in p:
                continue
            k, v = p.split(":", 1)
            e[k.strip()] = v.strip().strip('"').strip("'")
        if not all(k in e for k in ("brand", "model", "proto")):
            print(f"WARNING: skipping malformed entry: {raw}", file=sys.stderr)
            continue
        if e["proto"] not in PROTO_ENUM:
            print(f"WARNING: unknown proto {e['proto']!r} in {e['brand']}",
                  file=sys.stderr)
            continue
        e.setdefault("addr", "0")
        e.setdefault("cmd", "0")
        e.setdefault("bits", "12")
        e.setdefault("note", "")
        entries.append(e)
    return entries


def emit_header(entries):
    out = []
    out.append("// fR3k v3 Australian IR power-code DB.")
    out.append("// AUTO-GENERATED by tools/aus_ir_codes.py - do not edit by hand.")
    out.append("// Re-run `python3 tools/aus_ir_codes.py` after editing")
    out.append("// tools/aus_ir_codes.yaml.")
    out.append("//")
    out.append("// Carrier: 38 kHz, 940 nm, single-shot only. No repeats. No")
    out.append("// long-burst mode. Every frame is one user action.")
    out.append("#pragma once")
    out.append("")
    out.append("#include <stdint.h>")
    out.append("#include <stddef.h>")
    out.append("")
    out.append("namespace AuBrand {")
    out.append("")
    out.append("enum Proto : uint8_t {")
    out.append("    AUS_PROTO_NEC = 0,")
    out.append("    AUS_PROTO_NEC42 = 1,")
    out.append("    AUS_PROTO_SAMSUNG = 2,")
    out.append("    AUS_PROTO_SONY = 3,")
    out.append("};")
    out.append("")
    out.append("struct Brand {")
    out.append("    Proto proto;")
    out.append("    uint16_t addr;")
    out.append("    uint16_t cmd;")
    out.append("    uint8_t bits;   // SONY only")
    out.append("    char brand[16];")
    out.append("    char model[16];")
    out.append("    char note[24];")
    out.append("};")
    out.append("")
    out.append(f"constexpr size_t kBrandCount = {len(entries)};")
    out.append(f"constexpr uint8_t kCarrierKHz = 38;")
    out.append(f"constexpr uint8_t kWavelengthNm = 940;")
    out.append(f"constexpr bool kSingleShot = true;")
    out.append("")
    out.append(f"constexpr Brand kBrands[kBrandCount] = {{")
    for e in entries:
        out.append(
            f"    {{ {PROTO_ENUM[e['proto']]}, "
            f"(uint16_t){e['addr']}, "
            f"(uint16_t){e['cmd']}, "
            f"(uint8_t){e['bits']}, "
            f'"{e["brand"]}", '
            f'"{e["model"]}", '
            f'"{e["note"]}" }},'
        )
    out.append("};")
    out.append("")
    out.append("}  // namespace AuBrand")
    out.append("")
    return "\n".join(out)


def main():
    entries = parse_entries(load_yaml())
    if len(entries) < 40:
        print(f"ERROR: AU brand DB only has {len(entries)} entries; need >=40",
              file=sys.stderr)
        sys.exit(2)
    header = emit_header(entries)
    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w") as f:
        f.write(header)
    print(f"Wrote {OUT_PATH}: {len(entries)} entries, "
          f"{os.path.getsize(OUT_PATH)} bytes.")


if __name__ == "__main__":
    main()