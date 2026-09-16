#!/usr/bin/env bash
set -eu

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD_DIR="$ROOT_DIR/tmp/qemubt-runtime"

mkdir -p "$BUILD_DIR"
qmake -o "$BUILD_DIR/Makefile" "$ROOT_DIR/tests/qemubt-runtime/qemubt-runtime.pro"
make -C "$BUILD_DIR" -s
exec "$BUILD_DIR/qemubt-runtime"
