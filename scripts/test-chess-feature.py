#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Isolated installed-vsrViewer chess acceptance; never attach to a user launch.

Usage: test-chess-feature.py BUILD EVIDENCE_DIR [UI_PLAN] [--agentRestorationBytes N]
Multi-config builds: --config RelWithDebInfo --install-prefix PREFIX
Default host restoration budget stays 16777216; 67108864 is an explicit opt-in.
Requires Pillow, Xvfb/xauth/xset and XTest. Evidence excludes credentials.
The supplied UI plan uses the private 1280x900 default layout. Without a plan,
checkpoints accept bounded click/drag commands through ui-command.json. Private
UI input is a test-driver seam, not a viewer permission API. Exit 2 means an
observed acceptance blocker; it must never be interpreted as a passing run.
"""

import argparse
import base64
import ctypes as c
import ctypes.util
import hashlib
import glob
import http.client
import json
import math
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import tempfile
import time

from PIL import Image, ImageGrab

ASSET = Path('/data/gltf/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf')
HDR = Path('/data/Envmaps/footprint_court.hdr')


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class PrivateUI:
    def __init__(self, env, evidence):
        self.env, self.evidence = env, evidence
        os.environ['XAUTHORITY'] = env['XAUTHORITY']
        def library(name, package):
            soname = ctypes.util.find_library(name) or {'X11': 'libX11.so.6', 'Xtst': 'libXtst.so.6'}[name]
            try:
                return c.CDLL(soname, mode=c.RTLD_GLOBAL)
            except OSError:
                candidates = glob.glob(f'/nix/store/*-{package}-*/lib/{soname}')
                require(candidates, 'XTest dependency ' + name)
                return c.CDLL(candidates[0], mode=c.RTLD_GLOBAL)
        self.x = library('X11', 'libx11')
        self.t = library('Xtst', 'libxtst')
        self.x.XOpenDisplay.restype = c.c_void_p
        self.display = self.x.XOpenDisplay(env['DISPLAY'].encode())
        require(self.display, 'private authenticated XTest display')
        self.x.XFlush.argtypes = [c.c_void_p]
        self.x.XCloseDisplay.argtypes = [c.c_void_p]
        self.x.XDefaultRootWindow.argtypes = [c.c_void_p]
        self.x.XDefaultRootWindow.restype = c.c_ulong
        self.x.XQueryTree.argtypes = [c.c_void_p, c.c_ulong, c.POINTER(c.c_ulong), c.POINTER(c.c_ulong), c.POINTER(c.POINTER(c.c_ulong)), c.POINTER(c.c_uint)]
        self.x.XMoveResizeWindow.argtypes = [c.c_void_p, c.c_ulong, c.c_int, c.c_int, c.c_uint, c.c_uint]
        self.x.XFree.argtypes = [c.c_void_p]
        self.t.XTestFakeMotionEvent.argtypes = [c.c_void_p, c.c_int, c.c_int, c.c_int, c.c_ulong]
        self.t.XTestFakeButtonEvent.argtypes = [c.c_void_p, c.c_uint, c.c_int, c.c_ulong]

    def resize(self):
        root = self.x.XDefaultRootWindow(self.display)
        r, p, a, n = c.c_ulong(), c.c_ulong(), c.POINTER(c.c_ulong)(), c.c_uint()
        require(self.x.XQueryTree(self.display, root, c.byref(r), c.byref(p), c.byref(a), c.byref(n)), 'private window enumeration')
        for i in range(n.value):
            self.x.XMoveResizeWindow(self.display, a[i], 0, 0, 1280, 900)
        self.x.XFree(a)
        self.x.XFlush(self.display)
        time.sleep(1)

    def click(self, x, y, button=1):
        self.t.XTestFakeMotionEvent(self.display, -1, x, y, 0)
        self.x.XFlush(self.display)
        time.sleep(.25)
        self.t.XTestFakeButtonEvent(self.display, button, 1, 0)
        self.x.XFlush(self.display)
        time.sleep(.3)
        self.t.XTestFakeButtonEvent(self.display, button, 0, 0)
        self.x.XFlush(self.display)
        time.sleep(.7)

    def drag(self, x, y, xx, yy, button=1):
        self.t.XTestFakeMotionEvent(self.display, -1, x, y, 0)
        self.x.XFlush(self.display)
        time.sleep(.25)
        self.t.XTestFakeButtonEvent(self.display, button, 1, 0)
        self.x.XFlush(self.display)
        time.sleep(.4)
        for i in range(1, 11):
            self.t.XTestFakeMotionEvent(self.display, -1, round(x+(xx-x)*i/10), round(y+(yy-y)*i/10), 0)
            self.x.XFlush(self.display)
            time.sleep(.1)
        self.t.XTestFakeButtonEvent(self.display, button, 0, 0)
        self.x.XFlush(self.display)
        time.sleep(.5)

    def screenshot(self, name):
        ImageGrab.grab(xdisplay=self.env['DISPLAY']).save(self.evidence / (name + '.png'))

    def checkpoint(self, name):
        self.screenshot(name)
        if self.plan:
            commands = json.loads(self.plan.read_text())[name]
            for command in commands:
                require(command[0] in ('click', 'drag', 'resize', 'screenshot', 'continue'), 'restricted UI plan command')
                if command[0] != 'continue':
                    getattr(self, command[0])(*command[1:])
            return
        queue = self.evidence / 'ui-command.json'
        queue.unlink(missing_ok=True)
        print('PRIVATE_UI_CHECKPOINT ' + name, flush=True)
        until = time.monotonic() + 180
        renewed = time.monotonic()
        while True:
            if time.monotonic() - renewed > 10:
                self.client.rpc('vsr/renew', {})
                renewed = time.monotonic()
            require(time.monotonic() < until, 'bounded private UI checkpoint ' + name)
            if queue.exists():
                commands = json.loads(queue.read_text())
                queue.unlink()
                for command in commands:
                    if command[0] == 'continue':
                        return
                    require(command[0] in ('click', 'drag', 'resize', 'screenshot'), 'restricted UI command')
                    getattr(self, command[0])(*command[1:])
            time.sleep(.1)

    def close(self):
        self.x.XCloseDisplay(self.display)


class Client:
    def __init__(self, descriptor, evidence):
        self.descriptor, self.evidence = descriptor, evidence
        self.session = {}
        self.next_id = 0
        self.history = []
        self.session.update(self.rpc('vsr/attach', {}))

    def rpc(self, method, params, status=200):
        headers = {'Authorization': 'Bearer ' + self.descriptor['launch_credential'], 'Content-Type': 'application/json'}
        if self.session:
            headers.update({'VSR-Session-ID': self.session['session_id'], 'VSR-Session-Proof': 'Bearer ' + self.session['session_secret']})
        connection = http.client.HTTPConnection(self.descriptor['host'], self.descriptor['port'], timeout=15)
        try:
            connection.request('POST', self.descriptor['path'], json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params}), headers)
            response = connection.getresponse()
            value = json.loads(response.read())
            require(response.status == status, f'{method} HTTP {response.status}, expected {status}: {value.get("error")}')
            if status != 200:
                return value['error']
            require('result' in value, f'{method} RPC success: {value.get("error")}')
            return value['result']
        finally:
            connection.close()

    def tool(self, name, args):
        wire = self.rpc('tools/call', {'name': name, 'arguments': args})
        self.last_is_error = wire['isError']
        return wire['structuredContent']

    def operation(self, name, args, outcome='ok'):
        self.next_id += 1
        args = dict(args, request_id=self.next_id)
        result = self.tool(name, args)
        ref = result['operation_ref']
        until = time.monotonic() + 120
        while result['state'] != 'terminal':
            require(time.monotonic() < until, name + ' bounded operation timeout')
            time.sleep(.02)
            result = self.tool('vsr_operation_status', {'operation_ref': ref})
        require(self.last_is_error == (result['outcome'] != 'ok'), 'terminal isError mapping')
        self.history.append({'tool': name, 'arguments': args, 'status': result, 'isError': self.last_is_error})
        (self.evidence / 'operations.json').write_text(json.dumps(self.history, indent=2))
        if outcome is not None:
            require(result['outcome'] == outcome, name + ' terminal ' + json.dumps(result))
        return result

    def pages(self, name, args):
        items, cursor = [], ''
        for _ in range(100):
            result = self.operation(name, dict(args, **({'cursor': cursor} if cursor else {})))['result']
            items.extend(result['items'])
            if result['complete']:
                return items
            cursor = result['cursor']
        raise AssertionError('bounded pagination')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('ui_plan', nargs='?', type=Path)
    parser.add_argument('--agentRestorationBytes', type=int)
    parser.add_argument('--config', help='CMake multi-config executable subdirectory')
    parser.add_argument('--install-prefix', type=Path, help='Current installation prefix')
    args = parser.parse_args()
    if args.agentRestorationBytes is not None and args.agentRestorationBytes <= 0:
        parser.error('restoration bytes must be positive')
    build_root, evidence = args.build.resolve(), args.evidence.resolve()
    build = build_root / args.config if args.config else build_root
    install = args.install_prefix.resolve() if args.install_prefix else build_root/'install'
    require((build/'vsrViewer').is_file() and (install/'lib/libanari_library_vsr.so').is_file(), 'built viewer and installed ANARI VSR library exist')
    host_options = ([] if args.agentRestorationBytes is None else
                    ['--agentRestorationBytes', str(args.agentRestorationBytes)])
    effective_limit = args.agentRestorationBytes or 16777216
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence/'PASS').unlink(missing_ok=True)
    require(ASSET.is_file() and HDR.is_file(), 'actual chess and HDR assets exist')
    sources = [ASSET, HDR] + list(ASSET.parent.glob('*.jpg')) + list(ASSET.parent.glob('*.bin'))
    hashes = {str(p): digest(p) for p in sources}
    (evidence / 'provenance.json').write_text(json.dumps({'source_assets': hashes, 'build': str(build), 'build_root':str(build_root), 'config':args.config, 'install_prefix':str(install), 'host_options':host_options, 'effective_global_retention_byte_cap':effective_limit, 'binaries': {str(p): digest(p) for p in (build/'vsrViewer', build/'vsrIntegrationTests', install/'lib/libanari_library_vsr.so')}}, indent=2))
    with tempfile.TemporaryDirectory(prefix='chess-feature-') as temporary:
        root = Path(temporary)
        runtime, config = root/'runtime', root/'config'
        runtime.mkdir(mode=0o700)
        config.mkdir(mode=0o700)
        display = next(n for n in range(94, 200) if not Path(f'/tmp/.X{n}-lock').exists() and not Path(f'/tmp/.X11-unix/X{n}').exists())
        auth = root/'auth'
        auth.touch(mode=0o600)
        subprocess.run(['xauth', '-f', str(auth), 'add', f'localhost:{display}', 'MIT-MAGIC-COOKIE-1', secrets.token_hex(16)], check=True)
        env = dict(os.environ, DISPLAY=f'localhost:{display}', XAUTHORITY=str(auth), XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(config), SDL_VIDEODRIVER='x11', ANARI_LIBRARY='helide', VSR_ANARI_LIBRARIES='helide', TMPDIR='/tmp')
        env.pop('SDL_RENDER_DRIVER', None)
        env['LD_LIBRARY_PATH'] = ':'.join([str(install/'lib'), str(build)] + [p for p in env.get('LD_LIBRARY_PATH', '').split(':') if p and '/pi-integration/_out/lib' not in p])
        host = ui = None
        with (root/'host.log').open('wb') as log:
            xvfb = subprocess.Popen(['Xvfb', f':{display}', '-screen', '0', '1280x900x24', '-listen', 'tcp', '-nolisten', 'unix', '-auth', str(auth), '-noreset'], stdout=log, stderr=log, env=env)
            try:
                until = time.monotonic()+15
                while subprocess.run(['xset', 'q'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=2).returncode:
                    require(xvfb.poll() is None and time.monotonic()<until, 'private X readiness')
                    time.sleep(.1)
                host = subprocess.Popen([str(build/'vsrViewer'), '--agentIntegration', *host_options, '-gltf', str(ASSET), '-hdri', str(HDR)], stdout=log, stderr=log, env=env)
                until = time.monotonic()+60
                descriptors = []
                while not descriptors:
                    require(host.poll() is None and time.monotonic()<until, 'fresh installed viewer activation')
                    descriptors = list((runtime/'vsr').glob('vsr-mcp-*.json'))
                    time.sleep(.1)
                require(len(descriptors)==1, 'one private descriptor')
                descriptor = json.loads(descriptors[0].read_text())
                require(descriptor['image_exchange']['import'], 'fully bound production import')
                limits = descriptor['image_exchange']['exchange_limits']
                require(limits['effective_global_retention_byte_cap'] == effective_limit, 'explicit/default ActivityLog metadata')
                require(limits['session_snapshot_byte_cap'] == 67108864 and limits['transient_image_byte_cap'] == 268435456 and limits['byte_cap'] == 268435456 and limits['file_cap'] == 1024, 'independent caps unchanged')
                client = Client(descriptor, evidence)
                tools = client.rpc('tools/list', {})['tools']
                (evidence/'tools.json').write_text(json.dumps(tools, indent=2))
                schemas = {tool['name']:tool['inputSchema'] for tool in tools}
                require(schemas['vsr_image_import']['additionalProperties'] is False, 'strict Image Import schema')
                require(schemas['vsr_restore']['additionalProperties'] is False and 'data_version' not in schemas['vsr_restore']['properties'], 'strict existing restore schema without caller token')
                discovered = client.pages('vsr_discover', {})
                (evidence/'discovery.json').write_text(json.dumps(discovered, indent=2))
                scene = next(i for i in discovered if i['kind']=='scene')
                objects = client.pages('vsr_inspect', {'scene_id': scene['id']})
                nodes = client.pages('vsr_inspect', {'scene_id': scene['id'], 'kind':'nodes'})
                (evidence/'objects.json').write_text(json.dumps(objects, indent=2))
                (evidence/'nodes.json').write_text(json.dumps(nodes, indent=2))
                ui = PrivateUI(env, evidence)
                ui.client = client
                ui.plan = args.ui_plan
                ui.resize()
                ui.screenshot('initial')
                # Workflow steps follow the discovered live relationships.
                complete = run_workflows(client, ui, scene['id'], discovered, objects, nodes, descriptor, evidence)
                require(all(digest(p)==hashes[str(p)] for p in sources), 'all original assets unchanged')
                client.rpc('vsr/detach', {})
                host.terminate()
                require(host.wait(timeout=15)==0, 'normal private viewer exit')
                require(not descriptors[0].exists() and not Path(descriptor['image_exchange']['image_exchange_directory']).exists(), 'normal descriptor/exchange cleanup')
                if complete:
                    (evidence/'PASS').write_text('All combined live assertions passed.\n')
                else:
                    print('CHESS_ACCEPTANCE incomplete: see blockers.json', flush=True)
                    return 2
            finally:
                if ui:
                    ui.close()
                for process in (host, xvfb):
                    if process and process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=15)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
                # Viewer log has descriptor paths but never bearer secrets.
                (evidence/'host.log').write_bytes((root/'host.log').read_bytes())


def run_workflows(client, ui, scene_id, discovered, objects, nodes, descriptor, evidence):
    def name(item):
        return item['name']['text']

    def parameter(item, key):
        return next(p for p in item['parameters'] if p['name'] == key)

    def inspect(handle, kind='objects'):
        items = client.pages('vsr_inspect', {'scene_id': scene_id, 'kind': kind, 'handle': handle})
        require(len(items) == 1, 'exact live target inspection')
        return items[0]

    def restore(batch):
        status = client.operation('vsr_restore', {'scene_id': scene_id, 'target_batch_ref': batch})
        require(status['committed'] and status['kind'] == 'restore', 'authoritative restore receipt')
        return status['result']

    view = next(v for v in discovered if v.get('role') == 'interactive_viewport')
    independent = next(v for v in discovered if v.get('role') == 'independent_agent')
    require(view['id'] != independent['id'] and not view['camera_control']['enabled'], 'camera permission off at private launch')
    require(view['camera_control']['supported'] and view['capture_policy']['presented_frames_exposed'], 'existing qualified main viewport')
    require(not independent['capture_policy']['presented_frames_exposed'], 'independent view never substituted')
    unqualified = client.operation('vsr_view_frame', {'view_id':independent['id']})
    require(unqualified['result']['state']=='unavailable' and unqualified['result'].get('reason'), 'actual unqualified independent presentation stays unavailable')
    (evidence/'unqualified-frame.json').write_text(json.dumps(unqualified, indent=2))
    view_id = view['id']

    def info():
        return client.operation('vsr_view_info', {'view_id': view_id})['result']

    def frame(label, receipt=None):
        until = time.monotonic() + 120
        while time.monotonic() < until:
            status = client.operation('vsr_view_frame', {'view_id': view_id})
            f = status['result']
            if f['state'] != 'available' or f['stale']:
                time.sleep(.1)
                continue
            require(f['view_id'] == view_id and f['view_identity'] == view['view_identity'], 'explicit interactive frame identity')
            require(f['image_kind'] == 'presented_frame' and f['rendered_view_revision_basis'] == 'anari_render_start', 'qualified render-start provenance')
            require(f['rendered_scene_revision'] == f['current_scene_revision'], 'matching scene freshness')
            require(f['rendered_view_revision'] == f['current_view_revision'], 'matching view freshness')
            if receipt:
                require(f['current_view_revision'] == receipt['view_revision'], 'target superseded, never accept a newer pose')
                require(f['rendered_view_revision'] == receipt['view_revision'], 'exact rendered receipt revision')
                require(f['rendered_scene_revision'] == receipt['scene_revision'], 'chosen post-move Scene revision')
                camera = f['rendered_configuration']['camera']
                effective = receipt['effective_camera']
                for key in ('position', 'direction', 'up'):
                    require(all(abs(a-b) < 1e-5 for a, b in zip(camera[key], effective[key])), 'rendered camera ' + key)
                require(camera['projection'] == effective['projection'] and abs(camera['fovy_deg']-effective['fovy_deg']) < 1e-4, 'rendered projection/FOV')
            wire = client.rpc('tools/call', {'name': 'vsr_view_frame_image', 'arguments': {'operation_ref': status['operation_ref']}})
            content = next(c for c in wire['content'] if c['type'] == 'image')
            (evidence/(label+'.png')).write_bytes(base64.b64decode(content['data']))
            (evidence/(label+'-frame.json')).write_text(json.dumps(f, indent=2))
            return f
        raise AssertionError('presented frame timeout after 120 seconds')

    pose = {'view_id': view_id, 'position': [0, .517, -1.188], 'direction': [0, -.342, .94], 'up': [0, 1, 0], 'fovy_deg': 40}
    denied = client.rpc('tools/call', {'name': 'vsr_set_view_camera', 'arguments': dict(pose, request_id=client.next_id+1)}, status=403)
    require(denied['data']['code'] == 'camera_control_disabled', 'launch-off structured camera rejection')
    (evidence/'camera-disabled.json').write_text(json.dumps(denied, indent=2))
    # Existing local UI permission switch, never a new command-line permission.
    ui.click(85, 162)
    require(info()['camera_control']['enabled'], 'actual Agent Activity enable switch')
    ui.screenshot('camera-enabled')
    ui.click(591, 73)
    ui.drag(900, 400, 350, 400)
    ui.drag(1100, 220, 1100, 650)
    frame('baseline')

    sampler = next(o for o in objects if o['type'] == 'ANARI_SAMPLER' and name(o) == 'baseColor:Pawn_black_base_color')
    sampler = inspect(sampler['handle'])
    matrix = parameter(sampler, 'outTransform')
    require(matrix['editable'], 'inspect exposes matrix edit affordance')
    original = matrix['value']['data']
    swap = [original[1], original[0], original[2], original[3]]
    edited = client.operation('vsr_edit', {'scene_id': scene_id, 'edits': [{'target_kind': 'object', 'handle': sampler['handle'], 'version': sampler['version'], 'parameter': 'outTransform', 'value': swap}]})
    require(edited['kind'] == 'edit' and edited['committed'], 'matrix committing status')
    changed = inspect(sampler['handle'])
    require(parameter(changed, 'outTransform')['value']['data'] == swap, 'actual row-array matrix inspection')
    require(changed['version'] != sampler['version'], 'matrix version advances')
    # Independent application oracle, not an encoder/decoder round trip.
    def multiply(rows, vector):
        return [sum(a*b for a, b in zip(row, vector)) for row in rows]
    for value in (0, .02, .1):
        require(multiply(swap, [value, value, value, 1])[:3] == [value]*3, 'neutral body preserved by channel swap')
    require(multiply(swap, [.1, .8, .2, 1])[:3] == [.8, .1, .2], 'red/green asymmetric sampler oracle')
    frame('matrix-swap')
    restored = restore(edited['result']['batch_ref'])
    require(restored['image_versions'] == [] and 'matrix_versions' not in restored, 'matrix ordinary-version restore contract')
    require(parameter(inspect(sampler['handle']), 'outTransform')['value']['data'] == original, 'exact matrix restore')
    frame('matrix-restored')

    image_handle = parameter(sampler, 'image')['value']['handle']
    image = inspect(image_handle)
    metadata = image['image_info']
    exported = client.operation('vsr_image_export', {'scene_id': scene_id, 'handle': image_handle})
    er = exported['result']
    require(exported['kind'] == 'image_export' and not exported['committed'], 'READ export nonmutating status')
    require(er['dimensions'] == [2048, 2048] and er['channel_count'] == 4, 'original full-size chess RGBA Image, never a resized fixture')
    require(not image['mapped'], 'actual Image unmapped at inspection')
    for key in ('data_version', 'dimensions', 'element_type', 'channel_count', 'color_space', 'row_order'):
        require(er[key] == metadata[key], 'export exact captured metadata ' + key)
    directory = Path(descriptor['image_exchange']['image_exchange_directory'])
    source = directory/er['file_name']
    shutil.copyfile(source, evidence/'black-pawn-export.png')
    processing = subprocess.run([sys.executable, str(Path(__file__).with_name('recolour-chess-image.py')), str(source), str(directory/'processed.png')], check=True, capture_output=True, text=True)
    stats = json.loads(processing.stdout)
    (evidence/'external-pil.json').write_text(json.dumps(stats, indent=2))
    # Evaluate actual exported texels through the independently interpreted live
    # matrix. sRGB Images are sampled in linear space; no PNG gamma conversion.
    def linear(value):
        value /= 255
        return value/12.92 if value <= .04045 else ((value+.055)/1.055)**2.4
    actual_oracles = {}
    for label in ('exact_neutral_dark_sample', 'green_accent_sample'):
        raw = stats[label]
        sample = [linear(v) if er['color_space']=='srgb' else v/255 for v in raw[:3]] + [1]
        output = multiply(swap, sample)[:3]
        require(output == [sample[1], sample[0], sample[2]], 'actual Image sample channel-swap oracle')
        if label == 'exact_neutral_dark_sample':
            require(output == sample[:3], 'actual nonzero neutral dark texel remains neutral')
        actual_oracles[label] = {'raw_sample':raw, 'linear_sample':sample, 'matrix_output':output}
    (evidence/'actual-sample-oracles.json').write_text(json.dumps(actual_oracles, indent=2))
    shutil.copyfile(directory/'processed.png', evidence/'black-pawn-processed.png')
    require(stats['dimensions'] == er['dimensions'] and stats['channels'] == er['channel_count'] and er['row_order'] == 'top_down' and er['color_space'] == metadata['color_space'], 'external PIL raw shape/channels/orientation metadata')
    shared = [o['handle'] for o in objects if o['type'] == 'ANARI_SAMPLER' and any(p['name']=='image' and p['value'].get('handle') == image_handle for p in o['parameters'])]
    require(er['affected_complete'] and set(er['affected_samplers']) == set(shared), 'all discovered sharing Samplers accounted')
    # Sampler -> Material -> Surface -> placement: prove the shared pawn users.
    materials = [o['handle'] for o in objects if o['type']=='ANARI_MATERIAL' and any(p['value'].get('handle') in shared for p in o['parameters'])]
    surfaces = [o['handle'] for o in objects if o['type']=='ANARI_SURFACE' and any(p['value'].get('handle') in materials for p in o['parameters'])]
    placements = [n for n in nodes if n.get('object', {}).get('handle') in surfaces]
    require(len(placements)==8, 'actual eight black body placements share this Image')
    (evidence/'shared-users.json').write_text(json.dumps({'samplers':shared, 'materials':materials, 'surfaces':surfaces, 'placements':placements}, indent=2))
    # An explicit host override does not waive any Import/Restore invariant.
    imported = client.operation('vsr_image_import', {'scene_id': scene_id, 'handle': image_handle, 'data_version': er['data_version'], 'file_name':'processed.png'}, outcome=None)
    blockers = []
    if imported['outcome'] != 'ok':
        require(imported['outcome']=='busy' and imported['result']['reason']=='resource_limit' and not imported['committed'], 'exact default-budget resource rejection')
        require(inspect(image_handle)['image_info']['data_version'] == er['data_version'], 'rejected import preserves token')
        unchanged = client.operation('vsr_image_export', {'scene_id':scene_id, 'handle':image_handle})['result']
        with Image.open(directory/unchanged['file_name']) as actual:
            require(hashlib.sha256(actual.tobytes()).hexdigest() == stats['source_texels_sha256'], 'rejected import preserves every original texel')
        blockers.append({'criterion':'actual black-piece Image import/shared-user update/exact restore', 'reason':'default global retention cannot retain 2048x2048x4 texels plus Activity overhead', 'texel_bytes':math.prod(er['dimensions'])*er['channel_count'], 'exchange_limits':er['exchange_limits'], 'status':imported})
        (evidence/'blockers.json').write_text(json.dumps(blockers, indent=2))
    else:
        require(imported['committed'] and imported['kind']=='image_import', 'normal imported receipt')
        ir = imported['result']
        require(ir['data_version'] != er['data_version'], 'Import publishes fresh token')
        require(ir['restoration_policy']['effective_global_limit_bytes'] == er['exchange_limits']['effective_global_retention_byte_cap'], 'effective host retention receipt')
        require(ir['affected_complete'] and set(ir['affected_samplers']) == set(shared), 'Import updates all shared Samplers')
        require(inspect(image_handle)['image_info']['data_version'] == ir['data_version'], 'Import token agrees with live inspection')
        for handle in shared:
            require(parameter(inspect(handle), 'image')['value']['handle'] == image_handle, 'shared Sampler binding never redirected')
        after = client.operation('vsr_image_export', {'scene_id':scene_id, 'handle':image_handle})['result']
        with Image.open(directory/after['file_name']) as actual, Image.open(directory/'processed.png') as expected:
            require(actual.mode==expected.mode and actual.tobytes()==expected.tobytes(), 'all exact processed bytes in original Array')
        frame('image-imported')
        restored = restore(imported['result']['batch_ref'])
        require(restored['versions'] == [] and restored['node_versions'] == [] and len(restored['image_versions']) == 1 and restored['image_versions'][0]['handle'] == image_handle, 'normal Image-only restore receipt')
        fresh = restored['image_versions'][0]['data_version']
        require(fresh != imported['result']['data_version'] and fresh != er['data_version'], 'restore fresh token never rewinds')
        after = client.operation('vsr_image_export', {'scene_id':scene_id, 'handle':image_handle})['result']
        require(after['data_version'] == fresh and inspect(image_handle)['image_info']['data_version'] == fresh, 'Restore token agrees with export/inspection')
        for key in ('dimensions', 'element_type', 'channel_count', 'color_space', 'row_order'):
            require(after[key] == er[key], 'Restore preserves original metadata ' + key)
        for handle in shared:
            require(parameter(inspect(handle), 'image')['value']['handle'] == image_handle, 'Restore retains every shared binding')
        with Image.open(directory/after['file_name']) as actual, Image.open(source) as expected:
            require(actual.tobytes()==expected.tobytes(), 'exact original texels restored')
        frame('image-restored')

    # This authored asset is c3/d4/e6, not the starting position or checkmate.
    # Stage recoverable Fool's Mate in the PRIVATE scene using observed world
    # origins and the actual 0.0625 square spacing, never alter the glTF file.
    piece = {name(n):n for n in nodes if n['node_kind']=='transform'}
    king, queen = piece['King_W'], piece['Queen_B']
    x_e = king['world_matrix']['data'][0][3]
    z_one = king['world_matrix']['data'][2][3]
    square = abs(piece['Castle_W1']['world_matrix']['data'][0][3] - piece['Castle_W2']['world_matrix']['data'][0][3])/7
    require(abs(square-.0625) < .0003, 'measured square scale')
    def square_position(node, file, rank):
        y = node['world_matrix']['data'][1][3]
        return [x_e+(4-file)*square, y, z_one+(rank-1)*square]
    targets = [('Pawn_Body_W3',2,2), ('Pawn_Body_W4',3,2), ('Pawn_Body_W6',5,3), ('Pawn_Body_W7',6,4), ('Pawn_Body_B4',4,5), ('Queen_B',7,4)]
    entries = []
    for label, file, rank in targets:
        node = inspect(piece[label]['handle'], 'nodes')
        entries.append({'target_kind':'node', 'handle':node['handle'], 'version':node['node_edit_version'], 'structural_version':node['structural_version'], 'ancestry_version':node['ancestry_version'], 'parameter':'position', 'space':'world', 'value':square_position(node,file,rank)})
    staged = client.operation('vsr_edit', {'scene_id':scene_id, 'edits':entries})
    queen = inspect(queen['handle'], 'nodes')
    king = inspect(king['handle'], 'nodes')
    q = [row[3] for row in queen['world_matrix']['data'][:3]]
    k = [row[3] for row in king['world_matrix']['data'][:3]]
    (evidence/'chess-position.json').write_text(json.dumps({'authored_not_opening':True, 'private_staged_position':'1.f3 e5 2.g4 Qh4# (c/d pawns restored to rank 2)', 'measured_square':square, 'winning_queen':queen, 'defeated_king':king, 'stage_receipt':staged['result']}, indent=2))
    before = info()
    # Slightly behind/above the winning Queen; look along h4 -> e1.
    position = [q[0] + .035, q[1] + .105, q[2] + .035]
    target = [k[0], k[1] + .06, k[2]]
    pose = {'view_id':view_id, 'position':position, 'direction':[b-a for a,b in zip(position,target)], 'up':[0,1,0], 'fovy_deg':35}
    # Reopen Agent Activity through View menu (checkpoint also permits recovery
    # when a platform's default docking differs; all input stays private).
    ui.checkpoint('disable-edits')
    for tool, args in [('vsr_edit', {'scene_id':scene_id, 'edits':[{'target_kind':'object', 'handle':sampler['handle'], 'version':inspect(sampler['handle'])['version'], 'parameter':'outTransform', 'value':swap}]}), ('vsr_image_import', {'scene_id':scene_id, 'handle':image_handle, 'data_version':er['data_version'], 'file_name':'processed.png'})]:
        denied = client.rpc('tools/call', {'name':tool, 'arguments':dict(args, request_id=client.next_id+1)}, status=403)
        require(denied['data']['code']=='forbidden', 'Disable edits blocks ' + tool)
    client.operation('vsr_image_export', {'scene_id':scene_id, 'handle':image_handle})
    moved = client.operation('vsr_set_view_camera', pose)
    require(moved['committed'] and moved['kind']=='set_view_camera', 'separately permitted interactive move with edits disabled')
    frame('queens-eye', moved['result'])
    (evidence/'camera-before.json').write_text(json.dumps(before, indent=2))
    ui.checkpoint('before-agent-recovery')
    recovered = info()
    for key in ('position','direction','up'):
        require(all(abs(a-b)<1e-5 for a,b in zip(recovered['camera'][key],before['camera'][key])), 'Before agent exact recovered '+key)
    require(abs(recovered['camera']['fovy_deg']-before['camera']['fovy_deg'])<1e-4 and recovered['camera']['projection']==before['camera']['projection'], 'Before agent complete projection/FOV')
    require(recovered['view_revision'] != moved['result']['view_revision'], 'recovery revision advances')
    frame('before-agent-restored')
    navigated = recovered
    navigation = []
    for label, button, key in [('orbit', 1, 'direction'), ('pan', 2, 'position'), ('zoom', 3, 'position')]:
        previous = navigated
        ui.drag(900, 400, 920, 410, button)
        navigated = info()
        require(navigated['camera'][key] != previous['camera'][key] and navigated['view_revision'] != previous['view_revision'], 'normal ' + label + ' changes authoritative controller camera')
        navigation.append({'gesture':label, 'before':previous, 'after':navigated})
    (evidence/'navigation.json').write_text(json.dumps(navigation, indent=2))
    ui.screenshot('navigation-ui')
    ui.checkpoint('reenable-edits')
    restore(staged['result']['batch_ref'])
    for label, _, _ in targets:
        require(inspect(piece[label]['handle'], 'nodes')['local_matrix']['data']==piece[label]['local_matrix']['data'], 'exact authored chess placement restored '+label)
    (evidence/'summary.json').write_text(json.dumps({'matrix':'passed', 'external_pil':'passed', 'image_import':'blocked' if blockers else 'passed', 'qualified_interactive_camera':'passed', 'recovery_navigation':'passed', 'full_acceptance':not blockers}, indent=2))
    return not blockers


if __name__ == '__main__':
    sys.exit(main())
