r"""Machine Hunter - strumento texture.

  python mh_tex.py export            estrae le texture dei livelli in ..\TEXTURES\<LIVELLO>\NNN.png (64x64)
  python mh_tex.py esrgan [4|2]      upscale con Real-ESRGAN (GPU) in ..\TEXTURES_4X o ..\TEXTURES_2X
  python mh_tex.py upscale           crea ..\TEXTURES_2X (128x128) con un ingrandimento semplice (Lanczos), da sostituire con i tuoi upscale
  python mh_tex.py sprite_export     estrae gli sprite (.B) in ..\SPRITES (PNG con trasparenza)
  python mh_tex.py sprite_esrgan     upscale x4 degli sprite in ..\SPRITES_4X
  python mh_tex.py ui_hd [modello]   upscale x4 delle immagini dei menu (default realesr-animevideov3) in ..\UI_HD (lette da WINMX.dll)
  python mh_tex.py build [cartella] [cartella_sprite]  ricostruisce l'archivio dai PNG della cartella (default TEXTURES)
                                     - PNG 64x64   -> ..\DATA\DATA.ARC   (originale salvato come DATA.ARC.orig)
                                     - PNG 128x128 -> ..\DATA\HD2X.ARC   (letto dall'EXE texture 2x)
                                     - PNG 256x256 -> ..\DATA\HD4X.ARC   (letto dall'EXE texture 4x)

Note:
 - Il gioco e' a 256 colori per livello: i PNG vengono convertiti ai colori della palette del livello.
 - Alcune texture sono composte da tessere 32x32 o 16x16 (vedi manifest.json): ogni tessera va ingrandita
   per conto suo, l'immagine e' gia' disposta a griglia per questo.
 - Tutti i PNG di una cartella devono avere la stessa dimensione (tutti 64 o tutti 128).
"""
import os, sys, json, struct
import numpy as np
from PIL import Image
from earc import rnc1, entries

HERE = os.environ.get('MH_TOOLS') or os.path.dirname(os.path.abspath(__file__)); GAME = os.environ.get('MH_GAME') or os.path.dirname(HERE)
DATA = os.path.join(GAME, 'DATA')
ARC = os.environ.get('MH_ARC', os.path.join(DATA, 'DATA.ARC')); ORIG = ARC + '.orig'
HD2X = os.path.join(os.path.dirname(ARC), 'HD2X.ARC')
TEX = os.path.join(GAME, 'TEXTURES'); TEX2 = os.path.join(GAME, 'TEXTURES_2X')
LIGHT = 16   # riga della tabella di luce usata come colore "normale"
# palette/luce usate da ogni livello (dalla tabella livelli dell'EXE, 0x48db18)
PALOF = {'BOSS3MAP': 'ASTER1', 'DMMARS1': 'MARS', 'DMMILIT1': 'MILIT3', 'DMBARR1': 'MILIT1', 'DMALIEN1': 'ALIEN1',
         'DMLABS1': 'LABS0', 'DMSWAMP1': 'SWAMP', 'DMSEWER1': 'SEWER1', 'DMASTER1': 'ASTER1'}

def source_arc():
    return ORIG if os.path.exists(ORIG) else ARC

def load_arc(path):
    d = open(path, 'rb').read(); out = {}
    for nm, o, s in entries(d):
        blob = d[o:o+s]; out[nm] = rnc1(blob) if blob[:4] == b'RNC\x01' else blob
    return d, out

def level_colors(files, lvl):
    p = PALOF.get(lvl, lvl)
    pal = np.frombuffer(files[p+'.PLT'], np.uint8).reshape(256, 4)[:, :3]
    B = np.frombuffer(files[p+'.BRT'], np.uint8).reshape(-1, 256)[LIGHT]
    return pal[B]   # colore di ogni indice sorgente (256x3)

def bin_parts(d):
    n = struct.unpack('<I', d[:4])[0]; offs = struct.unpack('<%dI' % n, d[4:4+4*n]); return n, offs, 4+4*n

def tile_size(lum_blk):
    def r(a): return (np.abs(np.diff(a, axis=1)).mean()+np.abs(np.diff(a, axis=2)).mean())/2
    R = {64: r(lum_blk.reshape(1, 64, 64)), 32: r(lum_blk.reshape(4, 32, 32)), 16: r(lum_blk.reshape(16, 16, 16))}
    s = min(R, key=R.get); return s if s == 64 or R[s] <= 0.8*R[64] else 64

def blk_to_img(blk, k, scale=1):
    K = k*scale; n = 64//k; t = blk.reshape(n*n, K, K); img = np.zeros((64*scale, 64*scale), blk.dtype)
    for i in range(n*n): img[(i//n)*K:(i//n+1)*K, (i % n)*K:(i % n+1)*K] = t[i]
    return img

def img_to_blk(img, k, scale):
    K = k*scale; n = 64//k
    return np.concatenate([img[(i//n)*K:(i//n+1)*K, (i % n)*K:(i % n+1)*K].ravel() for i in range(n*n)])

def export():
    _, files = load_arc(source_arc()); man = {}
    for nm in sorted(files):
        if not nm.endswith('.BIN'): continue
        lvl = nm[:-4]; d = files[nm]; n, offs, base = bin_parts(d)
        col = level_colors(files, lvl); lum = col @ [0.3, 0.59, 0.11]
        od = os.path.join(TEX, lvl); os.makedirs(od, exist_ok=True); sizes = []
        for i, o in enumerate(offs):
            blk = np.frombuffer(d[base+o:base+o+4096], np.uint8); k = tile_size(lum[blk]); sizes.append(k)
            Image.fromarray(col[blk_to_img(blk, k)].astype(np.uint8)).save(os.path.join(od, '%03d.png' % i))
        man[lvl] = sizes
    json.dump(man, open(os.path.join(TEX, 'manifest.json'), 'w'), indent=0)
    print('esportate', sum(len(v) for v in man.values()), 'texture in', TEX)

def upscale():
    man = json.load(open(os.path.join(TEX, 'manifest.json')))
    for lvl, sizes in man.items():
        od = os.path.join(TEX2, lvl); os.makedirs(od, exist_ok=True)
        for i, k in enumerate(sizes):
            img = Image.open(os.path.join(TEX, lvl, '%03d.png' % i)).convert('RGB'); out = Image.new('RGB', (128, 128)); n = 64//k
            for t in range(n*n):   # ogni tessera separata, cosi' i bordi non si mescolano
                x, y = (t % n)*k, (t//n)*k
                tile = img.crop((x, y, x+k, y+k))
                big = Image.new('RGB', (3*k, 3*k))   # ripetizione 3x3 per un bordo che si ripete correttamente
                for a in range(3):
                    for c in range(3): big.paste(tile, (a*k, c*k))
                big = big.resize((6*k, 6*k), Image.LANCZOS).crop((2*k, 2*k, 4*k, 4*k))
                out.paste(big, (2*x, 2*y))
            out.save(os.path.join(od, '%03d.png' % i))
    json.dump(man, open(os.path.join(TEX2, 'manifest.json'), 'w'), indent=0)
    print('creato', TEX2)

def build(folder=TEX, sprfolder=None):
    raw, files = load_arc(source_arc())
    man = json.load(open(os.path.join(folder, 'manifest.json'))); scales = set()
    for lvl, sizes in man.items():
        d = files[lvl+'.BIN']; n, offs, base = bin_parts(d)
        col = level_colors(files, lvl).astype(int)
        use = np.bincount(np.frombuffer(d[base:], np.uint8), minlength=256)
        uniq = {}
        for v in np.argsort(-use):   # a parita' di colore, l'indice piu' usato; mai lo 0 (= trasparente, messo sotto dalla maschera)
            if v: uniq.setdefault(tuple(col[v]), v)
        keys = np.array(list(uniq.keys())); vals = np.array(list(uniq.values())); blks = []
        for i, o in enumerate(offs):
            img = np.asarray(Image.open(os.path.join(folder, lvl, '%03d.png' % i)).convert('RGB')).astype(int)
            sc = img.shape[0]//64; scales.add(sc)
            assert img.shape[:2] == (64*sc, 64*sc) and sc in (1, 2, 4), (lvl, i, img.shape)
            orig = np.frombuffer(d[base+o:base+o+4096], np.uint8)
            if sc == 1 and (col[blk_to_img(orig, sizes[i])] == img).all():
                blks.append(orig.tobytes()); continue
            flat = img.reshape(-1, 3); best = np.empty(len(flat), np.int64)
            for s in range(0, len(flat), 4096):
                dd = ((flat[s:s+4096, None, :]-keys[None])**2).sum(-1); best[s:s+4096] = vals[dd.argmin(1)]
            best = best.reshape(img.shape[:2]); best[np.kron(blk_to_img(orig, sizes[i]) == 0, np.ones((sc, sc), bool))] = 0
            blks.append(img_to_blk(best.astype(np.uint8), sizes[i], sc).tobytes())
        bs = len(blks[0])
        files[lvl+'.BIN'] = struct.pack('<I', n)+struct.pack('<%dI' % n, *[i*bs for i in range(n)])+b''.join(blks)
    assert len(scales) == 1, 'tutti i PNG devono avere la stessa dimensione (64, 128 o 256)'
    sc = scales.pop(); newb = {}
    if sprfolder:
        sman = json.load(open(os.path.join(sprfolder, 'manifest.json'))); scol = level_colors(files, SPRPAL)
        for bname in sman:
            fr = []
            for i, (code, orig) in enumerate(b_frames(files[bname+'.B'])):
                rgba = np.asarray(Image.open(os.path.join(sprfolder, bname, '%03d.png' % i)).convert('RGBA'))
                if rgba.shape[0] != orig.shape[0]*sc:
                    rgba = np.asarray(Image.fromarray(rgba).resize((orig.shape[0]*sc,)*2, Image.LANCZOS))
                fr.append((code, sprite_quantize(orig, rgba, scol)))
            newb[bname+'.B'] = b_build(fr)
        print('sprite ricodificati:', len(newb))
    ents = entries(raw); out = bytearray(8+20*len(ents)); out[:8] = raw[:8]; pos = len(out)
    for i, (nm, o, s) in enumerate(ents):
        blob = files[nm] if nm.endswith('.BIN') and nm[:-4] in man else newb.get(nm, raw[o:o+s])
        struct.pack_into('<II', out, 8+20*i, pos, len(blob)); out[16+20*i:28+20*i] = raw[16+20*i:28+20*i]
        out += blob; pos += len(blob)
    if sc == 1:
        if not os.path.exists(ORIG): os.replace(ARC, ORIG)
        dst = ARC
    else:
        dst = os.path.join(os.path.dirname(ARC), ('HD%dS.ARC' if sprfolder else 'HD%dX.ARC') % sc)
    open(dst, 'wb').write(out); print('scritto', dst, '(texture %dx%d)' % (64*sc, 64*sc))
    if sprfolder: player_color(dst)

def fix_holes(arc, out):
    """Texture HD con buchi: l'indice 0 e' il trasparente dei poligoni "a buchi" (B+4 bit0). Il vecchio build metteva il nero
    del PNG sull'indice quasi nero piu' usato (es. 49 = (2,1,1)) e i buchi diventavano neri opachi (satelliti, grate...).
    Qui: 0 esattamente dove era 0 nell'originale (ingrandito), e altrove nessuno 0 (sostituito col colore piu' vicino)."""
    _, orig = load_arc(source_arc()); raw, files = load_arc(arc)
    man = json.load(open(os.path.join(GAME, 'TEXTURES_4X', 'manifest.json'))); nfix = 0
    for lvl, sizes in man.items():
        d = orig[lvl+'.BIN']; n, offs, base = bin_parts(d)
        h = bytearray(files[lvl+'.BIN']); hn, hoffs, hbase = bin_parts(h); bs = len(h[hbase:]) // hn; sc = int(round((bs // 4096) ** 0.5))
        col = level_colors(orig, lvl).astype(int); use = np.bincount(np.frombuffer(d[base:], np.uint8), minlength=256)
        dist = ((col - col[0])**2).sum(1); dist[0] = 1 << 30; sub = min(range(1, 256), key=lambda v: (dist[v], -use[v]))
        for i, o in enumerate(offs):
            b = np.frombuffer(d[base+o:base+o+4096], np.uint8)
            hb = np.frombuffer(bytes(h[hbase+hoffs[i]:hbase+hoffs[i]+bs]), np.uint8)
            img = blk_to_img(hb, sizes[i], sc).copy(); m = np.kron(blk_to_img(b, sizes[i]) == 0, np.ones((sc, sc), bool))
            img[(img == 0) & ~m] = sub; img[m] = 0
            nb = img_to_blk(img, sizes[i], sc)
            if (nb != hb).any(): nfix += 1; h[hbase+hoffs[i]:hbase+hoffs[i]+bs] = nb.tobytes()
        files[lvl+'.BIN'] = bytes(h)
    ents = entries(raw); outb = bytearray(8+20*len(ents)); outb[:8] = raw[:8]; pos = len(outb)
    for i, (nm, o, s) in enumerate(ents):
        blob = files[nm] if nm.endswith('.BIN') and nm[:-4] in man else raw[o:o+s]
        struct.pack_into('<II', outb, 8+20*i, pos, len(blob)); outb[16+20*i:28+20*i] = raw[16+20*i:28+20*i]
        outb += blob; pos += len(blob)
    open(out, 'wb').write(outb); print('texture corrette:', nfix, '->', out)

def esrgan(scale=4, model='realesrgan-x4plus'):
    """Upscale con Real-ESRGAN (GPU). Ogni tessera viene ripetuta 3x3 per bordi che si ripetono senza giunture."""
    import subprocess, tempfile, shutil
    exe = os.path.join(HERE, 'realesrgan', 'realesrgan-ncnn-vulkan.exe')
    man = json.load(open(os.path.join(TEX, 'manifest.json')))
    outdir = os.path.join(GAME, 'TEXTURES_%dX' % scale)
    tmp = tempfile.mkdtemp(prefix='mh_esr_'); tin = os.path.join(tmp, 'in'); tout = os.path.join(tmp, 'out')
    os.makedirs(tin); os.makedirs(tout)
    for lvl, sizes in man.items():
        for i, k in enumerate(sizes):
            img = Image.open(os.path.join(TEX, lvl, '%03d.png' % i)).convert('RGB'); n = 64//k
            for t in range(n*n):
                x, y = (t % n)*k, (t//n)*k; tile = img.crop((x, y, x+k, y+k)); big = Image.new('RGB', (3*k, 3*k))
                for a in range(3):
                    for c in range(3): big.paste(tile, (a*k, c*k))
                big.save(os.path.join(tin, '%s_%03d_%02d.png' % (lvl, i, t)))
    print('upscale di', len(os.listdir(tin)), 'tessere con', model, '...')
    subprocess.run([exe, '-i', tin, '-o', tout, '-n', model, '-f', 'png'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
    for lvl, sizes in man.items():
        od = os.path.join(outdir, lvl); os.makedirs(od, exist_ok=True)
        for i, k in enumerate(sizes):
            n = 64//k; K = k*scale; out = Image.new('RGB', (64*scale, 64*scale))
            for t in range(n*n):
                up = Image.open(os.path.join(tout, '%s_%03d_%02d.png' % (lvl, i, t))).convert('RGB')
                c = up.crop((4*k, 4*k, 8*k, 8*k))           # tessera centrale (Real-ESRGAN e' sempre x4)
                if scale != 4: c = c.resize((K, K), Image.LANCZOS)
                out.paste(c, ((t % n)*K, (t//n)*K))
            out.save(os.path.join(od, '%03d.png' % i))
    json.dump(man, open(os.path.join(outdir, 'manifest.json'), 'w'), indent=0)
    shutil.rmtree(tmp, ignore_errors=True); print('creato', outdir)

def _detail(a):
    """energia dei dettagli fini (immagine meno la sua versione sfocata)"""
    from PIL import ImageFilter
    im = Image.fromarray(a.astype(np.uint8)); b = np.asarray(im.filter(ImageFilter.GaussianBlur(1.5)), np.float32)
    return float(np.abs(a.astype(np.float32) - b).mean())

def esrgan_fix(scale=4, model='realesrgan-x4plus', soglia=0.55, dry=False, rifai=False):
    """Texture scure / a basso contrasto che Real-ESRGAN ha appiattito (scambiando i dettagli per rumore):
    le rifa' aumentando il contrasto prima dell'ingrandimento e riportandolo com'era dopo."""
    import subprocess, tempfile, shutil
    exe = os.path.join(HERE, 'realesrgan', 'realesrgan-ncnn-vulkan.exe')
    man = json.load(open(os.path.join(TEX, 'manifest.json'))); outdir = os.path.join(GAME, 'TEXTURES_%dX' % scale)
    tmp = tempfile.mkdtemp(prefix='mh_fix_'); tin = os.path.join(tmp, 'in'); tout = os.path.join(tmp, 'out'); os.makedirs(tin); os.makedirs(tout)
    todo = []
    for lvl, sizes in man.items():
        for i, k in enumerate(sizes):
            o = np.asarray(Image.open(os.path.join(TEX, lvl, '%03d.png' % i)).convert('RGB'), np.float32)
            h = np.asarray(Image.open(os.path.join(outdir, lvl, '%03d.png' % i)).convert('RGB').resize((64, 64), Image.BOX), np.float32)
            do, dh = _detail(o), _detail(h)
            if (rifai and os.path.exists(os.path.join(outdir, '_appiattite', lvl, '%03d.png' % i))) or (not rifai and do > 1.5 and dh < soglia * do):
                todo.append((lvl, i, k)); n = 64//k
                L = o.mean(-1); m = o.reshape(-1, 3).mean(0); g = float(np.clip(48.0 / max(L.std(), 1.0), 1.0, 6.0))
                g = min(g, 120.0 / max(float(np.percentile(np.abs(o - m), 99.5)), 1.0)); g = max(g, 1.0)   # niente luci tagliate
                st = np.clip((o - m) * g + 128, 0, 255).astype(np.uint8)
                json.dump([m.tolist(), g], open(os.path.join(tin, '%s_%03d.json' % (lvl, i)), 'w'))
                for t in range(n*n):
                    x, y = (t % n)*k, (t//n)*k; tile = Image.fromarray(st).crop((x, y, x+k, y+k)); big = Image.new('RGB', (3*k, 3*k))
                    for a in range(3):
                        for c in range(3): big.paste(tile, (a*k, c*k))
                    big.save(os.path.join(tin, '%s_%03d_%02d.png' % (lvl, i, t)))
    print('texture appiattite da rifare:', len(todo)); print(' '.join('%s/%03d' % (l, i) for l, i, k in todo))
    if dry or not todo: shutil.rmtree(tmp, ignore_errors=True); return todo
    for f in os.listdir(tin):
        if f.endswith('.json'): os.replace(os.path.join(tin, f), os.path.join(tmp, f))
    subprocess.run([exe, '-i', tin, '-o', tout, '-n', model, '-f', 'png'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
    for lvl, i, k in todo:
        m, g = json.load(open(os.path.join(tmp, '%s_%03d.json' % (lvl, i)))); m = np.array(m, np.float32)
        n = 64//k; K = k*scale; out = Image.new('RGB', (64*scale, 64*scale))
        for t in range(n*n):
            up = Image.open(os.path.join(tout, '%s_%03d_%02d.png' % (lvl, i, t))).convert('RGB').crop((4*k, 4*k, 8*k, 8*k))
            if scale != 4: up = up.resize((K, K), Image.LANCZOS)
            out.paste(up, ((t % n)*K, (t//n)*K))
        a = (np.asarray(out, np.float32) - 128) / g + m
        o = np.asarray(Image.open(os.path.join(TEX, lvl, '%03d.png' % i)).convert('RGB'), np.float32)
        for c in range(3):   # stesso contrasto dell'originale
            sa, so = a[..., c].std(), o[..., c].std()
            if sa > 0.5: a[..., c] = (a[..., c] - a[..., c].mean()) * (so / sa) + o[..., c].mean()
        p = os.path.join(outdir, lvl, '%03d.png' % i); bak = os.path.join(outdir, '_appiattite', lvl)
        os.makedirs(bak, exist_ok=True)
        if not os.path.exists(os.path.join(bak, '%03d.png' % i)): shutil.copyfile(p, os.path.join(bak, '%03d.png' % i))
        Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8)).save(p)
    shutil.rmtree(tmp, ignore_errors=True); print('rifatte', len(todo), 'texture in', outdir, '(versioni precedenti in _appiattite)')
    return todo

# ---------------------------------------------------------------- sprite (.B)
SPR = os.path.join(GAME, 'SPRITES'); SPRPAL = 'ASTER1'   # livello usato per i colori di anteprima degli sprite

def b_frames(d):
    n = struct.unpack('<I', d[:4])[0]; offs = list(struct.unpack('<%dI' % n, d[4:4+4*n])) + [len(d)]
    out = []
    for i in range(n):
        code = struct.unpack('<I', d[offs[i]:offs[i]+4])[0]; fr = d[offs[i]+4:offs[i+1]]; k = 64//code
        size = struct.unpack('<i', fr[:4])[0]
        if size == -1:
            px = fr[4:4+k*k]
        else:
            px = bytearray(); p = 4
            while len(px) < size:
                c = struct.unpack('b', fr[p:p+1])[0]; p += 1
                if c < 0: px += fr[p:p-c]; p += -c
                else: px += bytes([fr[p]])*(c+2); p += 1
            px = bytes(px[:size])
        side = int(round(len(px)**0.5))
        out.append((code, np.frombuffer(px, np.uint8).reshape(side, side)))
    return out

def rle(px):
    px = bytes(px); out = bytearray(); i = 0; n = len(px); lit = bytearray()
    def flush():
        nonlocal lit
        while lit:
            ch = lit[:128]; out.append((256-len(ch)) & 0xff); out.extend(ch); lit = lit[128:]
    while i < n:
        j = i
        while j < n and px[j] == px[i] and j-i < 129: j += 1
        if j-i >= 3:
            flush(); out.append(j-i-2); out.append(px[i]); i = j
        else:
            lit.append(px[i]); i += 1
    flush()
    return bytes(out)

def b_build(frames):
    n = len(frames); body = []; pos = 4+4*n; offs = []
    for code, img in frames:
        fr = struct.pack('<I', code)+struct.pack('<i', img.size)+rle(img.ravel())
        offs.append(pos); pos += len(fr); body.append(fr)
    return struct.pack('<I', n)+struct.pack('<%dI' % n, *offs)+b''.join(body)

def sprite_export():
    _, files = load_arc(source_arc()); col = level_colors(files, SPRPAL); man = {}
    for nm in sorted(files):
        if not nm.endswith('.B'): continue
        od = os.path.join(SPR, nm[:-2]); os.makedirs(od, exist_ok=True); codes = []
        for i, (code, img) in enumerate(b_frames(files[nm])):
            rgba = np.zeros(img.shape+(4,), np.uint8); rgba[..., :3] = col[img]; rgba[..., 3] = (img != 0)*255
            Image.fromarray(rgba, 'RGBA').save(os.path.join(od, '%03d.png' % i)); codes.append(code)
        man[nm[:-2]] = codes
    json.dump(man, open(os.path.join(SPR, 'manifest.json'), 'w'), indent=0)
    print('esportati', sum(len(v) for v in man.values()), 'fotogrammi sprite in', SPR)

def _dilate(rgb, a):
    rgb = rgb.copy(); m = a > 0
    if not m.any(): return rgb
    for _ in range(64):
        if m.all(): break
        acc = np.zeros(rgb.shape, float); cnt = np.zeros(m.shape)
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            mm = np.roll(m, (dy, dx), (0, 1)); acc += np.roll(rgb, (dy, dx), (0, 1))*mm[..., None]; cnt += mm
        new = (~m) & (cnt > 0); rgb[new] = (acc[new]/cnt[new][:, None]).astype(np.uint8); m = m | new
    return rgb

def sprite_esrgan():
    import subprocess, tempfile, shutil
    exe = os.path.join(HERE, 'realesrgan', 'realesrgan-ncnn-vulkan.exe'); man = json.load(open(os.path.join(SPR, 'manifest.json')))
    outdir = os.path.join(GAME, 'SPRITES_4X'); tmp = tempfile.mkdtemp(prefix='mh_spr_')
    tin = os.path.join(tmp, 'in'); tout = os.path.join(tmp, 'out'); os.makedirs(tin); os.makedirs(tout); P = 8
    for b, codes in man.items():
        for i in range(len(codes)):
            im = np.asarray(Image.open(os.path.join(SPR, b, '%03d.png' % i)).convert('RGBA'))
            rgb = _dilate(im[..., :3], im[..., 3]); rgb = np.pad(rgb, ((P, P), (P, P), (0, 0)), mode='edge')
            Image.fromarray(rgb).save(os.path.join(tin, '%s__%03d.png' % (b, i)))
    print('upscale di', len(os.listdir(tin)), 'fotogrammi sprite ...')
    subprocess.run([exe, '-i', tin, '-o', tout, '-n', 'realesrgan-x4plus', '-f', 'png'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
    for b, codes in man.items():
        od = os.path.join(outdir, b); os.makedirs(od, exist_ok=True)
        for i in range(len(codes)):
            src = Image.open(os.path.join(SPR, b, '%03d.png' % i)).convert('RGBA'); k = src.size[0]
            up = Image.open(os.path.join(tout, '%s__%03d.png' % (b, i))).convert('RGB').crop((4*P, 4*P, 4*P+4*k, 4*P+4*k))
            a = src.getchannel('A').resize((4*k, 4*k), Image.BICUBIC)
            out = up.convert('RGBA'); out.putalpha(a); out.save(os.path.join(od, '%03d.png' % i))
    json.dump(man, open(os.path.join(outdir, 'manifest.json'), 'w'), indent=0)
    shutil.rmtree(tmp, ignore_errors=True); print('creato', outdir)

def sprite_quantize(orig, rgba, col):
    """orig: indici originali k x k; rgba: immagine ingrandita S*k.
    Ogni pixel sceglie tra gli indici originali vicini (3x3), cosi' i colori restano corretti in tutti i livelli."""
    k = orig.shape[0]; S = rgba.shape[0]//k; N = S*k
    yy, xx = np.mgrid[0:N, 0:N]; sy = yy//S; sx = xx//S
    cands = []
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1): cands.append(orig[np.clip(sy+dy, 0, k-1), np.clip(sx+dx, 0, k-1)])
    cands = np.stack(cands, -1).astype(np.int64)
    cc = col[cands].astype(float); rgb = rgba[..., :3].astype(float)[..., None, :]
    dist = ((cc-rgb)**2).sum(-1); dist[cands == 0] = 1e18
    best = np.take_along_axis(cands, dist.argmin(-1)[..., None], -1)[..., 0]
    nozero = (cands != 0).any(-1); best[~nozero] = orig[sy, sx][~nozero]
    best[rgba[..., 3] < 128] = 0
    return best.astype(np.uint8)

# ---------------------------------------------------------------- immagini dei menu (.BMP) -> UI_HD
def ui_hd(model='realesr-animevideov3'):
    """Ingrandisce x4 con Real-ESRGAN tutte le BMP dei menu/HUD. Per ognuna salva UI_HD\\NOME.raw:
       'MHB1', w, h, w*h indici (come nel file, righe nell'ordine del file), 4w*4h*3 RGB ingrandito (stesso ordine)."""
    import subprocess, tempfile, shutil
    exe = os.path.join(HERE, 'realesrgan', 'realesrgan-ncnn-vulkan.exe')
    _, files = load_arc(source_arc()); out = os.path.join(GAME, 'UI_HD'); os.makedirs(out, exist_ok=True)
    tmp = tempfile.mkdtemp(prefix='mh_ui_'); tin = os.path.join(tmp, 'in'); tout = os.path.join(tmp, 'out'); os.makedirs(tin); os.makedirs(tout)
    P = 8; imgs = {}
    for nm, d in sorted(files.items()):
        if not nm.endswith('.BMP'): continue
        w, h = struct.unpack('<ii', d[18:26]); off = struct.unpack('<I', d[10:14])[0]
        pal = np.frombuffer(d[54:54+1024], np.uint8).reshape(256, 4)[:, [2, 1, 0]]
        stride = (w + 3) & ~3
        idx = np.frombuffer(d[off:off+stride*abs(h)], np.uint8).reshape(abs(h), stride)[:, :w]
        imgs[nm] = (w, abs(h), idx, pal)
        rgb = np.pad(pal[idx], ((P, P), (P, P), (0, 0)), mode='edge')
        im = Image.fromarray(rgb); im = im.resize((im.width*2, im.height*2), Image.NEAREST)   # pixel netti: la rete non 'inventa' le lettere
        im.save(os.path.join(tin, nm[:-4] + '.png'))
    print('upscale di', len(imgs), 'immagini dei menu ...')
    subprocess.run([exe, '-i', tin, '-o', tout, '-n', model, '-s', '4', '-f', 'png'], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=0x08000000)
    for nm, (w, h, idx, pal) in imgs.items():
        big = Image.open(os.path.join(tout, nm[:-4] + '.png')).convert('RGB')
        big = big.resize(((w+2*P)*4, (h+2*P)*4), Image.LANCZOS)          # 2x pixel -> x4 rete -> /2 = x4 finale
        up = np.asarray(big)[4*P:4*P+4*h, 4*P:4*P+4*w]
        with open(os.path.join(out, nm[:-4] + '.raw'), 'wb') as f:
            f.write(b'MHB2' + struct.pack('<II', w, h) + np.ascontiguousarray(idx).tobytes() + np.ascontiguousarray(up).tobytes()
                    + np.ascontiguousarray(pal.astype(np.uint8)).tobytes())   # palette della BMP: la DLL ricolora con quella del gioco
        Image.fromarray(up).save(os.path.join(out, nm[:-4] + '.png'))   # anteprima / da ritoccare
    shutil.rmtree(tmp, ignore_errors=True); print('creato', out)

# ---------------------------------------------------------------- colore del giocatore
# La tuta arancione del giocatore (sprite DROID1, indici 196-202) passa sugli indici 33-38 e 42, inutilizzati ovunque
# (magenta piatto): WINMX.dll da' loro il colore scelto in MHFIX.INI ([giocatore] colore). Il resto del gioco non cambia.
PLAYER_SPR = 'DROID1.B'; PLAYER_FROM = list(range(196, 203)); PLAYER_TO = [33, 34, 35, 36, 37, 38, 42]   # 32 e 39-41 li usano le texture HD

def player_remap_blob(d):
    lut = np.arange(256, dtype=np.uint8); lut[PLAYER_FROM] = PLAYER_TO
    return b_build([(code, lut[img]) for code, img in b_frames(d)])

def player_color(arc):
    """applica lo spostamento degli indici allo sprite del giocatore nell'archivio indicato (copia .prima_colore)"""
    import shutil
    raw = open(arc, 'rb').read(); ents = entries(raw)
    nm_o_s = {nm: (o, s) for nm, o, s in ents}; o, s = nm_o_s[PLAYER_SPR]; blob = raw[o:o+s]
    if blob[:4] == b'RNC': blob = rnc1(blob)
    if not any(np.isin(img, PLAYER_FROM).any() for _, img in b_frames(blob)): print('gia fatto'); return
    if not os.path.exists(arc + '.prima_colore'): shutil.copyfile(arc, arc + '.prima_colore')
    new = player_remap_blob(blob)
    out = bytearray(8+20*len(ents)); out[:8] = raw[:8]; pos = len(out)
    for i, (nm, o, s) in enumerate(ents):
        b = new if nm == PLAYER_SPR else raw[o:o+s]
        struct.pack_into('<II', out, 8+20*i, pos, len(b)); out[16+20*i:28+20*i] = raw[16+20*i:28+20*i]
        out += b; pos += len(b)
    open(arc, 'wb').write(out); print('colore giocatore: indici spostati in', arc)

if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'export': export()
    elif cmd == 'upscale': upscale()
    elif cmd == 'esrgan': esrgan(int(sys.argv[2]) if len(sys.argv) > 2 else 4)
    elif cmd == 'esrgan_fix': esrgan_fix(dry='--prova' in sys.argv, rifai='--rifai' in sys.argv)
    elif cmd == 'build': build(os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else TEX,
                               os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None)
    elif cmd == 'fix_holes': fix_holes(os.path.abspath(sys.argv[2]), os.path.abspath(sys.argv[3]))
    elif cmd == 'sprite_export': sprite_export()
    elif cmd == 'sprite_esrgan': sprite_esrgan()
    elif cmd == 'ui_hd': ui_hd(*sys.argv[2:3])
    elif cmd == 'player_color': player_color(os.path.abspath(sys.argv[2]) if len(sys.argv) > 2 else os.path.join(GAME, 'DATA', 'HD4S.ARC'))
    else: print(__doc__)
