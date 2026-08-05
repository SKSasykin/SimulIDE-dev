#!/usr/bin/env bash
#
# build_qemu.sh - build the qemu emulators used by SimulIDE from source.
#
# The SimulIDE qemu fork (https://github.com/SKSasykin/SimulIDE-qemu) is pinned
# as a git submodule in third_party/qemu-simulide. The esp32/stm32 emulation
# changes (shared-memory bridge, SLC, build fixes) live directly in the fork.
# This script:
#   1. ensures the submodule is present and pinned to the expected commit
#   2. configures and builds qemu-system-xtensa + qemu-system-arm
#   3. copies the binaries and the esp32 ROM dumps (from the fork's pc-bios)
#      into resources/data/bin/
#
# It is idempotent: when the binaries are up to date it does nothing and
# returns 0, so it is safe to run on every make of the main project.
#
# Requirements: git, python3 (with pip for the venv), ninja, meson build tools
# and a C toolchain. GUI libs (sdl/gtk/vnc/curses) are NOT required.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_DIR="$REPO_ROOT/third_party/qemu-simulide"
# All build products live OUTSIDE the submodule, in the build_XX/ workspace
# (module dir qemu-simulide), so the submodule stays pristine.
BUILD_DIR="$REPO_ROOT/build_XX/qemu-simulide"
BIN_DIR="$REPO_ROOT/resources/data/bin"
PIN="fae418ed9cca65f63a3b4d527de819b3462fccb2"
TARGETS=("qemu-system-arm" "qemu-system-xtensa")
# esp32 ROM dumps loaded by the esp32 core (passed as the qemu -L directory).
# They are regenerated from the fork's pc-bios on every build, so they are not
# committed to the repository.
ROM_DIR="$BIN_DIR/esp32/rom/bin"
ROM_FILES=("esp32-v3-rom.bin" "esp32-v3-rom-app.bin" "esp32c3-rom.bin")

info() { printf '\033[1;34m[qemu]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[qemu] ERROR:\033[0m %s\n' "$*" >&2; }
warn() { printf '\033[1;33m[qemu] WARNING:\033[0m %s\n' "$*" >&2; }

# Homebrew (macOS) installs pkg-config modules outside the default search path.
# libgcrypt is REQUIRED by the esp32 machine (hw/misc/esp32_rsa.c), so its
# pkg-config module must be visible during meson configure.
for p in /opt/homebrew/lib/pkgconfig /usr/local/lib/pkgconfig; do
    if [ -d "$p" ] && [[ ":${PKG_CONFIG_PATH:-}:" != *":$p:"* ]]; then
        export PKG_CONFIG_PATH="$p${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    fi
done

require_gcrypt() {
    if ! pkg-config --atleast-version=1.8 libgcrypt; then
        err "pkg-config module 'libgcrypt' >= 1.8 not found (REQUIRED by the esp32 machine)."
        err "Install it, e.g.:"
        err "  macOS:    brew install libgcrypt"
        err "  Debian:   sudo apt install libgcrypt20-dev"
        err "  Fedora:   sudo dnf install libgcrypt-devel"
        exit 1
    fi
}

# Stale-config detection: a build.ninja configured without libgcrypt lacks the
# esp32 rsa device and would produce a broken binary. When detected we must
# wipe the build dir and reconfigure (checked before the fast path below).
STALE_CONFIG=false
if [ -f "$BUILD_DIR/build.ninja" ] && \
   ! grep -q "hw/misc/esp32_rsa.c" "$BUILD_DIR/build.ninja"; then
    STALE_CONFIG=true
fi

# Fast path: binaries + esp32 ROM dumps already in place (with crypto).
if [ -x "$BIN_DIR/qemu-system-xtensa" ] && [ -x "$BIN_DIR/qemu-system-arm" ] \
    && [ -f "$ROM_DIR/${ROM_FILES[0]}" ] && [ "$STALE_CONFIG" = false ]; then
    info "qemu binaries up to date, nothing to do."
    exit 0
fi

# --- 1. submodule -----------------------------------------------------------
if [ ! -f "$QEMU_DIR/configure" ]; then
    info "initializing qemu submodule..."
    git -C "$REPO_ROOT" submodule update --init --recursive third_party/qemu-simulide
fi

if [ ! -d "$QEMU_DIR/.git" ]; then
    err "submodule not present at $QEMU_DIR"
    err "run: git submodule update --init --recursive"
    exit 1
fi

HEAD="$(git -C "$QEMU_DIR" rev-parse HEAD 2>/dev/null || true)"
if [ "$HEAD" != "$PIN" ]; then
    info "pinning submodule to $PIN (was $HEAD)..."
    git -C "$QEMU_DIR" fetch --depth 1 origin "$PIN"
    git -C "$QEMU_DIR" checkout --detach "$PIN"
fi

# --- 2. configure -----------------------------------------------------------
# mkvenv (qemu's venv bootstrap) clears and recreates its own "pyvenv", so we
# feed it a SEPARATE bootstrap venv (like the original qemu-venv) that is never
# touched. The bootstrap only needs python + pip + distlib + meson.
BOOT="$BUILD_DIR/venv-bootstrap"
if [ "$STALE_CONFIG" = true ]; then
    info "stale build config without esp32 crypto (libgcrypt) detected, reconfiguring..."
    rm -rf "$BUILD_DIR"
fi
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    require_gcrypt
    if [ ! -x "$BOOT/bin/python3" ] || \
       ! "$BOOT/bin/python3" -m mesonbuild.mesonmain --version >/dev/null 2>&1; then
        info "creating python bootstrap venv (meson, distlib)..."
        python3 -m venv "$BOOT"
        "$BOOT/bin/python3" -m pip install --quiet meson tomli distlib
    fi
    info "configuring (minimal: no GUI libs, slirp, vnc, ...)..."
    mkdir -p "$BUILD_DIR"
    if ! ( cd "$BUILD_DIR" && \
        "$QEMU_DIR/configure" \
        --python="$BOOT/bin/python3" \
        --target-list=arm-softmmu,xtensa-softmmu \
        -Dslirp=disabled -Dvnc=disabled -Dsdl=disabled \
        -Dgtk=disabled -Dcurses=disabled -Dvde=disabled -Dnetmap=disabled \
        -Dgcrypt=enabled ); then
        err "qemu configure failed. See build/config.log"
        err "Install build dependencies (python3+venv, ninja, meson, a C compiler,"
        err "pkg-config with glib-2.0 and pixman) and re-run this script."
        exit 1
    fi
fi

# --- 3. build ---------------------------------------------------------------
info "building qemu (this can take several minutes on first run)..."
if ! ninja -C "$BUILD_DIR" "${TARGETS[@]}"; then
    err "qemu build failed. See build/meson-logs/meson-log.txt"
    exit 1
fi

# --- 4. install -------------------------------------------------------------
mkdir -p "$BIN_DIR"
for t in "${TARGETS[@]}"; do
    src="$BUILD_DIR/$t"
    if [ -x "$src" ]; then
        info "installing $t -> $BIN_DIR"
        install -m 755 "$src" "$BIN_DIR/$t"
    else
        err "build finished but $src not found!"
        exit 1
    fi
done

# The esp32 core loads its ROM dumps from resources/data/bin/esp32/rom/bin
# (passed as the qemu -L directory, see esp32.cpp). They are regenerated from
# the fork's pc-bios on every build so they never need manual copying.
mkdir -p "$ROM_DIR"
for rom in "${ROM_FILES[@]}"; do
    if [ -f "$QEMU_DIR/pc-bios/$rom" ]; then
        info "installing pc-bios/$rom -> $ROM_DIR"
        install -m 644 "$QEMU_DIR/pc-bios/$rom" "$ROM_DIR/$rom"
    else
        warn "pc-bios/$rom not found in submodule, skipping"
    fi
done

info "done. Emulators installed to $BIN_DIR"
