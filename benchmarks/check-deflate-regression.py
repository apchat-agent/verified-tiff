"""Require the measured gray16 Predictor 2 workload to stay within 5% of baseline."""
import json,pathlib,sys
before=json.loads(pathlib.Path(sys.argv[1]).read_text());after=json.loads(pathlib.Path(sys.argv[2]).read_text())
def sample(run):
    assert run['metadata']['rounds']>=9
    return next(r for r in run['records'] if (r['name'],r['mode'])==('gray16-deflate-p2','native'))
b,a=sample(before),sample(after)
assert b['input_sha256']==a['input_sha256'] and b['output_sha256']==a['output_sha256']
assert len(b['samples']['verified'])>=9 and len(a['samples']['verified'])>=9
ratio=a['median_ms']['verified']/b['median_ms']['verified']
print(f'gray16-deflate-p2 after/before: {ratio:.6f}; required <= 1.05',flush=True)
assert ratio<=1.05,'Deflate bulk-copy performance regression'
