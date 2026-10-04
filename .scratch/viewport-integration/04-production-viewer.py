"""Reuse the isolated authenticated runner, with actual production PNG client.
Only the fixture's newly launched vsrViewer is instrumented. No user display,
controller, config, device, camera or Scene is contacted.
"""
import pathlib
import subprocess
import runpy

root = pathlib.Path.cwd()
work = root / '.scratch/viewport-integration'
original = subprocess.Popen

def start(command, *args, **kwargs):
    env = kwargs.get('env', {}).copy()
    if command and (str(command[0]).endswith('/vsrViewer') or
                    len(command) > 1 and str(command[1]).endswith('/04-live-client.mjs')):
        oracles = pathlib.Path(env['XDG_RUNTIME_DIR']).parent / 'production-oracles'
        oracles.mkdir(exist_ok=True)
        env['VSR_PRODUCTION_ORACLE'] = str(oracles)
        if str(command[0]).endswith('/vsrViewer'):
            config = pathlib.Path(env['XDG_CONFIG_HOME']) / 'vela/vsrViewer'
            config.mkdir(parents=True, exist_ok=True)
            subprocess.run([str(work / 'logs/04-production-ui'),
                            str(config / 'uiState.vsr')], check=True, env=env)
            env['LD_PRELOAD'] = str(work / 'logs/04-production-oracle.so')
        else:
            command = list(command)
            command[1] = str(work / '04-production-client.mjs')
        kwargs['env'] = env
    return original(command, *args, **kwargs)

subprocess.Popen = start
runpy.run_path(str(work / '04-live-viewer.py'), run_name='__main__')
