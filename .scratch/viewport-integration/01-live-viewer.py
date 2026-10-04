"""Start and stop only this fixture's Xvfb/viewer/client processes."""
import os
import pathlib
import subprocess
import time
import secrets
import struct
import ctypes

root = pathlib.Path.cwd()
work = root / '.scratch/viewport-integration'
local = work / 'tmp/01-live'
local.mkdir(parents=True, exist_ok=True)
for name in ('runtime', 'config'):
    path = local / name
    path.mkdir(exist_ok=True)
    path.chmod(0o700)
env = os.environ.copy()
auth = local / 'xauthority'
# FamilyWild avoids localhost's Unix-vs-TCP family normalization. The cookie
# is fixture-private and never logged or passed on a process command line.
auth.touch(mode=0o600, exist_ok=True)
parts = (b'', b'191', b'MIT-MAGIC-COOKIE-1', secrets.token_bytes(16))
auth.write_bytes(struct.pack('!H', 65535)
                 + b''.join(struct.pack('!H', len(p)) + p for p in parts))
env.update(XAUTHORITY=str(auth),
           LD_LIBRARY_PATH=f'{root}/_out/lib:' + env.get('LD_LIBRARY_PATH', ''),
           TMPDIR=str(work / 'tmp'), XDG_RUNTIME_DIR=str(local / 'runtime'),
           XDG_CONFIG_HOME=str(local / 'config'), SDL_VIDEODRIVER='x11',
           VSR_ANARI_LIBRARIES='helide', ANARI_LIBRARY='helide')
node = '/nix/store/29zcshmp5hid6l4ckl2vg9bvnnrdr0sw-nodejs-slim-24.20.0/bin/node'
asset = '/data/gltf/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf'
viewer = None
xHandle = None
xlib = None
with (work / 'logs/01-xvfb.txt').open('w') as xlog, (work / 'logs/01-viewer.txt').open('w') as vlog:
    x = subprocess.Popen(['Xvfb', ':191', '-displayfd', '1', '-screen', '0', '1024x768x24',
                          '-nolisten', 'unix', '-nolisten', 'local', '-listen', 'tcp', '-auth', str(auth)],
                         stdout=subprocess.PIPE, stderr=xlog, text=True, env=env)
    try:
        display = x.stdout.readline().strip()
        assert display.isdigit(), 'Xvfb did not allocate a private display'
        print('private display:', display)
        env['DISPLAY'] = '127.0.0.1:' + display
        env.pop('WAYLAND_DISPLAY', None)
        # displayfd announces the socket before device initialization is fully
        # ready. Probe only this fixture display, using its private auth cookie.
        os.environ['XAUTHORITY'] = str(auth)
        xlib = ctypes.CDLL('/nix/store/4wkicwb2jx5h63d79g5qyp9kn4vx27yb-libx11-1.8.13/lib/libX11.so.6')
        xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        xlib.XOpenDisplay.restype = ctypes.c_void_p
        xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        for _ in range(100):
            xHandle = xlib.XOpenDisplay(env['DISPLAY'].encode())
            if xHandle:
                # Keep one connection alive: closing the last X client resets
                # the server and races SDL's availability probe.
                break
            time.sleep(.1)
        else:
            raise AssertionError('fixture X server not ready')
        command = [str(root / '_out/_cmake/RelWithDebInfo/vsrViewer'),
                   '--agentIntegration', '-gltf', asset]
        viewer = subprocess.Popen(command, stdout=vlog, stderr=vlog, env=env)
        deadline = time.monotonic() + 90
        descriptor = None
        while time.monotonic() < deadline:
            assert viewer.poll() is None, 'isolated viewer exited before MCP registration'
            matches = list((local / 'runtime/vsr').glob('vsr-mcp-*.json'))
            if matches:
                assert len(matches) == 1
                descriptor = matches[0]
                break
            time.sleep(.1)
        assert descriptor, 'isolated viewer did not publish a descriptor'
        with (work / 'logs/01-live-client.txt').open('w') as log:
            subprocess.run([node, str(work / '01-live-client.mjs'), str(descriptor),
                            str(work / '01-live-proof.json')],
                           check=True, timeout=90, stdout=log, stderr=log, env=env)
        print('isolated viewer + installed Pi client: PASS')
    finally:
        if xHandle:
            xlib.XCloseDisplay(xHandle)
        for process in (viewer, x):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        auth.unlink(missing_ok=True)
