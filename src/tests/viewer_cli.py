# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Only early-exit argv: never start a GUI or open a real asset."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="viewer-cli-", dir=os.environ.get("TMPDIR")) as tmp:
    root = Path(tmp)
    env = dict(os.environ)
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR"):
        env.pop(key, None)
    env.update(HOME=str(root / "home"), XDG_CONFIG_HOME=str(root / "config"),
               ANARI_LIBRARY="no_such_device", VSR_ANARI_LIBRARIES="no_such_device")
    cases = [(["--help"], True), (["-h"], True),
             (["-obj", "does-not-exist", "--help"], True),
             (["--unknown"], False), (["-obj", "--unknown"], False),
             (["--secondaryView"], False), (["-l"], False),
             (["-ensight_fields"], False), (["-vtu_property"], False),
             (["--camera"], False), (["-vsr"], False),
             (["-vsr", "one", "-vsr", "two"], False)]
    for args, success in cases:
        run = subprocess.run([sys.argv[1], *args], cwd=root, env=env,
                             capture_output=True, text=True, timeout=15)
        assert (run.returncode == 0) == success, (args, run.returncode, run.stderr)
        if success:
            assert "Usage: vsrViewer" in run.stdout, run.stdout
            assert not run.stderr, run.stderr
        else:
            assert run.stderr and "requires a value" in run.stderr or (
                "Unknown option" in run.stderr or "Scene Archive" in run.stderr), run.stderr
        assert not list(root.iterdir()), (args, list(root.iterdir()))
    print(f"{len(cases)} installed early-exit cases passed; no config/runtime/assets created")
