#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
EXECUTABLE="${1:-}"

if [[ -z "$EXECUTABLE" ]]; then
    for candidate in \
        "$ROOT_DIR"/build/executables/simulide-*.app/Contents/MacOS/simulide-* \
        "$ROOT_DIR"/build/executables/simulide-*; do
        [[ -x "$candidate" && ! -d "$candidate" ]] || continue
        if [[ -z "$EXECUTABLE" || "$candidate" -nt "$EXECUTABLE" ]]; then
            EXECUTABLE="$candidate"
        fi
    done
fi

if [[ -z "$EXECUTABLE" || ! -x "$EXECUTABLE" ]]; then
    printf '%s\n' "FAIL ide: SimulIDE executable not found; build the IDE or pass --executable"
    exit 1
fi

RESULT_DIR="$(mktemp -d "${TMPDIR:-/tmp}/simulide-esp-tests.XXXXXX")"
trap 'rm -rf "$RESULT_DIR"' EXIT
FAILED=0

run_smoke() {
    local name="$1"
    local circuit="$2"
    local duration_ms="$3"
    local log="$RESULT_DIR/${name//\//-}.log"

    HOME="$RESULT_DIR" SIMULIDE_TEST_MODE=1 QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
        "$EXECUTABLE" -silent -nogui -smoke-test "$ROOT_DIR/$circuit" "$duration_ms" >"$log" 2>&1 &
    local pid=$!
    (
        sleep $((duration_ms / 1000 + 15))
        kill "$pid" 2>/dev/null || true
    ) &
    local watchdog=$!

    if wait "$pid" && grep -q "TEST PASS: smoke run completed" "$log"; then
        printf 'PASS ide/%s\n' "$name"
    else
        printf 'FAIL ide/%s\n' "$name"
        while IFS= read -r line; do printf '  %s\n' "$line"; done <"$log"
        FAILED=1
    fi
    kill "$watchdog" 2>/dev/null || true
    wait "$watchdog" 2>/dev/null || true
}

run_http() {
    local name="$1"
    local circuit="$2"
    local log="$RESULT_DIR/${name//\//-}.log"
    local response=""

    HOME="$RESULT_DIR" SIMULIDE_TEST_MODE=1 QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}" \
        "$EXECUTABLE" -silent -nogui -smoke-test "$ROOT_DIR/$circuit" 15000 >"$log" 2>&1 &
    local pid=$!
    (
        sleep 30
        kill "$pid" 2>/dev/null || true
    ) &
    local watchdog=$!
    local attempt
    for attempt in {1..30}; do
        response="$(curl --silent --max-time 1 http://127.0.0.1:8080/ 2>/dev/null || true)"
        [[ "$response" == "Hello World!" ]] && break
        sleep 0.5
    done

    local process_ok=0
    if wait "$pid" && grep -q "TEST PASS: smoke run completed" "$log"; then
        process_ok=1
    fi
    kill "$watchdog" 2>/dev/null || true
    wait "$watchdog" 2>/dev/null || true
    if [[ "$process_ok" == "1" && "$response" == "Hello World!" ]]; then
        printf 'PASS ide/%s\n' "$name"
    else
        printf 'FAIL ide/%s: expected HTTP response %q, got %q\n' "$name" "Hello World!" "$response"
        while IFS= read -r line; do printf '  %s\n' "$line"; done <"$log"
        FAILED=1
    fi
}

printf 'Using SimulIDE: %s\n' "$EXECUTABLE"
run_smoke "esp8266/boot" "resources/data/examples/esp8266/esp8266 Blink.sim2" 2500
run_smoke "esp32/boot" "resources/data/examples/esp32/esp32 Blink.sim2" 2500
run_smoke "esp32-s3/boot" "resources/data/examples/esp32-s3/esp32-s3 Blink.sim2" 2500
run_smoke "esp32-c3/boot" "resources/data/examples/esp32-c3/esp32-c3 Blink.sim2" 2500

if ! command -v curl >/dev/null 2>&1; then
    printf '%s\n' "FAIL ide/wifi: curl is required for HTTP tests"
    FAILED=1
else
    run_http "esp32/wifi" "resources/data/examples/esp32/esp32 WiFi HTTP Hello World.sim2"
    run_http "esp32-s3/wifi" "resources/data/examples/esp32-s3/esp32-s3 WiFi HTTP Hello World.sim2"
    run_http "esp32-c3/wifi" "resources/data/examples/esp32-c3/esp32-c3 WiFi HTTP Hello World.sim2"
fi

exit "$FAILED"
