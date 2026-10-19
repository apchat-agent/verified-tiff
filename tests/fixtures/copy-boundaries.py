import pathlib,sys,zlib
sys.path.insert(0,'tests')
from deflate import Bits,fixed_symbol,wrapped
b=Bits();b.put(1,1);b.put(1,2)
raw=bytearray(bytes(range(256))+b'AB')
for v in raw:fixed_symbol(b,v)
def match(distance):
    fixed_symbol(b,285)
    base=[1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577]
    code=max(i for i,v in enumerate(base) if v<=distance)
    extra=0 if code<4 else code//2-1
    b.code(code,5);b.put(distance-base[code],extra)
    for _ in range(258):raw.append(raw[-distance])
match(258)
for _ in range(125):match(256)
assert len(raw)==32766
match(1);match(258);fixed_symbol(b,256)
wire=wrapped(b.finish(),raw)
assert zlib.decompress(wire)==raw
pathlib.Path('work/copy-boundaries.zlib').write_bytes(wire)
print('copy-boundaries.zlib:',len(wire),'compressed;',len(raw),'decoded')
