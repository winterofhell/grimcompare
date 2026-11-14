# Minimal GD .arc v3 reader (recon only). Layout verified against Text_EN.arc byte counts.
import struct,sys,os
def lz4_block(src, out_size):
    dst=bytearray(); i=0; n=len(src)
    while i<n:
        tok=src[i]; i+=1
        ll=tok>>4
        if ll==15:
            while True:
                b=src[i]; i+=1; ll+=b
                if b!=255: break
        dst+=src[i:i+ll]; i+=ll
        if i>=n: break
        off=src[i]|(src[i+1]<<8); i+=2
        ml=tok&15
        if ml==15:
            while True:
                b=src[i]; i+=1; ml+=b
                if b!=255: break
        ml+=4
        st=len(dst)-off
        for k in range(ml): dst.append(dst[st+k])
    assert len(dst)==out_size,(len(dst),out_size)
    return bytes(dst)
def read(path):
    d=open(path,'rb').read()
    magic,ver,nf,np_,ptsz,stsz,toff=struct.unpack_from('<4s6I',d,0)
    assert magic==b'ARC\0' and ver==3
    parts=[struct.unpack_from('<3I',d,toff+12*i) for i in range(np_)]
    strs=toff+ptsz
    ents=strs+stsz
    for i in range(nf):
        typ,off,csz,dsz,_u,ft,nparts,first,slen,soff=struct.unpack_from('<5IQ4I',d,ents+44*i)
        name=d[strs+soff:strs+soff+slen].decode('latin1')
        if typ==1 and csz==dsz:
            data=d[off:off+dsz]
        else:
            data=b''.join(lz4_block(d[p[0]:p[0]+p[1]],p[2]) if p[1]!=p[2] else d[p[0]:p[0]+p[1]] for p in parts[first:first+nparts])
        yield name,data
if __name__=='__main__':
    for name,data in read(sys.argv[1]):
        p=os.path.join(sys.argv[2],name); os.makedirs(os.path.dirname(p) or '.',exist_ok=True)
        open(p,'wb').write(data); print(name,len(data))
