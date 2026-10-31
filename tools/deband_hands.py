import sys, io
sys.path.insert(0,'/home/claude/work/src/begotten-engine/tools')
import numpy as np, pack_wad as P
from PIL import Image
WAD=sys.argv[1]; OUT=sys.argv[2]; STR=float(sys.argv[3]) if len(sys.argv)>3 else 1.0
ident,l=P.read_wad(WAD); d=dict(l)
pal=P.Palette(d['PLAYPAL'],d.get('PLAYPALX'))
glow={tuple(pal.rgb[i]) for i in range(512) if 240<=(i&255)<=254}
def bilateral(a,mask,sc,rad=5,ss=2.5):
    h,w,_=a.shape; out=np.zeros_like(a); wsum=np.zeros((h,w,1))
    pa=np.pad(a,((rad,rad),(rad,rad),(0,0))); pm=np.pad(mask.astype(float),rad)
    for dy in range(-rad,rad+1):
        for dx in range(-rad,rad+1):
            sw=np.exp(-(dx*dx+dy*dy)/(2*ss*ss))
            s=pa[rad+dy:rad+dy+h,rad+dx:rad+dx+w]; m=pm[rad+dy:rad+dy+h,rad+dx:rad+dx+w]
            dist=((s-a)**2).sum(2)
            wt=(sw*m*np.exp(-dist/(2*sc*sc)))[...,None]
            out+=s*wt; wsum+=wt
    return out/np.maximum(wsum,1e-9)
names=[n for n,_ in l if len(n)==6 and n[0]=='W' and n[1:4] in('ARC','BFG','CHG','HLG','PIS','PLG','PUN','SAW','SHG','SNP','TRN') and n[4].isalpha() and n[5]=='0' or (n[:4]=='WSHT' and len(n)==6)]
new=[]; stats=[]
for n,b in l:
    if n in names:
        r=P.decode_patch(b,pal.rgb)
        if r:
            img,lo,to=r
            a=np.array(img).astype(float); mask=a[...,3]>=128; rgb=a[...,:3]
            isg=np.zeros(mask.shape,bool)
            for y,x in zip(*np.nonzero(mask)):
                if tuple(int(v) for v in rgb[y,x]) in glow: isg[y,x]=True
            body=mask&~isg
            sm=rgb.copy()
            for _ in range(2): sm=bilateral(sm,body,20*STR)
            sm=np.where(body[...,None],sm,rgb)
            im=Image.fromarray(np.clip(sm+.5,0,255).astype(np.uint8),'RGB')
            idx=P.dither_indices(im,pal)
            w,h=img.size; cols=[]; wide=False
            for x in range(w):
                posts=[]; y=0
                while y<h:
                    if not mask[y,x]: y+=1; continue
                    y0=y; run=[]
                    while y<h and mask[y,x] and len(run)<254:
                        run.append(pal.index(tuple(int(v) for v in rgb[y,x]),True) if isg[y,x] else idx[y][x]); y+=1
                    if max(run)>255: wide=True
                    posts.append((y0,run))
                cols.append(posts)
            enc=[]
            for posts in cols:
                o=bytearray(); prev=-1
                for y0,run in posts: prev=P._post(o,y0,run,prev,wide)
                o.append(255); enc.append(bytes(o))
            import struct
            pos=8+4*w; offs=[]
            for c in enc: offs.append(pos); pos+=len(c)
            body_b=struct.pack('<hhhh',w,h,lo,to)+struct.pack('<%di'%w,*offs)+b''.join(enc)
            b=(P.WIDE_MAGIC+body_b) if wide else body_b
            stats.append(n)
    new.append((n,b))
print(len(stats),'reworked')
P.write_wad(OUT,ident,new)
