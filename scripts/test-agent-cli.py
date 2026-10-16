# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Installed early-exit CLI tests; never activate a host or open a GUI."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

bin_dir = Path(sys.argv[1])
integration = bool(int(sys.argv[2]))
mcp = bool(int(sys.argv[3]))
with tempfile.TemporaryDirectory(prefix="agent-cli-", dir=os.environ.get("TMPDIR")) as tmp:
    root = Path(tmp)
    env = dict(os.environ)
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR"):
        env.pop(key, None)
    env.update(HOME=str(root / "home"), XDG_CONFIG_HOME=str(root / "config"),
               ANARI_LIBRARY="no_such_device", VSR_ANARI_LIBRARIES="no_such_device")

    def run(name, args, success):
        result = subprocess.run([str(bin_dir / name), *args], env=env, cwd=root,
                                capture_output=True, text=True, timeout=15)
        assert (result.returncode == 0) == success, (name, args, result.returncode, result.stderr)
        if success:
            assert "Usage:" in result.stdout and not result.stderr, result
        else:
            assert result.stderr, (name, args)
        assert not list(root.iterdir()), (name, args, list(root.iterdir()))
        return result.stdout

    help_text = run("vsrViewer", ["--help"], True)
    assert ("--agentIntegration" in help_text) == integration
    assert ("--agentAssetRoot" in help_text) == (integration and mcp)
    for args in (["--agentIntegration", "--agentAssetRoot"],
                 ["--agentIntegration", "--agentRestorationBytes", "0"],
                 ["--agentIntegration", "--agentRestorationHardBytes", "1"],
                 ["--agentIntegration", "--agentRestorationBytes", "18446744073709551616"],
                 ["--agentIntegration", "--agentAssetRoot", "relative"],
                 ["--agentAssetRoot", "/assets"],
                 ["--agentIntegration", "--agentTypo"],
                 ["--agentIntegration", "--agentAssetRoot=/assets"]):
        run("vsrViewer", args, False)
    if not integration:
        run("vsrViewer", ["--agentIntegration"], False)
    if integration:
        headless = bin_dir / "vsrTutorialIntegrationHeadless"
        if headless.exists():
            text = run(headless.name, ["--help"], True)
            assert ("--agentAssetRoot" in text) == mcp
            run(headless.name, ["-h"], True)
            for args in (["--unknown"], ["--agentAssetRoot"],
                         ["--agentRestorationBytes", "0"], ["--agentRestorationHardBytes", "1"]):
                run(headless.name, args, False)
    print("Installed viewer/headless host help and rejected-argv checks passed; no side effects")
