#!/usr/bin/env python3
"""Exact baseline/candidate comparisons of fixtures, originals, README workloads."""
import argparse,hashlib,json,pathlib,subprocess,sys,struct,zlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline-root',type=pathlib.Path,required=True)
p.add_argument('--output',type=pathlib.Path,required=True)
a=p.parse_args();root=pathlib.Path(__file__).resolve().parents[1];base=a.baseline_root.resolve();a.output=a.output.resolve()
if not a.output.is_relative_to(root):
 p.error('--output must be inside the checkout (after resolving symlinks)')
a.output.mkdir(parents=True,exist_ok=True)
def sha(b):return hashlib.sha256(b).hexdigest()
bindings={label:dict(sources={str(p.relative_to(tree)):sha(p.read_bytes()) for folder in ('src','compat') for p in sorted((tree/folder).rglob('*')) if p.is_file() and p.suffix in ('.rs','.c','.h','.inc')},binaries={name:sha((tree/name).read_bytes()) for name in ['target/debug/verified-tiff','target/reader-benchmark/verified','target/compat/libtiff.so.6']}) for label,tree in [('before',base),('after',root)]}
records=[]
def compare(path,group,mode=None,cli_flags=(),expected=None):
 vals=[];codes=[]
 for label,tree in [('before',base),('after',root)]:
  dump=a.output/(label+'.bin')
  if dump.exists():dump.unlink()
  argv=[tree/'target/reader-benchmark/verified',path,mode,'0','memory',dump] if mode else [tree/'target/debug/verified-tiff',path,dump,*cli_flags]
  r=subprocess.run(list(map(str,argv)),capture_output=True,timeout=300);codes.append(r.returncode)
  vals.append(dump.read_bytes() if r.returncode==0 else None)
  if expected is not None:
   assert r.returncode==0,(path,label,r.stderr)
   assert pixels(dump)==expected,(path,label,'zlib pixel mismatch')
 assert codes[0]==codes[1],(path,mode,codes)
 assert vals[0]==vals[1],(path,mode,'bytes differ')
 if mode:assert codes==[0,0],(path,mode,codes)
 records.append(dict(group=group,cli_flags=list(cli_flags),file=str(path.relative_to(root)),mode=mode,input_sha256=sha(path.read_bytes()),exit_code=codes[0],bytes=len(vals[0]) if vals[0] is not None else None,output_sha256=sha(vals[0]) if vals[0] is not None else None))
 print(group,path.name,mode,codes[0],flush=True)
 a.output.joinpath('results.json').write_text(json.dumps(dict(bindings=bindings,records=records),indent=2)+'\n')
sys.path.insert(0,str(root/'tests'))
from corpus import tag
from extended import pixels
for path in sorted((root/'tests/fixtures').rglob('*')):
 if path.suffix.lower() in ('.tif','.tiff') and tag(path.read_bytes(),259,1) in (8,32946):compare(path,'deflate-fixtures')
for source in sorted((root/'tests/fixtures').rglob('*.zlib')):
 wire=source.read_bytes();raw=zlib.decompress(wire)
 tags=[(256,4,1,len(raw)),(257,4,1,1),(258,3,1,8),(259,3,1,8),(262,3,1,1),(273,4,1,134),(277,3,1,1),(278,4,1,1),(279,4,1,len(wire)),(284,3,1,1)]
 path=a.output/(source.stem+'.tif');path.write_bytes(b'II'+struct.pack('<HIH',42,8,len(tags))+b''.join(struct.pack('<HHII',*t) for t in tags)+bytes(4)+wire)
 compare(path,'deflate-zlib-fixtures',cli_flags=['--max-dimension',str(len(raw))],expected=raw)
 records[-1].update(source=str(source.relative_to(root)),source_sha256=sha(wire),zlib_bytes=len(raw),zlib_sha256=sha(raw))
for path in sorted((root/'.tools/tiff-4.7.1/test/images').iterdir()):
 if path.suffix.lower() in ('.tif','.tiff'):compare(path,'original-corpus')
for case in json.loads((root/'target/benchmark/inputs.json').read_text()):
 compare(root/case['file'],'readme','native')
 if case['rgba']:compare(root/case['file'],'readme','rgba')
from fixtures import make_tiff
for name,opts in [('white-none',dict(white=True)),('rgba-none',dict(channels=4,alpha=2)),('rgba-tile-none',dict(channels=4,alpha=2,tiled=True)),('bilevel-none',dict(bits=1))]:
 path=a.output/(name+'.tif');path.write_bytes(make_tiff(509,513,**opts)[0])
 # Keep paths inside the repository for stable evidence names.
 compare(path,'readme-extra','native');compare(path,'readme-extra','rgba')
print('TOTAL',len(records),'accepted',sum(r['exit_code']==0 for r in records),flush=True)
