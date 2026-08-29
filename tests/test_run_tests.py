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
        self.assert_invalid(valid_manifest(contains=None), "requires contains or ordered")
        self.assert_invalid(valid_manifest(contains="return"), "contains must")
        self.assert_invalid(valid_manifest(contains=[""]), "contains must")
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
