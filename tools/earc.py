import struct,sys,os
def rnc1(src):
    assert src[:4]==b'RNC\x01'
    usize,psize=struct.unpack('>II',src[4:12])
    p=18; out=bytearray()
    def lw(i): return src[i]|(src[i+1]<<8) if i+1<len(src) else (src[i] if i<len(src) else 0)
    st={'buf':lw(p),'cnt':16,'p':p}
    def adv(n):
        st['buf']>>=n; st['cnt']-=n
        if st['cnt']<16:
            st['p']+=2; st['buf']|=lw(st['p'])<<st['cnt']; st['cnt']+=16
    def rd(mask,n):
        v=st['buf']&mask; adv(n); return v
    def fix():
        st['cnt']-=16; st['buf']&=(1<<st['cnt'])-1; st['buf']|=lw(st['p'])<<st['cnt']; st['cnt']+=16
    def table():
        num=rd(0x1f,5)
        if not num: return []
        L=[rd(0xf,4) for _ in range(num)]; mx=max(1,max(L)); t=[]; code=0
        for i in range(1,mx+1):
            for j in range(num):
                if L[j]==i:
                    m=int('{:0{w}b}'.format(code,w=i)[::-1],2); t.append((m,i,j)); code+=1
            code<<=1
        return t
    def hr(t):
        for code,ln,v in t:
            if st['buf']&((1<<ln)-1)==code:
                adv(ln)
                if v>=2:
                    val=1<<(v-1); val|=rd(val-1,v-1); return val
                return v
        raise ValueError('bad huffman')
    adv(2)
    while len(out)<usize:
        raw=table(); dist=table(); ln=table()
        cnt=rd(0xffff,16)
        while True:
            n=hr(raw)
            if n:
                out+=src[st['p']:st['p']+n]; st['p']+=n; fix()
            cnt-=1
            if cnt<=0: break
            pos=hr(dist)+1; n=hr(ln)+2
            for _ in range(n): out.append(out[-pos])
    assert len(out)==usize,(len(out),usize)
    return bytes(out)
def entries(d):
    n=struct.unpack('<I',d[4:8])[0]; E=[]
    for i in range(n):
        o,s=struct.unpack('<II',d[8+i*20:16+i*20]); E.append((d[16+i*20:28+i*20].split(b'\0')[0].decode('latin1'),o,s))
    return E
if __name__=='__main__':
    arc,outdir=sys.argv[1],sys.argv[2]; d=open(arc,'rb').read(); os.makedirs(outdir,exist_ok=True); bad=0
    for nm,o,s in entries(d):
        blob=d[o:o+s]
        try: data=rnc1(blob) if blob[:4]==b'RNC\x01' else blob
        except Exception as e: print('ERR',nm,e); bad+=1; continue
        open(os.path.join(outdir,nm),'wb').write(data)
    print('done, errors:',bad)
