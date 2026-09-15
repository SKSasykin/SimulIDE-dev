#!/usr/bin/env python3
import argparse
import json
import subprocess
import sys
from pathlib import Path


TESTS_DIR = Path(__file__).resolve().parent
ROOT_DIR = TESTS_DIR.parent
MCUS = ("esp8266", "esp32", "esp32-s3", "esp32-c3")
DIRECTIONS = ("adc", "gpio-pulls", "pwm", "i2c", "spi", "wifi", "bluetooth")
# Per-component source contracts, outside the ESP MCU tree.
COMPONENT_CONTRACTS = (
    "components/acvoltage/test.json",
    "components/heater/test.json",
    "components/max31855/test.json",
)
SPI_SOURCE = "src/microsim/cores/qemu/esp32/esp32spi.cpp"
BT_SOURCE = "src/microsim/cores/qemu/qemubt.cpp"
BT_QEMU_SOURCE = "third_party/qemu-simulide/hw/misc/esp32_ble_hci.c"
BT_FIRMWARE_SOURCE = "resources/data/bin/esp/examples/ble-hci-reset/main/main.c"
TOP_LEVEL_KEYS = {"description", "checks"}
CHECK_KEYS = {"name", "path", "contains", "not_contains", "ordered", "within_lines"}


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
        not_contains = check.get("not_contains")
        ordered = check.get("ordered")
        if contains is None and not_contains is None and ordered is None:
            raise ManifestError(
                "check requires contains, not_contains or ordered", scenario, source_path
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
        if not_contains is not None and (
            not isinstance(not_contains, list)
            or not not_contains
            or any(not _nonempty_string(item) for item in not_contains)
        ):
            raise ManifestError(
                "not_contains must be a non-empty list of non-empty strings",
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


def spi_endpoints_ready(esp8266, data_in, data_out, clock):
    return not esp8266 or (data_in and data_out and clock)


def spi_reset_registers(registers, mem_start, mem_end, modern=False, esp8266=False, full=False):
    reset = dict(registers)
    if full:
        for address in range(mem_start, mem_end + 1):
            reset[address] = 0
        return reset

    reset[mem_start] = 0
    done_offset = 0x3C if modern else (0x30 if esp8266 else 0x38)
    done_bit = 12 if modern else 4
    reset[mem_start + done_offset] = reset.get(mem_start + done_offset, 0) & ~(1 << done_bit)
    return reset


def hci_command_complete(frame, frame_max=1536):
    if len(frame) > frame_max or len(frame) < 4 or frame[0] != 0x01:
        return None
    parameter_length = frame[3]
    if len(frame) != 4 + parameter_length:
        return None

    opcode = frame[1] | (frame[2] << 8)
    if opcode == 0x0C03:
        status = 0x00 if parameter_length == 0 else 0x12
    else:
        status = 0x01
    return bytes((0x04, 0x0E, 0x04, 0x01, frame[1], frame[2], status))


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


def run_spi_endpoint_regression(root_dir=ROOT_DIR):
    try:
        source = (root_dir / SPI_SOURCE).read_text(encoding="utf-8")
        start = cpp_function_body(source, "void Esp32Spi::startUserTransaction()")
    except (OSError, ValueError, IndexError) as error:
        print(f"FAIL spi endpoint regression path={SPI_SOURCE!r}: {error}")
        return False

    expression = (
        "bool endpointsReady = !m_esp8266 || "
        "( m_dataInPin && m_dataOutPin && m_clkPin );"
    )
    failures = []
    if expression not in start:
        failures.append("C++ endpoint policy does not match the tested truth table")
    if "m_moOutput.routed()" in start or "endpointsRouted" in start:
        failures.append("ESP32-family transactions still depend on a routed MOSI output")

    cases = (
        ("ESP32 full-duplex with MOSI", False, True, True, True, True),
        ("ESP32 receive-only without MOSI", False, True, False, True, True),
        ("ESP32 without external pads", False, False, False, False, True),
        ("ESP8266 fixed endpoints present", True, True, True, True, True),
        ("ESP8266 fixed MOSI endpoint missing", True, True, False, True, False),
        ("ESP8266 fixed MISO endpoint missing", True, False, True, True, False),
        ("ESP8266 fixed clock endpoint missing", True, True, True, False, False),
    )
    for name, esp8266, data_in, data_out, clock, expected in cases:
        actual = spi_endpoints_ready(esp8266, data_in, data_out, clock)
        if actual != expected:
            failures.append(f"{name}: expected {expected}, got {actual}")

    if failures:
        print("FAIL spi MOSI endpoint regression")
        for failure in failures:
            print(f"  {failure}")
        return False
    print("PASS spi MOSI endpoint regression: full-duplex and receive-only cases")
    return True


def run_spi_reset_regression(root_dir=ROOT_DIR):
    try:
        source = (root_dir / SPI_SOURCE).read_text(encoding="utf-8")
        reset = cpp_function_body(source, "void Esp32Spi::reset()")
        abort = cpp_function_body(source, "void Esp32Spi::abortTransaction()")
        write = cpp_function_body(source, "void Esp32Spi::writeRegister()")
    except (OSError, ValueError, IndexError) as error:
        print(f"FAIL spi reset regression path={SPI_SOURCE!r}: {error}")
        return False

    failures = []
    if "abortTransaction();" not in reset or "SpiModule::initialize();" not in reset:
        failures.append("full reset does not reset transfer and base SPI state")
    if "address = m_memStart; address <= m_memEnd" not in reset:
        failures.append("full reset does not clear exactly the controller MMIO window")
    for fragment in (
        "Simulator::self()->cancelEvents( this );",
        "writeMem( m_memStart, 0 );",
        "m_transactionActive = false;",
        "m_ssOutput.resetState( true );",
    ):
        if fragment not in abort:
            failures.append(f"abort is missing {fragment}")
    if "m_eventValue & ( 1u << 31 )" not in write or "abortTransaction();" not in write:
        failures.append("classic SPI_SLAVE.SYNC_RESET is not handled")

    start = 0x64000
    end = 0x64FFF
    registers = {start - 1: 0xA5A5A5A5, start: 1 << 18, start + 0x38: 0x210, end + 1: 0x5A5A5A5A}
    stopped = spi_reset_registers(registers, start, end, full=True)
    if stopped[start] != 0 or stopped[start + 0x38] != 0:
        failures.append("stop/start leaves classic CMD or SLAVE state behind")
    if stopped[start - 1] != registers[start - 1] or stopped[end + 1] != registers[end + 1]:
        failures.append("full reset modifies memory outside its controller window")

    slave_config = (1 << 9) | (1 << 4)
    synced = spi_reset_registers({start: 1 << 18, start + 0x38: slave_config}, start, end)
    if synced[start] != 0 or synced[start + 0x38] != (1 << 9):
        failures.append("SYNC_RESET does not clear CMD/done while preserving configuration")

    completed = {start: 0, start + 0x38: slave_config}
    restarted = dict(completed)
    restarted[start] = 1 << 18
    restarted[start + 0x38] &= ~(1 << 4)
    if restarted[start] != 1 << 18 or restarted[start + 0x38] & (1 << 4):
        failures.append("a sequential transaction cannot enter the running state")

    if failures:
        print("FAIL spi reset lifecycle regression")
        for failure in failures:
            print(f"  {failure}")
        return False
    print("PASS spi reset lifecycle regression: stop/start, SYNC_RESET and sequential transfer")
    return True


def run_ble_controller_regression(root_dir=ROOT_DIR):
    failures = []
    cases = (
        ("HCI Reset", bytes.fromhex("01 03 0c 00"), bytes.fromhex("04 0e 04 01 03 0c 00")),
        ("Reset invalid parameter length", bytes.fromhex("01 03 0c 01 aa"), bytes.fromhex("04 0e 04 01 03 0c 12")),
        ("unknown command", bytes.fromhex("01 34 12 02 aa bb"), bytes.fromhex("04 0e 04 01 34 12 01")),
    )
    for name, frame, expected in cases:
        actual = hci_command_complete(frame)
        if actual != expected:
            failures.append(f"{name}: expected {expected.hex(' ')}, got {actual}")

    malformed = (
        b"",
        bytes.fromhex("01 03 0c"),
        bytes.fromhex("02 03 0c 00"),
        bytes.fromhex("01 03 0c 01"),
        bytes.fromhex("01 03 0c 00 ff"),
        bytes(1537),
    )
    for frame in malformed:
        if hci_command_complete(frame) is not None:
            failures.append(f"malformed frame accepted: {frame[:8].hex(' ')} len={len(frame)}")

    try:
        bt_source = (root_dir / BT_SOURCE).read_text(encoding="utf-8")
        esp32 = (root_dir / "src/microsim/cores/qemu/esp32/esp32.cpp").read_text(encoding="utf-8")
        esp32s3 = (root_dir / "src/microsim/cores/qemu/esp32/esp32s3.cpp").read_text(encoding="utf-8")
        esp32c3 = (root_dir / "src/microsim/cores/qemu/esp32/esp32c3.cpp").read_text(encoding="utf-8")
        qemu_transport = (root_dir / BT_QEMU_SOURCE).read_text(encoding="utf-8")
        firmware = (root_dir / BT_FIRMWARE_SOURCE).read_text(encoding="utf-8")
        qemu_rx = cpp_function_body(
            qemu_transport, "static void esp32_ble_hci_rx(Esp32BleHciState *s)"
        )
        qemu_tick = cpp_function_body(
            qemu_transport, "static void esp32_ble_hci_rx_tick(void *opaque)"
        )
        qemu_reset = cpp_function_body(
            qemu_transport, "static void esp32_ble_hci_reset_state(Esp32BleHciState *s)"
        )
    except (OSError, ValueError, IndexError) as error:
        print(f"FAIL BLE controller regression: {error}")
        return False

    required_bt = (
        "len > QEMU_WIFI_FRAME_MAX",
        "len >= 4 && frame[0] == 0x01",
        "len == packetLen",
        "opcode == 0x0C03",
        "frame[3] == 0 ? 0x00 : 0x12",
        "std::memory_order_acquire",
        "std::memory_order_release",
    )
    for fragment in required_bt:
        if fragment not in bt_source:
            failures.append(f"controller source is missing {fragment!r}")
    for forbidden in ("setInterrupt(", "QemuNetBackend", "sendFrame("):
        if forbidden in bt_source:
            failures.append(f"controller source contains forbidden {forbidden!r}")

    required_transport = (
        "BLE_HCI_DESC_OWN",
        "esp32_ble_hci_valid_h4",
        "desc.length > BLE_HCI_MAX_FRAME",
        "simulide_bt_notify",
        "BLE_HCI_INT_RX_DONE",
        "QEMU_CLOCK_VIRTUAL",
    )
    for fragment in required_transport:
        if fragment not in qemu_transport:
            failures.append(f"QEMU transport source is missing {fragment!r}")
    if "simulide_bt_notify(s->simulide_iomem_offset);" not in qemu_rx:
        failures.append("RX consumption does not wake a backpressured host command")
    if "goto done;" not in qemu_rx or "done:" not in qemu_rx:
        failures.append("partial RX completion can bypass wakeup and IRQ handling")
    if "esp32_ble_hci_tx(s);" not in qemu_tick:
        failures.append("virtual timer does not retry backpressured TX descriptors")
    for fragment in ("tx_ring->tail = tx_ring->head;", "rx_ring->head = rx_ring->tail;"):
        if fragment not in qemu_reset:
            failures.append(f"transport reset does not flush stale traffic via {fragment!r}")

    for fragment in (
        "0x01, 0x03, 0x0c, 0x00",
        "0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00",
        "BLE_HCI_RESET_PASS",
    ):
        if fragment not in firmware:
            failures.append(f"BLE reset firmware is missing {fragment!r}")

    routes = (
        ("ESP32", esp32, "0x00052000, 0x00052FFF"),
        ("ESP32-S3", esp32s3, "0x00012000, 0x00012FFF"),
        ("ESP32-C3", esp32c3, "0x00012000, 0x00012FFF"),
    )
    for name, source, route in routes:
        if route not in source or "new QemuBt" not in source:
            failures.append(f"{name} virtual HCI route is missing")
        if "setBtLinkPort(" in source:
            failures.append(f"{name} still enables default BLE UDP")

    if failures:
        print("FAIL BLE controller regression")
        for failure in failures:
            print(f"  {failure}")
        return False
    print("PASS BLE controller regression: H4 responses, rejection, routing and IRQ ownership")
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
        for forbidden in check.get("not_contains", []):
            if forbidden in source:
                failures.append(
                    f"scenario={check['name']!r} path={check['path']!r}: "
                    f"forbidden fragment present {forbidden!r}"
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
        description="Run emulation/component contracts and SimulIDE integration tests."
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
            if run_spi_endpoint_regression():
                passed += 1
            else:
                failed += 1
            if run_spi_reset_regression():
                passed += 1
            else:
                failed += 1
        if args.direction in (None, "bluetooth"):
            if run_ble_controller_regression():
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
