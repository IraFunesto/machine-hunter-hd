r"""Indice per il render nuovo: impronta di ogni texture (blocco 256x256 di indici) e di ogni fotogramma sprite dell'archivio HD
-> file PNG a colori veri da cui viene (TEXTURES_4X\LIVELLO\nnn.png, SPRITES_4X\NOME\nnn.png).
WINMX.dll calcola la stessa impronta su cio' che il gioco ha in memoria e carica il PNG.

  python png_index.py            (scrive DATA\HDPNG.idx)
  python png_index.py --prova MHPOLY.bin   (controlla quante texture/sprite di un fotogramma registrato vengono trovate)

Impronta: somma su i di byte[i] * w[i] (mod 2^64), con w[i] = LCG 64 bit (w0 = 0x9E3779B97F4A7C15, w = w*6364136223846793005 + 1442695040888963407),
piu' la lunghezza * 0x100000001B3."""
import os, sys, json, struct
import numpy as np
HERE = os.environ.get('MH_TOOLS') or os.path.dirname(os.path.abspath(__file__)); GAME = os.environ.get('MH_GAME') or os.path.dirname(HERE)
sys.path.insert(0, HERE)
import mh_tex as m

def weights(n):
    w = np.empty(n, np.uint64); x = 0x9E3779B97F4A7C15; M = (1 << 64) - 1
    for i in range(n):
        w[i] = x; x = (x * 6364136223846793005 + 1442695040888963407) & M
    return w
W = weights(65536)

def fp(b):
    b = np.frombuffer(bytes(b), np.uint8) if not isinstance(b, np.ndarray) else b.ravel()
    with np.errstate(over='ignore'):
        h = np.sum(b.astype(np.uint64) * W[:len(b)], dtype=np.uint64)
        h = h + np.uint64(len(b)) * np.uint64(0x100000001B3)
    return int(h)

def build(arc=None, out=None):
    arc = arc or os.path.join(GAME, 'DATA', 'HD4S.ARC')
    _, files = m.load_arc(arc)
    tman = json.load(open(os.path.join(GAME, 'TEXTURES_4X', 'manifest.json')))
    sman = json.load(open(os.path.join(GAME, 'SPRITES_4X', 'manifest.json')))
    ents = {}; dup = 0; pals = []; palid = {}
    def pal_of(key, col):   # palette con cui e' stato salvato il PNG (per ricolorare coi colori veri del livello)
        if key not in palid: palid[key] = len(pals); pals.append(np.asarray(col, np.uint8)[:, :3].copy())
        return palid[key]
    for lvl, sizes in tman.items():
        d = files[lvl + '.BIN']; n, offs, base = m.bin_parts(d); pid = pal_of(m.PALOF.get(lvl, lvl), m.level_colors(files, lvl))
        for i, o in enumerate(offs):
            blk = d[base + o: base + o + 65536]
            if len(blk) < 65536: continue
            h = fp(np.frombuffer(blk, np.uint8))
            if h in ents: dup += 1; continue
            ents[h] = (0, sizes[i], i, lvl, pid)
    sp = m.level_colors(files, m.SPRPAL).astype(np.uint8)[:, :3].copy()
    sp[m.PLAYER_TO] = sp[m.PLAYER_FROM]   # tuta del giocatore: gli indici spostati hanno i colori arancioni da cui vengono
    spid = pal_of('__sprite__', sp)
    for nm in sman:
        for i, (code, img) in enumerate(m.b_frames(files[nm + '.B'])):
            h = fp(img)
            if h in ents: dup += 1; continue
            ents[h] = (1, img.shape[0], i, nm, spid)
    out = out or os.path.join(GAME, 'DATA', 'HDPNG.idx')
    with open(out, 'wb') as f:
        f.write(b'MHI2' + struct.pack('<II', len(ents), len(pals)))
        for pl in pals: f.write(pl.tobytes())
        for h, (kind, k, i, nm, pid) in ents.items():
            f.write(struct.pack('<QBBH11sB', h, kind, k if k < 256 else 0, i, nm.encode()[:11], pid))
    print('scritto', out, len(ents), 'voci (doppioni saltati %d)' % dup)

def prova(path):
    sys.path.insert(0, os.path.join(HERE, 'render_proto')); from poly_load import load
    idx = {}
    d = open(os.path.join(GAME, 'DATA', 'HDPNG.idx'), 'rb').read(); n, npal = struct.unpack('<II', d[4:12]); o = 12 + npal*768
    for k in range(n):
        h, kind, kk, i, nm, pid = struct.unpack('<QBBH11sB', d[o + 24*k: o + 24 + 24*k]); idx[h] = (kind, kk, i, nm.rstrip(b'\0').decode())
    pal, lut, R, tex, spr = load(path)
    ok = sum(fp(t) in idx for t in tex.values()); print('texture mappa trovate: %d / %d' % (ok, len(tex)))
    side = {6: 64, 9: 64, 7: 128, 10: 128, 8: 256, 11: 256}; ok = tot = 0
    for r in R:
        if r[0] >= 6 and r[28] >= 0:
            s = side[r[0]]; tot += 1; ok += fp(spr[r[28]][:s*s]) in idx
    print('sprite trovati: %d / %d' % (ok, tot))

if __name__ == '__main__':
    if sys.argv[1:2] == ['--prova']: prova(sys.argv[2])
    elif sys.argv[1:2] == ['--arc']: build(sys.argv[2], sys.argv[3])   # --arc ARCHIVIO INDICE_USCITA
    else: build()
