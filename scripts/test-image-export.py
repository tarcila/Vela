#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Verify eligible Image exchange through a freshly installed isolated viewer.

Usage: python3 scripts/test-image-export.py build-agent [--import] [--local-ui]
--import adds external raw processing, shared users and normal exact Restore.
--local-ui instead pauses for the private viewer's Agent Activity Restore action:
resize the private window if necessary, Disable agent edits, click Restore, then
create build-agent/t05-ui-proceed. Screenshot/display metadata are written under
build-agent. The 180-second pause needs Pillow; ordinary verification does not.
Requires Xvfb, xauth, xset and the qualified Helide Image-writing host.
No existing display, viewer, configuration or Pi descriptor is touched.
"""

import base64
import binascii
import http.client
import json
import os
from pathlib import Path
import secrets
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import time
import zlib


def require(condition, description):
    if not condition:
        raise AssertionError(description)


def chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", binascii.crc32(kind + payload)))


def fixture(directory):
    samples = bytes([17, 29, 41, 0, 3, 5, 7, 255, 101, 103, 107, 127,
                     211, 223, 227, 1, 13, 19, 23, 254, 31, 37, 43, 128])
    filtered = b"\0" + samples[:12] + b"\0" + samples[12:]
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", 3, 2, 8, 6, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(filtered)) + chunk(b"IEND", b""))
    (directory / "image.png").write_bytes(png)
    geometry = struct.pack("<12f", -1, -1, 0, 1, -1, 0, 1, 1, 0, -1, 1, 0)
    geometry += struct.pack("<8f", 0, 1, 1, 1, 1, 0, 0, 0)
    geometry += struct.pack("<6H", 0, 1, 2, 0, 2, 3)
    gltf = {
        "asset": {"version": "2.0"}, "scene": 0,
        "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "buffers": [{"byteLength": len(geometry), "uri":
                     "data:application/octet-stream;base64,"
                     + base64.b64encode(geometry).decode()}],
        "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 48},
                        {"buffer": 0, "byteOffset": 48, "byteLength": 32},
                        {"buffer": 0, "byteOffset": 80, "byteLength": 12}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4,
             "type": "VEC3", "min": [-1, -1, 0], "max": [1, 1, 0]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"}],
        "images": [{"uri": "image.png"}], "textures": [{"source": 0}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                  "metallicFactor": 0}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "TEXCOORD_0": 1},
                                     "indices": 2, "material": 0}]}],
    }
    # Distinct Samplers sharing one cache source, not duplicated texels.
    gltf["materials"].append(dict(gltf["materials"][0]))
    gltf["meshes"][0]["primitives"].append(
        dict(gltf["meshes"][0]["primitives"][0], material=1))
    path = directory / "fixture.gltf"
    path.write_text(json.dumps(gltf))
    return path, samples


def decode_export(path):
    png = path.read_bytes()
    require(png[:8] == b"\x89PNG\r\n\x1a\n", "PNG signature")
    offset, kinds, data = 8, [], b""
    shape = None
    while offset < len(png):
        length, = struct.unpack_from(">I", png, offset)
        kind = png[offset + 4:offset + 8]
        payload = png[offset + 8:offset + 8 + length]
        crc, = struct.unpack_from(">I", png, offset + 8 + length)
        require(crc == binascii.crc32(kind + payload), "PNG CRC")
        kinds.append(kind)
        if kind == b"IHDR":
            shape = struct.unpack(">IIBBBBB", payload)
        if kind == b"IDAT":
            data += payload
        offset += length + 12
    require(kinds == [b"IHDR", b"IDAT", b"IEND"], "raw PNG chunk policy")
    require(shape == (3, 2, 8, 6, 0, 0, 0), "native shape/channel policy")
    rows = zlib.decompress(data)
    require(len(rows) == 26 and rows[0] == rows[13] == 0, "exact filter/output bound")
    return rows[1:13] + rows[14:26]


def main():
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build-agent").resolve()
    verify_import = "--import" in sys.argv[2:]
    verify_local_ui = "--local-ui" in sys.argv[2:]
    require(not verify_local_ui or verify_import, "--local-ui requires --import")
    viewer = build / "vsrViewer"
    require(viewer.is_file() and (build / "install/lib").is_dir(),
            "build the viewer and install its libraries before verification")
    for command in ("Xvfb", "xauth", "xset"):
        require(shutil.which(command), f"missing {command}")
    with tempfile.TemporaryDirectory(prefix="ticket04-image-host-") as temporary:
        root = Path(temporary)
        runtime, config = root / "runtime", root / "config"
        runtime.mkdir(mode=0o700)
        config.mkdir(mode=0o700)
        asset, expected = fixture(root)
        fixture_source = (root / "image.png").read_bytes()
        display = next(n for n in range(94, 200)
                       if not Path(f"/tmp/.X{n}-lock").exists()
                       and not Path(f"/tmp/.X11-unix/X{n}").exists())
        auth = root / "xauth"
        auth.touch(mode=0o600)
        subprocess.run(["xauth", "-f", str(auth), "add", f"localhost:{display}",
                        "MIT-MAGIC-COOKIE-1", secrets.token_hex(16)], check=True)
        env = dict(os.environ, DISPLAY=f"localhost:{display}", XAUTHORITY=str(auth),
                   XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(config),
                   SDL_VIDEODRIVER="x11", ANARI_LIBRARY="helide",
                   VSR_ANARI_LIBRARIES="helide", TMPDIR=str(root))
        inherited = [p for p in env.get("LD_LIBRARY_PATH", "").split(":")
                     if p and "/pi-integration/_out/lib" not in p]
        env["LD_LIBRARY_PATH"] = ":".join([str(build / "install/lib"), str(build)] + inherited)
        with (root / "host.log").open("wb") as log:
            xvfb = subprocess.Popen(["Xvfb", f":{display}", "-screen", "0", "800x600x24",
                                     "-listen", "tcp", "-nolisten", "unix",
                                     "-auth", str(auth), "-noreset"],
                                    stdout=log, stderr=log, env=env)
            host = None
            try:
                deadline = time.monotonic() + 15
                while subprocess.run(["xset", "q"], env=env, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, timeout=2).returncode:
                    require(xvfb.poll() is None and time.monotonic() < deadline, "private X readiness")
                    time.sleep(0.1)
                host = subprocess.Popen([str(viewer), "--agentIntegration", "-gltf", str(asset)],
                                        stdout=log, stderr=log, env=env)
                deadline = time.monotonic() + 30
                descriptors = []
                while not descriptors:
                    require(host.poll() is None and time.monotonic() < deadline, "private viewer activation")
                    descriptors = list((runtime / "vsr").glob("vsr-mcp-*.json"))
                    time.sleep(0.05)
                require(len(descriptors) == 1, "fresh launch descriptor")
                descriptor = json.loads(descriptors[0].read_text())
                exchange = descriptor["image_exchange"]
                require(exchange["enabled"] and exchange["export"],
                        "qualified serialized Image exchange")
                if verify_import:
                    require(exchange["import"], "fully bound recoverable Image Import")
                require("available_session_snapshot_bytes" not in exchange["exchange_limits"],
                        "no launch-wide session ledger leak")
                directory = Path(exchange["image_exchange_directory"])
                require(stat.S_IMODE(directory.stat().st_mode) == 0o700, "private directory mode")
                session = {}

                def rpc(method, params, expected_status=200):
                    headers = {"Authorization": "Bearer " + descriptor["launch_credential"],
                               "Content-Type": "application/json"}
                    if session:
                        headers.update({"VSR-Session-ID": session["session_id"],
                                        "VSR-Session-Proof": "Bearer " + session["session_secret"]})
                    connection = http.client.HTTPConnection(descriptor["host"], descriptor["port"], timeout=10)
                    try:
                        connection.request("POST", descriptor["path"],
                                           json.dumps({"jsonrpc": "2.0", "id": 1,
                                                       "method": method, "params": params}), headers)
                        response = connection.getresponse()
                        value = json.loads(response.read())
                        require(response.status == expected_status, "authenticated RPC status")
                        if expected_status != 200:
                            return value["error"]
                        require("result" in value, "authenticated RPC result")
                        return value["result"]
                    finally:
                        connection.close()

                session.update(rpc("vsr/attach", {}))
                next_id = 0

                def tool(name, arguments):
                    return rpc("tools/call", {"name": name, "arguments": arguments})["structuredContent"]

                def operation(name, arguments):
                    nonlocal next_id
                    next_id += 1
                    arguments = dict(arguments, request_id=next_id)
                    admitted = tool(name, arguments)
                    until = time.monotonic() + 35
                    result = admitted
                    while result["state"] != "terminal":
                        require(time.monotonic() < until, "bounded operation deadline")
                        time.sleep(0.01)
                        result = tool("vsr_operation_status", {"operation_ref": admitted["operation_ref"]})
                    require(result["outcome"] == "ok", f"{name} terminal success")
                    return result, arguments

                discovered, _ = operation("vsr_discover", {})
                scene = next(item for item in discovered["result"]["items"] if item["kind"] == "scene")
                inspected, _ = operation("vsr_inspect", {"scene_id": scene["id"]})
                image = next(item for item in inspected["result"]["items"]
                             if item.get("image_info", {}).get("state") == "available")
                info = image["image_info"]
                require(info["dimensions"] == [3, 2] and info["channel_count"] == 4,
                        "eligible imported Image")
                exported, arguments = operation("vsr_image_export", {"scene_id": scene["id"],
                                                                       "handle": image["handle"]})
                receipt = exported["result"]
                require(exported["kind"] == "image_export" and not exported["committed"], "read-only kind")
                require(receipt["data_version"] == info["data_version"]
                        and receipt["color_space"] == "srgb" and receipt["row_order"] == "top_down",
                        "captured metadata/version")
                file = directory / receipt["file_name"]
                require(stat.S_IMODE(file.stat().st_mode) == 0o600 and file.stat().st_nlink == 1,
                        "private unique regular file")
                require(decode_export(file) == expected, "eligible host raw samples including transparent colors")
                file.unlink()
                replay = tool("vsr_image_export", arguments)
                require(replay["replayed"] and replay["result"] == receipt and not file.exists(),
                        "historical replay does not regenerate")
                if verify_import:
                    tools = rpc("tools/list", {})["tools"]
                    schema = next(t["inputSchema"] for t in tools
                                  if t["name"] == "vsr_image_import")
                    require(schema["additionalProperties"] is False,
                            "strict published Import schema")
                    changed = bytearray(expected)
                    for index in range(0, len(changed), 4):
                        changed[index], changed[index + 1] = changed[index + 1], changed[index]
                    rows = b"\0" + changed[:12] + b"\0" + changed[12:]
                    processed = directory / "processed.png"
                    processed.write_bytes(b"\x89PNG\r\n\x1a\n"
                                          + chunk(b"IHDR", struct.pack(">IIBBBBB", 3, 2, 8, 6, 0, 0, 0))
                                          + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
                    imported, import_args = operation("vsr_image_import", {
                        "scene_id": scene["id"], "handle": image["handle"],
                        "data_version": info["data_version"], "file_name": processed.name})
                    imported_receipt = imported["result"]
                    require(imported["kind"] == "image_import" and imported["committed"],
                            "normal committing Import")
                    require(imported_receipt["data_version"] != info["data_version"]
                            and len(imported_receipt["affected_samplers"]) == 2
                            and imported_receipt["affected_complete"], "shared cached users/version")
                    observed, _ = operation("vsr_image_export", {"scene_id": scene["id"],
                                                               "handle": image["handle"]})
                    require(decode_export(directory / observed["result"]["file_name"]) == changed,
                            "all raw imported samples including transparent colors")
                    processed.unlink()
                    retry = tool("vsr_image_import", import_args)
                    require(retry["replayed"] and retry["result"] == imported_receipt,
                            "Import replay never rereads deleted PNG")
                    if verify_local_ui:
                        from PIL import ImageGrab
                        rpc("vsr/detach", {})
                        session.clear()
                        (build / "t05-ui-env.json").write_text(json.dumps({
                            "DISPLAY": env["DISPLAY"], "XAUTHORITY": env["XAUTHORITY"]}))
                        (build / "t05-ui-env.json").chmod(0o600)
                        os.environ["XAUTHORITY"] = env["XAUTHORITY"]
                        time.sleep(1)
                        ImageGrab.grab(xdisplay=env["DISPLAY"]).save(build / "t05-ui.png")
                        proceed = build / "t05-ui-proceed"
                        proceed.unlink(missing_ok=True)
                        print("PRIVATE_UI_READY: Disable agent edits and click the eligible "
                              "Image batch's Restore; create " + str(proceed), flush=True)
                        until = time.monotonic() + 180
                        while not proceed.exists():
                            require(time.monotonic() < until, "bounded local UI recovery")
                            time.sleep(0.1)
                        session.update(rpc("vsr/attach", {}))
                        denied = rpc("tools/call", {"name": "vsr_image_import", "arguments": {
                            "request_id": next_id + 1, "scene_id": scene["id"],
                            "handle": image["handle"], "data_version": imported_receipt["data_version"],
                            "file_name": "absent.png"}}, expected_status=403)
                        require(denied["data"]["code"] == "forbidden",
                                "actual UI Disable edits blocks Import before admission")
                        observed_local, _ = operation("vsr_image_export", {
                            "scene_id": scene["id"], "handle": image["handle"]})
                        require(decode_export(directory / observed_local["result"]["file_name"])
                                == expected, "Agent Activity local Restore after disconnect")
                        print("IMAGE_LOCAL_UI_ACCEPTANCE disconnected=true edits_disabled=true "
                              "read_export=true exact_bytes=24 restored=true")
                    else:
                        restored, _ = operation("vsr_restore", {"scene_id": scene["id"],
                            "target_batch_ref": imported_receipt["batch_ref"]})
                        restore_receipt = restored["result"]
                        require(restored["kind"] == "restore" and restored["committed"]
                                and restore_receipt["versions"] == []
                                and restore_receipt["node_versions"] == []
                                and restore_receipt["image_versions"][0]["data_version"]
                                    != imported_receipt["data_version"], "one-way fresh Image restore receipt")
                        recovered, _ = operation("vsr_image_export", {"scene_id": scene["id"],
                                                                    "handle": image["handle"]})
                        require(decode_export(directory / recovered["result"]["file_name"]) == expected,
                                "exact prior texels restored in same cached Array")
                        print("IMAGE_IMPORT_ACCEPTANCE host=vsrViewer/Linux/Helide shared_samplers=2 "
                              "exact_bytes=24 normal_restore=true replay=deleted_input receipt="
                              + json.dumps(imported_receipt) + " restore=" + json.dumps(restore_receipt))
                    require(fixture_source == (root / "image.png").read_bytes(),
                            "original asset unchanged")
                rpc("vsr/detach", {})
                host.terminate()
                require(host.wait(timeout=15) == 0, "normal viewer exit")
                require(not directory.exists() and not descriptors[0].exists(), "normal launch cleanup")
                print("IMAGE_EXPORT_ACCEPTANCE host=vsrViewer/Linux/Helide serialized_array_writes=true "
                      "dimensions=3x2 channels=4 exact_bytes=24 transparent_colors=preserved "
                      "descriptor=strict replay=historical cleanup=normal receipt=" + json.dumps(receipt))
            except Exception:
                print((root / "host.log").read_text(errors="replace"), file=sys.stderr)
                raise
            finally:
                for process in (host, xvfb):
                    if process and process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)


if __name__ == "__main__":
    main()
