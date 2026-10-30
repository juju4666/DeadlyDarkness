import json, numpy as np
from PIL import Image
L=json.load(open('/tmp/hw/landmarks.json'))
BW,BH=192,176
def render(name, D, cy):
    d=L[name]; src=Image.open('/tmp/hw/HUD_Witch/%s.png'%name).convert('RGBA')
    e=np.array(d['eyes']); m=np.array(d['mouth']); c=(e+m)/2
    cx=(d['cheekL'][0]+d['cheekR'][0])/2
    s=D/d['d3']
    nw,nh=max(1,round(src.width*s)),max(1,round(src.height*s))
    im=src.convert('RGBa').resize((nw,nh),Image.LANCZOS).convert('RGBA')
    ox=round(BW/2-cx*s); oy=round(cy-c[1]*s)
    out=Image.new('RGBA',(BW,BH),(0,0,0,0)); out.alpha_composite(im,(0,0),(0,0)) if False else None
    canvas=Image.new('RGBA',(BW,BH),(0,0,0,0)); canvas.paste(im,(ox,oy),im)
    return canvas
if __name__=='__main__':
    names=['Idle_Healthy','Damage_Right_Healthy','Shock_Front_Healthy','Idle_Hurt','Damage_Left_Hurt','Dead']
    for tag,D,cy in (('A',74.9,96.8),('B',66,92),('C',60,90)):
        sh=Image.new('RGB',(200*6,184),(30,90,50))
        for i,n in enumerate(names):
            im=render(n,D,cy); sh.paste(im,(i*200+4,4),im)
        sh.save('/tmp/hw/cand_%s.png'%tag)
