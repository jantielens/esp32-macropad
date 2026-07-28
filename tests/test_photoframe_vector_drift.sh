#!/bin/bash
# =============================================================================
# Photoframe conformance-vector producer drift guard
# =============================================================================
# Regenerates committed goldens with the production gray16.py encoders, then
# fails when the generated artifacts differ from the repository versions.

set -e
cd "$(dirname "$0")/.."

KIT_DIR="docs/dev/photoframe-next-image/conformance/photoframe-next-image-v1"
PROFILE="e1003-landscape"
VECTORS_DIR="$KIT_DIR/profiles/$PROFILE/vectors"

python3 "$KIT_DIR/verify_vectors.py" "$PROFILE" --generate

if ! git diff --exit-code -- "$VECTORS_DIR"; then
    echo "FAIL: photoframe conformance vectors drifted from tools/photoframe-site/gray16.py" >&2
    echo "      Regenerate and review the committed goldens." >&2
    exit 1
fi

echo "PASS: photoframe conformance vectors match the production encoder."