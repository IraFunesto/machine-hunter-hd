r"""Immagini delle missioni in HD dalle foto fatte in gioco (tasto F12 -> FOTO\FOTO_nnn.BMP, 2240x960 senza HUD).

Per ogni foto: riconosce a quale missione corrisponde (confronto con le immagini originali 220x160 di UI_HD),
trova la posizione esatta, ritaglia la zona 880x640 (4x) e scrive UI_HD\NOME.raw con la grafica nuova.
La versione precedente resta in UI_HD\NOME.raw.esrgan.

  python mission_hd.py              (tutte le foto in FOTO\)
  python mission_hd.py FOTO_003.BMP  (solo quelle indicate)
  python mission_hd.py --ripristina MARS   (torna alla versione precedente)
"""
import os, sys, glob, struct, shutil
import numpy as np
from PIL import Image
from scipy.signal import fftconvolve

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(GAME, 'UI_HD'); FOTO = os.path.join(GAME, 'FOTO')
MISSIONS = ['ACITY', 'ASTER', 'BARRACKS', 'BOSS3', 'COMPOUND', 'DOCKS', 'HOSP', 'JUNK', 'LABS', 'MARS',
            'MINES', 'NEST', 'POWER', 'ROOFTOPS', 'SEWER', 'STREETS', 'SWAMP']
S = 4  # scala HD

def load_raw(name):
    path = os.path.join(UI, name + '.raw'); src = path + '.esrgan' if os.path.exists(path + '.esrgan') else path
    d = open(src, 'rb').read(); w, h = struct.unpack('<II', d[4:12]); o = 12
    idx = np.frombuffer(d[o:o+w*h], np.uint8).reshape(h, w); o += w*h
    bpp = 4 if d[:4] == b'MHB3' else 3; o += w*h*16*bpp
    pal = np.frombuffer(d[o:o+768], np.uint8).reshape(256, 3)
    return idx, pal

def feat(rgb):
    """luminanza con media/contrasto locali tolti: regge la differenza tra texture vecchie e nuove"""
    L = rgb[..., 0]*0.30 + rgb[..., 1]*0.59 + rgb[..., 2]*0.11
    k = np.ones((9, 9)) / 81
    m = fftconvolve(L, k, 'same'); v = fftconvolve((L - m)**2, k, 'same')
    return (L - m) / np.sqrt(v + 25)

def ncc(big, tpl):
    """correlazione normalizzata di tpl su tutte le posizioni valide di big"""
    th, tw = tpl.shape; t = (tpl - tpl.mean()) / (tpl.std() + 1e-6); n = th*tw
    num = fftconvolve(big, t[::-1, ::-1], 'valid')
    ones = np.ones_like(t)
    s1 = fftconvolve(big, ones, 'valid'); s2 = fftconvolve(big**2, ones, 'valid')
    sd = np.sqrt(np.maximum(s2 - s1**2/n, 1e-6))
    return num / sd / np.sqrt(n)

def write_raw(name, idx, pal, crop):
    path = os.path.join(UI, name + '.raw')
    if os.path.exists(path) and not os.path.exists(path + '.esrgan'): shutil.copyfile(path, path + '.esrgan')
    h, w = idx.shape
    open(path, 'wb').write(b'MHB2' + struct.pack('<II', w, h) + idx.tobytes() + crop.astype(np.uint8).tobytes() + pal.tobytes())
    Image.fromarray(crop.astype(np.uint8)).save(os.path.join(UI, name + '.png'))

def find_scale(P, o, lo=3.0, hi=7.0):
    """cerca posizione e scala dell'immagine originale o (220x160) nella foto P: ritorna (somiglianza, scala, x, y) in pixel della foto"""
    H, W = P.shape[:2]; h, w = o.shape[:2]
    pim = Image.fromarray(P.astype(np.uint8)); best = None
    t = feat(np.asarray(Image.fromarray(o.astype(np.uint8)).resize((w//2, h//2), Image.BILINEAR), np.float32))
    for sc in np.arange(lo, hi + 1e-6, 0.05):          # passata grossolana a meta' risoluzione
        f = 2*sc; pw, ph = int(W/f), int(H/f)
        if pw < w//2 or ph < h//2: continue
        c = ncc(feat(np.asarray(pim.resize((pw, ph), Image.BOX), np.float32)), t); y, x = np.unravel_index(np.argmax(c), c.shape)
        if best is None or c[y, x] > best[0]: best = (float(c[y, x]), sc, x*f, y*f)
    t = feat(o.astype(np.float32)); s0 = best[1]; best = None
    for sc in np.arange(s0 - 0.08, s0 + 0.081, 0.01):  # rifinitura
        pw, ph = int(W/sc), int(H/sc)
        if pw < w or ph < h: continue
        c = ncc(feat(np.asarray(pim.resize((pw, ph), Image.BOX), np.float32)), t); y, x = np.unravel_index(np.argmax(c), c.shape)
        if best is None or c[y, x] > best[0]: best = (float(c[y, x]), sc, x*sc, y*sc)
    return best

def process(photo, origs, name=None):
    P = np.asarray(Image.open(photo).convert('RGB'), np.float32); H, W = P.shape[:2]
    cands = [name] if name else list(origs)
    best = None
    for n in cands:
        idx, pal = origs[n]; r = find_scale(P, pal[idx].astype(np.float32))
        if r and (best is None or r[0] > best[0]): best = r + (n,)
    score, sc, X, Y, name = best
    idx, pal = origs[name]; h, w = idx.shape
    print(f'{os.path.basename(photo)}: {name}  somiglianza {score:.2f}  scala {sc:.2f} (normale 4)  posizione {X:.0f},{Y:.0f}')
    if score < 0.3:
        print('   somiglianza troppo bassa: la foto non sembra inquadrare la stessa zona'); return
    pim = Image.fromarray(P.astype(np.uint8))
    crop = np.asarray(pim.crop((int(round(X)), int(round(Y)), int(round(X + w*sc)), int(round(Y + h*sc)))).resize((w*S, h*S), Image.LANCZOS), np.float32)
    write_raw(name, idx, pal, crop)
    # versione larga (NOMEW.raw) per i briefing allargati: fino a 32:9 -> 220 + (854 - 320) = 754 pixel originali
    cx = X + w*sc/2; half_u = min(754/2, cx/sc, (W - cx)/sc); ww = int(half_u)*2
    if ww > w:
        wide = np.asarray(pim.crop((int(round(cx - ww*sc/2)), int(round(Y)), int(round(cx + ww*sc/2)), int(round(Y + h*sc)))).resize((ww*S, h*S), Image.LANCZOS), np.float32)
        small_w = wide.reshape(h, S, ww, S, 3).mean((1, 3))
        widx = ((small_w[:, :, None, :] - pal[None, None].astype(np.float32))**2).sum(-1).argmin(-1).astype(np.uint8)
        widx[widx == 0] = 1
        o0 = (ww - w)//2; widx[:, o0:o0 + w] = np.where(idx > 0, idx, widx[:, o0:o0 + w])
        write_raw(name + 'W', widx, pal, wide)
        print(f'   versione larga: {ww} pixel originali (servono 754 per il 32:9{", ok" if ww >= 754 else ": allontana un po' lo zoom"})')
    cmp_ = Image.new('RGB', (w*S*2 + 8, h*S))
    cmp_.paste(Image.fromarray(pal[idx]).resize((w*S, h*S), Image.NEAREST), (0, 0)); cmp_.paste(Image.fromarray(crop.astype(np.uint8)), (w*S + 8, 0))
    cmp_.save(os.path.join(FOTO, 'confronto_' + name + '.png'))
    print(f'   scritto UI_HD\\{name}.raw (confronto in FOTO\\confronto_{name}.png)')

def foto(photo):
    """La foto fatta in gioco diventa l'immagine dell'anteprima (UI_HD/NOMEF.raw), intera e senza modifiche:
    il gioco la ritaglia al centro secondo la finestra dell'anteprima (qualsiasi rapporto d'aspetto)."""
    name = level_of(photo)
    if not name: print(photo, ': livello sconosciuto (manca il .txt)'); return
    P = np.asarray(Image.open(photo).convert('RGB'), np.uint8); H, W = P.shape[:2]
    W4, H4 = W // 4 * 4, H // 4 * 4; P = P[(H - H4)//2:(H - H4)//2 + H4, (W - W4)//2:(W - W4)//2 + W4]
    idx, pal = load_raw(name); w, h = W4 // 4, H4 // 4
    assert w <= 1024 and h <= 480, 'foto troppo grande'
    out = os.path.join(UI, name + 'F.raw')
    open(out, 'wb').write(b'MHB2' + struct.pack('<II', w, h) + bytes(w*h) + P.tobytes() + pal.tobytes())
    print('%s: missione %s -> UI_HD/%sF.raw (%dx%d)' % (os.path.basename(photo), name, name, W4, H4))

def level_of(photo):
    t = os.path.splitext(photo)[0] + '.txt'
    if os.path.exists(t):
        for line in open(t):
            if line.startswith('level='):
                from mission_search import LEVEL_IMAGE
                lv = int(line.split('=')[1]); return LEVEL_IMAGE[lv] if 0 <= lv < len(LEVEL_IMAGE) else None
    return None

if __name__ == '__main__':
    args = sys.argv[1:]
    if args[:1] == ['--foto']:
        for a in args[1:]: foto(os.path.join(FOTO, a) if not os.path.isabs(a) else a)
        sys.exit()
    if args[:1] == ['--ripristina']:
        for n in args[1:]:
            p = os.path.join(UI, n.upper() + '.raw')
            if os.path.exists(p + '.esrgan'): shutil.copyfile(p + '.esrgan', p); print('ripristinato', n.upper())
        sys.exit()
    origs = {n: load_raw(n) for n in MISSIONS if os.path.exists(os.path.join(UI, n + '.raw'))}
    photos = [os.path.join(FOTO, a) if not os.path.isabs(a) else a for a in args] or sorted(glob.glob(os.path.join(FOTO, 'FOTO_*.BMP')))
    for p in photos: process(p, origs, level_of(p))
