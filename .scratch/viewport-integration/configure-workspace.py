"""Configure a new owned viewport workspace from the verified local feature flags."""
from pathlib import Path
import os
import re
import subprocess
import sys

rig = Path("/home/tarcila/Code/ANARI/vela")
workspace = Path(sys.argv[1]).resolve()
if workspace.parent != rig or not workspace.name.startswith("viewport-"):
    raise SystemExit("Refusing configuration outside owned viewport workspaces")
# The accepted integration tree is the current verified configuration source;
# completed older workspaces may legitimately have reclaimed their outputs.
source_cache = rig / "viewport-integration/_out/_cmake/CMakeCache.txt"
entries = {}
for line in source_cache.read_text().splitlines():
    match = re.match(r"([^#/:][^:]*):([^=]+)=(.*)$", line)
    if match:
        entries[match[1]] = (match[2], match[3])
build = workspace / "_out/_cmake"
install = workspace / "_out"
args = ["cmake", "-S", str(workspace), "-B", str(build), "-G", "Ninja Multi-Config",
        "-DCMAKE_CONFIGURATION_TYPES=Debug;RelWithDebInfo;Release",
        "-DCMAKE_INSTALL_PREFIX=" + str(install), "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
        "-DCMAKE_EXE_LINKER_FLAGS=-Wl,--compress-debug-sections=zlib"]
for key, (kind, value) in entries.items():
    if key.startswith("VSR_") and value in ("ON", "OFF"):
        args.append(f"-D{key}={value}")
    elif key == "BUILD_TESTING":
        args.append(f"-D{key}={value}")
    elif key in ("CMAKE_C_COMPILER", "CMAKE_CXX_COMPILER", "CMAKE_CUDA_COMPILER", "CMAKE_CUDA_HOST_COMPILER",
                 "CMAKE_CXX_FLAGS", "CMAKE_CUDA_FLAGS", "CMAKE_CUDA_FLAGS_INIT", "CMAKE_CUDA_ARCHITECTURES") and value:
        args.append(f"-D{key}={value}")
    elif key.endswith("_DIR") and kind in ("PATH", "FILEPATH") and value.startswith("/nix/store/") and "NOTFOUND" not in value:
        args.append(f"-D{key}={value}")
for needed in ("VSR_USE_CUDA", "VSR_USE_INTEGRATION", "VSR_USE_INTEGRATION_MCP", "VSR_USE_NETWORKING", "VSR_USE_LUA", "VSR_USE_SDL3", "VSR_BUILD_INTERACTIVE_APPS"):
    if entries.get(needed, (None, None))[1] != "ON":
        raise SystemExit(f"Verified configuration unexpectedly lacks {needed}")
if (build / "CMakeCache.txt").exists():
    previous = (build / "CMakeCache.txt").read_text()
    if f"CMAKE_HOME_DIRECTORY:INTERNAL={workspace}" not in previous:
        raise SystemExit("Existing output belongs to another source tree")
print("Configuring owned workspace", workspace, "with", len(args) - 8, "explicit verified options")
subprocess.run(args, check=True, cwd=workspace)
