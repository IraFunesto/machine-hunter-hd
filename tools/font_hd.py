r"""Caratteri HD ricostruiti dalla forma originale (niente reti neurali, niente pixel ingranditi).

Per ogni zona con testo nelle immagini UI_HD\*.raw:
  - separa il corpo della lettera dall'ombra scura;
  - stima la copertura del corpo (i grigi di bordo dell'originale sono antialiasing) e la ingrandisce x4
    con interpolazione morbida, poi ricava un contorno netto (soglia con bordo di 1 pixel HD);
  - fa lo stesso con la sagoma dell'ombra;
  - colora con il colore pulito del corpo (senza la grana dei pixel intermedi).
Scrive il formato MHB3 (RGBA): i bordi sono trasparenti veri, composti dalla DLL sopra cio' che c'e' sotto.
La versione precedente resta in UI_HD\NOME.raw.esrgan (sorgente di partenza a ogni esecuzione).

  python font_hd.py
"""
import os, struct, shutil
import numpy as np
from PIL import Image

GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UI = os.path.join(GAME, 'UI_HD')
def bands(x0, y0, x1, y1, step): return [(x0, y, x1, y + step) for y in range(y0, y1, step)]
# ogni riga di caratteri e' elaborata da sola: niente sbavature dalla riga sopra/sotto
ZONES = {
    'TEXT3': bands(0, 0, 256, 192, 16) + bands(0, 192, 256, 240, 8),
    'PCPANEL': bands(256, 0, 512, 48, 8) + [(168, 40, 256, 48), (0, 48, 240, 64)] + bands(240, 64, 512, 80, 8),
}

def up(a, mode=Image.BICUBIC):
    h, w = a.shape
    return np.asarray(Image.fromarray(a.astype(np.float32), 'F').resize((4*w, 4*h), mode), np.float32)

def smooth(x, lo=0.38, hi=0.62):
    t = np.clip((x - lo) / (hi - lo), 0, 1); return t*t*(3 - 2*t)

def lum(c): return c[..., 0]*0.30 + c[..., 1]*0.59 + c[..., 2]*0.11

def zone_rgba(idx, pal):
    C = pal[idx].astype(np.float32) / 255; nz = idx != 0; L = lum(C)
    # ombra: il colore scuro piu' frequente
    dark = idx[nz & (L < 0.12)]
    S = pal[np.bincount(dark).argmax()].astype(np.float32)/255 if dark.size else np.zeros(3, np.float32)
    LS = lum(S)
    # colore del corpo: il pixel piu' luminoso nei dintorni 3x3 (le lettere hanno bordi sfumati verso l'ombra)
    h, w = idx.shape; P = np.pad(np.where(nz[..., None], C, 0), ((1, 1), (1, 1), (0, 0))); PL = lum(P)
    best = np.zeros((h, w), np.float32); B = np.zeros_like(C)
    for dy in range(3):
        for dx in range(3):
            l = PL[dy:dy+h, dx:dx+w]; m = l > best; best = np.where(m, l, best); B = np.where(m[..., None], P[dy:dy+h, dx:dx+w], B)
    cov = np.where(nz, np.clip((L - LS) / np.maximum(lum(B) - LS, 0.02), 0, 1), 0).astype(np.float32)
    body = cov > 0.92
    # colore HD: media pesata dei colori dei pixel pieni, interpolata
    wgt = up(body.astype(np.float32), Image.BILINEAR) + 1e-4
    B4 = np.stack([up(np.where(body, C[..., k], 0), Image.BILINEAR) for k in range(3)], -1) / wgt[..., None]
    Bfb = np.stack([up(B[..., k], Image.BILINEAR) for k in range(3)], -1)
    B4 = np.where((wgt > 0.05)[..., None], B4, Bfb)
    a = smooth(up(cov)); sa = smooth(up(nz.astype(np.float32)))
    # ombra: nell'originale e' la lettera stessa spostata di qualche pixel -> la rifaccio spostando la sagoma HD
    bm = cov > 0.5; best_s, best_o = 0.0, None
    for oy in range(0, 3):
        for ox in range(0, 3):
            if not ox and not oy: continue
            sh = np.zeros_like(bm); sh[oy:, ox:] = bm[:h-oy, :w-ox]; un = sh | bm
            sc = (un & nz).sum() / max((un | nz).sum(), 1)
            if sc > best_s: best_s, best_o = sc, (ox, oy)
    if best_o and best_s > 0.8:
        ox, oy = best_o; s4 = np.zeros_like(a); s4[4*oy:, 4*ox:] = a[:4*h-4*oy, :4*w-4*ox]
        # passaggio continuo tra lettera e ombra (l'originale le unisce con pixel diagonali)
        for k in range(1, 4):
            t = np.zeros_like(a); t[k*oy:, k*ox:] = a[:4*h-k*oy, :4*w-k*ox]; s4 = np.maximum(s4, t)
        sa = s4
    rgb = S[None, None, :]*(1 - a[..., None]) + B4*a[..., None]
    alpha = np.maximum(a, sa)
    return np.clip(np.concatenate([rgb, alpha[..., None]], -1)*255 + 0.5, 0, 255).astype(np.uint8)

def process(name, zones):
    path = os.path.join(UI, name + '.raw'); bak = path + '.esrgan'
    if not os.path.exists(bak): shutil.copyfile(path, bak)
    d = open(bak, 'rb').read()
    assert d[:4] == b'MHB2', name
    w, h = struct.unpack('<II', d[4:12]); o = 12
    idx = np.frombuffer(d[o:o+w*h], np.uint8).reshape(h, w); o += w*h
    rgb = np.frombuffer(d[o:o+w*h*48], np.uint8).reshape(4*h, 4*w, 3); o += w*h*48
    pal = np.frombuffer(d[o:o+768], np.uint8).reshape(256, 3)
    out = np.concatenate([rgb, np.full((4*h, 4*w, 1), 255, np.uint8)], -1)
    for (x0, y0, x1, y1) in (zones or [(0, 0, w, h)]):
        out[4*y0:4*y1, 4*x0:4*x1] = zone_rgba(idx[y0:y1, x0:x1], pal)
    open(path, 'wb').write(b'MHB3' + d[4:12] + idx.tobytes() + out.tobytes() + pal.tobytes())
    prev = Image.new('RGBA', (4*w, 4*h), (40, 60, 90, 255)); prev.alpha_composite(Image.fromarray(out, 'RGBA'))
    prev.convert('RGB').save(os.path.join(UI, name + '.png'))
    print('ok', name, w, h)

if __name__ == '__main__':
    for n, z in ZONES.items(): process(n, z)
