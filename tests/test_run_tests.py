import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import run_tests


def valid_manifest(**check_changes):
    check = {"name": "guard", "path": "source.cpp", "contains": ["return;"]}
    check.update(check_changes)
    return {"description": "test contract", "checks": [check]}


class ManifestValidationTests(unittest.TestCase):
    def assert_invalid(self, manifest, message):
        with self.assertRaisesRegex(run_tests.ManifestError, message):
            run_tests.validate_manifest(manifest)

    def test_invalid_json_reports_location(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.json"
            path.write_text('{"description":', encoding="utf-8")
            with self.assertRaisesRegex(run_tests.ManifestError, "line 1, column"):
                run_tests.load_manifest(path)

    def test_top_level_and_required_fields(self):
        self.assert_invalid([], "top level")
        self.assert_invalid({"description": "x", "checks": []}, "non-empty list")
        self.assert_invalid({"description": "", "checks": [{}]}, "description")
        manifest = valid_manifest()
        manifest["typo"] = True
        self.assert_invalid(manifest, "unknown top-level")

    def test_check_shape_and_unknown_keys(self):
        self.assert_invalid(
            {"description": "x", "checks": ["bad"]}, "check must be an object"
        )
        self.assert_invalid(valid_manifest(name=17), "name must")
        self.assert_invalid(valid_manifest(path=""), "path must")
        self.assert_invalid(valid_manifest(contain=["x"]), "unknown check")

    def test_assertions_are_required_and_typed(self):
        self.assert_invalid(
            valid_manifest(contains=None), "requires contains, not_contains or ordered"
        )
        self.assert_invalid(valid_manifest(contains="return"), "contains must")
        self.assert_invalid(valid_manifest(contains=[""]), "contains must")
        self.assert_invalid(
            valid_manifest(contains=None, not_contains="return"), "not_contains must"
        )
        self.assert_invalid(
            valid_manifest(contains=None, not_contains=[""]), "not_contains must"
        )
        self.assert_invalid(
            valid_manifest(contains=None, ordered=["one"]), "at least two"
        )
        self.assert_invalid(
            valid_manifest(contains=None, ordered=["one", 2]), "non-empty strings"
        )

    def test_within_lines_validation(self):
        self.assert_invalid(valid_manifest(within_lines=3), "requires ordered")
        for value in (0, -1, "3", True):
            self.assert_invalid(
                valid_manifest(contains=None, ordered=["a", "b"], within_lines=value),
                "positive integer",
            )

    def test_paths_cannot_escape_repository(self):
        self.assert_invalid(valid_manifest(path="/tmp/source.cpp"), "stay inside")
        self.assert_invalid(valid_manifest(path="../source.cpp"), "stay inside")


class OrderedMatcherTests(unittest.TestCase):
    def test_order_and_span(self):
        source = "start\nif bad\nclear\nreturn\n"
        self.assertEqual(
            run_tests.find_ordered(source, ["start", "if bad", "return"], 4),
            (True, None),
        )
        found, detail = run_tests.find_ordered(source, ["return", "start"])
        self.assertFalse(found)
        self.assertIn("required order", detail)
        found, detail = run_tests.find_ordered(source, ["start", "return"], 2)
        self.assertFalse(found)
        self.assertIn("4 lines", detail)

    def test_later_first_fragment_can_match(self):
        source = "start\n\n\nend\nstart\nend\n"
        self.assertEqual(
            run_tests.find_ordered(source, ["start", "end"], 2), (True, None)
        )


class SpiClockTests(unittest.TestCase):
    def test_frequency_dividers_and_limits(self):
        value = run_tests.spi_clock_register(625, 64)
        self.assertEqual(run_tests.spi_clock_divider(value, False), 40_000)
        self.assertEqual(
            run_tests.spi_clock_divider(run_tests.spi_clock_register(16, 64), True),
            1_024,
        )

    def test_common_runner_executes_spi_regression(self):
        self.assertTrue(run_tests.run_spi_clock_regression())


class SpiEndpointTests(unittest.TestCase):
    def test_esp32_mosi_present_and_absent(self):
        self.assertTrue(run_tests.spi_endpoints_ready(False, True, True, True))
        self.assertTrue(run_tests.spi_endpoints_ready(False, True, False, True))

    def test_esp8266_fixed_endpoint_validation(self):
        self.assertTrue(run_tests.spi_endpoints_ready(True, True, True, True))
        self.assertFalse(run_tests.spi_endpoints_ready(True, True, False, True))
        self.assertFalse(run_tests.spi_endpoints_ready(True, False, True, True))
        self.assertFalse(run_tests.spi_endpoints_ready(True, True, True, False))

    def test_common_runner_executes_spi_endpoint_regression(self):
        self.assertTrue(run_tests.run_spi_endpoint_regression())


class SpiResetTests(unittest.TestCase):
    def test_full_reset_is_scoped_to_controller_window(self):
        registers = {9: 0xAA, 10: 1 << 18, 11: 0xFFFFFFFF, 12: 0x55}
        reset = run_tests.spi_reset_registers(registers, 10, 11, full=True)
        self.assertEqual(reset, {9: 0xAA, 10: 0, 11: 0, 12: 0x55})

    def test_sync_reset_clears_command_and_done_only(self):
        base = 0x64000
        registers = {base: 1 << 18, base + 0x38: (1 << 9) | (1 << 4)}
        reset = run_tests.spi_reset_registers(registers, base, base + 0xFFF)
        self.assertEqual(reset[base], 0)
        self.assertEqual(reset[base + 0x38], 1 << 9)

    def test_common_runner_executes_spi_reset_regression(self):
        self.assertTrue(run_tests.run_spi_reset_regression())


class BleControllerTests(unittest.TestCase):
    def test_reset_and_unknown_command_completions(self):
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 03 0c 00")),
            bytes.fromhex("04 0e 04 01 03 0c 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 03 0c 01 aa")),
            bytes.fromhex("04 0e 04 01 03 0c 12"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 34 12 00")),
            bytes.fromhex("04 0e 04 01 34 12 01"),
        )

    def test_nimble_startup_commands(self):
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 01 10 00")),
            bytes.fromhex("04 0e 0c 01 01 10 00 09 00 00 09 00 00 00 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 03 10 00")),
            bytes.fromhex("04 0e 0c 01 03 10 00 00 00 00 00 60 00 00 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 01 0c 08 90 80 00 02 00 80 00 20")),
            bytes.fromhex("04 0e 04 01 01 0c 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 63 0c 08 00 00 80 00 00 00 00 00")),
            bytes.fromhex("04 0e 04 01 63 0c 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 01 20 08 1f 00 00 00 00 00 00 00")),
            bytes.fromhex("04 0e 04 01 01 20 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 02 20 00")),
            bytes.fromhex("04 0e 07 01 02 20 00 1b 00 01"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 03 20 00")),
            bytes.fromhex("04 0e 0c 01 03 20 00 00 00 00 00 00 00 00 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 09 10 00")),
            bytes.fromhex("04 0e 0a 01 09 10 00 11 22 33 44 55 66"),
        )

    def test_flow_control_commands(self):
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 31 0c 01 01")),
            bytes.fromhex("04 0e 04 01 31 0c 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 31 0c 01 00")),
            bytes.fromhex("04 0e 04 01 31 0c 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 33 0c 07 ff 00 00 14 00 00 00")),
            bytes.fromhex("04 0e 04 01 33 0c 00"),
        )

    def test_privacy_commands(self):
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 2d 20 01 01")),
            bytes.fromhex("04 0e 04 01 2d 20 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 29 20 00")),
            bytes.fromhex("04 0e 04 01 29 20 00"),
        )
        add_dev = bytes.fromhex("01 27 20 27 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00")
        self.assertEqual(
            run_tests.hci_command_complete(add_dev),
            bytes.fromhex("04 0e 04 01 27 20 00"),
        )
        self.assertEqual(
            run_tests.hci_command_complete(bytes.fromhex("01 4e 20 08 00 00 00 00 00 00 00 01")),
            bytes.fromhex("04 0e 04 01 4e 20 00"),
        )

    def test_legacy_advertising_and_scanning_commands(self):
        commands = (
            "01 06 20 0f 00 08 00 08 00 00 00 00 00 00 00 00 00 07 00",
            "01 08 20 20 03 02 01 06 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
            "01 09 20 20 02 01 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
            "01 0a 20 01 01",
            "01 0b 20 07 00 10 00 10 00 00 00",
            "01 0c 20 02 01 01",
        )
        for command in commands:
            response = run_tests.hci_command_complete(bytes.fromhex(command))
            self.assertIsNotNone(response)
            self.assertEqual(response[6], 0)

    def test_legacy_advertising_report_bytes(self):
        report = run_tests.hci_le_advertising_report(
            0, 0, bytes.fromhex("11 22 33 44 55 02"), bytes.fromhex("02 01 06")
        )
        self.assertEqual(
            report,
            bytes.fromhex("04 3e 0f 02 01 00 00 11 22 33 44 55 02 03 02 01 06 d6"),
        )

    def test_invalid_legacy_radio_parameters_rejected(self):
        commands = (
            "01 06 20 0f 00 08 00 08 05 00 00 00 00 00 00 00 00 07 00",
            "01 06 20 0f 00 08 00 08 01 00 00 00 00 00 00 00 00 07 00",
            "01 08 20 20 20 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
            "01 0a 20 01 02",
            "01 0b 20 07 00 10 00 20 00 00 00",
            "01 0c 20 02 01 02",
        )
        for command in commands:
            response = run_tests.hci_command_complete(bytes.fromhex(command))
            self.assertIsNotNone(response)
            self.assertEqual(response[6], 0x12)

    def test_invalid_privacy_parameter_values_rejected(self):
        commands = (
            "01 2d 20 01 02",
            "01 27 20 27 02 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00",
            "01 4e 20 08 00 00 00 00 00 00 00 02",
        )
        for command in commands:
            response = run_tests.hci_command_complete(bytes.fromhex(command))
            self.assertIsNotNone(response)
            self.assertEqual(response[6], 0x12)

    def test_malformed_non_command_and_oversized_frames_are_rejected(self):
        for frame in (
            b"",
            bytes.fromhex("01 03 0c"),
            bytes.fromhex("02 03 0c 00"),
            bytes.fromhex("01 03 0c 01"),
            bytes.fromhex("01 03 0c 00 ff"),
            bytes(1537),
        ):
            self.assertIsNone(run_tests.hci_command_complete(frame))

    def test_invalid_parameter_lengths_rejected(self):
        for frame in (
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
        ):
            actual = run_tests.hci_command_complete(frame)
            self.assertIsNotNone(actual)
            self.assertEqual(actual[6], 0x12)

    def test_invalid_flow_control_mode_rejected(self):
        actual = run_tests.hci_command_complete(bytes.fromhex("01 31 0c 01 02"))
        self.assertIsNotNone(actual)
        self.assertEqual(actual[6], 0x12)

    def test_invalid_host_buffer_size_rejected(self):
        actual = run_tests.hci_command_complete(bytes.fromhex("01 33 0c 07 ff 00 01 14 00 00 00"))
        self.assertIsNotNone(actual)
        self.assertEqual(actual[6], 0x12)

    def test_common_runner_executes_ble_regression(self):
        self.assertTrue(run_tests.run_ble_controller_regression())


class CommandLineTests(unittest.TestCase):
    def test_invalid_mcu_and_direction_exit_with_usage_error(self):
        script = str(Path(run_tests.__file__))
        for arguments in (
            ["invalid-mcu", "--contracts-only"],
            ["esp32", "invalid-direction", "--contracts-only"],
        ):
            result = subprocess.run(
                [sys.executable, script, *arguments],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("invalid choice", result.stderr)


if __name__ == "__main__":
    unittest.main()
