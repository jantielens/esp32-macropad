#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <extension.elf> <output.ext>" >&2
    exit 2
fi

ELF=$1
PACKAGE=$2
KEY=${EXTENSION_SIGNING_KEY:-}

if [[ -z "$KEY" || ! -f "$KEY" ]]; then
    echo "EXTENSION_SIGNING_KEY must name a readable P-256 private-key PEM file" >&2
    exit 1
fi

if [[ ! -f "$ELF" ]]; then
    echo "Extension ELF not found: $ELF" >&2
    exit 1
fi

DER_SIGNATURE=$(mktemp)
RAW_SIGNATURE=$(mktemp)
trap 'rm -f "$DER_SIGNATURE" "$RAW_SIGNATURE"' EXIT

# OpenSSL emits DER ECDSA signatures. Convert the two INTEGERs into the
# fixed-width r || s P1363 format that the device verifies.
openssl dgst -sha256 -sign "$KEY" -out "$DER_SIGNATURE" "$ELF"
python3 - "$DER_SIGNATURE" "$RAW_SIGNATURE" <<'PY'
import sys

der = open(sys.argv[1], "rb").read()
if len(der) < 8 or der[0] != 0x30:
    raise SystemExit("Invalid DER ECDSA signature")
offset = 2
if der[1] & 0x80:
    count = der[1] & 0x7f
    if count == 0 or 2 + count > len(der):
        raise SystemExit("Invalid DER ECDSA signature length")
    offset += count
if offset >= len(der) or der[offset] != 0x02:
    raise SystemExit("Invalid DER ECDSA r value")
r_len = der[offset + 1]
r = der[offset + 2:offset + 2 + r_len]
offset += 2 + r_len
if offset >= len(der) or der[offset] != 0x02:
    raise SystemExit("Invalid DER ECDSA s value")
s_len = der[offset + 1]
s = der[offset + 2:offset + 2 + s_len]
if offset + 2 + s_len != len(der) or len(r) > 33 or len(s) > 33:
    raise SystemExit("Unexpected P-256 ECDSA signature length")
r = r.lstrip(b"\0")
s = s.lstrip(b"\0")
if len(r) > 32 or len(s) > 32:
    raise SystemExit("ECDSA scalar exceeds P-256 width")
open(sys.argv[2], "wb").write(r.rjust(32, b"\0") + s.rjust(32, b"\0"))
PY

[[ $(wc -c < "$RAW_SIGNATURE") -eq 64 ]]
cat "$ELF" "$RAW_SIGNATURE" > "$PACKAGE"
echo "Signed $ELF -> $PACKAGE"