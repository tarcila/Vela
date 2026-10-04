"""Fixture-private authenticated SDL/Xvfb; never opens the user's display."""
import os
import pathlib
import subprocess
import time
import secrets
import struct
import ctypes
import sys
import shlex

root = pathlib.Path.cwd()
work = root / '.scratch/viewport-integration'
device = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else 'native'
# Optional private evidence label: retain prior qualification logs/proofs.
artifact = '04' + ('-' + os.environ['VSR_VIEWER_TEST_LOG_LABEL']
                   if os.environ.get('VSR_VIEWER_TEST_LOG_LABEL') else '')
local = work / f'tmp/04-{mode}-{device}' / ('run-' + secrets.token_hex(6))
local.mkdir(parents=True, exist_ok=True)
for name in ('runtime', 'config'):
    path = local / name
    path.mkdir(exist_ok=True)
    path.chmod(0o700)
auth = local / 'xauthority'
auth.touch(mode=0o600, exist_ok=True)
parts = (b'', b'193', b'MIT-MAGIC-COOKIE-1', secrets.token_bytes(16))
auth.write_bytes(struct.pack('!H', 65535) + b''.join(struct.pack('!H', len(p)) + p for p in parts))
env = os.environ.copy()
env.update(XAUTHORITY=str(auth), LD_LIBRARY_PATH=f'{root}/_out/lib:' + env.get('LD_LIBRARY_PATH', ''),
           TMPDIR=str(work / 'tmp'), XDG_RUNTIME_DIR=str(local / 'runtime'), XDG_CONFIG_HOME=str(local / 'config'),
           SDL_VIDEODRIVER='x11', VSR_ANARI_LIBRARIES=device, ANARI_LIBRARY=device,
           VSR_VIEWER_TEST_ISOLATED='1', VSR_VIEWER_TEST_DEVICE=device)
env.pop('WAYLAND_DISPLAY', None)
pressure = mode.startswith('pressure')
if pressure:
    env['VSR_VIEWER_TEST_PRESSURE'] = '1'
    mode_tool = mode.removeprefix('pressure-')
else:
    mode_tool = mode
if mode_tool == 'fixture-native':
    env['VSR_VIEWER_TEST_NATIVE'] = '1'
    oracles = local / 'oracles'
    oracles.mkdir(exist_ok=True)
    done = local / 'native-done'
    done.unlink(missing_ok=True)
    env['VSR_VIEWER_TEST_WITHDRAW'] = str(done) + '-withdraw'
    env['VSR_VIEWER_TEST_ORACLES'] = str(oracles)
    env['VSR_VIEWER_TEST_DONE'] = str(done)
xlib = ctypes.CDLL('/nix/store/4wkicwb2jx5h63d79g5qyp9kn4vx27yb-libx11-1.8.13/lib/libX11.so.6')
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
viewer = client = xhandle = None
with (work / f'logs/{artifact}-{mode}-{device}-xvfb.log').open('w') as xlog, (work / f'logs/{artifact}-{mode}-{device}-viewer.log').open('w') as vlog:
    x = subprocess.Popen(['Xvfb', ':193', '-displayfd', '1', '-screen', '0', '4300x1200x24' if pressure else '1920x1080x24',
                          '-nolisten', 'unix', '-nolisten', 'local', '-listen', 'tcp', '-auth', str(auth)],
                         stdout=subprocess.PIPE, stderr=xlog, text=True, env=env)
    try:
        display = x.stdout.readline().strip()
        assert display.isdigit()
        env['DISPLAY'] = '127.0.0.1:' + display
        os.environ['XAUTHORITY'] = str(auth)
        for _ in range(100):
            xhandle = xlib.XOpenDisplay(env['DISPLAY'].encode())
            if xhandle:
                break
            time.sleep(.1)
        assert xhandle
        if mode_tool in ('probe', 'debug', 'memcheck', 'massif', 'nsys', 'sanitizer', 'baseline', 'baseline-memcheck', 'baseline-nsys'):
            test = '[.viewer-baseline]' if mode_tool.startswith('baseline') else '[.viewer-live]'
            command = [str(root / '_out/_cmake/RelWithDebInfo/vsrIntegrationTests'), test]
            if mode_tool == 'debug':
                command = ['gdb', '--batch', '-ex', 'run', '-ex', 'bt', '--args'] + command
            elif mode_tool in ('memcheck', 'baseline-memcheck'):
                command = ['valgrind', '--error-exitcode=99', '--leak-check=full',
                           '--show-leak-kinds=definite,indirect', '--errors-for-leak-kinds=definite,indirect'] + command
            elif mode_tool == 'massif':
                command = ['valgrind', '--tool=massif', '--detailed-freq=1',
                           '--massif-out-file=' + str(work / f'logs/{artifact}-{mode}-{device}.massif')] + command
            elif mode_tool == 'sanitizer':
                command = ['compute-sanitizer', '--tool', 'memcheck', '--target-processes', 'all',
                           '--error-exitcode', '99', '--leak-check', 'full'] + command
            elif mode_tool in ('nsys', 'baseline-nsys'):
                command = ['nsys', 'profile', '--trace=cuda,nvtx,osrt', '--sample=none', '--cpuctxsw=none',
                           '--force-overwrite=true', '--output=' + str(work / f'logs/{artifact}-{mode}-{device}')] + shlex.split(env.get('VSR_VIEWER_TEST_NSYS_ARGS', '')) + command
            viewer = subprocess.Popen(command, stdout=vlog, stderr=vlog, env=env)
            assert viewer.wait(timeout=300 if mode_tool in ('memcheck', 'baseline-memcheck', 'massif', 'nsys', 'baseline-nsys', 'sanitizer') else 120) == 0, 'real viewer submission fixture failed'
        elif mode_tool == 'fixture-native':
            viewer = subprocess.Popen([str(root / '_out/_cmake/RelWithDebInfo/vsrIntegrationTests'), '[.viewer-native]'], stdout=vlog, stderr=vlog, env=env)
            deadline = time.monotonic() + 90
            descriptor = None
            while time.monotonic() < deadline:
                assert viewer.poll() is None, 'real native test viewer exited before publication'
                matches = list((local / 'runtime/vsr').glob('vsr-mcp-*.json'))
                if matches:
                    assert len(matches) == 1
                    descriptor = matches[0]
                    break
                time.sleep(.1)
            assert descriptor
            node = '/nix/store/29zcshmp5hid6l4ckl2vg9bvnnrdr0sw-nodejs-slim-24.20.0/bin/node'
            proof = work / f'{artifact}-{device}-{mode}-proof.json'
            with (work / f'logs/{artifact}-{mode}-{device}-client.log').open('w') as clog:
                client = subprocess.Popen([node, str(work / '04-available-client.mjs'), str(descriptor), str(proof), device, str(done), str(oracles)], stdout=clog, stderr=clog, env=env)
                assert client.wait(timeout=90) == 0, 'native available-image client failed'
            assert viewer.wait(timeout=30) == 0, 'real native viewer qualification assertions failed'
        else:
            viewer = subprocess.Popen([str(root / '_out/_cmake/RelWithDebInfo/vsrViewer'), '--agentIntegration', '-gltf',
                '/data/gltf/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf'], stdout=vlog, stderr=vlog, env=env)
            deadline = time.monotonic() + 90
            descriptor = None
            while time.monotonic() < deadline:
                assert viewer.poll() is None
                matches = list((local / 'runtime/vsr').glob('vsr-mcp-*.json'))
                if matches:
                    assert len(matches) == 1
                    descriptor = matches[0]
                    break
                time.sleep(.1)
            assert descriptor
            proof = work / f'{artifact}-{device}-{mode}-proof.json'
            marker = local / 'resize-ready'
            proof.unlink(missing_ok=True)
            marker.unlink(missing_ok=True)
            node = '/nix/store/29zcshmp5hid6l4ckl2vg9bvnnrdr0sw-nodejs-slim-24.20.0/bin/node'
            with (work / f'logs/{artifact}-{mode}-{device}-client.log').open('w') as clog:
                client = subprocess.Popen([node, str(work / '04-live-client.mjs'), str(descriptor), str(proof), device, str(marker)], stdout=clog, stderr=clog, env=env)
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline and not proof.exists():
                    assert client.poll() is None, 'native MCP client failed'
                    time.sleep(.05)
                assert proof.exists()
                xlib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
                xlib.XDefaultRootWindow.restype = ctypes.c_ulong
                xlib.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
                root_window = xlib.XDefaultRootWindow(xhandle)
                root_out, parent_out = ctypes.c_ulong(), ctypes.c_ulong()
                children = ctypes.POINTER(ctypes.c_ulong)()
                count = ctypes.c_uint()
                assert xlib.XQueryTree(xhandle, root_window, ctypes.byref(root_out), ctypes.byref(parent_out), ctypes.byref(children), ctypes.byref(count))
                # This authenticated X server belongs exclusively to this fixture;
                # no user's PID/window/controller can occur on it.
                xlib.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
                candidates = []
                for i in range(count.value):
                    gx, gy = ctypes.c_int(), ctypes.c_int()
                    width, height, border, depth = (ctypes.c_uint() for _ in range(4))
                    assert xlib.XGetGeometry(xhandle, children[i], ctypes.byref(root_out), ctypes.byref(gx), ctypes.byref(gy), ctypes.byref(width), ctypes.byref(height), ctypes.byref(border), ctypes.byref(depth))
                    if width.value > 300 and height.value > 300:
                        candidates.append(children[i])
                assert len(candidates) == 1, 'private SDL viewer window is not unique'
                xlib.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
                xlib.XFlush.argtypes = [ctypes.c_void_p]
                xlib.XFree.argtypes = [ctypes.c_void_p]
                xlib.XResizeWindow(xhandle, candidates[0], 1280, 960)
                xlib.XMoveWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_int, ctypes.c_int]
                xlib.XMoveWindow(xhandle, candidates[0], 0, 0)
                xlib.XFlush(xhandle)
                xlib.XFree(children)
                marker.touch()
                assert client.wait(timeout=180 if env.get('VSR_PRODUCTION_LIFECYCLE') else 90) == 0
                marker.unlink(missing_ok=True)
        print(device, mode, 'PASS; fixture assertions/logs define the exact qualification scope')
    finally:
        if xhandle:
            xlib.XCloseDisplay(xhandle)
        for process in (client, viewer, x):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        auth.unlink(missing_ok=True)
