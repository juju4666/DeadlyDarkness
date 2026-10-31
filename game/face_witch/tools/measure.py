"""Measures the face landmarks of the witch photos: eyes, mouth, cheeks, forehead, chin, and the 3D distance between
the middle of the eyes and the mouth (d3), which layout.py uses to give every face the same scale.
Needs mediapipe 0.10.14 on Python 3.12 (pip install mediapipe==0.10.14 pillow "numpy<2").
usage: measure.py <folder with the PNG photos> <landmarks.json>"""
import sys, glob, os, json
import numpy as np
import mediapipe as mp
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
SHRINK = 3        # the detector does not need 12 megapixels; landmarks are scaled back
fm = mp.solutions.face_mesh.FaceMesh(static_image_mode=True, max_num_faces=1, refine_landmarks=True, min_detection_confidence=0.2)
EYE_L = [33, 133, 159, 145]
EYE_R = [362, 263, 386, 374]
out = {}
for f in sorted(glob.glob(os.path.join(src, '*.png'))):
    im = Image.open(f).convert('RGBA')
    W, H = im.size
    bg = Image.new('RGB', im.size, (128, 128, 128))
    bg.paste(im, mask=im.split()[3])
    small = bg.resize((W // SHRINK, H // SHRINK), Image.LANCZOS)
    r = fm.process(np.asarray(small))
    n = os.path.basename(f)[:-4]
    if not r.multi_face_landmarks:
        print('NO FACE', n)
        continue
    sw, sh = small.size
    L = np.array([[p.x * W, p.y * H, p.z * W] for p in r.multi_face_landmarks[0].landmark])
    eyes = (L[EYE_L].mean(0) + L[EYE_R].mean(0)) / 2
    mouth = (L[13] + L[14]) / 2
    d3 = float(np.linalg.norm(eyes - mouth))
    out[n] = dict(W=W, H=H, eyes=eyes[:2].tolist(), mouth=mouth[:2].tolist(), d3=d3,
                  forehead=L[10, :2].tolist(), chin=L[152, :2].tolist(), cheekL=L[234, :2].tolist(), cheekR=L[454, :2].tolist())
    print('%-22s %dx%d d3=%6.1f  eyes (%.0f,%.0f) mouth (%.0f,%.0f)' % (n, W, H, d3, eyes[0], eyes[1], mouth[0], mouth[1]))
json.dump(out, open(dst, 'w'), indent=1)
