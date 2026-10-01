#!/usr/bin/env python3
"""Independently verify the optional ZIP exported by tst_vrrdiagnostics."""
import json
from pathlib import Path
import sys
import zipfile


def main():
    path = Path(sys.argv[1])
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None, "ZIP CRC or data descriptor mismatch"
        assert set(archive.namelist()) == {"Moonlight.log", "Moonlight.vrrtrace", "capture-info.json"}
        assert archive.read("Moonlight.log") == b"already redacted test log\n"
        assert archive.read("Moonlight.vrrtrace") == b"compressed trace fixture\n"
        info = json.loads(archive.read("capture-info.json"))
        assert "comparison_arm" not in info and "timing_policy" not in info
        assert info["requested_fps"] == 116 and info["deep_trace"] is True
        assert info["clean_session_close"] is True and info["trace_files"] == 1
        assert len(info["executable_sha256"]) == 64
    print("Diagnostic ZIP passed independent CRC, content and manifest checks")


if __name__ == "__main__":
    main()
