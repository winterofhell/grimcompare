# Minimal GD .arz v3 reader (recon only).
import struct,sys
sys.path.insert(0,__import__('os').path.dirname(__file__))
from arc import lz4_block
def load(path):
    d=open(path,'rb').read()
    _,ver,rs,rsz,rc,ss,ssz=struct.unpack_from('<2H5I',d,0)
    assert ver==3
    strings=[]; p=ss; end=ss+ssz
    while p<end:
        n,=struct.unpack_from('<I',d,p); p+=4
        for _ in range(n):
            l,=struct.unpack_from('<I',d,p); p+=4
            strings.append(d[p:p+l].decode('latin1')); p+=l
    recs={}; p=rs
    for _ in range(rc):
        ni,tl=struct.unpack_from('<2I',d,p); p+=8
        typ=d[p:p+tl].decode('latin1'); p+=tl
        off,cs,ds,_ft=struct.unpack_from('<3IQ',d,p); p+=20
        recs[strings[ni]]=(typ,off+24,cs,ds)
    return d,strings,recs
def fields(arz,name):
    d,strings,recs=arz
    typ,off,cs,ds=recs[name]
    raw=lz4_block(d[off:off+cs],ds)
    out={}; p=0
    while p<len(raw):
        t,c,k=struct.unpack_from('<2HI',raw,p); p+=8
        vals=[]
        for i in range(c):
            if t==1: v=struct.unpack_from('<f',raw,p)[0]
            elif t==2: v=strings[struct.unpack_from('<I',raw,p)[0]]
            else: v=struct.unpack_from('<i',raw,p)[0]
            vals.append(v); p+=4
        out[strings[k]]=vals if c>1 else vals[0]
    return typ,out
