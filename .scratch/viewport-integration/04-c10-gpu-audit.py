"""Bounded positive Nsight attribution, not an all-driver/VRAM ceiling."""
import json
import pathlib
import sqlite3

root = pathlib.Path.cwd() / '.scratch/viewport-integration'
reports = {}
for label in ('on', 'off'):
    c = sqlite3.connect(root / f'logs/04-c10-{label}.sqlite')
    tables = {row[0] for row in c.execute("select name from sqlite_master where type='table'")}
    required = ('NVTX_EVENTS', 'CUPTI_ACTIVITY_KIND_RUNTIME', 'CUDA_GPU_MEMORY_USAGE_EVENTS',
                'CUDA_UM_CPU_PAGE_FAULT_EVENTS', 'CUDA_UM_GPU_PAGE_FAULT_EVENTS',
                'CUPTI_ACTIVITY_KIND_KERNEL', 'CUPTI_ACTIVITY_KIND_MEMCPY')
    assert all(t in tables for t in required), tables
    totals = {t: c.execute('select count(*) from ' + t).fetchone()[0] for t in required}
    assert all(v > 0 for v in totals.values()), totals
    apis = dict(c.execute('select s.value,count(*) from CUPTI_ACTIVITY_KIND_RUNTIME r join StringIds s on s.id=r.nameId group by 1'))
    assert any('MallocManaged' in name and count > 0 for name, count in apis.items()), apis
    assert any('Synchronize' in name and count > 0 for name, count in apis.items()), apis
    ranges = c.execute("select start,end,globalTid,coalesce(text,s.value) from NVTX_EVENTS n left join StringIds s on s.id=n.textId where coalesce(text,s.value) like '04.observation.%'").fetchall()
    markers = {}
    for start, end, tid, name in ranges:
        assert end and end >= start
        row = markers.setdefault(name, {'ranges': 0, 'maxMs': 0, 'ownerCudaApis': 0,
                                       'processCpuManagedFaults': 0, 'processGpuAllocations': 0,
                                       'concurrentGpuFaults': 0})
        row['ranges'] += 1
        row['maxMs'] = max(row['maxMs'], (end-start)/1e6)
        row['ownerCudaApis'] += c.execute('select count(*) from CUPTI_ACTIVITY_KIND_RUNTIME where globalTid=? and start between ? and ?', (tid,start,end)).fetchone()[0]
        for table, key in (('CUDA_UM_CPU_PAGE_FAULT_EVENTS','processCpuManagedFaults'),
                           ('CUDA_GPU_MEMORY_USAGE_EVENTS','processGpuAllocations'),
                           ('CUDA_UM_GPU_PAGE_FAULT_EVENTS','concurrentGpuFaults')):
            row[key] += c.execute('select count(*) from '+table+' where start between ? and ?', (start,end)).fetchone()[0]
    assert markers and markers['04.observation.start']['ranges'] > 100
    if label == 'on':
        assert markers['04.observation.acquire']['ranges'] > 100
        assert markers['04.observation.present']['ranges'] > 100
    for row in markers.values():
        assert row['ownerCudaApis'] == row['processCpuManagedFaults'] == row['processGpuAllocations'] == 0, row
    copies = c.execute('select copyKind,sum(bytes),count(*) from CUPTI_ACTIVITY_KIND_MEMCPY group by copyKind').fetchall()
    kinds = dict(c.execute('select id,label from ENUM_CUDA_MEMCPY_OPER'))
    reports[label] = {'positiveActivityCounts': totals, 'positiveNormalManagedAllocationCalls': {k:v for k,v in apis.items() if 'MallocManaged' in k},
                      'normalCopiesAndMigrations': [{'kind': kinds[k], 'bytes': b, 'count': n} for k,b,n in copies],
                      'observationMarkers': markers}
result = {'reports': reports, 'conclusion': 'At actual maximum-two-view callbacks, no observation-owner CUDA runtime entry, managed CPU fault or GPU allocation. Normal rendering has positive CUDA/managed allocation/synchronization/migration activity in both configurations.',
          'limits': 'Temporal process GPU-fault overlap is disclosed, not attributed to observation. Normal frame counts/timing differ. This does not prove whole-process VRAM/RSS, all-time arbitrary-driver bounds, absent untraced driver APIs or zero normal display migration. Reachable CPU-only callback graph and independent CPU memory/static ownership bounds are required alongside this trace.'}
(root / '04-c10-gpu-audit.json').write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps({k:v['observationMarkers'] for k,v in reports.items()}, indent=2))
