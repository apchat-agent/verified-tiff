"""Regenerate a distance-two stream crossing both history and output boundaries."""
import pathlib,sys,zlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
from deflate import Bits,fixed_symbol,wrapped
b=Bits();b.put(1,1);b.put(1,2);raw=bytearray([127,31,0])
for byte in raw:fixed_symbol(b,byte)
for _ in range(129):
    fixed_symbol(b,285);b.code(1,5)
    for _ in range(258):raw.append(raw[-2])
fixed_symbol(b,256);wire=wrapped(b.finish(),raw)
assert len(raw)==33285 and raw==bytes([127])+bytes([31,0])*16642
assert zlib.decompress(wire)==raw
pathlib.Path(__file__).with_suffix('.zlib').write_bytes(wire)
print('period-two.zlib:',len(wire),'compressed;',len(raw),'decoded')
