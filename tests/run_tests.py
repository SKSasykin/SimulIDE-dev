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
# Per-component source contracts, outside the ESP MCU tree.
COMPONENT_CONTRACTS = (
    "components/max31855/test.json",
)
SPI_SOURCE = "src/microsim/cores/qemu/esp32/esp32spi.cpp"
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


def spi_clock_divider(value, modern):
    if value & 0x80000000:
        return 1
    pre = (value >> 18) & (0xF if modern else 0x1FFF)
    n = (value >> 12) & 0x3F
    return (pre + 1) * (n + 1)


def spi_clock_register(pre, n):
    return ((pre - 1) << 18) | ((n - 1) << 12)


def cpp_function_body(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
        cursor += 1
    return source[opening + 1 : cursor - 1]


def run_spi_clock_regression(root_dir=ROOT_DIR):
    failures = []
    try:
        source = (root_dir / SPI_SOURCE).read_text(encoding="utf-8")
        configure = cpp_function_body(source, "void Esp32Spi::configureClock()")
        start = cpp_function_body(source, "void Esp32Spi::startUserTransaction()")
    except (OSError, ValueError, IndexError) as error:
        print(f"FAIL spi clock regression path={SPI_SOURCE!r}: {error}")
        return False

    if "m_modern ? 0x0C : 0x18" not in configure:
        failures.append("variant SPI_CLOCK offsets are missing")
    if "readMem( m_memStart + clockOffset )" not in configure:
        failures.append("configureClock does not read the saved SPI_CLOCK register")
    if "m_eventValue" in configure:
        failures.append("configureClock still uses the current event value")
    if "configureClock();" not in start:
        failures.append("transaction start no longer reapplies the saved clock")

    variants = (
        ("ESP32", False, 1 << 18),
        ("ESP8266", False, 1 << 18),
        ("ESP32-S3", True, 1 << 24),
        ("ESP32-C3", True, 1 << 24),
    )
    for variant, modern, command in variants:
        clock_value = spi_clock_register(5, 16)
        configured = spi_clock_divider(clock_value, modern)
        if configured != 80 or spi_clock_divider(command, modern) == configured:
            failures.append(f"{variant} SPI_CLOCK/SPI_CMD regression setup is invalid")

    frequencies = (
        (500_000, 5, 16, 80, 1_000_000),
        (10_000, 125, 32, 4_000, 50_000_000),
        (1_000, 625, 64, 40_000, 500_000_000),
        (100, 6_250, 64, 400_000, 5_000_000_000),
    )
    for requested, pre, n, expected_divider, expected_half_period in frequencies:
        divider = spi_clock_divider(spi_clock_register(pre, n), False)
        half_period = divider * 1_000_000_000_000 // 40_000_000 // 2
        if divider != expected_divider or half_period != expected_half_period:
            failures.append(f"legacy {requested} Hz clock period is incorrect")

    legacy_max = spi_clock_divider(spi_clock_register(8_192, 64), False)
    modern_max = spi_clock_divider(spi_clock_register(16, 64), True)
    if legacy_max != 524_288 or 80_000_000 / legacy_max <= 100:
        failures.append("legacy 100 Hz hardware limit is incorrect")
    if modern_max != 1_024 or 80_000_000 / modern_max != 78_125:
        failures.append("modern minimum SPI clock is incorrect")

    if failures:
        print("FAIL spi clock frequency regression")
        for failure in failures:
            print(f"  {failure}")
        return False
    print("PASS spi clock frequency regression: saved divider and frequency changes")
    return True


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
        for relative in COMPONENT_CONTRACTS:
            manifest = (TESTS_DIR / relative).resolve()
            if not manifest.is_file():
                print(f"FAIL {relative}: missing test.json")
                failed += 1
            elif run_contract(manifest):
                passed += 1
            else:
                failed += 1
        if args.direction in (None, "spi"):
            if run_spi_clock_regression():
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
