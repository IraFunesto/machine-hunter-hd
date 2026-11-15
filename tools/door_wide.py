r"""Porte del menu principale (TOPDOR, BOTDOR) allargate per gli schermi larghi: UI_HD\TOPDORW.raw, BOTDORW.raw.

I lati mancanti non sono stirati: vengono costruiti ripetendo a specchio la fascia vicino al bordo dell'originale
(solo pannello, bordo del pannello e fascia di metallo, che proseguono in orizzontale; bulloni esclusi):
giunture continue e stessa luce. Larghezza 854 pixel originali (32:9 a 240 righe): la DLL ne prende il centro
largo quanto serve (16:9, 21:9...).

  python door_wide.py            (scrive i due file; anteprima in tools\render_proto\porte_larghe.png)
"""
import os, struct, random
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__)); UI = os.path.join(os.path.dirname(HERE), 'UI_HD')
WIDE = 854                       # 320 + (854 - 320): massimo, 32:9
# per fasce di righe (pixel originali, estremi inclusi): (riga0, riga1, colonna0, colonna1) da ripetere a specchio;
# dove non ci sono bulloni ne' bordi verticali l'intervallo e' largo (ripetizione meno visibile). sinistra, destra
SRC = {
    'TOPDOR': ([(0, 75, 0, 10), (76, 147, 0, 26)],
               [(0, 71, 276, 319), (72, 147, 240, 319)]),
    'BOTDOR': ([(0, 40, 0, 34), (41, 62, 0, 40), (63, 147, 0, 90)],
               [(0, 30, 240, 319), (31, 47, 258, 319), (48, 62, 305, 319), (63, 147, 258, 319)]),
}

def load(name):
    d = open(os.path.join(UI, name + '.raw'), 'rb').read()
    mg = d[:4]; w, h = struct.unpack_from('<II', d, 4); o = 12
    idx = np.frombuffer(d, np.uint8, w*h, o).reshape(h, w).copy(); o += w*h
    bpp = 4 if mg == b'MHB3' else 3
    rgb = np.frombuffer(d, np.uint8, w*h*16*bpp, o).reshape(h*4, w*4, bpp)[..., :3].astype(np.float32); o += w*h*16*bpp
    pal = d[o:o + 768] if len(d) >= o + 768 else bytes(768)
    return idx, rgb, pal

def extend(idx, rgb, bands, n, left):
    """n colonne nuove: per ogni fascia di righe, le colonne pulite vicino al bordo ripetute a specchio partendo dal
    bordo dell'immagine -> giunture continue, stessa luce; ritorna (idx, rgb) larghi n (da sinistra a destra)"""
    h = idx.shape[0]; oi = np.zeros((h, n), np.uint8); orgb = np.zeros((h*4, n*4, 3), np.float32)
    for y0, y1, a, b in bands:
        L = (b - a + 1) * 4; cols = []
        for d in range(n * 4):                  # d = distanza dal bordo in colonne HD
            p = d % (2 * L); off = p if p < L else 2 * L - 1 - p
            cols.append(a * 4 + off if left else b * 4 + 3 - off)
        if left: cols = cols[::-1]
        cols = np.array(cols)
        orgb[y0*4:(y1+1)*4] = rgb[y0*4:(y1+1)*4][:, cols]
        oi[y0:y1+1] = idx[y0:y1+1][:, cols[1::4] // 4]
    return oi, orgb

def build(name):
    idx, rgb, pal = load(name); h, w = idx.shape; side = (WIDE - w) // 2
    li, lr = extend(idx, rgb, SRC[name][0], side, True)
    ri, rr = extend(idx, rgb, SRC[name][1], WIDE - w - side, False)
    oi = np.concatenate([li, idx, ri], 1); orgb = np.concatenate([lr, rgb, rr], 1)
    for j0 in (side*4, (side + w)*4):                                       # giunture con l'originale: sfumatura di 2 pixel HD per lato
        a = orgb[:, j0-3].copy(); b = orgb[:, j0+2].copy()
        for k, t in enumerate(np.linspace(0, 1, 6)[1:-1]): orgb[:, j0-2+k] = a*(1-t) + b*t
    trans = np.repeat(np.repeat(oi == 0, 4, 0), 4, 1); orgb[trans] = 0
    out = np.clip(orgb + 0.5, 0, 255).astype(np.uint8)
    path = os.path.join(UI, name + 'W.raw')
    open(path, 'wb').write(b'MHB2' + struct.pack('<II', WIDE, h) + oi.tobytes() + out.tobytes() + pal)
    print('scritto', path, WIDE, 'x', h)
    return out, oi

if __name__ == '__main__':
    a, ai = build('TOPDOR'); b, bi = build('BOTDOR')
    prev = np.concatenate([a, np.full((8, a.shape[1], 3), 90, np.uint8), b], 0)
    m = np.concatenate([np.repeat(np.repeat(ai == 0, 4, 0), 4, 1), np.zeros((8, a.shape[1]), bool), np.repeat(np.repeat(bi == 0, 4, 0), 4, 1)], 0)
    prev[m] = (40, 0, 40)
    im = Image.fromarray(prev); im.thumbnail((1900, 900)); os.makedirs(os.path.join(HERE, 'render_proto'), exist_ok=True); im.save(os.path.join(HERE, 'render_proto', 'porte_larghe.png'))
