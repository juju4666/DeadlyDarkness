import mediapipe as mp, numpy as np, json, glob, os
from PIL import Image
fm = mp.solutions.face_mesh.FaceMesh(static_image_mode=True, max_num_faces=1, refine_landmarks=True, min_detection_confidence=0.2)
EYE_L=[33,133,159,145]; EYE_R=[362,263,386,374]
out={}
for f in sorted(glob.glob('/tmp/hw/HUD_Witch/*.png')):
    im=Image.open(f).convert('RGBA'); W,H=im.size
    bg=Image.new('RGB',im.size,(128,128,128)); bg.paste(im,mask=im.split()[3])
    r=fm.process(np.asarray(bg))
    if not r.multi_face_landmarks: print('NO FACE',f); continue
    L=np.array([[p.x*W,p.y*H,p.z*W] for p in r.multi_face_landmarks[0].landmark])
    eyes=(L[EYE_L].mean(0)+L[EYE_R].mean(0))/2
    mouth=(L[13]+L[14])/2
    d3=float(np.linalg.norm(eyes-mouth)); d2=float(np.linalg.norm(eyes[:2]-mouth[:2]))
    oval=L[[10,152,234,454],:2]
    n=os.path.basename(f)[:-4]
    out[n]=dict(W=W,H=H,eyes=eyes[:2].tolist(),mouth=mouth[:2].tolist(),d3=d3,d2=d2,forehead=L[10,:2].tolist(),chin=L[152,:2].tolist(),cheekL=L[234,:2].tolist(),cheekR=L[454,:2].tolist())
    print('%-24s %5dx%-5d d3=%6.1f d2=%6.1f  canvas/d3 = %.2f x %.2f  face h/d3=%.2f w/d3=%.2f'%(n,W,H,d3,d2,W/d3,H/d3,np.linalg.norm(L[10,:2]-L[152,:2])/d3,abs(L[454,0]-L[234,0])/d3))
json.dump(out,open('/tmp/hw/landmarks.json','w'),indent=1)
