import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "Tools" / "agent" / "noqeri_agent.py"

class AgentLoopTests(unittest.TestCase):
    def test_invalid_source_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run([sys.executable, str(SCRIPT), str(Path(directory) / "missing.noqeri")],
                                    text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("existing .noqeri", result.stderr)

    def test_compiler_failure_is_json_and_skips_later_steps(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "main.noqeri"
            source.write_text("print(1)\n", encoding="utf-8")
            fake = Path(directory) / "compiler.py"
            fake.write_text("#!/usr/bin/env python3\nimport json,sys\nprint(json.dumps({'code':'NQR-T0001','file':'main.noqeri','line':1,'column':1}),file=sys.stderr)\nsys.exit(1)\n", encoding="utf-8")
            fake.chmod(0o755)
            result = subprocess.run([sys.executable, str(SCRIPT), str(source), "--compiler", str(fake),
                                     "--tests", directory, "--build"], text=True, capture_output=True)
            self.assertEqual(result.returncode, 1)
            report = json.loads(result.stdout)
            self.assertEqual([s["step"] for s in report["attempts"][0]["steps"]], ["check"])
            self.assertEqual(report["attempts"][0]["diagnostics"][0]["line"], 1)
            self.assertEqual(report["next_action"], "edit_source_and_retry")

if __name__ == "__main__":
    unittest.main()
