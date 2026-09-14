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
