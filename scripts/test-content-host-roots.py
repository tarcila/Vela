#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Installed headless host: trusted startup asset roots and launch-private input.

Qualifies the actual installed `vsrTutorialIntegrationHeadless` CLI:
  * `--agentAssetRoot` names ordinary operator directories, which are NOT
    chmod'd and need not be 0700;
  * a launch-private 0700 working input namespace is created and removed;
  * `vsr_content_options` discovers only the implemented RGBE/HDRI profile and
    the host root ids/kinds (never paths, never a grant);
  * a real HDR file under each root creates an HDRI Light, which ordinary
    `vsr_restore` removes again;
  * malformed/unsafe root options are refused before anything is published.

No live Scene, viewer, device or user configuration is touched: the host runs
in a private XDG runtime/config with helide and its own procedural fixture.

Usage: test-content-host-roots.py BUILD EVIDENCE_DIR [--install-prefix DIR]
"""

import argparse
import importlib.util
import json
import os
from pathlib import Path
import stat
import struct
import subprocess
import sys
import tempfile
import time

HDR_HEADER = b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n"
HDR_PIXEL = bytes([128, 64, 32, 132])  # linear (8, 4, 2)


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def write_hdr(path):
    path.write_bytes(HDR_HEADER + HDR_PIXEL)


def start(build, env, evidence, name, extra):
    log = (evidence / (name + '.log')).open('wb')
    host = subprocess.Popen([str(build / 'vsrTutorialIntegrationHeadless'), *extra],
                            env=env, stdout=subprocess.PIPE, stderr=log)
    return host, log


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('evidence', type=Path)
    parser.add_argument('--config', help='CMake multi-config executable subdirectory')
    parser.add_argument('--install-prefix', type=Path)
    args = parser.parse_args()
    build_root = args.build.resolve()
    build = build_root / args.config if args.config else build_root
    install = args.install_prefix.resolve() if args.install_prefix else build_root / 'install'
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    require((build / 'vsrTutorialIntegrationHeadless').is_file(), 'built headless host exists')

    spec = importlib.util.spec_from_file_location(
        'chess', Path(__file__).with_name('test-chess-feature.py'))
    chess = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(chess)

    summary = {'refused_invocations': [], 'created': []}
    with tempfile.TemporaryDirectory(prefix='content-host-roots-') as temporary:
        root = Path(temporary)
        runtime, config = root / 'runtime', root / 'config'
        runtime.mkdir(mode=0o700)
        config.mkdir(mode=0o700)
        # Deliberately an ORDINARY shared operator folder, mode 0755.
        assets = root / 'assets'
        assets.mkdir(mode=0o755)
        write_hdr(assets / 'sky.hdr')
        outside = root / 'outside'
        outside.mkdir(mode=0o755)
        write_hdr(outside / 'secret.hdr')
        (assets / 'escape.hdr').symlink_to(outside / 'secret.hdr')

        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(config),
                   TMPDIR='/tmp', ANARI_LIBRARY='helide', VSR_ANARI_LIBRARIES='helide',
                   SDL_VIDEODRIVER='dummy')
        env['LD_LIBRARY_PATH'] = ':'.join(
            [str(install / 'lib'), str(build)]
            + [p for p in env.get('LD_LIBRARY_PATH', '').split(':')
               if p and '/pi-integration/_out/lib' not in p])

        # 1. Malformed or unsafe root options are refused before activation.
        for extra in (['--agentAssetRoot'],
                      ['--agentAssetRoot', 'relative/dir'],
                      ['--agentAssetRoot', '.'],
                      ['--agentAssetRoot', '/'],
                      ['--agentAssetRoot', str(assets) + '/../outside'],
                      ['--agentAssetRoot=' + str(assets)],
                      ['--agentAssetRoot', str(assets), '--agentAssetRoot', str(assets)]):
            result = subprocess.run([str(build / 'vsrTutorialIntegrationHeadless'), *extra],
                                    env=env, capture_output=True, timeout=30)
            require(result.returncode == 1 and not result.stdout,
                    'refused root option exits without a descriptor: ' + ' '.join(extra))
            require(b'--agentAssetRoot' in result.stderr, 'refusal names the host option')
            require(not list((runtime / 'vsr').glob('*.json')) if (runtime / 'vsr').exists() else True,
                    'no descriptor published for a refused launch')
            summary['refused_invocations'].append(extra)
        # A nonexistent but well-formed directory is refused by the host, not silently dropped.
        result = subprocess.run([str(build / 'vsrTutorialIntegrationHeadless'),
                                 '--agentAssetRoot', str(root / 'absent')],
                                env=env, capture_output=True, timeout=30)
        require(result.returncode == 1 and b'asset root' in result.stderr,
                'unopenable startup root is reported and fatal')
        summary['refused_invocations'].append(['--agentAssetRoot', '<absent>'])

        before_mode = stat.S_IMODE(assets.stat().st_mode)
        host, log = start(build, env, evidence, 'headless-host',
                          ['--agentAssetRoot', str(assets)])
        try:
            until = time.monotonic() + 60
            descriptors = []
            while not descriptors:
                require(host.poll() is None and time.monotonic() < until, 'host publishes a descriptor')
                descriptors = list((runtime / 'vsr').glob('vsr-mcp-*.json'))
                time.sleep(.1)
            descriptor = json.loads(descriptors[0].read_text())
            client = chess.Client(descriptor, evidence)
            discovered = client.pages('vsr_discover', {})
            scene_id = next(i['id'] for i in discovered if i['kind'] == 'scene')

            options = client.operation('vsr_content_options', {'scene_id': scene_id})['result']
            (evidence / 'content-options.json').write_text(json.dumps(options, indent=2))
            require(options['kinds'] == ['hdri'], 'only the implemented HDRI kind is advertised')
            require(options['source_profile'] == 'radiance_rgbe_minus_y_plus_x',
                    'only the implemented RGBE source profile is advertised')
            profiles = options['asset_root_profiles']
            require(options['asset_roots'] == 2 and len(profiles) == 2, 'startup + launch-private roots')
            kinds = sorted(p['kind'] for p in profiles)
            require(kinds == ['host_startup', 'launch_private'], 'both trusted host root kinds: ' + str(kinds))
            for profile in profiles:
                require(profile['approved'], 'root discovery reports approval')
                for value in (str(assets), str(runtime), '/'):
                    require(value not in json.dumps(profile), 'root discovery never publishes a path')
            # No tool may grant, add or widen a root.
            tools = client.rpc('tools/list', {})['tools']
            names = {t['name'] for t in tools}
            require('vsr_create_hdri' in names and 'vsr_content_options' in names,
                    'production headless host registers the content tools')
            for tool in tools:
                schema = json.dumps(tool['inputSchema'])
                for forbidden in ('asset_root_path', 'directory', 'approve_root', 'root_path'):
                    require(forbidden not in schema, tool['name'] + ' exposes no root grant field')
            create_schema = next(t for t in tools if t['name'] == 'vsr_create_hdri')['inputSchema']
            root_field = create_schema['properties']['asset_root']
            require(root_field['type'] == 'integer' and root_field['maximum'] == 15,
                    'creation selects an existing host root id only: ' + json.dumps(root_field))

            startup_id = next(p['id'] for p in profiles if p['kind'] == 'host_startup')
            private_id = next(p['id'] for p in profiles if p['kind'] == 'launch_private')

            # 2. Confinement holds for the real installed host.
            for root_id, name in ((startup_id, 'escape.hdr'),
                                  (startup_id, '../outside/secret.hdr'),
                                  (startup_id, str(outside / 'secret.hdr')),
                                  (private_id, 'sky.hdr')):
                status = client.operation('vsr_create_hdri',
                                          {'scene_id': scene_id, 'asset_root': root_id,
                                           'file_name': name}, outcome=None)
                require(status['outcome'] != 'ok' and not status['committed'],
                        'confined refusal for ' + name + ': ' + json.dumps(status))
            refused_root = client.operation('vsr_create_hdri',
                                            {'scene_id': scene_id, 'asset_root': 7,
                                             'file_name': 'sky.hdr'}, outcome=None)
            require(refused_root['outcome'] != 'ok', 'unapproved root id refused')

            # 3. The ordinary operator folder works without any chmod demand.
            require(stat.S_IMODE(assets.stat().st_mode) == before_mode == 0o755,
                    'host never changes the operator asset folder mode')

            def create_and_restore(root_id, file_name, label):
                status = client.operation('vsr_create_hdri',
                                          {'scene_id': scene_id, 'asset_root': root_id,
                                           'file_name': file_name, 'root_name': label,
                                           'scale': 0.0625})
                require(status['committed'], 'creation committed')
                result = status['result']
                light = result['primary']['light']
                items = client.pages('vsr_inspect', {'scene_id': scene_id, 'object_type': 'ANARI_LIGHT'})
                require(any(i['handle'] == light for i in items), 'created Light is inspectable')
                (evidence / (label + '-create.json')).write_text(json.dumps(status, indent=2))
                removed = client.operation('vsr_restore',
                                           {'scene_id': scene_id,
                                            'target_batch_ref': result['batch_ref']})
                require(removed['committed'], 'ordinary Restore removed the unchanged content')
                after = client.pages('vsr_inspect', {'scene_id': scene_id, 'object_type': 'ANARI_LIGHT'})
                require(not any(i['handle'] == light for i in after), 'created Light is gone')
                require(len(after) == len(items) - 1, 'surrounding Lights are preserved')
                summary['created'].append({'root': root_id, 'file': file_name,
                                           'dimensions': result.get('dimensions')})

            create_and_restore(startup_id, 'sky.hdr', 'startup-root-hdri')

            # 4. The launch-private namespace is a real, usable, 0700 input.
            log.flush()
            text = (evidence / 'headless-host.log').read_text(errors='replace')
            marker = 'content input (launch-private, 0700): '
            require(marker in text, 'host reports its launch-private input directory')
            private_path = Path(text.split(marker)[1].split('\n')[0].strip())
            require(stat.S_IMODE(private_path.stat().st_mode) == 0o700, 'working input is 0700')
            require(private_path.stat().st_uid == os.geteuid(), 'working input is owned by this launch')
            write_hdr(private_path / 'work.hdr')
            create_and_restore(private_id, 'work.hdr', 'launch-private-hdri')
            summary['private_input_retained_while_nonempty'] = True

            client.rpc('vsr/detach', {})
        finally:
            host.terminate()
            code = host.wait(timeout=30)
            log.close()
        require(code == 0, 'host exits cleanly')
        require(not descriptors[0].exists(), 'descriptor is unpublished at exit')
        # Teardown never deletes content: a non-empty namespace survives.
        require((private_path / 'work.hdr').is_file(),
                'host teardown does not delete files left in the working input')
        (private_path / 'work.hdr').unlink()
        private_path.rmdir()
        require((assets / 'sky.hdr').is_file() and stat.S_IMODE(assets.stat().st_mode) == 0o755,
                'operator assets and permissions survive untouched')

    summary['binaries'] = {str(p): chess.digest(p) for p in
                           (build / 'vsrTutorialIntegrationHeadless',
                            install / 'lib/libanari_library_vsr.so')
                           if p.is_file()}
    (evidence / 'summary.json').write_text(json.dumps(summary, indent=2))
    print('CONTENT_HOST_ROOTS PASS: {} refused invocations, {} confined creations'.format(
        len(summary['refused_invocations']), len(summary['created'])))


if __name__ == '__main__':
    main()
