"""Format already-recorded reader-harness medians; this does not run benchmarks."""
import json,pathlib,sys
before=json.loads(pathlib.Path(sys.argv[1]).read_text())
after=json.loads(pathlib.Path(sys.argv[2]).read_text())
assert before['metadata']['rounds']>=9 and after['metadata']['rounds']>=9
assert before['metadata']['cpu']==after['metadata']['cpu']
assert before['metadata']['milliseconds']==after['metadata']['milliseconds']
assert before['metadata']['driver_sha256']==after['metadata']['driver_sha256']
assert before['metadata']['harness_sha256']==after['metadata']['harness_sha256']
for name in ['target/reader-benchmark/reference','target/reader-benchmark/encode']:
    assert before['metadata']['binary_sha256'][name]==after['metadata']['binary_sha256'][name],name
assert len(before['records'])==len(after['records'])==7
print('| Workload | Before ms | After ms | Speedup | Before / libtiff | After / libtiff |')
print('|---|---:|---:|---:|---:|---:|')
for b,a in zip(before['records'],after['records']):
    assert (b['name'],b['mode'])==(a['name'],a['mode'])
    assert b['input_sha256']==a['input_sha256']
    assert b['output_sha256']==a['output_sha256']
    bv,br=b['median_ms']['verified'],b['median_ms']['reference']
    av,ar=a['median_ms']['verified'],a['median_ms']['reference']
    print(f"| {b['name']} {b['mode']} | {bv:.4f} | {av:.4f} | {bv/av:.2f}× | {bv/br:.2f}× | {av/ar:.2f}× |")
