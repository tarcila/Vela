"""Summarize the one SAME09 lifecycle probe; never starts a viewer or renders.

Reproduce explicitly (not invoked by this summarizer):
  LD_LIBRARY_PATH="$PWD/_out/lib:$LD_LIBRARY_PATH" \
  TMPDIR="$PWD/.scratch/viewport-integration/tmp" \
  _out/_cmake/RelWithDebInfo/vsrIntegrationTests \
  '[.world-bounds-natural-qualification]'
The hidden diagnostic must fail on this driver profile, not be retried to green.
"""
import hashlib
import json
import pathlib

root = pathlib.Path('/home/tarcila/Code/ANARI/vela/viewport-world-bounds')
assert pathlib.Path.cwd() == root
work = root / '.scratch/viewport-integration'
log = work / 'logs/09-c1-natural-probe.log'
queries = []
frames = []
for line in log.read_text().splitlines():
    if not line.startswith(('NATURAL_WORLD ', 'NATURAL_FRAME ')):
        continue
    kind, *fields = line.split()
    row = dict(field.split('=', 1) for field in fields)
    if kind == 'NATURAL_WORLD':
        row['min'] = [float(x) for x in row['min'].split(',')]
        row['max'] = [float(x) for x in row['max'].split(',')]
        edited = row['phase'] == 'after_notified_edit_and_frame'
        row['expected_min'] = [49., 3., 4.] if edited else [2., 3., 4.]
        row['expected_max'] = [51., 5., 6.] if edited else [4., 5., 6.]
        row['literal_match'] = (row['success'] == '1'
                                and row['min'] == row['expected_min']
                                and row['max'] == row['expected_max'])
        queries.append(row)
    else:
        frames.append(row)
assert len(queries) == 20 and len(frames) == 16
assert sum(not row['literal_match'] for row in queries) == 5
assert all(int(row['nonbackground_pixels']) > 0 for row in frames)
post_render = [row for row in queries if int(row['renders']) > 0]
proof = {
    'outcome': 'ready-for-human',
    'blocking_ticket': '10-barney-world-property-qualification',
    'raw_log_sha256': hashlib.sha256(log.read_bytes()).hexdigest(),
    'measured_executable_sha256':
        '69c83f980cf2a20b6103b52910573e0d939a154725d400de20f20c26141e5e2e',
    'source_note': 'Measured before exception-safe device/frame cleanup was added; no probe rerun or driver/source enablement.',
    'diagnostic': {'cases': 1, 'assertions': 448, 'failures': 30, 'exit': 30},
    'fixture_combinations': 8,
    'completed_ordinary_frames': len(frames),
    'property_calls': len(queries),
    'incorrect_property_calls': sum(not row['literal_match'] for row in queries),
    'max_property_ms': max(float(row['ms']) for row in queries),
    'max_post_render_property_ms': max(float(row['ms']) for row in post_render),
    'inspection_work': {'renders': 0, 'resets': 0, 'populations': 0},
    'heartbeat_scope': 'Owner-thread ticks immediately before/after property, with measured call gap; not independent GUI/SDL heartbeat qualification.',
    'interpretation': 'Barney scalar passes. Barney one-element transform-array fails after actual completed ordinary frames, with or without an earlier cold query. Helide passes both. No internal driver cache/build cause established.',
    'queries': queries,
    'frames': frames,
}
(work / '09-c1-natural-proof.json').write_text(json.dumps(proof, indent=2) + '\n')
print('Recorded one failing real-driver lifecycle probe: 20 calls, 16 ordinary frames, 30 literal assertion failures.')
