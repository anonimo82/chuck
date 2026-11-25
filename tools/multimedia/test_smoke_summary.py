"""Exercise aggregation without building or running ChucK."""
import contextlib
import io
import pathlib
import tempfile
import unittest
from unittest.mock import patch
import run_smoke_tests as runner

class SummaryTest(unittest.TestCase):
    def run_fixture(self, scripts):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            names = []
            for i, script in enumerate(scripts):
                name = f"suite{i}.py"
                (root / name).write_text(script, encoding="utf-8")
                names.append(name)
            output = io.StringIO()
            with patch.object(runner, "HERE", root), patch.object(runner, "SUITES", names), contextlib.redirect_stdout(output):
                code = runner.main()
            return code, output.getvalue()

    def test_repeated_batches(self):
        code, log = self.run_fixture(["for i in range(3):\n print('SMOKE_EXPECTED 5')\n print('SMOKE_COUNTS 5 5 0')"])
        self.assertEqual(code, 0)
        self.assertIn("expected=15 | PASS=15 | FAIL=0", log)
        self.assertIn("status=PASS", log)

    def test_started_but_unreported(self):
        code, log = self.run_fixture(["print('SMOKE_EXPECTED 5')\nraise SystemExit(1)"])
        self.assertEqual(code, 1)
        self.assertIn("unreported=5", log)
        self.assertIn("status=FAIL", log)

    def test_fixture_error_after_checks(self):
        code, log = self.run_fixture(["print('SMOKE_EXPECTED 2')\nprint('SMOKE_COUNTS 2 2 0')\nraise SystemExit(1)"])
        self.assertEqual(code, 1)
        self.assertIn("PASS=2 | FAIL=0", log)
        self.assertIn("status=FAIL", log)

    def test_failed_check(self):
        code, log = self.run_fixture(["print('SMOKE_EXPECTED 2')\nprint('SMOKE_COUNTS 2 1 1')"])
        self.assertEqual(code, 1)
        self.assertIn("PASS=1 | FAIL=1", log)

if __name__ == "__main__":
    unittest.main()
