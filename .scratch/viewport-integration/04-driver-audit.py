"""Positive context attribution, not a suppression or a clean-tool claim."""
import json
import pathlib
import re
import sys

headers = re.compile(r'^(?:Syscall param|Conditional jump|realloc\(\) with size 0|Unsafe allocation with size of zero|[\d,]+ bytes in [\d,]+ blocks are definitely lost)')

def inspect(path):
    text = pathlib.Path(path).read_text()
    lines = [re.sub(r'^==\d+== ?', '', line) for line in text.splitlines()]
    contexts = []
    for i, line in enumerate(lines):
        if not headers.match(line):
            continue
        origins = []
        for frame in lines[i+1:]:
            if not frame.strip():
                break
            if re.match(r'   (at|by) 0x', frame):
                frame = re.sub(r'0x[0-9A-Fa-f]+', '<address>', frame)
                frame = re.sub(r'/nix/store/[^/]+/', '<store>/', frame)
                origins.append(frame.strip())
            if len(origins) == 3:
                break
        assert len(origins) == 3, (line, origins)
        contexts.append(dict(kind=line, firstThreeFrames=origins))
    summary = re.search(r'ERROR SUMMARY: ([\d,]+) errors from (\d+) contexts', text)
    assert summary
    assert len(contexts) == int(summary[2]), (len(contexts), summary[2])
    definite = re.search(r'definitely lost: ([\d,]+) bytes in (\d+) blocks', text)
    return dict(path=path, toolPassed=False, errors=int(summary[1].replace(',', '')),
                contexts=contexts, definiteLostBytes=int(definite[1].replace(',', '')))

baseline, observed = map(inspect, sys.argv[1:3])
# Match positive stacks, not grep absence. Test/Catch call sites are not an
# ownership attribution, so compare the top three actual origin frames only.
signature = lambda c: (re.sub(r'[\d,]+', '<count>', c['kind']), tuple(c['firstThreeFrames']))
left = {signature(c) for c in baseline['contexts']}
matched = [signature(c) in left for c in observed['contexts']]
assert all(matched), matched
report = dict(baseline=baseline, observed=observed, matchedOriginContexts=sum(matched),
              limitations='Both Memcheck tools failed. Matching GUI/driver/DBus origins is attribution only, not blanket suppression, clean GPU memory, or a proof about uninstrumented driver storage.')
pathlib.Path(sys.argv[3]).write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(dict(baselineErrors=baseline['errors'], observerErrors=observed['errors'],
                      matchedOriginContexts=sum(matched), toolPassed=False)))
