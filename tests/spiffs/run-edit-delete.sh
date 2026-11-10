#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
WORK_DIR="$ROOT_DIR/tmp/spiffs-edit-test"
QMAKE="${QMAKE:-/opt/homebrew/opt/qt@5/bin/qmake}"
MKSPiffs="${MKSPIFFS:-$HOME/.platformio/packages/tool-mkspiffs/mkspiffs_espressif32_arduino}"

rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR/fs"
trap 'rm -rf "$WORK_DIR"' EXIT

printf 'old text\n' > "$WORK_DIR/fs/edit.txt"
printf '\000\001\377' > "$WORK_DIR/fs/data.bin"
"$MKSPiffs" -c "$WORK_DIR/fs" -b 4096 -p 256 -s 65536 "$WORK_DIR/image.bin" >/dev/null

cat > "$WORK_DIR/test.pro" <<EOF
QT += core
TEMPLATE = app
CONFIG -= app_bundle
SOURCES += $ROOT_DIR/tests/spiffs/test-edit-delete.cpp $ROOT_DIR/src/microsim/cores/qemu/spiffsimage.cpp
INCLUDEPATH += $ROOT_DIR/src/microsim/cores/qemu
EOF

"$QMAKE" -o "$WORK_DIR/Makefile" "$WORK_DIR/test.pro" >/dev/null
make -C "$WORK_DIR" -j2 >/dev/null
"$WORK_DIR/test" "$WORK_DIR/image.bin"
dd if="$WORK_DIR/image.bin.partitioned" of="$WORK_DIR/partition.bin" bs=4096 skip=16 count=16 2>/dev/null
"$MKSPiffs" -l "$WORK_DIR/partition.bin" | grep -F 'edit.txt' >/dev/null
printf '%s\n' 'SPIFFS edit/delete PASS'
