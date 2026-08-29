#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


TESTS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TESTS_DIR.parent
MCUS = ("esp8266", "esp32", "esp32-s3", "esp32-c3")
DIRECTIONS = ("adc", "gpio-pulls", "pwm", "i2c", "spi", "wifi")
TOP_LEVEL_KEYS = {"description", "checks"}
CHECK_KEYS = {"name", "path", "contains", "ordered", "within_lines"}


class ManifestError(ValueError):
    def __init__(self, message, scenario="<manifest>", source_path="<manifest>"):
        super().__init__(message)
        self.scenario = scenario
        self.source_path = source_path


def _nonempty_string(value):
    return isinstance(value, str) and bool(value.strip())


def validate_manifest(manifest):
    if not isinstance(manifest, dict):
        raise ManifestError("top level must be an object")
    unknown = set(manifest) - TOP_LEVEL_KEYS
    if unknown:
        raise ManifestError(f"unknown top-level key(s): {', '.join(sorted(unknown))}")
    if not _nonempty_string(manifest.get("description")):
        raise ManifestError("description must be a non-empty string")
    checks = manifest.get("checks")
    if not isinstance(checks, list) or not checks:
        raise ManifestError("checks must be a non-empty list")

    for index, check in enumerate(checks):
        fallback = f"checks[{index}]"
        if not isinstance(check, dict):
            raise ManifestError("check must be an object", fallback)
        scenario = check.get("name", fallback)
        source_path = check.get("path", "<missing>")
        unknown = set(check) - CHECK_KEYS
        if unknown:
            raise ManifestError(
                f"unknown check key(s): {', '.join(sorted(unknown))}",
                scenario,
                source_path,
            )
        if not _nonempty_string(check.get("name")):
            raise ManifestError("name must be a non-empty string", fallback, source_path)
        if not _nonempty_string(source_path):
            raise ManifestError("path must be a non-empty string", scenario, "<missing>")
        path = Path(source_path)
        if path.is_absolute() or ".." in path.parts:
            raise ManifestError("path must stay inside the repository", scenario, source_path)

        contains = check.get("contains")
        ordered = check.get("ordered")
        if contains is None and ordered is None:
            raise ManifestError(
                "check requires contains or ordered", scenario, source_path
            )
        if contains is not None and (
            not isinstance(contains, list)
            or not contains
            or any(not _nonempty_string(item) for item in contains)
        ):
            raise ManifestError(
                "contains must be a non-empty list of non-empty strings",
                scenario,
                source_path,
            )
        if ordered is not None and (
            not isinstance(ordered, list)
            or len(ordered) < 2
            or any(not _nonempty_string(item) for item in ordered)
        ):
            raise ManifestError(
                "ordered must contain at least two non-empty strings",
                scenario,
                source_path,
            )
        within_lines = check.get("within_lines")
        if within_lines is not None:
            if ordered is None:
                raise ManifestError(
                    "within_lines requires ordered", scenario, source_path
                )
            if (
                isinstance(within_lines, bool)
                or not isinstance(within_lines, int)
                or within_lines <= 0
            ):
                raise ManifestError(
                    "within_lines must be a positive integer", scenario, source_path
                )


def load_manifest(manifest_path):
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ManifestError(
            f"invalid JSON at line {error.lineno}, column {error.colno}: {error.msg}"
        ) from error
    except OSError as error:
        raise ManifestError(f"cannot read manifest: {error}") from error
    validate_manifest(manifest)
    return manifest


def find_ordered(source, fragments, within_lines=None):
    first = fragments[0]
    search_from = 0
    smallest_span = None
    while True:
        first_pos = source.find(first, search_from)
        if first_pos < 0:
            break
        cursor = first_pos + len(first)
        complete = True
        for fragment in fragments[1:]:
            position = source.find(fragment, cursor)
            if position < 0:
                complete = False
                break
            cursor = position + len(fragment)
        if complete:
            span = source.count("\n", first_pos, cursor) + 1
            if within_lines is None or span <= within_lines:
                return True, None
            smallest_span = span if smallest_span is None else min(smallest_span, span)
        search_from = first_pos + 1
    if smallest_span is not None:
        return False, (
            f"ordered fragments require {smallest_span} lines; limit is {within_lines}"
        )
    return False, "ordered fragments were not found in the required order"


def run_contract(manifest_path, root_dir=ROOT_DIR, tests_dir=TESTS_DIR):
    relative = manifest_path.relative_to(tests_dir)
    try:
        manifest = load_manifest(manifest_path)
    except ManifestError as error:
        print(
            f"FAIL {relative.parent} scenario={error.scenario!r} "
            f"path={error.source_path!r}: {error}"
        )
        return False
    failures = []
    for check in manifest["checks"]:
        source_path = (root_dir / check["path"]).resolve()
        root = root_dir.resolve()
        if root != source_path and root not in source_path.parents:
            failures.append(
                f"scenario={check['name']!r} path={check['path']!r}: "
                "resolved path leaves the repository"
            )
            continue
        if not source_path.is_file():
            failures.append(
                f"scenario={check['name']!r} path={check['path']!r}: missing source"
            )
            continue
        source = source_path.read_text(encoding="utf-8", errors="replace")
        for expected in check.get("contains", []):
            if expected not in source:
                failures.append(
                    f"scenario={check['name']!r} path={check['path']!r}: "
                    f"missing fragment {expected!r}"
                )
        if "ordered" in check:
            found, detail = find_ordered(
                source, check["ordered"], check.get("within_lines")
            )
            if not found:
                failures.append(
                    f"scenario={check['name']!r} path={check['path']!r}: {detail}"
                )
    if failures:
        print(f"FAIL {relative.parent} negative source contract")
        for failure in failures:
            print(f"  {failure}")
        return False
    negative = sum(
        check["name"].startswith("negative source contract:")
        for check in manifest["checks"]
    )
    positive = len(manifest["checks"]) - negative
    print(
        f"PASS {relative.parent} source contract: {manifest['description']} "
        f"({positive} positive, {negative} negative)"
    )
    return True


def main():
    parser = argparse.ArgumentParser(
        description="Run ESP emulation contracts and SimulIDE integration tests."
    )
    parser.add_argument("mcu", nargs="?", choices=MCUS)
    parser.add_argument("direction", nargs="?", choices=DIRECTIONS)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--contracts-only", action="store_true")
    mode.add_argument("--ide-only", action="store_true")
    parser.add_argument(
        "--executable", help="SimulIDE executable used by tests/ide/run-smoke-tests.sh"
    )
    args = parser.parse_args()

    passed = 0
    failed = 0
    if not args.ide_only:
        mcus = (args.mcu,) if args.mcu else MCUS
        directions = (args.direction,) if args.direction else DIRECTIONS
        print("== ESP emulation contracts ==")
        for mcu in mcus:
            for direction in directions:
                manifest = TESTS_DIR / mcu / direction / "test.json"
                if not manifest.is_file():
                    print(f"FAIL {mcu}/{direction}: missing test.json")
                    failed += 1
                elif run_contract(manifest):
                    passed += 1
                else:
                    failed += 1

    if not args.contracts_only and not args.mcu and not args.direction:
        print("== SimulIDE integration tests ==")
        command = [str(TESTS_DIR / "ide" / "run-smoke-tests.sh")]
        if args.executable:
            command.append(args.executable)
        result = subprocess.run(command, cwd=ROOT_DIR, check=False)
        if result.returncode == 0:
            passed += 1
        else:
            failed += 1

    print(f"== Result: {passed} passed, {failed} failed ==")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
