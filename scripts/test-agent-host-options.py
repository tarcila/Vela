#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

"""Installed host CLI checks; private XDG, no display or user scene access.

Usage: test-agent-host-options.py BUILD EVIDENCE_DIR
"""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    build, evidence = (Path(p).resolve() for p in sys.argv[1:])
    evidence.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('chess', Path(__file__).with_name('test-chess-feature.py'))
    chess = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(chess)
    with tempfile.TemporaryDirectory(prefix='agent-host-options-') as temporary:
        root = Path(temporary)
        runtime, config = root/'runtime', root/'config'
        runtime.mkdir(mode=0o700)
        config.mkdir(mode=0o700)
        env = dict(os.environ, XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(config), TMPDIR='/tmp', ANARI_LIBRARY='helide', VSR_ANARI_LIBRARIES='helide', SDL_VIDEODRIVER='dummy')
        env['LD_LIBRARY_PATH'] = ':'.join([str(build/'install/lib'), str(build)] + [p for p in env.get('LD_LIBRARY_PATH', '').split(':') if p and '/pi-integration/_out/lib' not in p])
        invalid = [[], ['0'], ['-1'], ['+1'], [''], ['1.5'], ['1e6'], ['64MiB'], ['18446744073709551616'], ['--agentIntegration'], ['64', '--agentRestorationBytes', '64']]
        count = 0
        for name in ('vsrViewer', 'vsrTutorialIntegrationHeadless'):
            for values in invalid:
                flags = ['--agentRestorationBytes', *values]
                # A valid importer flag before the invalid host option must not
                # accidentally open its scene; malformed options reject first.
                prefix = ['--agentIntegration', '-gltf', str(root/'must-not-open.gltf')] if name == 'vsrViewer' else []
                result = subprocess.run([str(build/name), *prefix, *flags], env=env, capture_output=True, timeout=10)
                assert result.returncode == 1 and not result.stdout
                assert b'--agentRestorationBytes requires' in result.stderr
                assert not list(runtime.rglob('*.json')) and not list(config.iterdir())
                count += 1
            result = subprocess.run([str(build/name), '--agentRestorationBytes=64'], env=env, capture_output=True, timeout=10)
            assert result.returncode == 1 and b'--agentRestorationBytes requires' in result.stderr
            count += 1
            for flags in (['--agentRestorationHardBytes'],
                          ['--agentRestorationHardBytes', '0'],
                          ['--agentRestorationHardBytes', '1'],
                          ['--agentRestorationHardBytes', '18446744073709551616'],
                          ['--agentRestorationHardBytes=8589934592'],
                          ['--agentRestorationHardBytes', '8589934592', '--agentRestorationHardBytes', '8589934592']):
                result = subprocess.run([str(build/name), *flags], env=env, capture_output=True, timeout=10)
                assert result.returncode == 1 and not result.stdout
                assert b'--agentRestorationHardBytes' in result.stderr
                assert not list(runtime.rglob('*.json')) and not list(config.iterdir())
                count += 1
        limits_seen = []
        configurations = (([], 4294967296, 8589934592),
                          (['--agentRestorationBytes', '67108864'], 67108864, 8589934592),
                          (['--agentRestorationHardBytes', '600000', '--agentRestorationBytes', '120000'], 120000, 600000))
        for flags, cap, hard in configurations:
            with (evidence/f'headless-{cap}.log').open('wb') as log:
                host = subprocess.Popen([str(build/'vsrTutorialIntegrationHeadless'), *flags], env=env, stdout=log, stderr=log)
                try:
                    until = time.monotonic() + 30
                    descriptors = []
                    while not descriptors:
                        assert host.poll() is None and time.monotonic() < until
                        descriptors = list((runtime/'vsr').glob('vsr-mcp-*.json'))
                        time.sleep(.1)
                    descriptor = json.loads(descriptors[0].read_text())
                    limits = descriptor['image_exchange']['exchange_limits']
                    assert limits['effective_global_retention_byte_cap'] == cap
                    assert limits['effective_single_entry_hard_cap_bytes'] == hard
                    assert 'session_snapshot_byte_cap' not in limits
                    assert limits['available_global_retention_bytes'] == cap
                    assert limits['oversized_retention'] is False
                    assert limits['approval_available'] is True
                    assert limits['physical_byte_cap'] == 2 * hard + 268435456
                    assert limits['transient_image_byte_cap'] == 268435456
                    assert limits['file_cap'] == 1024 and limits['byte_cap'] == 268435456
                    client = chess.Client(descriptor, evidence)
                    discovered = client.pages('vsr_discover', {})
                    assert any(i['kind'] == 'scene' for i in discovered)
                    client.rpc('vsr/detach', {})
                    limits_seen.append(limits)
                finally:
                    host.terminate()
                    assert host.wait(timeout=15) == 0
                assert not descriptors[0].exists()
        (evidence/'summary.json').write_text(json.dumps({'malformed_host_invocations': count, 'limits': limits_seen}, indent=2))
        print(f'HOST_OPTIONS PASS: {count} malformed invocations; default/soft-only/soft-hard metadata and binding')


if __name__ == '__main__':
    main()
