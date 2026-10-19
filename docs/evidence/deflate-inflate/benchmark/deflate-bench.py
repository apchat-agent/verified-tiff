#!/usr/bin/env python3
"""Seven Deflate workloads using the repository's reader C harness."""
import hashlib,json,os,pathlib,random,shutil,statistics,subprocess,sys,time

root=pathlib.Path.cwd()
out=root/'target/reader-benchmark'
dest=root/'work'/sys.argv[1]
dest.mkdir(exist_ok=True)
cpu=min(os.sched_getaffinity(0))
os.sched_setaffinity(0,{cpu})
cases=[('gray-deflate','native',8,1,0,1,1024,1024,64),
       ('rgb-deflate','native',8,3,0,1,1024,1024,64),
       ('rgb-deflate','crop',8,3,0,1,1024,1024,64),
       ('noise-deflate','native',8,1,1,1,1024,1024,64),
       ('gray16-deflate-p2','native',16,1,0,2,1024,1024,64),
       ('large-strip-deflate','firstrow',8,1,0,1,4096,4096,4096),
       ('large-strip-deflate','crop',8,1,0,1,4096,4096,4096)]
def run(args):
    return subprocess.run(list(map(str,args)),check=True,capture_output=True,text=True).stdout
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
proof_path=root/'target/verification-summary.json'
proof=json.loads(proof_path.read_text())
assert proof['success'] and proof['errors']==0 and proof['is-verifying-entire-crate']
for name,digest in proof['core_sources_sha256'].items():assert sha(root/name)==digest,name
metadata=dict(cpu=cpu,rounds=9,milliseconds=100,
              uname=run(['uname','-a']).strip(),
              proof_sha256=sha(proof_path),
              proof_verified=proof['verified'],
              source_sha256={str(p.relative_to(root)):sha(p) for folder in ['src','compat'] for p in sorted((root/folder).rglob('*')) if p.is_file() and p.suffix in ['.rs','.c','.h','.inc']},
              binary_sha256={str(p.relative_to(root)):sha(p) for p in [out/'verified',out/'reference',out/'encode',root/'target/compat/libtiff.so.6']},
              harness_sha256={name:sha(root/name) for name in ['benchmarks/reader.c','benchmarks/reader-encode.c']},
              driver_sha256=sha(pathlib.Path(__file__)),
              toolchain=json.loads((root/'toolchain-lock.json').read_text()))
# The baseline pipeline may finish preparing while the isolated candidate gate
# is still running. Keep timed samples free of that scheduled verification job.
if sys.argv[1]=='baseline':
    for receipt in ['prefix-full-gate.exit','bulk-full-gate.exit','candidate-fixtures-preparation.exit','bulk-bench-preparation.exit','prefix-runtime-done.exit','bulk-runtime-done.exit','bulk-extra-checks.exit']:
        while not (root/'work'/receipt).exists():time.sleep(5)
        if receipt not in ['prefix-runtime-done.exit','bulk-runtime-done.exit','bulk-extra-checks.exit']:
            assert (root/'work'/receipt).read_text().strip()=='0',receipt
# A completed regression attempt may be blocked by the brief's download
# allowlist. Record that outcome; timing is never evidence that it passed.
if sys.argv[1]=='baseline':
    metadata['regression_exit_codes']={name:int((root/'work'/receipt).read_text()) for name,receipt in [
        ('baseline','container-baseline-test.exit'),('prefix','prefix-runtime-done.exit'),('bulk','bulk-runtime-done.exit'),
        ('c_compat','bulk-compat.exit'),('c_sanitizers','bulk-sanitize.exit'),('fuzz','bulk-fuzz.exit')]}
else:
    metadata['regression_exit_codes']=json.loads((root/'work/baseline/results.json').read_text())['metadata']['regression_exit_codes']
records=[]
rng=random.Random(20261018)
for name,mode,bits,ch,noise,pred,w,h,rows in cases:
    wire=dest/(name+'.tif')
    if not wire.exists():
        if sys.argv[1]=='baseline':
            run([out/'encode',wire,8,bits,ch,noise,0,pred,w,h,rows,1,2 if ch==3 else 1,0,1])
        else:
            shutil.copyfile(root/'work/baseline'/wire.name,wire)
    raw={};checks={};samples={e:[] for e in ['verified','reference']}
    for e in samples:
        dump=dest/(name+'-'+mode+'-'+e+'.bin')
        checks[e]=json.loads(run([out/e,wire,mode,0,'memory',dump]))
        raw[e]=dump.read_bytes()
    assert raw['verified']==raw['reference'],(name,mode)
    if sys.argv[1]!='baseline':
        assert raw['verified']==(root/'work/baseline'/(name+'-'+mode+'-verified.bin')).read_bytes()
    for _ in range(9):
        order=list(samples);rng.shuffle(order)
        for e in order:
            sample=json.loads(run([out/e,wire,mode,100,'memory']))
            assert sample['hash']==checks[e]['hash']
            samples[e].append(sample)
    medians={e:statistics.median(s['ns_per_operation']/1e6 for s in v) for e,v in samples.items()}
    records.append(dict(name=name,mode=mode,input_sha256=sha(wire),output_sha256=hashlib.sha256(raw['verified']).hexdigest(),samples=samples,median_ms=medians))
    print(name,mode,json.dumps(medians),flush=True)
    (dest/'results.json').write_text(json.dumps(dict(metadata=metadata,records=records),indent=2)+'\n')
