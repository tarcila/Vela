"""Fixture-private real viewers; never connects to the user's viewer/display."""
import os
import pathlib
import subprocess
import time
import secrets
import struct
import ctypes
import sys

root = pathlib.Path.cwd()
work = root / '.scratch/viewport-integration'
device = sys.argv[1] if len(sys.argv) > 1 else 'helide'
local = work / ('tmp/02-live-' + device)
local.mkdir(parents=True, exist_ok=True)
for name in ('runtime', 'config'):
    path = local / name
    path.mkdir(exist_ok=True)
    path.chmod(0o700)
env = os.environ.copy()
auth = local / 'xauthority'
auth.touch(mode=0o600, exist_ok=True)
parts = (b'', b'192', b'MIT-MAGIC-COOKIE-1', secrets.token_bytes(16))
auth.write_bytes(struct.pack('!H', 65535) + b''.join(struct.pack('!H', len(p)) + p for p in parts))
env.update(XAUTHORITY=str(auth), LD_LIBRARY_PATH=f'{root}/_out/lib:' + env.get('LD_LIBRARY_PATH', ''),
           TMPDIR=str(work / 'tmp'), XDG_RUNTIME_DIR=str(local / 'runtime'), XDG_CONFIG_HOME=str(local / 'config'),
           SDL_VIDEODRIVER='x11', VSR_ANARI_LIBRARIES=device, ANARI_LIBRARY=device)
node = '/nix/store/29zcshmp5hid6l4ckl2vg9bvnnrdr0sw-nodejs-slim-24.20.0/bin/node'
viewer = None
xhandle = None
xlib = ctypes.CDLL('/nix/store/4wkicwb2jx5h63d79g5qyp9kn4vx27yb-libx11-1.8.13/lib/libX11.so.6')
xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
xlib.XOpenDisplay.restype = ctypes.c_void_p
xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
with (work / f'logs/02-{device}-xvfb.txt').open('w') as xlog, (work / f'logs/02-{device}-viewer.txt').open('w') as vlog:
    x = subprocess.Popen(['Xvfb', ':192', '-displayfd', '1', '-screen', '0', '1024x768x24',
                          '-nolisten', 'unix', '-nolisten', 'local', '-listen', 'tcp', '-auth', str(auth)],
                         stdout=subprocess.PIPE, stderr=xlog, text=True, env=env)
    try:
        display = x.stdout.readline().strip()
        assert display.isdigit()
        env['DISPLAY'] = '127.0.0.1:' + display
        env.pop('WAYLAND_DISPLAY', None)
        os.environ['XAUTHORITY'] = str(auth)
        for _ in range(100):
            xhandle = xlib.XOpenDisplay(env['DISPLAY'].encode())
            if xhandle:
                break
            time.sleep(.1)
        assert xhandle, 'fixture X server unavailable'
        viewer = subprocess.Popen([str(root / '_out/_cmake/RelWithDebInfo/vsrViewer'), '--agentIntegration',
                                   '-gltf', '/data/gltf/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf'],
                                  stdout=vlog, stderr=vlog, env=env)
        deadline = time.monotonic() + 90
        descriptor = None
        while time.monotonic() < deadline:
            assert viewer.poll() is None, 'fixture viewer exited before registration'
            matches = list((local / 'runtime/vsr').glob('vsr-mcp-*.json'))
            if matches:
                assert len(matches) == 1
                descriptor = matches[0]
                break
            time.sleep(.1)
        assert descriptor
        proof = work / f'02-{device}-proof.json'
        marker = local / 'resize-ready'
        proof.unlink(missing_ok=True)
        marker.unlink(missing_ok=True)
        with (work / f'logs/02-{device}-client.txt').open('w') as log:
            client = subprocess.Popen([node, str(work / '02-live-client.mjs'), str(descriptor),
                                       str(proof), device, str(marker)], stdout=log, stderr=log, env=env)
            try:
                deadline = time.monotonic() + 90
                while time.monotonic() < deadline and not proof.exists():
                    assert client.poll() is None, 'metadata client failed before resize'
                    time.sleep(.05)
                assert proof.exists(), 'no baseline metadata proof'
                # This authenticated display contains only this fixture's SDL
                # viewer. Resize its actual top-level window, not a HUD estimate.
                xlib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
                xlib.XDefaultRootWindow.restype = ctypes.c_ulong
                xlib.XQueryTree.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                           ctypes.POINTER(ctypes.c_ulong), ctypes.POINTER(ctypes.c_ulong),
                                           ctypes.POINTER(ctypes.POINTER(ctypes.c_ulong)), ctypes.POINTER(ctypes.c_uint)]
                root_window = xlib.XDefaultRootWindow(xhandle)
                root_out, parent_out = ctypes.c_ulong(), ctypes.c_ulong()
                children = ctypes.POINTER(ctypes.c_ulong)()
                count = ctypes.c_uint()
                assert xlib.XQueryTree(xhandle, root_window, ctypes.byref(root_out), ctypes.byref(parent_out),
                                      ctypes.byref(children), ctypes.byref(count))
                xlib.XGetGeometry.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.POINTER(ctypes.c_ulong),
                                              ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
                                              ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint),
                                              ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
                candidates = []
                for i in range(count.value):
                    gx, gy = ctypes.c_int(), ctypes.c_int()
                    width, height, border, depth = (ctypes.c_uint() for _ in range(4))
                    assert xlib.XGetGeometry(xhandle, children[i], ctypes.byref(root_out),
                                            ctypes.byref(gx), ctypes.byref(gy), ctypes.byref(width),
                                            ctypes.byref(height), ctypes.byref(border), ctypes.byref(depth))
                    if width.value > 300 and height.value > 300:
                        candidates.append(children[i])
                assert len(candidates) == 1, 'no unique private SDL viewer window'
                window = candidates[0]
                xlib.XResizeWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_uint, ctypes.c_uint]
                xlib.XFlush.argtypes = [ctypes.c_void_p]
                xlib.XFree.argtypes = [ctypes.c_void_p]
                xlib.XResizeWindow(xhandle, window, 1280, 960)
                xlib.XFlush(xhandle)
                xlib.XFree(children)
                marker.touch()
                assert client.wait(timeout=90) == 0, 'metadata resize client failed'
            finally:
                if client.poll() is None:
                    client.terminate()
                    client.wait(timeout=10)
                marker.unlink(missing_ok=True)
        print(device, 'real viewer metadata PASS')
    finally:
        if xhandle:
            xlib.XCloseDisplay(xhandle)
        for process in (viewer, x):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        auth.unlink(missing_ok=True)
