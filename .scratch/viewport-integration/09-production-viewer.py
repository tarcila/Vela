"""Ticket 09 private production query. Never contacts the user's display/config."""
import ctypes
import os
import pathlib
import secrets
import struct
import subprocess
import sys
import time

root = pathlib.Path.cwd()
assert root == pathlib.Path('/home/tarcila/Code/ANARI/vela/viewport-world-bounds')
work = root / '.scratch/viewport-integration'
device = sys.argv[1]
assert device in ('barney', 'helide')
local = work / 'tmp' / ('09-' + device + '-' + secrets.token_hex(6))
local.mkdir(parents=True)
for name in ('runtime', 'config', 'oracle'):
    (local / name).mkdir(mode=0o700)
display = '193' # same proven private TCP/Xauthority route as ticket 04/07
auth = local / 'xauthority'
parts = (b'', display.encode(), b'MIT-MAGIC-COOKIE-1', secrets.token_bytes(16))
auth.write_bytes(struct.pack('!H', 65535) + b''.join(struct.pack('!H', len(p)) + p for p in parts))
auth.chmod(0o600)
env = os.environ.copy()
env.update(XAUTHORITY=str(auth), LD_LIBRARY_PATH=f'{root}/_out/lib:' + env.get('LD_LIBRARY_PATH', ''),
           TMPDIR=str(work / 'tmp'), XDG_RUNTIME_DIR=str(local / 'runtime'), XDG_CONFIG_HOME=str(local / 'config'),
           SDL_VIDEODRIVER='x11', VSR_ANARI_LIBRARIES=device, ANARI_LIBRARY=device,
           VSR_BOUNDS_ORACLE=str(local / 'oracle'), LD_PRELOAD=str(work / 'logs/09-production-oracle.so'))
env.pop('WAYLAND_DISPLAY', None)
fixture = local / 'literal.obj'
fixture.write_text('o LiteralTriangle\nv -2 -3 -4\nv 5 -3 -4\nv -2 6 7\nf 1 2 3\n')
node = '/nix/store/503mh0fj8j1cfvrxcavr76szbn4dk2r7-nodejs-slim-24.21.0/bin/node'
x = viewer = client = handle = None
xlib = None
with (work / f'logs/09-production-{device}-xvfb.log').open('w') as xlog, (work / f'logs/09-production-{device}-viewer.log').open('w') as vlog, (work / f'logs/09-production-{device}-client.log').open('w') as clog:
    try:
        x = subprocess.Popen(['Xvfb', ':' + display, '-displayfd', '1', '-screen', '0', '1920x1080x24',
                              '-nolisten', 'unix', '-nolisten', 'local', '-listen', 'tcp', '-auth', str(auth)],
                             stdout=subprocess.PIPE, stderr=xlog, text=True, env=env)
        actual = x.stdout.readline().strip()
        assert actual == display
        env['DISPLAY'] = '127.0.0.1:' + actual
        os.environ['XAUTHORITY'] = str(auth)
        xlib = ctypes.CDLL('/nix/store/4wkicwb2jx5h63d79g5qyp9kn4vx27yb-libx11-1.8.13/lib/libX11.so.6')
        xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        xlib.XOpenDisplay.restype = ctypes.c_void_p
        xlib.XCloseDisplay.argtypes = [ctypes.c_void_p]
        handle = None
        for _ in range(100):
            handle = xlib.XOpenDisplay(env['DISPLAY'].encode())
            if handle:
                break
            time.sleep(.1)
        assert handle, 'private authenticated X connection failed'
        # Keep a client connected so Xvfb cannot reset between readiness and
        # SDL initialization (the proven 04 runner retains this handle too).
        viewer = subprocess.Popen([str(root / '_out/_cmake/RelWithDebInfo/vsrViewer'), '--agentIntegration', '-obj', str(fixture)],
                                  stdout=vlog, stderr=vlog, env=env)
        deadline = time.monotonic() + 90
        descriptor = None
        while time.monotonic() < deadline:
            assert viewer.poll() is None, 'private production viewer exited'
            matches = list((local / 'runtime/vsr').glob('vsr-mcp-*.json'))
            if matches:
                assert len(matches) == 1
                descriptor = matches[0]
                break
            time.sleep(.1)
        assert descriptor
        client = subprocess.Popen([node, str(work / '09-production-client.mjs'), str(descriptor),
                                   str(work / f'09-production-{device}-proof.json'), device, str(local / 'oracle')],
                                  stdout=clog, stderr=clog, env=env)
        assert client.wait(timeout=90) == 0, 'private native MCP query failed'
        print(device, 'production native query PASS; proof states partial scope')
    finally:
        if handle and xlib:
            xlib.XCloseDisplay(handle)
        for process in (client, viewer, x):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        auth.unlink(missing_ok=True)
