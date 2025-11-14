#!/bin/bash
# Fetch the pinned pyyaml source tree for compatibility testing.
# Run from anywhere inside the python-libfyaml tree.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$SCRIPT_DIR/pyyaml-test"
REPO="https://github.com/yaml/pyyaml.git"
COMMIT="d51d8a138f7230834fc6e95635ff09ebd329185f"

if [ -d "$DEST/.git" ]; then
    current=$(git -C "$DEST" rev-parse HEAD 2>/dev/null || true)
    if [ "$current" = "$COMMIT" ]; then
        echo "pyyaml-test already at $COMMIT, nothing to do."
        exit 0
    fi
    echo "pyyaml-test exists but at wrong commit, updating..."
    git -C "$DEST" fetch --quiet origin
    git -C "$DEST" checkout --quiet "$COMMIT"
else
    echo "Cloning pyyaml..."
    git clone --quiet --no-checkout "$REPO" "$DEST"
    git -C "$DEST" checkout --quiet "$COMMIT"
fi

# Place our conftest plugin where pytest can find it
cp "$SCRIPT_DIR/conftest_libfyaml.py" "$DEST/tests/legacy_tests/"

echo "pyyaml-test ready at $COMMIT"
