#!/usr/bin/env python3
"""Contracts for observed latency reports; no display or running stream needed."""
import hashlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / "scripts/report-vrr-latency.py"
spec = importlib.util.spec_from_file_location("report", SCRIPT)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def trace(rows, footer=True):
    keys = list(rows[0])
    body = (",".join(keys) + "\n" + "".join(
        ",".join(str(row[k]) for k in keys) + "\n" for row in rows)).encode()
    if footer:
        body += (f"#vrr_trace_footer,format_version=2,clean_shutdown=1,arrival_sequence_allocated={len(rows)},"
                 f"rows_enqueued={len(rows)},rows_dropped=0,size_capped=0,write_failed=0,"
                 f"decoded_sha256={hashlib.sha256(body).hexdigest()}\n").encode()
    return body


def row(i, **overrides):
    base = 100000 + i * 10000
    result = dict(arrival_sequence=i, frame=i, rtp_timestamp=i * 900, rtp_valid=1,
                  decoder_output_us=base, decode_complete_us=base + 4000,
                  pacer_arrival_us=base + 100, dequeue_us=base + 200,
                  submission_boundary_us=base + 7000, present_end_us=base + 8000,
                  prepare_us=1000, present_call_us=1000, presented=1, disposition="presented",
                  session_latency_mode=2, playout_delay_us=6000,
                  history_state_valid=1, history_can_release=1)
    result.update(overrides)
    return result


class ReportTest(unittest.TestCase):
    def test_decode_wait_is_not_visible_queue_delay(self):
        r = self.analyze([row(1, decode_sync_wait_us=4000)])
        self.assertEqual(r["metrics"]["queue_pacing_us"]["mean"], 2000)
        self.assertEqual(r["metrics"]["client_processing_us"]["mean"], 8000)
        self.assertEqual(r["metrics"]["decode_sync_wait_us"]["mean"], 4000)


    def analyze(self, rows, **kwargs):
        return report.summarize(io.BytesIO(trace(rows, **kwargs)), Path("capture.csv"))

    def test_overlay_boundary_is_not_gpu_readiness(self):
        r = self.analyze([row(i) for i in range(1, 5)])
        self.assertTrue(r["complete_capture"])
        m = r["metrics"]
        self.assertEqual(m["client_processing_us"]["mean"], 8000)
        self.assertEqual(m["queue_pacing_us"]["mean"], 6000)
        self.assertEqual(m["rendering_us"]["mean"], 2000)
        self.assertEqual(m["ready_to_submission_us"]["mean"], 3000)
        self.assertEqual(m["output_to_submission_us"]["mean"], 7000)
        self.assertEqual(r["cadence"]["jerk_pairs"], 2)

    def test_legacy_does_not_invent_output_time(self):
        rows = [row(1)]
        del rows[0]["decoder_output_us"]
        r = self.analyze(rows)
        self.assertNotIn("client_processing_us", r["metrics"])
        self.assertIn("N/A", report.markdown([r]))
        self.assertIn("Missing measured presets: Balanced Target, Smooth", report.markdown([r]))

    def test_invalid_partition_is_not_clamped_to_zero(self):
        r = self.analyze([row(1, prepare_us=9000)])
        self.assertNotIn("queue_pacing_us", r["metrics"])
        self.assertEqual(r["unavailable_or_invalid"]["queue_pacing_us"], 1)

    def test_truncated_or_tampered_capture_is_not_complete(self):
        self.assertFalse(self.analyze([row(1)], footer=False)["complete_capture"])
        raw = trace([row(1)]).replace(b",6000,", b",6001,")
        r = report.summarize(io.BytesIO(raw), Path("bad.csv"))
        self.assertFalse(r["complete_capture"])

    def test_duplicate_arrivals_and_mixed_presets(self):
        r = self.analyze([row(1), row(1, session_latency_mode=1)])
        self.assertFalse(r["complete_capture"])
        self.assertEqual(r["preset"], "Mixed")
        self.assertEqual(len(r["policy_fingerprints"]), 2)

    def test_host_stall_on_dropped_frame_and_out_of_order_terminal(self):
        rows = [row(2, rtp_timestamp=3600, presented=0, disposition="stale"),
                row(1), row(3, rtp_timestamp=4500), row(4, rtp_timestamp=5400)]
        r = self.analyze(rows)
        self.assertTrue(r["complete_capture"])
        self.assertEqual(r["cadence"]["source_stalls_over_25ms"], 1)
        self.assertEqual(r["cadence"]["jerk_pairs"], 0)

    def test_rtp_wrap_is_a_valid_interval(self):
        r = self.analyze([row(1, rtp_timestamp=0xffffffff - 449),
                          row(2, rtp_timestamp=450), row(3, rtp_timestamp=1350)])
        self.assertEqual(r["cadence"]["source_pairs"], 2)
        self.assertEqual(r["cadence"]["jerk_pairs"], 1)

    def test_compressed_capture(self):
        import struct
        import zlib
        body = trace([row(1)])
        payload = struct.pack(">I", len(body)) + zlib.compress(body)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.vrrtrace"
            path.write_bytes(b"MLVRR1\n" + struct.pack("<I", len(payload)) + payload)
            self.assertTrue(report.analyze(path)["complete_capture"])

    def test_oscillation_reports_each_phase_separately(self):
        rows = [row(i + 1, session_latency_mode=2 - (i // 3) % 3,
                    session_latency_oscillation=1, latency_test_phase=i // 3)
                for i in range(12)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "oscillation.csv"
            path.write_bytes(trace(rows))
            result = report.analyze(path)
        self.assertEqual(len(result["segments"]), 4)
        self.assertEqual([s["preset"] for s in result["segments"]],
                         ["Low Latency", "Balanced Target", "Smooth", "Low Latency"])
        for segment in result["segments"]:
            self.assertEqual(segment["rows"], 3)
            self.assertEqual(segment["cadence"]["jerk_pairs"], 1)
            self.assertTrue(segment["complete_capture"])
            self.assertEqual(len(segment["policy_fingerprints"]), 1)
        text = report.markdown([result])
        self.assertIn("Smooth (phase 2)", text)
        self.assertNotIn("Missing measured presets", text)


if __name__ == "__main__":
    unittest.main()
