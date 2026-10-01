#!/usr/bin/env python3
"""Verify an exported worker fixture and reject modified buffer diagnostics.

Usage: check_buffer_trace_audit.py /path/to/vrrreplay /path/to/warm-history.csv
The footer hash is repaired after each edit so only the semantic audit can fail.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("replay", type=Path)
    parser.add_argument("fixture", type=Path)
    args = parser.parse_args()
    replay = str(args.replay.resolve())
    original = args.fixture.read_bytes().splitlines(keepends=True)
    columns = original[0].decode().strip().split(",")
    selected = next(i for i, line in enumerate(original[1:], 1)
                    if not line.startswith(b"#") and
                    line.decode().strip().split(",")[columns.index("buffer_update_valid")] == "1")
    baseline = subprocess.run([replay, str(args.fixture.resolve()), "--require-exact-baseline"],
                              capture_output=True)
    assert baseline.returncode == 0, baseline.stderr.decode()
    keys = ["buffer_cap_us", "buffer_update_valid", "buffer_request_after_us",
            "buffer_attributed_frame", "buffer_interval_error_us", "buffer_lateness_us",
            "buffer_clipped_increase_us", "buffer_hold_remaining_us"]
    keys += [key for key in ("buffer_calibration_complete", "buffer_calibration_samples",
                            "buffer_calibration_coverage_us") if key in columns]
    with tempfile.TemporaryDirectory() as directory:
        for key in keys:
            lines = list(original)
            fields = lines[selected].decode().strip().split(",")
            column = columns.index(key)
            fields[column] = str(int(fields[column]) + 1)
            lines[selected] = (",".join(fields) + "\n").encode()
            footer_at = next(i for i, line in enumerate(lines) if line.startswith(b"#vrr_trace_footer,"))
            body = b"".join(lines[:footer_at])
            footer = lines[footer_at].decode().strip().split(",")
            footer = [f"decoded_sha256={hashlib.sha256(body).hexdigest()}"
                      if part.startswith("decoded_sha256=") else part for part in footer]
            path = Path(directory) / "modified.csv"
            path.write_bytes(body + (",".join(footer) + "\n").encode())
            result = subprocess.run([replay, str(path), "--require-exact-baseline"], capture_output=True)
            assert result.returncode == 3 and b"Buffer diagnostic drift" in result.stderr, (
                key, result.returncode, result.stderr.decode())
    print(f"Exact baseline and all {len(keys)} buffer-diagnostic rejection checks passed")


if __name__ == "__main__":
    main()
