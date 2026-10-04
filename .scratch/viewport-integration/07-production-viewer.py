"""Read-only MCP diagnostics on an isolated production viewer; preserve old proofs."""
import os
import pathlib
import runpy
import subprocess
import sys

root = pathlib.Path.cwd()
assert root.name == 'viewport-first-observation'
device = sys.argv[1] if len(sys.argv) > 1 else 'barney'
original = subprocess.Popen
node = '/nix/store/503mh0fj8j1cfvrxcavr76szbn4dk2r7-nodejs-slim-24.21.0/bin/node'

def start(command, *args, **kwargs):
    if command and str(command[0]).endswith('/bin/node'):
        command = list(command)
        command[0] = node
        if len(command) > 1 and str(command[1]).endswith('/04-production-client.mjs'):
            command[1] = str(root / '.scratch/viewport-integration/07-production-client.mjs')
    return original(command, *args, **kwargs)

subprocess.Popen = start
os.environ.setdefault('VSR_VIEWER_TEST_LOG_LABEL', '07')
sys.argv = ['04-production-viewer.py', device, 'first-observation']
runpy.run_path(str(root / '.scratch/viewport-integration/04-production-viewer.py'), run_name='__main__')
