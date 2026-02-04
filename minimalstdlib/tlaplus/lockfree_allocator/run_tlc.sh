#!/bin/bash
# Script to download TLA+ tools and run the model checker

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TLA_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
TLA_JAR="$TLA_DIR/tla2tools.jar"
TLA_VERSION="1.8.0"
TMP_DIR="$(mktemp -d /tmp/tlc-XXXXXX)"

# Specs/configs may be under a lockfree_allocator sub-folder
if [ -d "$SCRIPT_DIR/lockfree_allocator" ]; then
    SPEC_DIR="$SCRIPT_DIR/lockfree_allocator"
else
    SPEC_DIR="$SCRIPT_DIR"
fi

# Download TLA+ tools if not present
if [ ! -f "$TLA_JAR" ]; then
    echo "Downloading TLA+ tools v${TLA_VERSION}..."
    wget -q "https://github.com/tlaplus/tlaplus/releases/download/v${TLA_VERSION}/tla2tools.jar" -O "$TLA_JAR"
    echo "Downloaded $TLA_JAR"
fi

# Check Java is available
if ! command -v java &> /dev/null; then
    echo "Error: Java is required but not found. Install with: sudo apt install default-jre"
    exit 1
fi

# Create temp directory for TLC state files
mkdir -p "$TMP_DIR"

# Default config or use argument
CONFIG="${1:-LockfreeAllocatorTagged.cfg}"
SPEC="LockfreeAllocatorTagged.tla"

CONFIG_PATH="$SPEC_DIR/$CONFIG"
SPEC_PATH="$SPEC_DIR/$SPEC"

echo "=== Running TLC Model Checker ==="
echo "Spec: $SPEC_PATH"
echo "Config: $CONFIG_PATH"
echo "Temp dir: $TMP_DIR"
echo ""

cd "$SPEC_DIR"

# Run TLC with reasonable memory, state files in project-local temp dir
java -Xmx2g -XX:+UseParallelGC \
    -jar "$TLA_JAR" \
    -config "$CONFIG_PATH" \
    -metadir "$TMP_DIR" \
    -workers auto \
    -cleanup \
    "$SPEC_PATH"

echo ""
echo "=== Model checking complete ==="
