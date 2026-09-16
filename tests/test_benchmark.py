import importlib.util, tempfile, unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("benchmark", ROOT / "tools/benchmark/benchmark.py")
benchmark = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(benchmark)
class BenchmarkTests(unittest.TestCase):
    def test_metric_parser(self):
        values = benchmark.parse_metrics("B2_METRIC video.upload_bytes=4096\nMETRIC framebuffer.sha256=abc\n")
        self.assertEqual(values, {"video.upload_bytes": 4096, "framebuffer.sha256": "abc"})
    def test_runner(self):
        result = benchmark.run_scenario({"name":"self-test","command":["/bin/sh","-c","echo 'B2_METRIC guest.work_units=12'; echo DESKTOP"],"milestones":{"desktop":"DESKTOP"}})
        self.assertTrue(result["ok"]); self.assertEqual(result["metrics"]["guest.work_units"], 12)
        self.assertTrue(result["metrics"]["milestone.desktop.observed"]); self.assertIn("process.cpu_percent", result["metrics"])
    def test_compare(self):
        result = benchmark.compare({"runs":[{"metrics":{"x":10}}]}, {"runs":[{"metrics":{"x":8}}]})
        self.assertEqual(result["comparison"]["x"]["percent"], -20)
    def test_framebuffer_checksum(self):
        with tempfile.TemporaryDirectory() as directory:
            framebuffer = Path(directory) / "framebuffer.bin"
            framebuffer.write_bytes(b"BasiliskII")
            digest = benchmark.sha256(framebuffer)
            result = benchmark.run_scenario({"command":["/usr/bin/true"], "framebuffer_path":str(framebuffer),
                                             "expected_framebuffer_sha256":digest})
            self.assertTrue(result["ok"])
            self.assertEqual(result["metrics"]["framebuffer.sha256"], digest)
if __name__ == "__main__": unittest.main()
