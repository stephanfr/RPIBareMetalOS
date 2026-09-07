#!/bin/bash -eu
#
#  Builds QEMU from source for the dev container.
#
#  The distro packages available in the base image predate QEMU's Raspberry Pi 4
#  machine model (raspi4b), which the regression suite needs in order to exercise
#  the RPi4 code paths. RPi3 and RPi4 have materially different reserved-memory
#  topologies, and only the RPi4 layout fragments usable RAM -- see
#  qemu_regression_test.py.
#
#  Only the aarch64 system target is built; a full-target build takes many times
#  longer for no benefit here.

QEMU_VERSION=${QEMU_VERSION:-9.2.0}
QEMU_PREFIX=${QEMU_PREFIX:-/usr/local}

SRC_DIR=/tmp/qemu-build

rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"

echo "Downloading QEMU ${QEMU_VERSION}..."
wget -q "https://download.qemu.org/qemu-${QEMU_VERSION}.tar.xz" -O "$SRC_DIR/qemu.tar.xz"
tar xf "$SRC_DIR/qemu.tar.xz" -C "$SRC_DIR"

cd "$SRC_DIR/qemu-${QEMU_VERSION}"

echo "Configuring QEMU ${QEMU_VERSION} (aarch64-softmmu only)..."
./configure \
    --target-list=aarch64-softmmu \
    --prefix="$QEMU_PREFIX" \
    --disable-docs \
    --disable-gtk \
    --disable-sdl \
    --disable-vnc \
    --disable-werror

echo "Building QEMU (this takes a few minutes)..."
make -j"$(nproc)"
make install

cd /
rm -rf "$SRC_DIR"

hash -r

#  Fail loudly here rather than letting a wrong version pin or a partial build
#  surface later as a confusing "unsupported machine type" during a test run.

echo
"$QEMU_PREFIX/bin/qemu-system-aarch64" --version

if ! "$QEMU_PREFIX/bin/qemu-system-aarch64" -machine help | grep -qE '^raspi4b[[:space:]]'; then
    echo "ERROR: QEMU ${QEMU_VERSION} built successfully but does not provide the 'raspi4b' machine." >&2
    echo "       Available Raspberry Pi machines:" >&2
    "$QEMU_PREFIX/bin/qemu-system-aarch64" -machine help | grep -i raspi >&2 || true
    exit 1
fi

echo "QEMU ${QEMU_VERSION} installed to ${QEMU_PREFIX} with raspi3b + raspi4b support."
