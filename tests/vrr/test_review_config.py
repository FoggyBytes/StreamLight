#!/usr/bin/env python3
"""Keep controller experiments isolated from historical replay defaults."""
import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("review_config", ROOT / "scripts/make-vrr-review-config.py")
review = importlib.util.module_from_spec(spec)
spec.loader.exec_module(review)


class ReviewConfigTest(unittest.TestCase):
    def setUp(self):
        self.base = {"timestamp_playout_enabled": 1, "playout_responsive_buffer": 7,
                     "playout_smoothing_gain_per_mille": 500, "playout_delay_max_us": 16000,
                     "playout_delay_cap_source_period_per_mille": 500}
        self.summary = {"simulation": {"resolved_parameters": {"controller": self.base}}}
        self.variants = json.loads((ROOT / "tests/vrr/configs/vrr17-review-variants.json").read_text())

    def test_preserves_complete_base_without_mutating_inputs(self):
        original = copy.deepcopy((self.summary, self.variants))
        config = review.make_config(self.summary, self.variants)
        self.assertEqual(config["parameters"]["controller"], self.base)
        self.assertEqual(config["scenarios"][0], {"name": "session-policy", "mode": "fixed"})
        smoothing = next(s for s in config["scenarios"] if s["name"] == "rtp-cadence")
        self.assertEqual(smoothing["parameters"]["controller"], {"playout_smoothing_gain_per_mille": 0})
        config["parameters"]["controller"]["playout_delay_max_us"] = 1
        smoothing["parameters"]["controller"]["playout_smoothing_gain_per_mille"] = 999
        self.assertEqual((self.summary, self.variants), original)

    def test_accepts_session_policy_from_batch(self):
        batch = {"scenarios": [dict(self.summary, scenario="session-policy")]}
        self.assertEqual(review.make_config(batch, self.variants),
                         review.make_config(self.summary, self.variants))

    def test_rejects_historical_policy(self):
        self.base["playout_responsive_buffer"] = 3
        with self.assertRaises(ValueError):
            review.make_config(self.summary, self.variants)

    def test_variant_file_is_not_a_runnable_config(self):
        self.assertNotIn("config_schema", self.variants)
        self.variants["review_schema"] = 2
        with self.assertRaises(ValueError):
            review.make_config(self.summary, self.variants)


if __name__ == "__main__":
    unittest.main()
