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


def detailed_row(i, **overrides):
    base = 100000 + i * 10000
    result = row(i, decision_valid=1, decode_sync_wait_us=400,
        decision_us=base + 1000, decision_end_us=base + 1100,
        render_wait_entry_us=base + 1200, render_wait_final_us=base + 3000,
        prepare_start_us=base + 3000, prepare_end_us=base + 4000,
        target_wait_entry_us=base + 4100, target_wait_final_us=base + 6000,
        correction_wait_start_us=base + 6500, correction_wait_end_us=base + 6700,
        present_start_us=base + 7000,
        buffer_update_valid=0, buffer_update_frame=i, buffer_update_at_us=base + 7000,
        buffer_action=0, buffer_attributed_frame=i, buffer_request_before_us=1000,
        buffer_request_after_us=1000, buffer_clipped_increase_us=0,
        buffer_interval_error_us=3000, buffer_lateness_us=3000)
    result.update(overrides)
    return result


class ReportTest(unittest.TestCase):
    def test_client_costs_partition_without_double_counting(self):
        r = self.analyze([detailed_row(1)])
        costs = r["client_costs_us"]
        self.assertEqual(sum(v["mean"] for v in costs.values()), 8000)
        self.assertEqual(costs["decode_sync"]["mean"], 400)
        self.assertEqual(costs["render_wait"]["mean"], 1800)
        self.assertEqual(costs["spacing_wait"]["mean"], 200)
        self.assertEqual(costs["other_worker"]["mean"], 1400)
        self.assertEqual(r["metrics"]["queue_pacing_us"]["mean"], 5600)

    def test_missing_or_overlapping_costs_are_unavailable(self):
        r = self.analyze([detailed_row(1, correction_wait_start_us=110001)])
        self.assertEqual(r["client_costs_us"], {})
        self.assertEqual(r["unavailable_or_invalid"]["client_cost_partition"], 1)
        legacy = self.analyze([row(1)])
        self.assertEqual(legacy["client_costs_us"], {})
        self.assertEqual(legacy["buffer_updates"]["rows"], 0)

    def test_partition_total_uses_only_valid_stage_rows(self):
        r = self.analyze([detailed_row(1), detailed_row(2,
            present_end_us=130000, correction_wait_start_us=120001)])
        total = r["metrics"]["partitioned_client_processing_us"]
        self.assertEqual(total["count"], 1)
        self.assertEqual(total["mean"], 8000)
        self.assertEqual(r["metrics"]["client_processing_us"]["mean"], 9000)
        self.assertTrue(all(cost["count"] == 1 for cost in r["client_costs_us"].values()))
        self.assertIn("| Partitioned total | 1 | 8.000", report.markdown([r]))

    def test_gpu_ready_detail_requires_successful_measured_wait(self):
        r = self.analyze([detailed_row(1, gpu_ready_timing_valid=1,
            gpu_ready_wait_result_valid=0, gpu_ready_wait_result=0,
            gpu_ready_wait_start_us=113000, gpu_ready_time_us=113500)])
        self.assertNotIn("gpu_render_ready_wait_us", r["metrics"])
        r = self.analyze([detailed_row(1, gpu_ready_timing_valid=1,
            gpu_ready_wait_result_valid=1, gpu_ready_wait_result=0,
            gpu_ready_wait_start_us=113000, gpu_ready_time_us=113500)])
        self.assertEqual(r["metrics"]["gpu_render_ready_wait_us"]["mean"], 500)

    def test_buffer_growth_charges_the_delayed_frame(self):
        r = self.analyze([detailed_row(1), detailed_row(2, buffer_update_valid=1,
            buffer_action=2, buffer_attributed_frame=1, buffer_request_after_us=1250,
            decode_sync_wait_us=600)])
        event = r["buffer_updates"]["events"][0]
        self.assertEqual(event["change_us"], 250)
        self.assertEqual(event["attributed_frame"], 1)
        self.assertEqual(event["observed_client_costs_us"]["decode_sync"], 400)

    def test_capped_attempt_is_not_added_latency(self):
        r = self.analyze([detailed_row(1, buffer_update_valid=1, buffer_action=3,
            buffer_request_before_us=4000, buffer_request_after_us=4000,
            buffer_clipped_increase_us=250)])
        event = r["buffer_updates"]["events"][0]
        self.assertEqual(event["change_us"], 0)
        self.assertEqual(event["clipped_step_us"], 250)
        self.assertEqual(r["buffer_updates"]["actions"], {"growth capped": 1})

    def test_calibration_evidence_stays_separate_from_measured_costs(self):
        r = self.analyze([detailed_row(1, buffer_update_valid=1, buffer_action=2,
            buffer_request_after_us=1250, buffer_calibration_complete=1,
            buffer_calibration_samples=63, buffer_calibration_coverage_us=520000)])
        event = r["buffer_updates"]["events"][0]
        self.assertEqual(event["initial_calibration_complete"], 1)
        self.assertEqual(event["calibration_intervals"], 63)
        self.assertEqual(event["calibration_coverage_us"], 520000)
        self.assertEqual(sum(v["mean"] for v in r["client_costs_us"].values()), 8000)
        legacy = self.analyze([detailed_row(1, buffer_update_valid=1, buffer_action=2,
            buffer_request_after_us=1250)])["buffer_updates"]["events"][0]
        self.assertIsNone(legacy["initial_calibration_complete"])
        self.assertIsNone(legacy["calibration_intervals"])
        self.assertIsNone(legacy["calibration_coverage_us"])

    def test_stale_buffer_update_identity_is_rejected(self):
        r = self.analyze([detailed_row(1, buffer_update_valid=1, buffer_update_frame=2)])
        self.assertEqual(r["buffer_updates"]["rows"], 0)
        self.assertEqual(r["unavailable_or_invalid"]["buffer_update"], 1)

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
