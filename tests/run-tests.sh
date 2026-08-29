#!/usr/bin/env bash
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
export PYTHONDONTWRITEBYTECODE=1

if [[ "${SIMULIDE_SKIP_TESTS:-0}" == "1" ]]; then
    printf '%s\n' "ESP tests skipped (SIMULIDE_SKIP_TESTS=1)"
    exit 0
fi

printf '%s\n' "== Contract runner self-tests =="
python3 -m unittest discover -s "$ROOT_DIR/tests" -p 'test_*.py' || exit 1
exec python3 "$ROOT_DIR/tests/run_tests.py" "$@"
