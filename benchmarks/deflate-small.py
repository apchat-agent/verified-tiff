#!/usr/bin/env python3
"""Small-strip setup sweep; explicit built readers, no private receipt dependencies."""
import argparse, hashlib, json, os, pathlib, random, statistics, struct, subprocess, zlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--reader',action='append',required=True,help='LABEL=path/to/reader (repeat, baseline first)')
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--rounds',type=int,default=9)
p.add_argument('--validate-only',action='store_true')
a=p.parse_args(); assert a.rounds>=9
readers=dict(v.split('=',1) for v in a.reader)
for v in readers.values():
 if not pathlib.Path(v).is_file():p.error('missing reader: '+v)
a.output.mkdir(parents=True,exist_ok=True)
os.sched_setaffinity(0,{min(os.sched_getaffinity(0))})
def sha(b):return hashlib.sha256(b).hexdigest()
def binding(reader):
 tree=pathlib.Path(reader).resolve().parents[2]
 return dict(harness_sha256=sha((tree/'benchmarks/reader.c').read_bytes()),reader_sha256=sha(pathlib.Path(reader).read_bytes()),library_sha256=sha((tree/'target/compat/libtiff.so.6').read_bytes()),sources={str(p.relative_to(tree)):sha(p.read_bytes()) for folder in ('src','compat') for p in sorted((tree/folder).rglob('*')) if p.is_file() and p.suffix in ('.rs','.c','.h','.inc')})
bindings={k:binding(v) for k,v in readers.items()}
def tiff(width, strips):
 # Classic little-endian gray8, each strip is a separate zlib stream.
 n=len(strips); tags=[(256,4,1,width),(257,4,1,n),(258,3,1,8),(259,3,1,8),(262,3,1,1),(273,4,n,0),(277,3,1,1),(278,4,1,1),(279,4,n,0),(284,3,1,1)]
 end=8+2+12*len(tags)+4; offsets=[]; at=end+(8*n if n>1 else 0)
 for b in strips:offsets.append(at);at+=len(b)
 tags[5]=(273,4,n,end if n>1 else offsets[0]);tags[8]=(279,4,n,end+4*n if n>1 else len(strips[0]))
 data=b'II'+struct.pack('<HIH',42,8,len(tags))+b''.join(struct.pack('<HHII',*t) for t in tags)+bytes(4)
 if n>1:data+=struct.pack('<'+'I'*n,*offsets)+struct.pack('<'+'I'*n,*map(len,strips))
 return data+b''.join(strips)
class Bits:
 def __init__(self):self.bits=[]
 def put(self,v,n):self.bits.extend((v>>i)&1 for i in range(n))
 def code(self,v,n):self.bits.extend((v>>i)&1 for i in reversed(range(n)))
 def finish(self):
  data=bytearray((len(self.bits)+7)//8)
  for i,v in enumerate(self.bits):data[i//8]|=v<<(i%8)
  return bytes(data)
def empty_stream(dynamic,empty):
 b=Bits()
 for _ in range(empty):
  b.put(0,1);b.put(2 if dynamic else 1,2)
  if dynamic:
   b.put(0,5);b.put(0,5);b.put(14,4)
   for k in [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1]:b.put(1 if k in (0,1) else 0,3)
   for k in range(258):b.code(1 if k in (0,256) else 0,1)
   b.code(1,1) # EOB; the distance alphabet is empty.
  else:b.code(0,7) # fixed EOB
 b.put(1,1);b.put(1,2);b.code(48,8);b.code(0,7) # final fixed literal zero + EOB
 wire=b'\x78\x01'+b.finish()+struct.pack('>I',zlib.adler32(bytes(1)))
 assert zlib.decompress(wire)==bytes(1)
 return wire
rng=random.Random(20261019); pixels_rng=random.Random(20261019); records=[];cases=[]
for width in (16,64,256,1024,4096):
 for count in (1,64):
  raw=bytes(pixels_rng.randrange(8) for _ in range(width)); compressed=zlib.compress(raw,6)
  block=(compressed[2]>>1)&3
  assert compressed[2]&1 and block in (1,2) # exactly one Huffman block per strip
  cases.append((f'w{width}-strips{count}',width,count,raw,compressed,None,count*(3 if block==2 else 2)))
for dynamic in (False,True):
 for empty in ((8,64) if dynamic else (0,8,64)):
  cases.append((f'empty-{"dynamic" if dynamic else "fixed"}-{empty}',1,1,bytes(1),empty_stream(dynamic,empty),empty,empty*(3 if dynamic else 2)+2))
for name,width,count,raw,compressed,empty,trees in cases:
 wire=tiff(width,[compressed]*count)
 path=a.output/(name+'.tif');path.write_bytes(wire)
 outputs={};checks={};samples={k:[] for k in readers}
 for label,reader in readers.items():
  dump=a.output/(name+'-'+label+'.bin')
  preflight=subprocess.run([reader,str(path),'native','0','memory',str(dump)],check=True,capture_output=True,timeout=60)
  checks[label]=json.loads(preflight.stdout)['hash']
  outputs[label]=dump.read_bytes();assert outputs[label]==raw*count,(name,label)
 for _ in range(0 if a.validate_only else a.rounds):
  order=list(readers);rng.shuffle(order)
  for label in order:
   r=subprocess.run([readers[label],str(path),'native','100','memory'],check=True,capture_output=True,text=True,timeout=60)
   sample=json.loads(r.stdout);assert sample['hash']==checks[label];samples[label].append(sample)
 med={} if a.validate_only else {k:statistics.median(s['ns_per_operation']/1e6 for s in v) for k,v in samples.items()}
 record=dict(name=name,width=width,strips=count,empty_blocks=empty,trees=trees,first_block_type=(compressed[2]>>1)&3,input_sha256=sha(wire),output_sha256=sha(raw*count),samples=samples,median_ms=med)
 records.append(record);print(name,'block',record['first_block_type'],json.dumps(med),flush=True)
 a.output.joinpath('results.json').write_text(json.dumps(dict(driver_sha256=sha(pathlib.Path(__file__).read_bytes()),case_count=len(cases),bindings=bindings,rounds=0 if a.validate_only else a.rounds,milliseconds=100,reader_sha256={k:sha(pathlib.Path(v).read_bytes()) for k,v in readers.items()},records=records),indent=2)+'\n')
