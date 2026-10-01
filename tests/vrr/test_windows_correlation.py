import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("correlation", Path(__file__).resolve().parents[2] / "scripts/correlate-vrr-windows.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class CorrelationTest(unittest.TestCase):
    def test_identity_and_ambiguity(self):
        frame = {"event": "d3d11_present", "a": "10", "object_id": "32", "c": "1000", "d": "1010", "e": "10000000"}
        present = {"ProcessID": "10", "SwapChainAddress": "0x20", "QPCTime": "1005", "Dropped": "1"}
        wrong_chain = dict(present, SwapChainAddress="0x21")
        wrong_process = dict(present, ProcessID="11")
        output, counts = module.correlate([frame], [wrong_chain, wrong_process, present])
        self.assertEqual(counts, {"matched": 1})
        self.assertEqual(output[0]["pm_Dropped"], "1")  # keep dropped frames
        self.assertEqual(module.correlate([frame], [wrong_chain])[1], {"missing": 1})
        self.assertEqual(module.correlate([frame], [present, dict(present, QPCTime="1006")])[1], {"ambiguous": 1})
        self.assertEqual(module.correlate([frame, frame], [present])[1], {"matched": 1, "duplicate_claim": 1})
        self.assertEqual(module.correlate([dict(frame, c="0")], [present])[1], {"invalid_clock": 1})


if __name__ == "__main__":
    unittest.main()
