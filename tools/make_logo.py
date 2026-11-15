r"""Ricrea da zero il logo del menu (MAINLOGO, 256x68) a risoluzione 4x, ispirandosi all'originale.
   python make_logo.py   -> aggiorna UI_HD\MAINLOGO.raw (+ anteprima MAINLOGO.png)
La DLL riconosce il logo originale nel gioco (indici) e mostra al suo posto questa versione.
"""
import os, struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from scipy import ndimage
import mh_tex

S = 4; W0, H0 = 256, 68; W, H = W0*S, H0*S
FONTS = r'C:\Windows\Fonts'
rng = np.random.default_rng(7)

def noise(scale, amp=1.0):
    n = ndimage.gaussian_filter(rng.standard_normal((H, W)), scale)
    return amp * n / (np.abs(n).max() + 1e-9)

def text_mask(txt, font, size, box, spacing=0, stretch=True, stroke=0):
    """maschera (0..1) del testo, adattata al riquadro box=(x0,y0,x1,y1) in pixel 4x"""
    f = ImageFont.truetype(os.path.join(FONTS, font), size)
    if spacing:
        widths = [f.getbbox(c)[2] - f.getbbox(c)[0] for c in txt]
        tw = sum(widths) + spacing*(len(txt)-1); th = size*2
        im = Image.new('L', (tw + 4*stroke + 20, th), 0); d = ImageDraw.Draw(im); x = 10 + 2*stroke
        for c, wc in zip(txt, widths):
            d.text((x - f.getbbox(c)[0], size//3), c, font=f, fill=255, stroke_width=stroke, stroke_fill=255); x += wc + spacing
    else:
        tb = f.getbbox(txt, stroke_width=stroke); im = Image.new('L', (tb[2]-tb[0]+20, tb[3]-tb[1]+20), 0)
        ImageDraw.Draw(im).text((10-tb[0], 10-tb[1]), txt, font=f, fill=255, stroke_width=stroke, stroke_fill=255)
    bb = im.getbbox(); im = im.crop(bb)
    x0, y0, x1, y1 = box
    im = im.resize((x1-x0, y1-y0), Image.LANCZOS) if stretch else im
    out = np.zeros((H, W)); a = np.asarray(im, float)/255; out[y0:y0+a.shape[0], x0:x0+a.shape[1]] = a
    return out

def main():
    _, files = mh_tex.load_arc(mh_tex.source_arc())
    d = files['MAINLOGO.BMP']
    pal = np.frombuffer(d[54:1078], np.uint8).reshape(256, 4)[:, [2, 1, 0]].astype(float)
    idx = np.frombuffer(d[1078:1078+W0*H0], np.uint8).reshape(H0, W0)
    orig = pal[idx]

    # ---- targa: sagoma liscia dall'originale
    m = np.kron((idx != 0).astype(float), np.ones((S, S)))
    plate = (ndimage.gaussian_filter(m, 2.2) > 0.5).astype(float)
    dist = ndimage.distance_transform_edt(np.pad(plate, 1))[1:-1, 1:-1]     # anche i limiti dell'immagine sono bordo
    yy, xx = np.mgrid[0:H, 0:W]
    Lv0 = np.array([-0.55, -0.6, 0.58]); Lv0 /= np.linalg.norm(Lv0)
    def lit0(height, k=1.0):
        gy, gx = np.gradient(height)
        nrm = np.stack([-gx*k, -gy*k, np.ones_like(gx)], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        return np.clip((nrm*Lv0).sum(-1), 0, 1) / Lv0[2]
    # metallo argentato a chiazze, con puntini e graffi chiari
    lum = 0.64 + 0.16*noise(30) + 0.10*noise(8) + 0.05*noise(2)
    blot = np.clip(noise(12) - 0.2, 0, None) * 2.0
    spk = ndimage.gaussian_filter((rng.random((H, W)) > 0.99935).astype(float), 2.2) * 14
    base = np.stack([lum*122 + blot*95, lum*118 + blot*95, lum*114 + blot*95], -1) + (np.clip(spk, 0, 1)*90)[..., None]
    # targa con bordo in rilievo: bisello esterno + scanalatura interna
    BAND = 19.0                                                              # fascia esterna (~4.8 px originali): qui stanno i rivetti
    band = ndimage.gaussian_filter((dist < BAND).astype(float), 1.0)
    hp = np.clip(dist/6.0, 0, 1)*2.4 + band*1.4 + 0.25*noise(2)           # bisello esterno + gradino verso l'interno
    sh = lit0(ndimage.gaussian_filter(hp, 1.0), 2.2)
    base *= (0.6 + 0.4*sh)[..., None]
    base *= (1 - 0.45*band)[..., None]                                       # fascia in rilievo scura
    base *= (1 - 0.7*np.clip(1 - np.abs(dist - 1.5)/1.5, 0, 1))[..., None]  # filo scuro sul bordo esterno

    # rivetti in file regolari (posizioni misurate sull'originale, coordinate 256x68)
    # (riga, colonna, direzione in cui cercare il centro della fascia: 'v' verticale, 'h' orizzontale)
    riv_pts = [(4.8, x, 'v') for x in np.arange(21.4 - 1.3, 89.0 - 1.3, 7.45)] + [(4.8, x, 'v') for x in np.arange(127.5, 151.0, 7.67)] + \
              [(4.8, x, 'v') for x in np.arange(188.2 + 1.3, 232.5 + 1.3, 7.23)] + [(61.9, x, 'v') for x in np.arange(62.7, 138.0, 7.5)] + \
              [(y, 4.6, 'h') for y in (41.0, 45.3, 49.6, 53.9)] + [(y, 248.2, 'h') for y in (6.0, 10.3, 14.6)]
    rh = np.zeros((H, W))
    for (ry, rx, dirn) in riv_pts:
        cy, cx = (ry+0.5)*S, (rx+0.5)*S
        if plate[int(min(cy, H-1)), int(min(cx, W-1))] < 0.5: continue
        # sposta il rivetto sulla linea centrale della fascia scura (dist = BAND/2) lungo la direzione indicata
        iy, ix = int(min(cy, H-1)), int(min(cx, W-1))
        if dirn == 'v':
            ys = np.arange(max(0, iy-30), min(H, iy+31)); cand = ys[np.argmin(np.abs(dist[ys, ix] - BAND/2) + 0.02*np.abs(ys - iy))]; cy = float(cand)
        else:
            xs = np.arange(max(0, ix-30), min(W, ix+31)); cand = xs[np.argmin(np.abs(dist[iy, xs] - BAND/2) + 0.02*np.abs(xs - ix))]; cx = float(cand)
        r = np.hypot(yy-cy, xx-cx)
        rh += np.clip(1 - (r/4.6)**2, 0, None)**0.5 * 3.6 - 1.0*np.exp(-((r - 5.5)/0.9)**2)
    rmask = np.clip(rh*3, 0, 1)
    rsh = lit0(rh, 1.6)
    hl = np.clip(rsh - 1.0, 0, 1)**1.5
    rcol = np.stack([20 + 25*rsh + 170*hl, 19 + 23*rsh + 160*hl, 18 + 21*rsh + 150*hl], -1)
    base = base*(1 - rmask[..., None]) + rcol*rmask[..., None]
    base *= (1 - 0.45*np.clip(-rh, 0, 1))[..., None]
    # ---- illuminazione (luce dall'alto a sinistra): rilievo -> normali -> diffusa + speculare
    Lv = np.array([-0.55, -0.6, 0.58]); Lv /= np.linalg.norm(Lv)
    Hv = Lv + np.array([0, 0, 1.0]); Hv /= np.linalg.norm(Hv)
    def lit(height, k=1.0, shin=40):
        gy, gx = np.gradient(height)
        nrm = np.stack([-gx*k, -gy*k, np.ones_like(gx)], -1); nrm /= np.linalg.norm(nrm, axis=-1, keepdims=True)
        return np.clip((nrm*Lv).sum(-1), 0, 1) / Lv[2], np.clip((nrm*Hv).sum(-1), 0, 1)**shin
    def gold(top, bot, y0, y1):
        t = np.clip((yy - y0)/max(1, (y1-y0)), 0, 1)[..., None]
        return (np.array(top)*(1-t) + np.array(bot)*t) * (1 + 0.08*noise(2.5)[..., None])
    # ammaccature e grana del ferro
    dent = np.zeros((H, W))
    for _ in range(170):
        cy, cx = rng.uniform(0, H), rng.uniform(0, W); r = rng.uniform(3, 15)
        dent -= np.exp(-((yy-cy)**2 + (xx-cx)**2)/(2*r*r)) * rng.uniform(0.5, 1.5)
    grain = noise(1.0)*0.5 + noise(2.5)*0.8 + noise(9)*1.5

    # ---- HUNTER: lettere di ferro sporco in rilievo, filo dorato smussato, bordo scuro e ombra netta
    hb = (12*S, 21*S, 239*S, 56*S)
    m = text_mask('HUNTER', 'timesbd.ttf', 260, (hb[0]+8, hb[1]+8, hb[2]-8, hb[3]-8))
    inside = m >= 0.5
    din = ndimage.distance_transform_edt(inside); dout = ndimage.distance_transform_edt(~inside)
    RIM = 4.5                                                            # filo dorato (dentro la lettera)
    shadow = ndimage.shift(ndimage.gaussian_filter(np.clip(3.5 - dout, 0, 1), 3), (11, 9))
    base *= (1 - 0.78*np.clip(shadow, 0, 1))[..., None]
    edge = np.clip(3.5 - dout, 0, 1)                                     # bordo scuro esterno
    base *= (1 - 0.85*edge)[..., None]
    hgt = np.where(inside, 4 + np.minimum(din, 10)*0.8, 0.0)
    hgt = ndimage.gaussian_filter(hgt, 1.2) + (dent*1.6 + grain*0.9)*inside
    dif, spe = lit(hgt, 1.5, 50)
    rim = np.clip(RIM + 1 - din, 0, 1) * inside
    goldc = gold((255, 215, 140), (165, 108, 52), hb[1], hb[3]) * (0.35 + 0.85*dif)[..., None] + (255*0.8*spe)[..., None]
    ironl = 0.55 + 0.25*noise(20) + 0.25*np.clip(noise(7) - 0.2, 0, None)*2
    iron = np.stack([96*ironl, 93*ironl, 90*ironl], -1) * (0.22 + 0.95*dif)[..., None] + (230*0.55*spe)[..., None]
    a_in = ndimage.gaussian_filter(inside.astype(float), 0.7)
    letter = iron*(1 - rim[..., None]) + goldc*rim[..., None]
    base = base*(1 - a_in[..., None]) + letter*a_in[..., None]

    # ---- -MACHINE-: lettere dorate a blocco, smussate, con ombra netta in basso a destra
    mb = (17*S, 10*S, 237*S, 19*S)
    mm = text_mask('-MACHINE-', 'ariblk.ttf', 200, mb, spacing=95) >= 0.5
    mdin = ndimage.distance_transform_edt(mm); mdout = ndimage.distance_transform_edt(~mm)
    msh = ndimage.shift(ndimage.gaussian_filter(mm.astype(float), 1.5), (8, 7))
    base *= (1 - 0.85*np.clip(msh, 0, 1))[..., None]
    base *= (1 - 0.8*np.clip(2.5 - mdout, 0, 1))[..., None]
    mh = ndimage.gaussian_filter(np.where(mm, np.minimum(mdin, 5), 0.0), 1.0) + grain*0.25*mm
    mdif, mspe = lit(mh, 1.6, 40)
    mcol = gold((255, 214, 135), (196, 132, 62), mb[1], mb[3]) * (0.45 + 0.75*mdif)[..., None] + (255*0.6*mspe)[..., None]
    am = ndimage.gaussian_filter(mm.astype(float), 0.7)
    base = base*(1 - am[..., None]) + mcol*am[..., None]
    tm = text_mask('TM', 'arialbd.ttf', 60, (238*S, 45*S, 246*S, 49*S))
    base = base*(1-tm[..., None]) + np.array([200, 150, 90])*tm[..., None]

    img = np.clip(base, 0, 255) * plate[..., None]
    img = img.astype(np.uint8)
    out = os.path.join(mh_tex.GAME, 'UI_HD')
    with open(os.path.join(out, 'MAINLOGO.raw'), 'wb') as f:
        f.write(b'MHB2' + struct.pack('<II', W0, H0) + np.ascontiguousarray(idx).tobytes() + np.ascontiguousarray(img).tobytes()
                + np.ascontiguousarray(pal.astype(np.uint8)).tobytes())
    Image.fromarray(img).save(os.path.join(out, 'MAINLOGO.png'))
    print('logo ricreato:', os.path.join(out, 'MAINLOGO.png'))

if __name__ == '__main__':
    main()
