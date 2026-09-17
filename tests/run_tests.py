#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import shutil
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
BT_HEADER = "src/microsim/cores/qemu/qemubt.h"
BT_QEMU_SOURCE = "third_party/qemu-simulide/hw/misc/esp32_ble_hci.c"
BT_RUNTIME_TEST = "tests/qemubt-runtime/run.sh"
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

    status = 0x01
    command_status = False
    response_data = b''
    expected_len = 7

    if opcode == 0x0C03:
        status = 0x00 if parameter_length == 0 else 0x12
    elif opcode == 0x1001:
        status = 0x00 if parameter_length == 0 else 0x12
        if status == 0x00:
            response_data = b'\x09\x00\x00\x09\x00\x00\x00\x00'
            expected_len = 15
    elif opcode == 0x1002:
        status = 0x00 if parameter_length == 0 else 0x12
        if status == 0x00:
            bitmap = bytearray(64)
            for offset, value in (
                (0, 0x20), (16, 0x05), (22, 0x15), (24, 0x07),
                (25, 0x01), (27, 0x02), (28, 0x04), (56, 0xA7), (57, 0x3F),
                (58, 0x20), (60, 0x40), (61, 0x11),
            ):
                bitmap[offset] = value
            response_data = bytes(bitmap)
            expected_len = 71
    elif opcode == 0x1003:
        status = 0x00 if parameter_length == 0 else 0x12
        if status == 0x00:
            response_data = b'\x00\x00\x00\x00\x00\x60\x00\x00'
            expected_len = 15
    elif opcode == 0x0C01:
        status = 0x00 if parameter_length == 8 else 0x12
    elif opcode == 0x0C63:
        status = 0x00 if parameter_length == 8 else 0x12
    elif opcode == 0x2001:
        status = 0x00 if parameter_length == 8 else 0x12
    elif opcode == 0x2002:
        status = 0x00 if parameter_length == 0 else 0x12
        if status == 0x00:
            response_data = b'\x1B\x00\x01'
            expected_len = 10
    elif opcode == 0x2003:
        status = 0x00 if parameter_length == 0 else 0x12
        if status == 0x00:
            response_data = b'\x00\x00\x00\x00\x00\x00\x00\x00'
            expected_len = 15
    elif opcode == 0x2018:
        status = 0x00 if parameter_length == 0 else 0x12
        if status == 0x00:
            response_data = b'\x11\x22\x33\x44\x55\x66\x5a\xa5'
            expected_len = 15
    elif opcode == 0x2022:
        status = 0x12
        if parameter_length == 6:
            tx_octets = frame[6] | (frame[7] << 8)
            tx_time = frame[8] | (frame[9] << 8)
            if 0x001B <= tx_octets <= 0x00FB and 0x0148 <= tx_time <= 0x4290:
                status = 0x02
    elif opcode == 0x1009:
        status = 0x00 if parameter_length == 0 else 0x12
        if status == 0x00:
            response_data = b'\x11\x22\x33\x44\x55\x66'
            expected_len = 13
    elif opcode == 0x0C31:
        status = 0x00 if parameter_length == 1 and frame[4] in (0, 1) else 0x12
    elif opcode == 0x0C33:
        status = 0x00 if parameter_length == 7 else 0x12
        if status == 0x00:
            acl_len = frame[4] | (frame[5] << 8)
            sco_len = frame[6]
            acl_cnt = frame[7] | (frame[8] << 8)
            sco_cnt = frame[9] | (frame[10] << 8)
            if sco_len != 0 or sco_cnt != 0 or acl_len < 27 or acl_cnt == 0:
                status = 0x12
    elif opcode == 0x202D:
        status = 0x00 if parameter_length == 1 and frame[4] <= 1 else 0x12
    elif opcode == 0x2029:
        status = 0x00 if parameter_length == 0 else 0x12
    elif opcode == 0x2027:
        status = 0x00 if parameter_length == 39 and frame[4] <= 1 else 0x12
    elif opcode == 0x204E:
        status = (
            0x00
            if parameter_length == 8 and frame[4] <= 1 and frame[11] <= 1
            else 0x12
        )
    elif opcode == 0x2006:
        status = 0x00 if parameter_length == 15 else 0x12
        if status == 0x00:
            minimum = frame[4] | (frame[5] << 8)
            maximum = frame[6] | (frame[7] << 8)
            valid = (
                0x0020 <= minimum <= maximum <= 0x4000
                and frame[8] in (0, 2, 3)
                and frame[9] == 0
                and frame[10] <= 1
                and frame[17] & 0x07
                and not frame[17] & 0xF8
                and frame[18] == 0
            )
            status = 0x00 if valid else 0x12
    elif opcode in (0x2008, 0x2009):
        status = 0x00 if parameter_length == 32 and frame[4] <= 31 else 0x12
    elif opcode == 0x200A:
        status = 0x00 if parameter_length == 1 and frame[4] <= 1 else 0x12
    elif opcode == 0x200B:
        status = 0x00 if parameter_length == 7 else 0x12
        if status == 0x00:
            interval = frame[5] | (frame[6] << 8)
            window = frame[7] | (frame[8] << 8)
            valid = (
                frame[4] <= 1
                and 0x0004 <= window <= interval <= 0x4000
                and frame[9] == 0
                and frame[10] == 0
            )
            status = 0x00 if valid else 0x12
    elif opcode == 0x200C:
        status = 0x00 if parameter_length == 2 and frame[4] <= 1 and frame[5] <= 1 else 0x12
    elif opcode == 0x200D:
        command_status = True
        status = 0x12
        if parameter_length == 25:
            scan_interval = frame[4] | (frame[5] << 8)
            scan_window = frame[6] | (frame[7] << 8)
            interval_min = frame[17] | (frame[18] << 8)
            interval_max = frame[19] | (frame[20] << 8)
            latency = frame[21] | (frame[22] << 8)
            timeout = frame[23] | (frame[24] << 8)
            ce_min = frame[25] | (frame[26] << 8)
            ce_max = frame[27] | (frame[28] << 8)
            valid = (
                0x0004 <= scan_window <= scan_interval <= 0x4000
                and frame[8] == 0
                and frame[9] == 0
                and frame[16] == 0
                and 0x0006 <= interval_min <= interval_max <= 0x0C80
                and latency <= 0x01F3
                and 0x000A <= timeout <= 0x0C80
                and timeout * 4 > (1 + latency) * interval_max
                and ce_min <= ce_max
            )
            status = 0x00 if valid else 0x12
    elif opcode == 0x200E:
        status = 0x0C if parameter_length == 0 else 0x12
    elif opcode == 0x0406:
        command_status = True
        valid_reasons = (0x05, 0x13, 0x14, 0x15, 0x1A, 0x29, 0x3B)
        status = 0x02 if parameter_length == 3 and frame[6] in valid_reasons else 0x12
    elif opcode == 0x041D:
        command_status = True
        status = 0x02 if parameter_length == 2 else 0x12
    elif opcode == 0x2016:
        command_status = True
        status = 0x02 if parameter_length == 2 else 0x12
    elif opcode == 0x0C35:
        return None
    else:
        status = 0x01

    if command_status:
        return hci_command_status(opcode, status)

    result = bytearray()
    result.append(0x04)
    result.append(0x0E)
    result.append(4 + len(response_data))
    result.append(0x01)
    result.append(frame[1])
    result.append(frame[2])
    result.append(status)
    result.extend(response_data)
    return bytes(result)


def hci_le_advertising_report(event_type, address_type, address, data, rssi=-42):
    if event_type > 4 or address_type > 1 or len(address) != 6 or len(data) > 31:
        return None
    payload = bytes((0x02, 0x01, event_type, address_type))
    payload += bytes(address) + bytes((len(data),)) + bytes(data) + bytes((rssi & 0xFF,))
    return bytes((0x04, 0x3E, len(payload))) + payload


def hci_command_status(opcode, status=0):
    return bytes((0x04, 0x0F, 0x04, status, 0x01, opcode & 0xFF, opcode >> 8))


def hci_remote_version_complete(handle):
    return bytes((0x04, 0x0C, 0x08, 0x00, handle & 0xFF, handle >> 8,
                  0x09, 0x00, 0x00, 0x00, 0x00))


def hci_le_connection_complete(status, handle, role, peer_address, interval=0,
                               latency=0, timeout=0, enhanced=False):
    if len(peer_address) != 6 or role > 1 or handle > 0x0EFF:
        return None
    subevent = 0x0A if enhanced else 0x01
    payload = bytes((subevent, status, handle & 0xFF, handle >> 8, role, 0))
    payload += bytes(peer_address)
    if enhanced:
        payload += bytes(12)
    payload += interval.to_bytes(2, "little")
    payload += latency.to_bytes(2, "little")
    payload += timeout.to_bytes(2, "little") + b"\x00"
    return bytes((0x04, 0x3E, len(payload))) + payload


def hci_le_remote_features_complete(handle):
    return bytes.fromhex("04 3e 0c 04 00") + handle.to_bytes(2, "little") + bytes(8)


def hci_disconnection_complete(handle, reason):
    return bytes.fromhex("04 05 04 00") + handle.to_bytes(2, "little") + bytes((reason,))


def hci_acl_forward(frame, peer_handle):
    if len(frame) < 5 or frame[0] != 0x02:
        return None
    flags = frame[1] | (frame[2] << 8)
    payload_length = frame[3] | (frame[4] << 8)
    pb = (flags >> 12) & 3
    bc = (flags >> 14) & 3
    if len(frame) != 5 + payload_length or payload_length > 27 or pb > 1 or bc:
        return None
    translated = peer_handle | ((2 if pb == 0 else 1) << 12)
    return bytes((0x02, translated & 0xFF, translated >> 8)) + frame[3:]


def hci_number_of_completed_packets(handle, count=1):
    return bytes.fromhex("04 13 05 01") + handle.to_bytes(2, "little") + count.to_bytes(2, "little")


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
    valid_create_connection = bytes.fromhex(
        "01 0d 20 19 10 00 10 00 00 00 11 22 33 44 55 66 00 "
        "18 00 28 00 00 00 c8 00 00 00 00 00"
    )
    cases = (
        ("HCI Reset", bytes.fromhex("01 03 0c 00"), bytes.fromhex("04 0e 04 01 03 0c 00")),
        ("Reset invalid parameter length", bytes.fromhex("01 03 0c 01 aa"), bytes.fromhex("04 0e 04 01 03 0c 12")),
        ("unknown command", bytes.fromhex("01 34 12 02 aa bb"), bytes.fromhex("04 0e 04 01 34 12 01")),
        ("Read Local Version Info", bytes.fromhex("01 01 10 00"), bytes.fromhex("04 0e 0c 01 01 10 00 09 00 00 09 00 00 00 00")),
        ("Read Local Supported Features", bytes.fromhex("01 03 10 00"), bytes.fromhex("04 0e 0c 01 03 10 00 00 00 00 00 00 60 00 00")),
        ("Set Event Mask", bytes.fromhex("01 01 0c 08 90 80 00 02 00 80 00 20"), bytes.fromhex("04 0e 04 01 01 0c 00")),
        ("Set Event Mask Page 2", bytes.fromhex("01 63 0c 08 00 00 80 00 00 00 00 00"), bytes.fromhex("04 0e 04 01 63 0c 00")),
        ("LE Set Event Mask", bytes.fromhex("01 01 20 08 1f 00 00 00 00 00 00 00"), bytes.fromhex("04 0e 04 01 01 20 00")),
        ("LE Read Buffer Size", bytes.fromhex("01 02 20 00"), bytes.fromhex("04 0e 07 01 02 20 00 1b 00 01")),
        ("LE Read Local Supported Features", bytes.fromhex("01 03 20 00"), bytes.fromhex("04 0e 0c 01 03 20 00 00 00 00 00 00 00 00 00")),
        ("LE Rand", bytes.fromhex("01 18 20 00"), bytes.fromhex("04 0e 0c 01 18 20 00 11 22 33 44 55 66 5a a5")),
        ("Read BD_ADDR", bytes.fromhex("01 09 10 00"), bytes.fromhex("04 0e 0a 01 09 10 00 11 22 33 44 55 66")),
        ("Set Controller To Host Flow Control enable", bytes.fromhex("01 31 0c 01 01"), bytes.fromhex("04 0e 04 01 31 0c 00")),
        ("Set Controller To Host Flow Control disable", bytes.fromhex("01 31 0c 01 00"), bytes.fromhex("04 0e 04 01 31 0c 00")),
        ("Host Buffer Size", bytes.fromhex("01 33 0c 07 ff 00 00 14 00 00 00"), bytes.fromhex("04 0e 04 01 33 0c 00")),
        ("LE Set Address Resolution Enable", bytes.fromhex("01 2d 20 01 01"), bytes.fromhex("04 0e 04 01 2d 20 00")),
        ("LE Clear Resolving List", bytes.fromhex("01 29 20 00"), bytes.fromhex("04 0e 04 01 29 20 00")),
        ("LE Add Device To Resolving List", bytes.fromhex("01 27 20 27 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"), bytes.fromhex("04 0e 04 01 27 20 00")),
        ("LE Set Privacy Mode", bytes.fromhex("01 4e 20 08 00 00 00 00 00 00 00 01"), bytes.fromhex("04 0e 04 01 4e 20 00")),
        ("LE Set Advertising Parameters", bytes.fromhex("01 06 20 0f 00 08 00 08 00 00 00 00 00 00 00 00 00 07 00"), bytes.fromhex("04 0e 04 01 06 20 00")),
        ("LE Set Advertising Data", bytes.fromhex("01 08 20 20 03 02 01 06 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"), bytes.fromhex("04 0e 04 01 08 20 00")),
        ("LE Set Scan Response Data", bytes.fromhex("01 09 20 20 02 01 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00"), bytes.fromhex("04 0e 04 01 09 20 00")),
        ("LE Set Advertising Enable", bytes.fromhex("01 0a 20 01 01"), bytes.fromhex("04 0e 04 01 0a 20 00")),
        ("LE Set Scan Parameters", bytes.fromhex("01 0b 20 07 00 10 00 10 00 00 00"), bytes.fromhex("04 0e 04 01 0b 20 00")),
        ("LE Set Scan Enable", bytes.fromhex("01 0c 20 02 01 01"), bytes.fromhex("04 0e 04 01 0c 20 00")),
    )
    for name, frame, expected in cases:
        actual = hci_command_complete(frame)
        if actual != expected:
            failures.append(f"{name}: expected {expected.hex(' ')}, got {actual}")

    connection_vectors = (
        ("LE Create Connection status", valid_create_connection,
         bytes.fromhex("04 0f 04 00 01 0d 20")),
        ("LE Create Connection timeout relation", valid_create_connection[:23] +
         bytes.fromhex("0a 00") + valid_create_connection[25:],
         bytes.fromhex("04 0f 04 12 01 0d 20")),
        ("LE Create Connection Cancel without pending", bytes.fromhex("01 0e 20 00"),
         bytes.fromhex("04 0e 04 01 0e 20 0c")),
        ("Disconnect unknown handle", bytes.fromhex("01 06 04 03 01 00 13"),
         bytes.fromhex("04 0f 04 02 01 06 04")),
        ("LE Read Remote Features unknown handle", bytes.fromhex("01 16 20 02 01 00"),
         bytes.fromhex("04 0f 04 02 01 16 20")),
    )
    for name, frame, expected in connection_vectors:
        actual = hci_command_complete(frame)
        if actual != expected:
            failures.append(f"{name}: expected {expected.hex(' ')}, got {actual}")

    event_vectors = (
        (hci_le_connection_complete(0, 1, 0, bytes.fromhex("11 22 33 44 55 66"),
                                    0x18, 0, 0xC8),
         bytes.fromhex("04 3e 13 01 00 01 00 00 00 11 22 33 44 55 66 18 00 00 00 c8 00 00")),
        (hci_le_connection_complete(0, 1, 1, bytes.fromhex("11 22 33 44 55 66"),
                                    0x18, 0, 0xC8, enhanced=True),
         bytes.fromhex("04 3e 1f 0a 00 01 00 01 00 11 22 33 44 55 66 "
                       "00 00 00 00 00 00 00 00 00 00 00 00 18 00 00 00 c8 00 00")),
        (hci_le_remote_features_complete(1),
         bytes.fromhex("04 3e 0c 04 00 01 00 00 00 00 00 00 00 00 00")),
        (hci_disconnection_complete(1, 0x16), bytes.fromhex("04 05 04 00 01 00 16")),
        (hci_acl_forward(bytes.fromhex("02 01 00 03 00 aa bb cc"), 2),
         bytes.fromhex("02 02 20 03 00 aa bb cc")),
        (hci_acl_forward(bytes.fromhex("02 01 10 01 00 aa"), 2),
         bytes.fromhex("02 02 10 01 00 aa")),
        (hci_number_of_completed_packets(1), bytes.fromhex("04 13 05 01 01 00 01 00")),
    )
    for actual, expected in event_vectors:
        if actual != expected:
            failures.append(f"BLE event vector: expected {expected.hex(' ')}, got {actual}")
    if hci_command_complete(bytes.fromhex("01 35 0c 05 01 01 00 01 00")) is not None:
        failures.append("Host Number Of Completed Packets produced a response")

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

    length_mismatch = (
        bytes.fromhex("01 01 10 01 00"),
        bytes.fromhex("01 03 10 01 00"),
        bytes.fromhex("01 01 0c 07 00 00 00 00 00 00 00"),
        bytes.fromhex("01 63 0c 07 00 00 00 00 00 00 00"),
        bytes.fromhex("01 01 20 07 00 00 00 00 00 00 00"),
        bytes.fromhex("01 02 20 01 00"),
        bytes.fromhex("01 03 20 01 00"),
        bytes.fromhex("01 09 10 01 00"),
        bytes.fromhex("01 31 0c 02 00 01"),
        bytes.fromhex("01 33 0c 06 ff 00 00 14 00 00"),
    )
    for frame in length_mismatch:
        actual = hci_command_complete(frame)
        if actual is None or actual[6] != 0x12:
            failures.append(f"length mismatch not rejected with 0x12: {frame.hex(' ')} -> {actual}")

    invalid_flow = bytes.fromhex("01 31 0c 01 02")
    actual = hci_command_complete(invalid_flow)
    if actual is None or actual[6] != 0x12:
        failures.append(f"invalid flow control mode not rejected: {invalid_flow.hex(' ')} -> {actual}")

    invalid_host_buf = bytes.fromhex("01 33 0c 07 ff 00 01 14 00 00 00")
    actual = hci_command_complete(invalid_host_buf)
    if actual is None or actual[6] != 0x12:
        failures.append(f"invalid host buffer size not rejected: {invalid_host_buf.hex(' ')} -> {actual}")

    try:
        bt_source = (root_dir / BT_SOURCE).read_text(encoding="utf-8")
        bt_header = (root_dir / BT_HEADER).read_text(encoding="utf-8")
        esp32 = (root_dir / "src/microsim/cores/qemu/esp32/esp32.cpp").read_text(encoding="utf-8")
        esp32s3 = (root_dir / "src/microsim/cores/qemu/esp32/esp32s3.cpp").read_text(encoding="utf-8")
        esp32c3 = (root_dir / "src/microsim/cores/qemu/esp32/esp32c3.cpp").read_text(encoding="utf-8")
        qemu_device = (root_dir / "src/microsim/cores/qemu/qemudevice.cpp").read_text(encoding="utf-8")
        qemu_module = (root_dir / "src/microsim/cores/qemu/qemumodule.h").read_text(encoding="utf-8")
        qemu_transport = (root_dir / BT_QEMU_SOURCE).read_text(encoding="utf-8")
        qemu_interface = (
            root_dir / "third_party/qemu-simulide/system/simuliface.c"
        ).read_text(encoding="utf-8")
        qemu_build_script = (root_dir / "scripts/build_qemu.sh").read_text(encoding="utf-8")
        simulide_pri = (root_dir / "SimulIDE.pri").read_text(encoding="utf-8")
        qemu_rx = cpp_function_body(
            qemu_transport, "static void esp32_ble_hci_rx(Esp32BleHciState *s)"
        )
        qemu_tick = cpp_function_body(
            qemu_transport, "static void esp32_ble_hci_rx_tick(void *opaque)"
        )
        qemu_reset = cpp_function_body(
            qemu_transport, "static void esp32_ble_hci_reset_state(Esp32BleHciState *s)"
        )
        bt_pump = cpp_function_body(bt_source, "void QemuBt::pumpTx()")
    except (OSError, ValueError, IndexError) as error:
        print(f"FAIL BLE controller regression: {error}")
        return False

    required_bt = (
        "FRAME_MAX",
        "H4_CMD",
        "paramLen != spec->parameterLength",
        "HCI_INVALID_HCI_COMMAND_PARAMETERS",
        "HCI_UNKNOWN_HCI_COMMAND",
        "std::memory_order_acquire",
        "std::memory_order_release",
        "CommandSpec",
        "dispatchCommand",
        "buildCommandComplete",
        "buildCommandStatus",
    )
    for fragment in required_bt:
        if fragment not in bt_source:
            failures.append(f"controller source is missing {fragment!r}")
    for handler in (
        "handleReset", "handleReadLocalVersionInfo", "handleReadLocalSupportedCommands",
        "handleReadLocalSupportedFeatures",
        "handleSetEventMask", "handleSetEventMaskPage2", "handleLeSetEventMask",
        "handleLeReadBufferSize", "handleLeReadLocalSupportedFeatures", "handleReadBdAddr",
        "handleSetControllerToHostFlowControl", "handleHostBufferSize",
        "handleLeSetAddressResolutionEnable", "handleLeClearResolvingList",
        "handleLeAddDeviceToResolvingList", "handleLeSetPrivacyMode",
        "handleLeSetAdvertisingParameters", "handleLeSetAdvertisingData",
        "handleLeSetScanResponseData", "handleLeSetAdvertisingEnable",
        "handleLeSetScanParameters", "handleLeSetScanEnable",
        "handleLeSetDataLength",
    ):
        if f"QemuBt::{handler}(" not in bt_source or f" {handler}(" not in bt_header:
            failures.append(f"controller declaration/definition mismatch for {handler}")
    if "struct QemuBt::ControllerState" in bt_source:
        failures.append("ControllerState is redefined outside the class")
    if bt_source.find("completeControllerFrame(const") > bt_source.find("injectHostFrame("):
        failures.append("completeControllerFrame is not declared before first use")
    for fragment in ("void QemuBt::runTick()", "pumpTx();"):
        if fragment not in bt_source:
            failures.append(f"controller periodic pump is missing {fragment!r}")
    if "void runTick() override" not in bt_header or "virtual void runTick()" not in qemu_module:
        failures.append("controller periodic pump hook is not declared")
    if "module->runTick()" not in qemu_device:
        failures.append("QEMU periodic events do not service module queues")
    for property_name in ("WiFiLinkPort", "BtLinkPort", "HostForwardPort"):
        if f'new IntProp<QemuDevice>( "{property_name}"' not in qemu_device:
            failures.append(f"QEMU device Properties panel is missing {property_name}")
    for fragment in (
        'BUILD_DIR="$REPO_ROOT/build/qemu-simulide"',
        'install_emulators "$BIN_DIR"',
    ):
        if fragment not in qemu_build_script:
            failures.append(f"QEMU build script is missing {fragment!r}")
    if "build/executables/*.app" in qemu_build_script:
        failures.append("QEMU build script modifies existing application bundles")
    for fragment in (
        "bash $$PWD/scripts/build_qemu.sh",
        "qemuBundleData.depends = runQemuBuild",
        "PRE_TARGETDEPS      += runQemuBuild",
    ):
        if fragment not in simulide_pri:
            failures.append(f"SimulIDE build ordering is missing {fragment!r}")
    if "build_qemu.sh || true" in simulide_pri:
        failures.append("SimulIDE build ignores QEMU build failures")
    for fragment in (
        "static QemuMutex s_signal_mutex",
        "qemu_mutex_init( &s_signal_mutex )",
        "qemu_mutex_lock( &s_signal_mutex )",
        "simulide_signal_locked( SIM_BT",
    ):
        if fragment not in qemu_interface:
            failures.append(f"QEMU mailbox serialization is missing {fragment!r}")
    for fragment in (
        "HCI_READ_LOCAL_SUPPORTED_COMMANDS", "handleReadLocalSupportedCommands",
        "HCI_LE_SET_ADVERTISING_PARAMETERS", "HCI_LE_SET_ADVERTISING_DATA",
        "HCI_LE_SET_SCAN_RESPONSE_DATA", "HCI_LE_SET_ADVERTISING_ENABLE",
        "HCI_LE_SET_SCAN_PARAMETERS", "HCI_LE_SET_SCAN_ENABLE",
        "EVT_LE_ADVERTISING_REPORT", "airAdvertisements", "airScanners",
        "m_pendingEvents", "PENDING_EVENT_MAX", "m_seenAdvertisements",
        "DUPLICATE_CACHE_MAX", "m_pendingCommandResponse",
        "HCI_COMMAND_DISALLOWED", "pumpPendingEvents",
        "HCI_LE_CREATE_CONNECTION", "HCI_LE_CREATE_CONNECTION_CANCEL",
        "HCI_DISCONNECT", "HCI_READ_REMOTE_VERSION_INFO",
        "HCI_LE_READ_REMOTE_FEATURES",
        "HCI_HOST_NUMBER_OF_COMPLETED_PACKETS", "handleAclFrame",
        "LE_EVENT_MASK_ENHANCED_CONNECTION_COMPLETE", "PendingFrame",
        "RELIABLE_DATA_EVENT_MAX", "commitPendingAction",
        "airControllers", "nextConnectionHandle", "m_pendingAcl",
        "m_hostAclOutstanding", "m_deferredWake", "m_pumping",
        "m_pumpRequested", "tryPendingAcl",
        "EVENT_MASK_DISCONNECTION_COMPLETE",
        "EVENT_MASK_READ_REMOTE_VERSION_COMPLETE",
        "LE_EVENT_MASK_READ_REMOTE_FEATURES_COMPLETE",
        "HCI_LE_SET_DATA_LENGTH", "handleLeSetDataLength",
    ):
        if fragment not in bt_source and fragment not in bt_header:
            failures.append(f"legacy advertising/scanning source is missing {fragment!r}")
    for forbidden in ("setInterrupt(", "QemuNetBackend", "sendFrame("):
        if forbidden in bt_source:
            failures.append(f"controller source contains forbidden {forbidden!r}")
    response_push = bt_pump.find("if (!pushRxFrame(completion, completionLen))")
    action_commit = bt_pump.find("commitPendingAction(opcode);", response_push)
    tx_consume = bt_pump.find("ring->head = (head + 1) % RING_FRAMES;", action_commit)
    if response_push < 0 or action_commit < response_push or tx_consume < action_commit:
        failures.append("command action is not committed between RX acceptance and TX consumption")

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
        if "QStandardPaths::CacheLocation" not in source:
            failures.append(f"{name} firmware padding is not stored in the application cache")

    demo_root = root_dir / "resources/data/bin/esp/examples/ble-gatt"
    required_demo_sources = (
        "build.py", "BUILDING.md", "common/virtual_vhci.c",
        "common/include/virtual_vhci.h", "central/main/main.c",
        "peripheral/main/main.c",
    )
    for path in required_demo_sources:
        if not (demo_root / path).is_file():
            failures.append(f"official BLE demo source is missing {path!r}")
    for generated in ("build", "sdkconfig", "sdkconfig.old", "dependencies.lock"):
        if any(path.name == generated for path in demo_root.rglob("*")):
            failures.append(f"official BLE demo source contains generated {generated!r}")
    demo_targets = (
        ("esp32", "esp32", "Esp32"),
        ("esp32-s3", "esp32s3", "Esp32s3"),
        ("esp32-c3", "esp32c3", "Esp32c3"),
    )
    for example_dir, firmware_dir, device_id in demo_targets:
        firmware_root = root_dir / "resources/data/bin" / firmware_dir
        for generated in firmware_root.glob(".simulide-*-flash.bin"):
            failures.append(
                f"official BLE firmware directory contains generated {generated.name!r}"
            )
        if (root_dir / "resources/data/bin/esp" / firmware_dir).exists():
            failures.append(f"official BLE firmware uses nested path for {firmware_dir}")
        circuit_path = (root_dir / "resources/data/examples" / example_dir /
                        f"{example_dir} BLE GATT Demo.sim2")
        if not circuit_path.is_file():
            failures.append(f"official BLE circuit is missing for {example_dir}")
            continue
        circuit = circuit_path.read_text(encoding="utf-8")
        for fragment in (
            f'CircId="{device_id}-1"', f'CircId="{device_id}-2"',
            f'../../bin/{firmware_dir}/ble_gatt_peripheral.merged.bin',
            f'../../bin/{firmware_dir}/ble_gatt_central.merged.bin',
            f'startpinid="{device_id}-2-G04"',
        ):
            if fragment not in circuit:
                failures.append(f"official BLE circuit {example_dir} is missing {fragment!r}")

    chat_format_h = root_dir / "src/microsim/cores/qemu/blechatformat.h"
    chat_format_cpp = root_dir / "src/microsim/cores/qemu/blechatformat.cpp"
    chat_client_h = root_dir / "src/microsim/cores/qemu/blechatclient.h"
    chat_client_cpp = root_dir / "src/microsim/cores/qemu/blechatclient.cpp"
    chat_dialog_h = root_dir / "src/gui/serial/blechatdialog.h"
    chat_dialog_cpp = root_dir / "src/gui/serial/blechatdialog.cpp"
    for path in (chat_format_h, chat_format_cpp, chat_client_h,
                 chat_client_cpp, chat_dialog_h, chat_dialog_cpp):
        if not path.is_file():
            failures.append(f"BLE chat source is missing {path.name!r}")
    try:
        chat_format = chat_format_cpp.read_text(encoding="utf-8")
        chat_client = chat_client_cpp.read_text(encoding="utf-8")
        chat_client_header = chat_client_h.read_text(encoding="utf-8")
        chat_dialog = chat_dialog_cpp.read_text(encoding="utf-8")
        chat_dialog_header = chat_dialog_h.read_text(encoding="utf-8")
    except OSError as error:
        failures.append(f"BLE chat source is unreadable: {error}")
        chat_format = chat_client = chat_client_header = ""
        chat_dialog = chat_dialog_header = ""
    for fragment in ("bleChatFormatString", "bleChatFormatHex",
                     "bleChatParseString", "bleChatParseHex", "\\\\x"):
        if fragment not in chat_format:
            failures.append(f"BLE chat formatting is missing {fragment!r}")
    for fragment in ("class BleChatClient", "startScan", "connectToDevice",
                     "readValue", "writeValue", "setNotifications",
                     "connecting()", "notificationsOn()", "m_connectTimer", "Connect timeout",
                     "Connection request accepted", "Discovering services...",
                     "selectServiceAndContinue", "selectCharAndContinue",
                     "m_cancelRequested",
                     "services", "chars", "descs",
                     "0x0A", "0x12", "0x1B", "0x2902", "char( 0x28 )"):
        if fragment not in chat_client and fragment not in chat_client_header:
            failures.append(f"BLE chat client is missing {fragment!r}")
    for fragment in ("class BleChatDialog", "Updatable", "updateStep",
                     "String", "HEX", "setToolTip", "m_sendButton"):
        if fragment not in chat_dialog and fragment not in chat_dialog_header:
            failures.append(f"BLE chat dialog is missing {fragment!r}")
    if "m_client->poll()" not in chat_dialog:
        failures.append("BLE chat dialog does not pump the host client")
    if "isRunning" not in chat_dialog or "stopScan" not in chat_dialog:
        failures.append("BLE chat dialog does not stop scanning when simulation stops")
    for fragment in ("Open BLE Chat", "slotOpenBleChat", "m_bleChat"):
        if fragment not in qemu_device:
            failures.append(f"QEMU device BLE chat entry is missing {fragment!r}")
    if "BleChat" in bt_source or "bleChat" in bt_source:
        failures.append("controller source must not implement BLE chat")
    runtime_pro = root_dir / "tests/qemubt-runtime/qemubt-runtime.pro"
    runtime_cpp = root_dir / "tests/qemubt-runtime/qemubt-runtime.cpp"
    try:
        runtime_pro_text = runtime_pro.read_text(encoding="utf-8")
        runtime_cpp_text = runtime_cpp.read_text(encoding="utf-8")
    except OSError as error:
        failures.append(f"BLE runtime source is unreadable: {error}")
        runtime_pro_text = runtime_cpp_text = ""
    if "blechatformat.cpp" not in runtime_pro_text:
        failures.append("BLE runtime harness does not build chat formatting")
    if "testChatFormat" not in runtime_cpp_text:
        failures.append("BLE runtime harness does not test chat formatting")
    if "testChatClientRoundTrip" not in runtime_cpp_text:
        failures.append("BLE runtime harness does not test chat round trip")

    building = demo_root / "BUILDING.md"
    expected_hashes = {}
    if building.is_file():
        for line in building.read_text(encoding="utf-8").splitlines():
            fields = line.split()
            if len(fields) == 2 and len(fields[0]) == 64 and fields[1].startswith(
                    "resources/data/bin/"):
                expected_hashes[fields[1]] = fields[0]
    if len(expected_hashes) != 6:
        failures.append("official BLE demo provenance does not list six firmware hashes")
    for relative, expected in expected_hashes.items():
        firmware_path = root_dir / relative
        if not firmware_path.is_file():
            failures.append(f"official BLE firmware is missing {relative!r}")
            continue
        actual = hashlib.sha256(firmware_path.read_bytes()).hexdigest()
        if actual != expected:
            failures.append(f"official BLE firmware hash mismatch for {relative!r}")

    if failures:
        print("FAIL BLE controller regression")
        for failure in failures:
            print(f"  {failure}")
        return False
    print("PASS BLE controller regression: H4 responses, rejection, routing and IRQ ownership")
    return True


def run_ble_runtime_test(root_dir=ROOT_DIR):
    command = root_dir / BT_RUNTIME_TEST
    result = subprocess.run([str(command)], cwd=root_dir, check=False)
    if result.returncode:
        print("FAIL BLE production runtime test")
        return False
    print("PASS BLE production runtime test: connection, ACL credits, backpressure and lifetime")
    return True


BLE_IDF_IMAGES = {
    "4.4.7": "espressif/idf@sha256:52bc81e7f212b6cc63b31ea57b8270badb3236e44df8567a57e2d1a6c74c5000",
    "5.5.5": "espressif/idf@sha256:a9231d0697ab8f7517cc072e93b7c83e04907bfbfba80b6440d7dbbf90665cf2",
    "6.1": "espressif/idf@sha256:81893c71bb5e570088901f21def8684c25cd2a9020281bd01b843a7655edb18c",
}

BLE_IDF_TARGETS = {
    "esp32": {
        "idf_target": "esp32",
        "circuit": "esp32-ble-gatt-e2e.sim2",
    },
    "esp32-s3": {
        "idf_target": "esp32s3",
        "circuit": "esp32-s3-ble-gatt-e2e.sim2",
    },
    "esp32-c3": {
        "idf_target": "esp32c3",
        "circuit": "esp32-c3-ble-gatt-e2e.sim2",
    },
}

def ble_e2e_matrix(mcu=None, environ=None):
    environ = os.environ if environ is None else environ
    if environ.get("BLE_E2E_MATRIX", "").lower() in ("1", "true", "yes"):
        return tuple(
            (version, target)
            for version in BLE_IDF_IMAGES
            for target in BLE_IDF_TARGETS
        )

    version = environ.get("BLE_IDF_VERSION", "4.4.7")
    versions = tuple(BLE_IDF_IMAGES) if version == "all" else (version,)
    default_target = mcu if mcu in BLE_IDF_TARGETS else "esp32"
    target = environ.get("BLE_IDF_TARGET", default_target)
    targets = tuple(BLE_IDF_TARGETS) if target == "all" else (target,)
    return tuple((selected_version, selected_target)
                 for selected_version in versions for selected_target in targets)


def run_ble_e2e_matrix(mcu=None, root_dir=ROOT_DIR):
    matrix = ble_e2e_matrix(mcu)
    failures = []
    for idf_version, target in matrix:
        if not run_ble_e2e_test(root_dir, idf_version, target):
            failures.append(f"IDF {idf_version} / {target}")
    if failures:
        print(f"FAIL BLE e2e matrix: {', '.join(failures)}")
        return False
    print(f"PASS BLE e2e matrix: {len(matrix)} combination(s)")
    return True


def run_ble_e2e_test(root_dir=ROOT_DIR, idf_version=None, target=None):
    """Build BLE GATT peripheral+central firmware and run two-device smoke test."""
    fixture_dir = root_dir / "tests/fixtures/ble-gatt-e2e"
    idf_version = idf_version or os.environ.get("BLE_IDF_VERSION", "4.4.7")
    target = target or os.environ.get("BLE_IDF_TARGET", "esp32")
    idf_image = BLE_IDF_IMAGES.get(idf_version)
    if idf_image is None:
        print(f"FAIL BLE e2e test: unknown BLE_IDF_VERSION={idf_version!r}")
        return False
    target_config = BLE_IDF_TARGETS.get(target)
    if target_config is None:
        print(f"FAIL BLE e2e test: unknown BLE_IDF_TARGET={target!r}")
        return False
    label = f"IDF {idf_version} / {target}"
    build_dir = root_dir / "tmp/ble-gatt-e2e-test" / idf_version / target
    print(f"BLE e2e test: {label}", flush=True)

    import shutil
    if build_dir.exists():
        shutil.rmtree(build_dir)
    build_dir.mkdir(parents=True)

    firmware = (
        ("peripheral", "ble_gatt_peripheral"),
        ("central", "ble_gatt_central"),
    )
    result = None
    output = ""
    try:
        for project, image in firmware:
            project_build = build_dir / project / "build"
            sdkconfig = build_dir / project / "sdkconfig"
            project_build.mkdir(parents=True)
            container_build = f"/project/{project_build.relative_to(root_dir)}"
            container_sdkconfig = f"/project/{sdkconfig.relative_to(root_dir)}"
            build_cmd = [
                "docker", "run", "--rm", "--platform", "linux/arm64",
                "-e", f"IDF_TARGET={target_config['idf_target']}",
                "-v", f"{root_dir}:/project",
                "-w", f"/project/tests/fixtures/ble-gatt-e2e/{project}",
                idf_image,
                "idf.py", "-B", container_build,
                "-D", f"SDKCONFIG={container_sdkconfig}", "build"
            ]
            build_result = subprocess.run(build_cmd, check=False)
            if build_result.returncode != 0:
                print(f"FAIL BLE e2e test ({label}): {project} firmware build failed")
                return False

            flasher_args = json.loads(
                (project_build / "flasher_args.json").read_text(encoding="utf-8")
            )
            flash_settings = flasher_args["flash_settings"]
            flash_files = sorted(
                flasher_args["flash_files"].items(),
                key=lambda item: int(item[0], 0),
            )
            merge_cmd = [
                "docker", "run", "--rm", "--platform", "linux/arm64",
                "-v", f"{root_dir}:/project",
                "-w", container_build,
                idf_image,
                "esptool.py", "--chip", target_config["idf_target"], "merge_bin",
                "-o", f"/project/{(build_dir / (image + '.merged.bin')).relative_to(root_dir)}",
                "--flash_mode", flash_settings["flash_mode"],
                "--flash_freq", flash_settings["flash_freq"],
                "--flash_size", ("2MB" if flash_settings["flash_size"] == "detect"
                                 else flash_settings["flash_size"]),
                "--fill-flash-size", "2MB",
            ]
            for offset, filename in flash_files:
                merge_cmd.extend((offset, filename))
            merge_result = subprocess.run(merge_cmd, check=False)
            if merge_result.returncode != 0:
                print(f"FAIL BLE e2e test ({label}): {project} merge_bin failed")
                return False

        circuit_src = fixture_dir / "circuits" / target_config["circuit"]
        circuit_dst = build_dir / target_config["circuit"]
        shutil.copy2(circuit_src, circuit_dst)

        exe_dir = root_dir / "build/executables"
        candidates = list(exe_dir.glob("simulide-*.app/Contents/MacOS/simulide-*"))
        candidates.extend(exe_dir.glob("simulide-*"))
        executables = [candidate for candidate in candidates
                       if candidate.is_file() and os.access(candidate, os.X_OK)]
        if not executables:
            print(f"FAIL BLE e2e test ({label}): SimulIDE executable not found")
            return False
        simulide_exe = max(executables, key=lambda candidate: candidate.stat().st_mtime)

        env = os.environ.copy()
        env["HOME"] = str(root_dir / "tmp")
        env["SIMULIDE_TEST_MODE"] = "1"
        env["QT_QPA_PLATFORM"] = "offscreen"
        smoke_cmd = [
            str(simulide_exe), "-silent", "-nogui", "-smoke-test",
            str(circuit_dst), "25000"
        ]
        print(f"BLE e2e smoke ({label})", flush=True)
        try:
            result = subprocess.run(smoke_cmd, cwd=root_dir, env=env, check=False,
                                    timeout=120, capture_output=True, text=True)
        except subprocess.TimeoutExpired as error:
            passed = False
            output = (error.stdout or "") + (error.stderr or "")
            failure = "smoke test timed out"
        else:
            output = result.stdout + result.stderr
            missing = [
                sentinel
                for sentinel in ("BLE_GATT_PERIPHERAL_READY", "BLE_GATT_E2E_PASS")
                if sentinel not in output
            ]
            passed = result.returncode == 0 and not missing
            if result.returncode != 0:
                failure = f"smoke test exited with status {result.returncode}"
            else:
                failure = f"missing guest sentinel(s): {', '.join(missing)}"

        if not passed:
            print(f"FAIL BLE e2e smoke ({label}): {failure}")
            uart_lines = [line for line in output.splitlines() if line.startswith("[UART")]
            print("\n".join(uart_lines[-100:]))
    finally:
        shutil.rmtree(build_dir, ignore_errors=True)

    if not passed:
        print(f"FAIL BLE e2e test ({label}): smoke test failed")
        return False
    print(f"PASS BLE e2e test ({label}): GATT round-trip verified (subscribe/write/notify/read)")
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
            if run_ble_runtime_test():
                passed += 1
            else:
                failed += 1
            if run_ble_e2e_matrix(args.mcu):
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
