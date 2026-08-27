#!/usr/bin/env bash
#
# build_qemu.sh - build the qemu emulators used by SimulIDE from source.
#
# The SimulIDE qemu fork (https://github.com/SKSasykin/SimulIDE-qemu) is pinned
# as a git submodule in third_party/qemu-simulide. The esp32/stm32 emulation
# changes (shared-memory bridge, SLC, build fixes) live directly in the fork.
# This script:
#   1. ensures the submodule is present and pinned to the expected commit
#   2. configures and builds qemu-system-xtensa + qemu-system-arm + qemu-system-riscv32
#   3. copies the binaries and the esp32 ROM dumps (from the fork's pc-bios)
#      into resources/data/bin/, codesigning the emulators on macOS with the
#      allow-jit entitlement (required by qemu's TCG on Apple Silicon)
#
# It is idempotent: when the binaries are up to date it does nothing and
# returns 0, so it is safe to run on every make of the main project.
#
# Requirements: git, python3 (with pip for the venv), ninja, meson build tools,
# libgcrypt, libslirp and a C toolchain. GUI libs (sdl/gtk/vnc/curses) are NOT
# required.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
QEMU_DIR="$REPO_ROOT/third_party/qemu-simulide"
# All build products live OUTSIDE the submodule, in the build/ workspace
# (module dir qemu-simulide), so the submodule stays pristine.
BUILD_DIR="$REPO_ROOT/build/qemu-simulide"
BIN_DIR="$REPO_ROOT/resources/data/bin"
TARGETS=("qemu-system-arm" "qemu-system-xtensa" "qemu-system-riscv32")
HEAD_STAMP="$BUILD_DIR/.simulide-qemu-head"
# esp32 ROM dumps loaded by the esp32 core (passed as the qemu -L directory).
# They are regenerated from the fork's pc-bios on every build, so they are not
# committed to the repository.
ROM_DIR="$BIN_DIR/esp/rom/bin"
ROM_FILES=("esp32-v3-rom.bin" "esp32-v3-rom-app.bin" "esp32c3-rom.bin" "esp32s3_rev0_rom.bin")
# macOS: qemu's TCG JIT (MAP_JIT) hangs in the kernel unless the binary carries
# the com.apple.security.cs.allow-jit entitlement. See esp32-simulide-bridge
# work: without it the emulators hang at startup (even `--version`) when run
# standalone from a terminal, which also breaks the qemu test harness.
JIT_ENT="$REPO_ROOT/scripts/qemu-jit.entitlements"

info() { printf '\033[1;34m[qemu]\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m[qemu] ERROR:\033[0m %s\n' "$*" >&2; }
warn() { printf '\033[1;33m[qemu] WARNING:\033[0m %s\n' "$*" >&2; }

is_macos() { [ "$(uname -s)" = "Darwin" ]; }
is_jit_signed() {
    if ! is_macos; then return 0; fi
    codesign -d --entitlements - "$1" 2>/dev/null | grep -q "com.apple.security.cs.allow-jit"
}
sign_jit() {
    if ! is_macos; then return; fi
    local f="$1"
    if ! is_jit_signed "$f"; then
        info "codesigning $f (allow-jit)"
        # qemu's install step adds an icon as a resource fork, which codesign
        # rejects as "detritus". Strip all xattrs before signing.
        xattr -cr "$f" 2>/dev/null || true
        codesign --force --sign - --entitlements "$JIT_ENT" "$f" || warn "codesign failed for $f"
    fi
}

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

require_slirp() {
    if ! pkg-config --atleast-version=4.0 slirp; then
        err "pkg-config module 'slirp' >= 4.0 not found (REQUIRED for ESP32 virtual WiFi)."
        err "Install it, e.g.:"
        err "  macOS:    brew install libslirp"
        err "  Debian:   sudo apt install libslirp-dev"
        err "  Fedora:   sudo dnf install libslirp-devel"
        exit 1
    fi
}

# Stale-config detection: configurations without ESP32 crypto or SLIRP produce
# a broken ESP32 virtual network. Reconfigure before taking the fast path.
STALE_CONFIG=false
if [ -f "$BUILD_DIR/build.ninja" ] && \
   { ! grep -q "hw/misc/esp32_rsa.c" "$BUILD_DIR/build.ninja" || \
     ! grep -q "hw/xtensa/esp8266.c" "$BUILD_DIR/build.ninja" || \
      ! grep -q '^#define CONFIG_SLIRP' "$BUILD_DIR/config-host.h"; }; then
    STALE_CONFIG=true
fi

# Fast path: binaries + ROMs must have been installed from the current
# submodule commit. A dirty submodule always goes through Ninja so source
# timestamp changes are honored during development.
QEMU_HEAD="$(git -C "$QEMU_DIR" rev-parse HEAD 2>/dev/null || true)"
EXPECTED_HEAD="$(git -C "$REPO_ROOT" rev-parse HEAD:third_party/qemu-simulide 2>/dev/null || true)"
INSTALLED_HEAD="$(cat "$HEAD_STAMP" 2>/dev/null || true)"
ROMS_READY=true
for rom in "${ROM_FILES[@]}"; do
    if [ ! -f "$ROM_DIR/$rom" ]; then ROMS_READY=false; break; fi
done
if [ -x "$BIN_DIR/qemu-system-xtensa" ] && [ -x "$BIN_DIR/qemu-system-arm" ] \
    && [ -x "$BIN_DIR/qemu-system-riscv32" ] \
    && is_jit_signed "$BIN_DIR/qemu-system-xtensa" \
    && [ "$ROMS_READY" = true ] && [ "$STALE_CONFIG" = false ] \
    && [ -n "$QEMU_HEAD" ] && [ "$INSTALLED_HEAD" = "$QEMU_HEAD" ] \
    && [ "$QEMU_HEAD" = "$EXPECTED_HEAD" ] \
    && [ -z "$(git -C "$QEMU_DIR" status --porcelain --untracked-files=no)" ]; then
    info "qemu binaries up to date, nothing to do."
    exit 0
fi

# --- 1. submodule -----------------------------------------------------------
if [ ! -f "$QEMU_DIR/configure" ]; then
    info "initializing qemu submodule..."
    git -C "$REPO_ROOT" submodule update --init --recursive third_party/qemu-simulide
fi

# After `git submodule update --init`, $QEMU_DIR/.git is a *gitfile* (regular
# file) pointing into the parent repo's .git/modules, not a directory.
# Check for existence, not directory-ness.
if [ ! -e "$QEMU_DIR/.git" ]; then
    err "submodule not present at $QEMU_DIR"
    err "run: git submodule update --init --recursive"
    exit 1
fi

# --- 2. configure -----------------------------------------------------------
# mkvenv (qemu's venv bootstrap) clears and recreates its own "pyvenv", so we
# feed it a SEPARATE bootstrap venv (like the original qemu-venv) that is never
# touched. The bootstrap only needs python + pip + distlib + meson.
BOOT="$BUILD_DIR/venv-bootstrap"
if [ "$STALE_CONFIG" = true ]; then
    info "stale build config without ESP32 crypto or SLIRP detected, reconfiguring..."
    rm -rf "$BUILD_DIR"
fi
if [ ! -f "$BUILD_DIR/build.ninja" ]; then
    require_gcrypt
    require_slirp
    if [ ! -x "$BOOT/bin/python3" ] || \
       ! "$BOOT/bin/python3" -m mesonbuild.mesonmain --version >/dev/null 2>&1; then
        info "creating python bootstrap venv (meson, distlib)..."
        python3 -m venv "$BOOT"
        "$BOOT/bin/python3" -m pip install --quiet meson tomli distlib
    fi
    info "configuring (minimal: SLIRP enabled, no GUI libs or VNC)..."
    mkdir -p "$BUILD_DIR"
    if ! ( cd "$BUILD_DIR" && \
        "$QEMU_DIR/configure" \
        --python="$BOOT/bin/python3" \
        --target-list=arm-softmmu,xtensa-softmmu,riscv32-softmmu \
        -Dslirp=enabled -Dvnc=disabled -Dsdl=disabled \
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
# The app bundle data dir is populated by the main build by copying
# resources/data into the .app. On a first parallel `make` that copy can run
# while qemu is still compiling, and incremental makes never refresh it
# (directory mtimes only change for direct children). So in addition to the
# canonical resources/data/bin location, mirror the emulators + ROMs into the
# built .app bundle when it already exists.
BUNDLE_BIN_DIR=""
for d in "$REPO_ROOT"/build/executables/*.app/Contents/MacOS/data/bin; do
    if [ -d "$d" ]; then
        BUNDLE_BIN_DIR="$d"
        break
    fi
done

install_emulators() {
    local bin_dir="$1"
    local rom_dir="$bin_dir/esp/rom/bin"
    mkdir -p "$bin_dir" "$rom_dir"
    for t in "${TARGETS[@]}"; do
        install -m 755 "$BUILD_DIR/$t" "$bin_dir/$t"
        sign_jit "$bin_dir/$t"
    done
    # The esp32 core loads its ROM dumps from esp/rom/bin (passed as the
    # qemu -L directory, see esp32.cpp). They are regenerated from the fork's
    # pc-bios on every build so they never need manual copying.
    for rom in "${ROM_FILES[@]}"; do
        if [ -f "$QEMU_DIR/pc-bios/$rom" ]; then
            install -m 644 "$QEMU_DIR/pc-bios/$rom" "$rom_dir/$rom"
        else
            warn "pc-bios/$rom not found in submodule, skipping"
        fi
    done
}

info "installing emulators + ROM dumps -> $BIN_DIR"
install_emulators "$BIN_DIR"
if [ -n "$BUNDLE_BIN_DIR" ]; then
    info "mirroring emulators + ROM dumps -> $BUNDLE_BIN_DIR"
    install_emulators "$BUNDLE_BIN_DIR"
fi
printf '%s\n' "$(git -C "$QEMU_DIR" rev-parse HEAD)" > "$HEAD_STAMP"

info "done. Emulators installed to $BIN_DIR"
