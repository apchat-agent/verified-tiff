"""Independent Deflate token histogram, checked against Python zlib output."""
import collections,json,pathlib,struct,sys,zlib
class Bits:
    def __init__(self,data):self.data=data;self.at=0
    def get(self,n):
        v=0
        for i in range(n):v|=((self.data[self.at//8]>>(self.at%8))&1)<<i;self.at+=1
        return v
    def align(self):self.at=(self.at+7)//8*8
class Huff:
    def __init__(self,lens):
        counts=collections.Counter(lens);code=0;self.codes={};nxt={}
        for n in range(1,16):code=(code+(counts[n-1] if n>1 else 0))*2;nxt[n]=code
        for symbol,n in enumerate(lens):
            if n:self.codes[n,nxt[n]]=symbol;nxt[n]+=1
    def read(self,b):
        code=0
        for n in range(1,16):
            code=2*code+b.get(1)
            if (n,code) in self.codes:return self.codes[n,code]
        raise ValueError('invalid Huffman code')
lengths=[3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258]
lextra=[0]*8+[1]*4+[2]*4+[3]*4+[4]*4+[5]*4+[0]
dbases=[1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577]
def decode(wire,stats):
    b=Bits(wire[2:-4]);out=bytearray()
    while True:
        last=b.get(1);kind=b.get(2);stats['blocks'][kind]+=1
        if kind==0:
            b.align();n=b.get(16);assert b.get(16)==65535-n
            out.extend(b.get(8) for _ in range(n));stats['stored_bytes']+=n
        else:
            assert kind in [1,2]
            if kind==1:lit=Huff([8]*144+[9]*112+[7]*24+[8]*8);dist=Huff([5]*32)
            else:
                nl=b.get(5)+257;nd=b.get(5)+1;nc=b.get(4)+4;cl=[0]*19
                for j in [16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15][:nc]:cl[j]=b.get(3)
                h=Huff(cl);lens=[]
                while len(lens)<nl+nd:
                    x=h.read(b)
                    if x<16:lens.append(x)
                    elif x==16:lens.extend([lens[-1]]*(b.get(2)+3))
                    elif x==17:lens.extend([0]*(b.get(3)+3))
                    else:lens.extend([0]*(b.get(7)+11))
                assert len(lens)==nl+nd;lit=Huff(lens[:nl]);dist=Huff(lens[nl:])
            while True:
                x=lit.read(b)
                if x==256:break
                if x<256:out.append(x);stats['literal_bytes']+=1;continue
                i=x-257;n=lengths[i]+b.get(lextra[i]);d=dist.read(b);distance=dbases[d]+b.get(0 if d<4 else d//2-1)
                stats['matches'][distance]+=1;stats['match_bytes'][distance]+=n
                for _ in range(n):out.append(out[-distance])
        if last:break
    assert bytes(out)==zlib.decompress(wire)
    stats['decoded_bytes']+=len(out)
def main(path):
    data=path.read_bytes();order='<' if data[:2]==b'II' else '>';u16=lambda at:struct.unpack_from(order+'H',data,at)[0];u32=lambda at:struct.unpack_from(order+'I',data,at)[0]
    assert u16(2)==42;at=u32(4);tags={}
    for i in range(u16(at)):
        p=at+2+12*i;tag,kind,count=struct.unpack_from(order+'HHI',data,p)
        if tag not in [273,279]:continue
        size={3:2,4:4}[kind];start=p+8 if size*count<=4 else u32(p+8)
        tags[tag]=list(struct.unpack_from(order+str(count)+('H' if kind==3 else 'I'),data,start))
    stats={k:collections.Counter() for k in ['blocks','matches','match_bytes']};stats.update(stored_bytes=0,literal_bytes=0,decoded_bytes=0)
    for at,n in zip(tags[273],tags[279]):decode(data[at:at+n],stats)
    return dict(file=path.name,strips=len(tags[273]),**stats)
print(json.dumps([main(pathlib.Path(p)) for p in sys.argv[1:]],indent=2))
