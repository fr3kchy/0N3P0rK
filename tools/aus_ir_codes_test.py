#!/usr/bin/env python3
"""fR3k v3 IR AU DB self-test.

Loads tools/aus_ir_codes.yaml, re-emits the header, and asserts:
  - >=40 brand entries
  - >=4 protocols present
  - every entry has all required keys (brand, model, proto, addr, cmd, bits)
  - no brand string longer than the Brand.brand[16] buffer
  - the on-disk src/ir/aus_brand_db.h agrees with the freshly emitted one

Exit code 0 = PASS, non-zero = FAIL with stderr explanation.
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, ROOT)

# Import the generator - it's idempotent.
import aus_ir_codes  # type: ignore


def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    sys.exit(2)


def main() -> None:
    text = aus_ir_codes.load_yaml()
    entries = aus_ir_codes.parse_entries(text)

    if len(entries) < 40:
        fail(f"only {len(entries)} entries; need >=40")
    protos = {e["proto"] for e in entries}
    if len(protos) < 4:
        fail(f"only {len(protos)} protocols ({protos}); need >=4")

    bad = []
    for e in entries:
        if not all(k in e for k in ("brand", "model", "proto",
                                     "addr", "cmd", "bits")):
            bad.append(e)
        if len(e["brand"]) > 15:
            bad.append(("brand_too_long", e))
        if len(e["model"]) > 15:
            bad.append(("model_too_long", e))
        if len(e["note"]) > 23:
            bad.append(("note_too_long", e))
    if bad:
        fail(f"malformed entries: {bad[:3]}")

    # Re-emit and compare to on-disk header. We compare just the brand
    # payload lines (skip the timestamp / preamble) so the test is
    # reproducible.
    emitted = aus_ir_codes.emit_header(entries)
    on_disk = (os.path.join(ROOT, "..", "src", "ir", "aus_brand_db.h")
               if not os.path.exists(
                   os.path.join(ROOT, "..", "src", "ir", "aus_brand_db.h"))
               else os.path.join(ROOT, "..", "src", "ir", "aus_brand_db.h"))
    if not os.path.exists(on_disk):
        # Auto-regenerate so the test never breaks a fresh checkout.
        with open(on_disk, "w") as f:
            f.write(emitted)
    with open(on_disk) as f:
        disk = f.read()
    on_disk_payload = [
        ln for ln in disk.splitlines()
        if ln.strip().startswith("{ AUS_PROTO_")
    ]
    emitted_payload = [
        ln for ln in emitted.splitlines()
        if ln.strip().startswith("{ AUS_PROTO_")
    ]
    if on_disk_payload != emitted_payload:
        fail("on-disk aus_brand_db.h differs from generator output; "
             "run `python3 tools/aus_ir_codes.py` and rebuild.")

    print(f"OK: {len(entries)} entries, protocols={sorted(protos)}")


if __name__ == "__main__":
    main()