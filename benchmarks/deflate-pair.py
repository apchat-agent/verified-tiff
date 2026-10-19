#!/usr/bin/env python3
"""Seven requested Deflate workloads: explicit trees, self-contained inputs."""
import argparse,hashlib,json,os,pathlib,random,statistics,subprocess
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-root',type=pathlib.Path,required=True)
p.add_argument('--candidate-root',type=pathlib.Path,default=pathlib.Path('.'))
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args(); roots={'before':a.baseline_root.resolve(),'after':a.candidate_root.resolve()};a.output=a.output.resolve();a.output.mkdir(parents=True,exist_ok=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(argv):return subprocess.run(list(map(str,argv)),check=True,capture_output=True,text=True,timeout=300).stdout
bindings={}
for label,root in roots.items():
 receipt=root/'target/verification-summary.json';proof=json.loads(receipt.read_text())
 assert proof['success'] and proof['errors']==0 and proof['is-verifying-entire-crate']
 for name,h in proof['core_sources_sha256'].items():assert sha(root/name)==h,name
 bindings[label]=dict(harness_sha256={name:sha(root/name) for name in ['benchmarks/reader.c','benchmarks/reader-encode.c']},proof_sha256=sha(receipt),sources=proof['core_sources_sha256'],binaries={name:sha(root/name) for name in ['target/reader-benchmark/verified','target/compat/libtiff.so.6']})
encoder=roots['before']/'target/reader-benchmark/encode';reference=roots['before']/'target/reader-benchmark/reference'
engines={label:root/'target/reader-benchmark/verified' for label,root in roots.items()};engines['reference']=reference
for path in [encoder,*engines.values()]:assert path.is_file(),path
os.sched_setaffinity(0,{min(os.sched_getaffinity(0))});rng=random.Random(20261018);records=[]
cases=[('gray-deflate','native',8,1,0,1,1024,1024,64),('rgb-deflate','native',8,3,0,1,1024,1024,64),('rgb-deflate','crop',8,3,0,1,1024,1024,64),('noise-deflate','native',8,1,1,1,1024,1024,64),('gray16-deflate-p2','native',16,1,0,2,1024,1024,64),('large-strip-deflate','firstrow',8,1,0,1,4096,4096,4096),('large-strip-deflate','crop',8,1,0,1,4096,4096,4096)]
for name,mode,bits,ch,noise,pred,w,h,rows in cases:
 wire=a.output/(name+'.tif');run([encoder,wire,8,bits,ch,noise,0,pred,w,h,rows,1,2 if ch==3 else 1,0,1]);raw={};samples={k:[] for k in engines};checks={}
 for label,exe in engines.items():
  dump=a.output/(label+'.bin');checks[label]=json.loads(run([exe,wire,mode,0,'memory',dump]));raw[label]=dump.read_bytes()
 assert raw['before']==raw['after']==raw['reference'],(name,mode)
 for _ in range(9):
  order=list(engines);rng.shuffle(order)
  for label in order:
   sample=json.loads(run([engines[label],wire,mode,100,'memory']));assert sample['hash']==checks[label]['hash'];samples[label].append(sample)
 med={k:statistics.median(s['ns_per_operation']/1e6 for s in v) for k,v in samples.items()}
 records.append(dict(name=name,mode=mode,input_sha256=sha(wire),output_sha256=hashlib.sha256(raw['before']).hexdigest(),samples=samples,median_ms=med));print(name,mode,med,flush=True)
 a.output.joinpath('results.json').write_text(json.dumps(dict(driver_sha256=sha(pathlib.Path(__file__)),bindings=bindings,rounds=9,milliseconds=100,encoder_sha256=sha(encoder),reference_sha256=sha(reference),records=records),indent=2)+'\n')
# Compatibility views for the existing gray16 regression gate. Both views use
# the same interleaved reference samples; these are not separate timing phases.
for label in roots:
 view=dict(metadata=dict(rounds=9,bindings=bindings[label]),records=[dict(name=r['name'],mode=r['mode'],input_sha256=r['input_sha256'],output_sha256=r['output_sha256'],samples=dict(verified=r['samples'][label],reference=r['samples']['reference']),median_ms=dict(verified=r['median_ms'][label],reference=r['median_ms']['reference'])) for r in records])
 a.output.joinpath(label+'.json').write_text(json.dumps(view,indent=2)+'\n')
