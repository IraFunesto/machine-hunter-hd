r"""Ricerca automatica dell'inquadratura delle immagini delle missioni.

La DLL (modalita' debug shot=4, sezione [scan] di MHFIX.INI) carica il livello, muove solo la telecamera su una griglia
di posizioni e rotazioni e salva miniature in SCAN\sNNNN.raw. Questo script confronta le miniature con l'immagine
originale della missione e stampa le inquadrature piu' simili.

  python mission_search.py MARS            (cartella SCAN)
  python mission_search.py MARS --top 20
"""
import os, sys, glob, struct
import numpy as np
from PIL import Image
import mission_hd as M

GAME = M.GAME
# livello del gioco -> immagine del briefing (tabella del gioco a 0x48a8b0, byte +1, e immagini a 0x48b030)
LEVEL_IMAGE = ['MARS', 'MINES', 'COMPOUND', 'BARRACKS', 'JUNK', 'POWER', 'ACITY', 'DOCKS', 'LABS',
               'SWAMP', 'SEWER', 'HOSP', 'STREETS', 'ROOFTOPS', 'ASTER', 'NEST', 'BOSS3']

def load_shots(folder):
    out = []
    for f in sorted(glob.glob(os.path.join(folder, 's*.raw'))):
        d = open(f, 'rb').read(); w, h, x, y, a, _ = struct.unpack('<6i', d[:24])
        out.append((f, x, y, a, np.frombuffer(d[24:24 + w*h*3], np.uint8).reshape(h, w, 3).astype(np.float32)))
    return out

def search(name, folder, top=10, scale=None):
    idx, pal = M.load_raw(name); orig = pal[idx].astype(np.float32)
    shots = load_shots(folder)
    if not shots: print('nessuna miniatura'); return []
    h0, w0 = shots[0][4].shape[:2]
    # la miniatura e' lo schermo intero (H = 240 unita' del menu) ridotto: l'originale e' in unita' del menu
    s = scale or h0 / 240.0
    tpl = np.asarray(Image.fromarray(orig.astype(np.uint8)).resize((max(8, round(220*s)), max(8, round(160*s))), Image.BILINEAR), np.float32)
    ft = M.feat(tpl); res = []
    for f, x, y, a, img in shots:
        c = M.ncc(M.feat(img), ft); yy, xx = np.unravel_index(np.argmax(c), c.shape)
        res.append((float(c[yy, xx]), x, y, a, xx / s, yy / s, f))
    res.sort(key=lambda r: -r[0])
    for r in res[:top]: print('%.3f  pos %6d,%6d  imbardata %4d  offset %5.1f,%5.1f  %s' % (r[0], r[1], r[2], r[3], r[4], r[5], os.path.basename(r[6])))
    return res

if __name__ == '__main__':
    a = sys.argv[1:]; top = 10
    if '--top' in a: i = a.index('--top'); top = int(a[i+1]); del a[i:i+2]
    search(a[0].upper(), a[1] if len(a) > 1 else os.path.join(GAME, 'SCAN'), top)
