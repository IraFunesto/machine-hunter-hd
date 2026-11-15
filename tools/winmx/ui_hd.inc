/* Menu HD: riconosce le immagini BMP originali nel livello 2D e le sostituisce con le versioni Real-ESRGAN (UI_HD\*.raw). */
typedef struct { int w, h; BYTE *idx[2]; BYTE *rgb[2]; int smp[2][12]; int nsmp; int nz; char name[16]; BYTE pal[768]; int haspal; } HdImg;
static HdImg hdi[64]; static int n_hdi, hd_loaded; static int sm_on;
static BYTE *hd_tmap; static DWORD *hd_comp, *hd_ref; static int hd_vw, hd_vh;
typedef struct { int k, o, x, y; } HdMatch;
static HdMatch hd_m[32], hd_prev[32]; static int n_m, n_prev;

static void hd_load(void)
{
    hd_loaded = 1; char pat[MAX_PATH]; path_in_dir(pat, "UI_HD\\*.raw");
    WIN32_FIND_DATAA fd; HANDLE fh = FindFirstFileA(pat, &fd); if (fh == INVALID_HANDLE_VALUE) return;
    do {
        if (n_hdi >= 64) break;
        char rel[MAX_PATH], path[MAX_PATH]; snprintf(rel, sizeof rel, "UI_HD\\%s", fd.cFileName); path_in_dir(path, rel);
        FILE *f = fopen(path, "rb"); if (!f) continue;
        char mg[4]; DWORD wh[2];
        if (fread(mg, 1, 4, f) == 4 && (!memcmp(mg, "MHB1", 4) || !memcmp(mg, "MHB2", 4) || !memcmp(mg, "MHB3", 4)) && fread(wh, 4, 2, f) == 2 && wh[0] && wh[1] && wh[0] <= 1024 && wh[1] <= 480) {
            HdImg *m = &hdi[n_hdi]; int w = wh[0], h = wh[1]; m->w = w; m->h = h;
            { int i = 0; for (; i < 15 && fd.cFileName[i] && fd.cFileName[i] != '.'; i++) m->name[i] = fd.cFileName[i]; m->name[i] = 0; }
            int bpp = mg[3] == '3' ? 4 : 3; size_t n16 = (size_t)w*h*16;
            m->idx[0] = malloc(w*h); m->rgb[0] = malloc(n16*4);
            m->idx[1] = malloc(w*h); m->rgb[1] = NULL;   /* copia capovolta non piu usata */
            if (fread(m->idx[0], 1, w*h, f) == (size_t)w*h && fread(m->rgb[0], 1, n16*bpp, f) == n16*bpp) {
                if (bpp == 3) for (size_t i = n16; i-- > 0; ) { BYTE *d = m->rgb[0] + i*4, *sp = m->rgb[0] + i*3; BYTE c0 = sp[0], c1 = sp[1], c2 = sp[2]; d[0] = c0; d[1] = c1; d[2] = c2; d[3] = 255; }
                m->haspal = mg[3] >= '2' && fread(m->pal, 1, 768, f) == 768;
                for (int y = 0; y < h; y++) memcpy(m->idx[1] + y*w, m->idx[0] + (h-1-y)*w, w);
                if (m->rgb[1]) for (int y = 0; y < 4*h; y++) memcpy(m->rgb[1] + (size_t)y*w*16, m->rgb[0] + (size_t)(4*h-1-y)*w*16, (size_t)w*16);
                m->nz = 0; for (int i = 0; i < w*h; i++) m->nz += m->idx[0][i] != 0;
                for (int o = 0; o < 2; o++) {            /* 12 punti di controllo non trasparenti, sparsi */
                    int n = 0;
                    for (int gy = 0; gy < 3 && n < 12; gy++) for (int gx = 0; gx < 4 && n < 12; gx++) {
                        int y0 = (gy*2+1)*h/6, x0 = (gx*2+1)*w/8;
                        for (int t = 0; t < w*h; t++) { int yy = (y0 + t/w) % h, xx = (x0 + t%w) % w; if (m->idx[o][yy*w+xx]) { m->smp[o][n++] = yy*w+xx; break; } }
                    }
                    m->nsmp = n;
                }
                n_hdi++;
            }
        }
        fclose(f);
    } while (FindNextFileA(fh, &fd));
    FindClose(fh);
    mhlog("menu HD: %d immagini caricate", n_hdi);
}

/* cerca le immagini nel livello 2D L (VW x VH); ritorna 1 se le corrispondenze sono cambiate */
static int hd_match(const BYTE *L, int VW, int VH)
{
    n_m = 0;
    for (int k = 0; k < n_hdi && n_m < 32; k++) {
        HdImg *m = &hdi[k]; if (m->w > VW || m->h > VH) continue;
        for (int o = 0; o < 2 && n_m < 32; o++) {
            const BYTE *T = m->idx[o]; int found = 0;
            for (int y = 0; y + m->h <= VH && !found; y++) for (int x = 0; x + m->w <= VW && !found; x++) {
                const BYTE *base = L + (size_t)y*VW + x; int ok = 1;
                int miss = 0;
                for (int s = 0; s < m->nsmp; s++) { int a = m->smp[o][s]; if (base[(a/m->w)*VW + a%m->w] != T[a] && ++miss*2 > m->nsmp) { ok = 0; break; } }
                if (!ok) continue;
                int good = 0;
                for (int j = 0; j < m->h; j++) { const BYTE *r = base + (size_t)j*VW, *t = T + j*m->w; for (int i = 0; i < m->w; i++) if (t[i] && r[i] == t[i]) good++; }
                if (good*10 >= m->nz*2) { hd_m[n_m].k = k; hd_m[n_m].o = o; hd_m[n_m].x = x; hd_m[n_m].y = y; n_m++; found = 1; }
            }
        }
    }
    int changed = n_m != n_prev || memcmp(hd_m, hd_prev, n_m*sizeof(HdMatch));
    if (changed) { memcpy(hd_prev, hd_m, n_m*sizeof(HdMatch)); n_prev = n_m; }
    return changed;
}

static void hd_build(int VW, int VH)
{
    if (hd_vw != VW || hd_vh != VH) { free(hd_tmap); free(hd_comp); free(hd_ref); hd_tmap = malloc(VW*VH); hd_comp = malloc((size_t)VW*VH*64); hd_ref = malloc((size_t)VW*VH*4); hd_vw = VW; hd_vh = VH; }
    memset(hd_tmap, 0, VW*VH); memset(hd_comp, 0, (size_t)VW*VH*64); memset(hd_ref, 0, (size_t)VW*VH*4);
    for (int n = 0; n < n_m; n++) {
        HdImg *m = &hdi[hd_m[n].k]; int o = hd_m[n].o, X = hd_m[n].x, Y = hd_m[n].y;
        for (int j = 0; j < m->h; j++) for (int i = 0; i < m->w; i++) { BYTE e = m->idx[o][j*m->w+i]; if (e) hd_tmap[(Y+j)*VW + X+i] = e; }
        for (int j = 0; j < 4*m->h; j++) {
            if (!m->rgb[o]) continue;
            const BYTE *src = m->rgb[o] + (size_t)j*m->w*16; DWORD *dst = hd_comp + (size_t)(4*Y+j)*VW*4 + 4*X;
            for (int i = 0; i < 4*m->w; i++) dst[i] = 0xFF000000u | src[i*4] | src[i*4+1] << 8 | src[i*4+2] << 16;
        }
    }
}

/* verifica veloce: le immagini riconosciute sono ancora nella stessa posizione? */
static int hd_verify(const BYTE *L, int VW, int VH)
{
    for (int n = 0; n < n_m; n++) {
        HdImg *m = &hdi[hd_m[n].k]; int o = hd_m[n].o; const BYTE *base = L + (size_t)hd_m[n].y*VW + hd_m[n].x;
        int miss = 0;
        for (int s = 0; s < m->nsmp; s++) { int a = m->smp[o][s]; if (base[(a/m->w)*VW + a%m->w] != m->idx[o][a] && ++miss*2 > m->nsmp) return 0; }
    }
    return 1;
}


/* ===================== source-map: il gioco ci dice cosa disegna ===================== */
/* 0x40210d -> 0x437c40(indice archivio, buffer, flag): carica una BMP; 0x4014e2 -> 0x4432d0: blitter universale */
typedef struct { const BYTE *buf; int k; } BufMap;
static BufMap bmap[64]; static int n_bmap;
typedef struct { const BYTE *src; int pitch, sx, sy, w, h, dx, dy, x0, y0, x1, y1; DWORD ra, ra2; } BlitRec;
static BlitRec brec[8192], bprev[8192]; static int n_brec, n_bprev;
typedef int (__cdecl *load_t)(int, BYTE*, int);
typedef int (__cdecl *blit_t)(const BYTE*, int, int, int, int, int, int, int);
static load_t load_orig = (load_t)0x437c40;
static blit_t blit_orig = (blit_t)0x4432d0;

static int hd_find(const char *name)
{
    char nm[16]; int i = 0; for (; i < 15 && name[i] && name[i] != '.'; i++) nm[i] = (char)toupper((unsigned char)name[i]); nm[i] = 0;
    for (int k = 0; k < n_hdi; k++) if (!_stricmp(hdi[k].name, nm)) return k;
    return -1;
}
static int sm_dbg;
static int __cdecl load_hook(int index, BYTE *dst, int flag)
{
    int r = load_orig(index, dst, flag);
    if (!hd_loaded) hd_load();
    const char *name = (const char*)(0x62e2d0 + index*0x18);
    int k = hd_find(name), j;
    if (cfg_shot) { mhlog("carica %.12s -> %p (k %d)", name, dst, k); if (!_strnicmp(name, "MARS", 4)) sm_dbg = 90; }
    for (j = 0; j < n_bmap && bmap[j].buf != dst; j++);
    if (j == n_bmap && n_bmap < 64) n_bmap++;
    if (j < 64) { bmap[j].buf = dst; bmap[j].k = k; }
    return r;
}
static int txt_on; static BYTE sf_bw[256][2];   /* larghezza vera del glifo (font piccolo) per inizio x e riga */
static short ink_tab[2048][4]; static int ink_n;   /* sx, sy, colonne vuote a sinistra (lettere dei testi) */
static int ink_get(int sx, int sy, int h) { for (int i = 0; i < ink_n; i++) if (ink_tab[i][0] == sx && ink_tab[i][1] == sy && ink_tab[i][3] == h) return ink_tab[i][2]; return -1; }
static void ink_put(const BYTE *src, int pitch, int sx, int sy, int w, int h)
{
    if (ink_get(sx, sy, h) >= 0 || ink_n >= 2048 || !src) return;
    int c; for (c = 0; c < w; c++) { int any = 0; for (int y = 0; y < h && !any; y++) if (src[(size_t)(sy + y)*pitch + sx + c]) any = 1; if (any) break; }
    ink_tab[ink_n][0] = (short)sx; ink_tab[ink_n][1] = (short)sy; ink_tab[ink_n][2] = (short)(c < w ? c : 0); ink_tab[ink_n][3] = (short)h; ink_n++;
}
static int __cdecl blit_hook(const BYTE *src, int pitch, int sx, int sy, int w, int h, int dx, int dy)
{
    if (txt_on) ink_put(src, pitch, sx, sy, w, h);
    if (txt_on && h == 8 && w > 1) {   /* font piccolo: disegna solo la lettera, non la colonna di spaziatura */
        int sxx = -1, rs = 0;
        if (pitch == 256 && sy >= 192 && sy < 240) { sxx = sx; rs = ((sy - 192) >> 3) & 1; }
        else if (pitch == 512 && sx >= 256 && sx < 512 && sy < 48) { sxx = sx - 256; rs = (sy >> 3) & 1; }
        if (sxx >= 0 && sf_bw[sxx][rs] && w > sf_bw[sxx][rs]) w = sf_bw[sxx][rs];
    }
    if (txt_on) {   /* testi a coordinate fisse 320x240 (pausa ecc.): centrati nel livello HUD piu' grande */
        int LW = *(int*)0x48cdc8, LH = *(int*)0x48cdcc;
        if (LW == *(int*)0x74b814 && LH == *(int*)0x74b818 && LW > 320) { dx += (LW - 320) / 2; dy += (LH - 240) / 2; }
    }
    int boff = (int)(*(BYTE**)0x67ce38 - G_BITS);   /* menu: buffer spostato per centrare (0x74b828) */
    if (sm_on && n_brec < 8192 && (boff == 0 || boff == *(int*)0x74b828)) {
        BlitRec *r = &brec[n_brec++];
        r->ra = (DWORD)(DWORD_PTR)__builtin_return_address(0); { DWORD *sp = (DWORD*)&src; r->ra2 = 0; for (int q = 8; q < 40; q++) { DWORD v = sp[q]; if (v > 0x401000 && v < 0x480000 && *(BYTE*)(v-5) == 0xE8) { r->ra2 = v; break; } } }
        r->src = src; r->pitch = pitch; r->sx = sx; r->sy = sy; r->w = w; r->h = h; r->dx = dx; r->dy = dy;
        r->x1 = *(DWORD*)0x6a3c10 & 0xffff; r->y1 = *(DWORD*)0x6a3c18 & 0xffff;
        r->x0 = *(DWORD*)0x6a3c14 & 0xffff; r->y0 = *(DWORD*)0x6a3c1c & 0xffff;
        r->dx += boff; r->x0 += boff; r->x1 += boff;
    }
    return blit_orig(src, pitch, sx, sy, w, h, dx, dy);
}
static void patch_tramp(DWORD at, void *to)
{
    DWORD old; BYTE *p = (BYTE*)at; if (p[0] != 0xE9) return;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    *(DWORD*)(p+1) = (DWORD)(DWORD_PTR)to - (at + 5);
    VirtualProtect(p, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), p, 5);
}
/* mappa (Select/TAB) larga col rapporto d'aspetto: il livello 2D e' largo VW e i menu sono spostati di (VW-320)/2;
   cornice (0x422b20, tessere 16x16 con 0x424a60), linee (0x424bb0) e rettangoli (0x424840) erano limitati a 0..319.
   Qui rifatti con i limiti allargati di mw_b pixel per lato (0 se il livello non e' spostato = come l'originale). */
static int mw_on;
static int map_wb(void)
{
    if (!mw_on) return 0;
    int b = (int)(*(BYTE**)0x67ce38 - G_BITS);
    return (b > 0 && b == *(int*)0x74b828) ? b : 0;
}
static void __cdecl mw_tile(int x, int y, int id)
{
    { DWORD ra = (DWORD)(DWORD_PTR)__builtin_return_address(0); if (ra > 0x423a40 && ra < 0x423b60) x -= map_wb(); }
    const int *t = (const int*)(0x48add0 + id * 16); int sx = t[0], sy = t[1], w = t[2], h = t[3];
    int b = map_wb(), P = *(int*)0x48cdc8;
    if (x + w < -b || y + h < 0 || x > 319 + b || y > 239) return;
    if (x < -b) { sx += -b - x; w -= -b - x; x = -b; }
    if (y < 0) { sy -= y; h += y; y = 0; }
    if (x + w > 320 + b) w = 320 + b - x;
    if (y + h > 240) h = 240 - y;
    if (w <= 0 || h <= 0) return;
    BYTE *d = *(BYTE**)0x67ce38 + y * P + x; const BYTE *s = *(BYTE**)0x67ce3c + sy * 512 + sx;
    for (int j = 0; j < h; j++, d += P, s += 512) memcpy(d, s, w);
}
static void __cdecl mw_frame(void)
{
    int b = map_wb();
    {   /* finestra di celle disegnate (0x423851: mov eax, 360 -> 360/zoom celle): allargata come lo schermo, vale dal fotogramma dopo */
        DWORD want = 360 * (320 + 2 * b) / 320, old;
        if (*(BYTE*)0x423851 == 0xB8 && *(DWORD*)0x423852 != want) {
            VirtualProtect((void*)0x423852, 4, PAGE_EXECUTE_READWRITE, &old); *(DWORD*)0x423852 = want; VirtualProtect((void*)0x423852, 4, old, &old);
            FlushInstructionCache(GetCurrentProcess(), (void*)0x423852, 4);
        }
    }
    for (int x = 16 - b; x < 304 + b; x += 16) { mw_tile(x, 0, 5); mw_tile(x, 224, 10); }
    for (int y = 16; y < 220; y += 16) { mw_tile(-b, y, 7); mw_tile(304 + b, y, 8); }
    mw_tile(-b, 0, 4); mw_tile(-b, 224, 9); mw_tile(304 + b, 0, 6); mw_tile(304 + b, 224, 11);
}
static void __cdecl mw_rect(BYTE *buf, int x0, int y0, int x1, int y1, int col)
{
    int b = map_wb(), P = *(int*)0x48cdc8; BYTE c = *(BYTE*)(0x4bd7a0 + (col & 0xff));
    { DWORD ra = (DWORD)(DWORD_PTR)__builtin_return_address(0); if (ra > 0x423a40 && ra < 0x423b60) { x0 -= b; x1 -= b; } }
    if (x0 > x1 || y0 > y1) return;
    if (x0 < 17 - b) x0 = 17 - b; if (x1 > 302 + b) x1 = 302 + b; if (y0 < 17) y0 = 17; if (y1 > 222) y1 = 222;
    if (x0 > x1 || y0 > y1) return;
    for (int y = y0; y <= y1; y++) memset(buf + y * P + x0, c, x1 - x0 + 1);
}
/* mappa HD ([video] mappa_hd=1): il gioco calcola i vertici con 4 volte piu' precisione (patch a 0x4243b6/0x4243c8/0x424438/0x42444a)
   e linee e "tappi" neri (quadrilateri che coprono le linee dietro i muri) vengono registrati qui e disegnati dalla scheda video
   alla risoluzione dello schermo (gl_present.inc), invece che a 240 righe nel livello 2D. */
typedef struct { float v[8]; int kind; BYTE c; } MwCmd;   /* kind 0 linea (v[0..3]), 1 quadrilatero (v[0..7]); coordinate del livello 2D */
static MwCmd mw_cmd[16384]; static volatile int mw_ncmd; static int mw_hd, mw_gl_ok;
static void __cdecl mw_line(BYTE *buf, int x0, int y0, int x1, int y1, int col)
{
    if (mw_hd) {
        int b = map_wb();
        if (mw_gl_ok) {
            if (mw_ncmd < 16384) { MwCmd *m = &mw_cmd[mw_ncmd++]; m->kind = 0; m->c = *(BYTE*)(0x4bd7a0 + (signed char)col);
                m->v[0] = x0 * 0.25f + b; m->v[1] = y0 * 0.25f; m->v[2] = x1 * 0.25f + b; m->v[3] = y1 * 0.25f; }
            return;
        }
        x0 = (x0 + 2) >> 2; y0 = (y0 + 2) >> 2; x1 = (x1 + 2) >> 2; y1 = (y1 + 2) >> 2;   /* senza OpenGL: come prima */
    }
    int b = map_wb(), P = *(int*)0x48cdc8; BYTE c = *(BYTE*)(0x4bd7a0 + (signed char)col);
    int L = 17 - b, R = 302 + b, T = 17, B = 222;
    int dx = x1 - x0, dy = y1 - y0, n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    if ((x0 < L && x1 < L) || (x0 > R && x1 > R) || (y0 < T && y1 < T) || (y0 > B && y1 > B)) return;
    if (n == 0) { if (x0 >= L && x0 <= R && y0 >= T && y0 <= B) buf[y0 * P + x0] = c; return; }
    if (n > 4096) return;
    /* passo a passo (DDA 16.16): basta e avanza per le linee della mappa */
    int fx = x0 * 65536 + 32768, fy = y0 * 65536 + 32768, sx = dx * 65536 / n, sy = dy * 65536 / n;
    for (int i = 0; i <= n; i++, fx += sx, fy += sy) {
        int x = fx >> 16, y = fy >> 16;
        if (x >= L && x <= R && y >= T && y <= B) buf[y * P + x] = c;
    }
}
typedef void (__cdecl *mwq_t)(int, int, int, int, int, int, int, int);
static void __cdecl mw_quad(int a, int b_, int c, int d, int e, int f, int g, int h)
{
    DWORD ra = (DWORD)(DWORD_PTR)__builtin_return_address(0);
    if (mw_hd && ra == 0x424555) {   /* tappo della mappa (coordinate x4) */
        if (mw_gl_ok) {
            if (mw_ncmd < 16384) { int b = map_wb(); MwCmd *m = &mw_cmd[mw_ncmd++]; m->kind = 1; m->c = 0;
                int q[8] = { a, b_, c, d, e, f, g, h }; for (int i = 0; i < 8; i++) m->v[i] = q[i] * 0.25f + ((i & 1) ? 0 : b); }
            return;
        }
        a = (a + 2) >> 2; b_ = (b_ + 2) >> 2; c = (c + 2) >> 2; d = (d + 2) >> 2; e = (e + 2) >> 2; f = (f + 2) >> 2; g = (g + 2) >> 2; h = (h + 2) >> 2;
    }
    ((mwq_t)0x436d00)(a, b_, c, d, e, f, g, h);
}
typedef int (__cdecl *spr_t)(short*, void*, int);
static int spr_log;
static int __cdecl spr_hook(short *pos, void *obj, int c)
{
    if (spr_log > 0) { spr_log--; mhlog("  spr ra %06x pos %d,%d obj %p c %d", (DWORD)(DWORD_PTR)__builtin_return_address(0), pos[0], pos[1], obj, c); }
    return ((spr_t)0x442e30)(pos, obj, c);
}
/* fondali 320x240 copiati dalle caverne dell'EXE (non passano dal blitter): registrati come copia a (0,0) */
static void __cdecl bg_hook(const BYTE *src, BYTE *dst, int opaque)
{
    int boff = (int)(dst - G_BITS);
    if (!sm_on || (boff != 0 && boff != *(int*)0x74b828)) return;
    if (opaque) n_brec = 0;                       /* lo schermo viene ripulito */
    if (n_brec >= 8192) return;
    BlitRec *r = &brec[n_brec++]; memset(r, 0, sizeof *r);
    r->src = src; r->pitch = 320; r->w = 320; r->h = 240; r->dx = boff; r->x0 = boff; r->x1 = 0xffff; r->y1 = 0xffff;
}
/* testo su griglia 6x6 (0x401e3d -> 0x443880): font 7x7 nel pannello con avanzamento fisso 6 nell'originale
   (lettere larghe accavallate, strette staccate). Rifatto con spaziatura proporzionale e passando dal blitter (-> HD). */
static const BYTE *g6_font; static signed char g6_x0[64], g6_w[64]; static int g6_dig;
static void g6_measure(const BYTE *font)
{
    g6_font = font; g6_dig = 0;
    for (int k = 0; k < 64; k++) {
        const BYTE *c = font + 0x80f0 + (k & 31)*8 + (k >= 32 ? 0x1000 : 0); int x0 = 99, x1 = -1;
        for (int y = 0; y < 7; y++) for (int x = 0; x < 7; x++) if (c[y*0x200 + x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
        g6_x0[k] = x1 < 0 ? 0 : x0; g6_w[k] = x1 < 0 ? 0 : x1 - x0 + 1;
        if (k >= 16 && k < 26 && g6_w[k] > g6_dig) g6_dig = g6_w[k];
    }
}
#define HUD_NUM_DOWN 12     /* HUD a sinistra: numero sotto l'icona (era sopra il suo angolo in basso) */
#define HUD_SLOT_EXTRA 17   /* e ogni posizione della colonna scende di tanto, per fare posto al numero */
static void __cdecl g6_hook(int cx, int cy, const char *str)
{
    const BYTE *font = *(const BYTE**)0x67ce3c; if (!font) return;
    if (font != g6_font) g6_measure(font);
    int pen = cx*6, y = cy*6;
    /* numero del piano nella mappa larga: il blitter scarta x negative -> disegno con la base del livello non spostata */
    { DWORD ra = (DWORD)(DWORD_PTR)__builtin_return_address(0);   /* HUD a sinistra: numero degli oggetti sotto la sua icona (come a destra) */
      int k = ra == 0x4444d6 ? 0 : ra == 0x444569 ? 1 : ra == 0x4445db ? 2 : ra == 0x44464d ? 3 : -1;
      if (k >= 0 && *(int*)0x74b814 > 320) y += HUD_NUM_DOWN + HUD_SLOT_EXTRA*k; }
    BYTE *g6_base = NULL; { DWORD ra = (DWORD)(DWORD_PTR)__builtin_return_address(0); int mb = map_wb(); if (ra > 0x423a40 && ra < 0x423b60 && mb) { g6_base = *(BYTE**)0x67ce38; *(BYTE**)0x67ce38 = g6_base - mb; } }
    for (const unsigned char *p = (const unsigned char*)str; *p; p++) {
        int k = (BYTE)(*p - 0x20) & 0x3f;                 /* come l'originale: minuscole = maiuscole */
        if (*p >= 0x60) k = (*p - 0x40) & 0x3f;
        int w = g6_w[k];
        if (!w) { pen += 4; continue; }
        int adv = w + 1, off = 0;
        if (k >= 16 && k < 26) { adv = g6_dig + 1; off = (g6_dig - w) / 2; }   /* cifre a larghezza fissa (numeri allineati) */
        int sx = 240 + (k & 31)*8 + g6_x0[k], sy = 64 + (k >= 32 ? 8 : 0);
        blit_hook(font, 0x200, sx, sy, w, 7, pen + off, y);
        pen += adv;
    }
    if (g6_base) *(BYTE**)0x67ce38 = g6_base;
}
/* giocatore invisibile (F11, per le foto delle missioni): il rasterizzatore degli sprite (0x40215d -> 0x441d80) salta
   i poligoni che usano i fotogrammi d'animazione del giocatore (puntatori nella sua struttura a 0x621be4..0x621bec) */
static volatile int hide_player; static int rast_log;
typedef int (__cdecl *rast_t)(void*, int);
/* icone dell'HUD "luminose" (flag 4 in B+4, ramo additivo di 0x441d80 con la tabella 0x6360d0): nel gioco originale si sommano
   alla scena; nel livello HUD separato si sommano al nero e i bordi scuri diventano un alone grigio opaco. Qui si segna la loro
   area (ui_addm) e la composizione (gl_present) la somma alla scena. */
static BYTE *ui_addm; static int ui_addm_w, ui_addm_h; static volatile int ui_addm_on;
static void addm_mark(void *poly)
{
    if (!poly || !(((BYTE*)poly)[4] & 4) || !*(volatile int*)0x48b3d8) return;
    int LW = *(int*)0x74b814, LH = *(int*)0x74b818; if (LW <= 320 || LH <= 0 || *(int*)0x48cdc8 != LW) return;
    int *v = *(int**)0x6a5c44; if (!v || IsBadReadPtr(v, 48)) return;
    int x0 = 99999, y0 = 99999, x1 = -99999, y1 = -99999;
    for (int i = 0; i < 4; i++) { int x = v[i*3], y = v[i*3+1]; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 >= LW) x1 = LW - 1; if (y1 >= LH) y1 = LH - 1; if (x1 < x0 || y1 < y0) return;
    if (ui_addm_w != LW || ui_addm_h != LH) { free(ui_addm); ui_addm = calloc((size_t)LW*LH, 1); ui_addm_w = LW; ui_addm_h = LH; }
    if (!ui_addm) return;
    for (int y = y0; y <= y1; y++) memset(ui_addm + (size_t)y*LW + x0, 255, x1 - x0 + 1);
    ui_addm_on = 1;
}
static int __cdecl rast_hook(void *poly, int c)
{
    void *t = poly ? *(void**)poly : NULL;
    if (rast_log > 0) { rast_log--; const int *v = *(const int**)0x6a5c44; int x0 = 99999, x1 = -99999, y0 = 99999, y1 = -99999;
        if (v) for (int i = 0; i < 4; i++) { int x = v[i*3], y = v[i*3+1]; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
        mhlog("  rast ra %06x poly %p tex %p [%08x %08x %08x] box %d,%d-%d,%d", (DWORD)(DWORD_PTR)__builtin_return_address(0), poly, t, ((DWORD*)poly)[1], ((DWORD*)poly)[2], ((DWORD*)poly)[3], x0, y0, x1, y1); }
    if (hide_player && t && (t == *(void**)0x621be4 || t == *(void**)0x621be8 || t == *(void**)0x621bec)) return 0;
    {   /* HUD a destra: due icone (bersagli, sopravvissuti) con la percentuale sotto. L'immagine di quella in alto arriva fino in fondo
           e la percentuale la toccava: la alzo di 8 pixel per avere lo stesso spazio della seconda */
        int *v = *(int**)0x6a5c44;
        if (v && !IsBadReadPtr(v, 48)) {
            int x0 = 99999, y0 = 99999; for (int i = 0; i < 4; i++) { if (v[i*3] < x0) x0 = v[i*3]; if (v[i*3+1] < y0) y0 = v[i*3+1]; }
            static int ymax = -1; int lw = *(int*)0x74b814;
            if (lw > 320 && x0 > lw - 80) {
                if (y0 > ymax) ymax = y0;
                if (y0 < ymax - 20) { for (int i = 0; i < 4; i++) v[i*3+1] -= 8; addm_mark(poly); int r = ((rast_t)0x441d80)(poly, c); for (int i = 0; i < 4; i++) v[i*3+1] += 8; return r; }
            }
        }
    }
    {   /* HUD a sinistra: colonna degli oggetti (icone alte ~26 ogni 24 pixel, numero in grigia 6x6 a (2, 6+4k)): ogni posizione
           scende di HUD_SLOT_EXTRA cosi' il numero sta sotto la sua icona con lo stesso spazio delle icone di destra */
        int *v = *(int**)0x6a5c44; int lw = *(int*)0x74b814;
        if (lw > 320 && v && !IsBadReadPtr(v, 48)) {
            int x1 = -99999, y0 = 99999, y1 = -99999;
            for (int i = 0; i < 4; i++) { if (v[i*3] > x1) x1 = v[i*3]; if (v[i*3+1] < y0) y0 = v[i*3+1]; if (v[i*3+1] > y1) y1 = v[i*3+1]; }
            if (x1 < 70 && y0 >= 0 && y0 < 170 && y1 - y0 < 48) {
                int k = (y0 - 6) / 24; if (k < 0) k = 0; if (k > 3) k = 3; int dy = HUD_SLOT_EXTRA * k;
                if (dy) { for (int i = 0; i < 4; i++) v[i*3+1] += dy; addm_mark(poly); int r = ((rast_t)0x441d80)(poly, c); for (int i = 0; i < 4; i++) v[i*3+1] -= dy; return r; }
            }
        }
    }
    addm_mark(poly);
    return ((rast_t)0x441d80)(poly, c);
}
/* telecamera libera (foto delle missioni): 0x420fb0 chiama 0x401e2e -> 0x413ea0(telecamera 0x4b1930, record giocatore).
   Con cam_ovr la telecamera riceve una posizione finta e si aggancia subito (campo +0x40); il giocatore resta dov'e'. */
static volatile int cam_ovr, cam_x, cam_y, cam_z, cam_ang = -1, cam_yaw_set = -1, cam_mode;   /* cam_mode: 0 come nel gioco, 1 ruota col giocatore, 2 fissa */   /* cam_yaw_set: rotazione della modalita' foto (inclinazione invariata) */
typedef void (__cdecl *cam_t)(BYTE*, const BYTE*);
static void __cdecl cam_hook(BYTE *cam, const BYTE *pos)
{
    static int nl; if (cfg_shot && nl < 6 && cam_ovr) { nl++; mhlog("cam_hook cam %p pos %p: cam %d,%d,%d snap %d", cam, pos, ((int*)cam)[0] >> 16, ((int*)cam)[1] >> 16, ((int*)cam)[2] >> 16, ((int*)cam)[0x10]); }
    if (cam_ovr && pos) {
        static BYTE fake[0x300]; memcpy(fake, pos, sizeof fake);
        ((int*)fake)[0] = cam_x; ((int*)fake)[1] = cam_y; ((int*)fake)[2] = cam_z;
        *(int*)(cam + 0x40) = 1;
        if (cam_ang >= 0) *(volatile int*)0x4b164c = cam_ang;
        ((cam_t)0x413ea0)(cam, fake); return;
    }
    ((cam_t)0x413ea0)(cam, pos);
}
typedef void (__cdecl *cam2_t)(BYTE*, const BYTE*, int);
static void __cdecl cam2_hook(BYTE *cam, const BYTE *pos, int arg)
{
    static int nl; if (cfg_shot && nl < 6 && cam_ovr) { nl++; mhlog("cam2_hook cam %p pos %p arg %d: cam %d,%d,%d +40 %d", cam, pos, arg, ((int*)cam)[0] >> 16, ((int*)cam)[1] >> 16, ((int*)cam)[2] >> 16, ((int*)cam)[0x10]); }
    if (cam_ovr && pos) {
        static BYTE fake[0x300]; memcpy(fake, pos, sizeof fake);
        ((int*)fake)[0] = cam_x; ((int*)fake)[1] = cam_y; ((int*)fake)[2] = cam_z;
        *(int*)(cam + 0x40) = 1;
        if (cam_ang >= 0) { *(int*)(fake + 0x258) = cam_ang & 2047; arg &= ~1; }   /* imbardata: 2048 = giro completo */
        if (cam_mode == 1) arg &= ~1;   /* telecamera che ruota col giocatore */
        if (cam_mode == 2) arg |= 1;                                                                   /* telecamera fissa */
        if (cam_yaw_set >= 0 && !(arg & 1)) *(int*)(fake + 0x258) = cam_yaw_set;   /* rotazione manuale anche con la telecamera che ruota */
        ((cam2_t)0x420e00)(cam, fake, arg);
        if (cam_yaw_set >= 0 && (arg & 1)) *(int*)(cam + 0x14) = ((cam_yaw_set + 512) & 2047) << 16;
        return;
    }
    ((cam2_t)0x420e00)(cam, pos, arg);
}
static volatile int ghost_player;   /* modalita' foto: il giocatore non viene disegnato */
typedef void (__cdecl *actor_t)(BYTE*, BYTE*, int);
static DWORD actor_orig;
/* modalita' foto, "NPC verso la telecamera": i personaggi pensano (si girano verso il giocatore invisibile = telecamera)
   ma restano fermi nel punto salvato; niente proiettili */
static volatile int npc_aim; static int aim_pos[48][3]; static BYTE aim_on[48]; static int aim_ent[48], aim_think[48];
/* stesso personaggio di quando e' stata salvata la posizione? (i posti si riusano: un altro personaggio non deve prendere quella posizione) */
static int aim_same(int i, const BYTE *r) { return aim_on[i] && *(const int*)(r + 0x58) && aim_ent[i] == *(const int*)(r + 0x98) && aim_think[i] == *(const int*)(r + 0x94); }
static volatile int pl_vis, pl_placed; static int pl_pos[3], pl_zone = -1, pl_ang;   /* modalita' foto: personaggio messo in un punto e visibile */
static void __cdecl actor_hook(BYTE *rec, BYTE *b, int c)
{
    if (ghost_player && rec == (BYTE*)0x621b98 + 0x2b4 * *(volatile int*)0x629d5c) {
        if (!pl_vis || !pl_placed) return;
        int sv[3]; short sa = *(short*)(rec + 0xe); memcpy(sv, rec, 12); memcpy(rec, pl_pos, 12); *(short*)(rec + 0xe) = (short)((sa & ~2047) | pl_ang);
        ((actor_t)actor_orig)(rec, b, c); memcpy(rec, sv, 12); *(short*)(rec + 0xe) = sa; return;   /* disegnato nel punto scelto */
    }
    if (ghost_player && npc_aim && rec >= (BYTE*)0x621b98) { unsigned i = (unsigned)(rec - (BYTE*)0x621b98) / 0x2b4; if (i < 48 && aim_same(i, rec)) memcpy(rec, aim_pos[i], 12); }
    ((actor_t)actor_orig)(rec, b, c);
}
/* raccolta oggetti / ostaggi (0x402a86 -> 0x446b90(chi, oggetto, ...)): in modalita' foto il giocatore non raccoglie niente */
typedef int (__cdecl *pick_t)(int, int, int, int);
static DWORD pick_orig;
static int __cdecl pick_hook(int who, int item, int a3, int a4)
{
    if (ghost_player && (who & 0x80000000) && (who & 7) == *(volatile int*)0x629d5c) return 0;
    return ((pick_t)pick_orig)(who, item, a3, a4);
}
/* 0x4010b9 -> 0x44d3b0(personaggio): legge la cella della mappa sotto il personaggio (0x41e9b0) senza controllare NULL
   (personaggio fuori dalla mappa -> crash 0x44d3c5): se la cella non c'e' non fa niente */
/* IA dei nemici (stato "girovaga", 0x4057b0 e simili): prima di avanzare chiedono a 0x406070 (thunk 0x4024ff) se davanti
   e' libero. Quel controllo prova UN punto solo, sulla linea della direzione, a distanza +0x218 (48) = il raggio del corpo
   (le tabelle seno/coseno 0x6340c8/0x65c040 sono in scala 0x8000, quindi dist 96 -> 48 unita'). Arrivando di sbieco contro
   un muro, o contro strisce sottili e angoli (le casse: quarti di cella 0005/f000), quel punto resta libero mentre una
   "spalla" del corpo e' gia' contro l'ostacolo: il nemico continua a spingere strisciando. Qui il controllo guarda anche
   un po' piu' avanti (raggio + 24) e le due spalle (avanti 0.7 r + 8, di lato 0.75 r) con la stessa funzione del gioco
   (0x41ec00): se uno e' bloccato, l'IA lo sa prima di urtare e reagisce come davanti a un muro.
   [video] ia_ostacoli=0 = controllo originale. */
typedef int (__cdecl *ahead_t)(BYTE*, int, int);
static DWORD ahead_orig; static int ai_fix = 1;
static int ai_probe(const BYTE *rec, int c, int s, int fwd, int lat)
{
    int P[3] = { ((const int*)rec)[0], ((const int*)rec)[1], ((const int*)rec)[2] };
    ((short*)P)[1] += (short)((c * fwd - s * lat) >> 15); ((short*)P)[3] += (short)((s * fwd + c * lat) >> 15);
    return (short)((int (__cdecl*)(int*))0x41ec00)(P) == 1;
}
static int __cdecl ahead_hook(BYTE *rec, int dir, int dist)
{
    int r = ((ahead_t)ahead_orig)(rec, dir, dist);
    DWORD off = (DWORD)(rec - (BYTE*)0x621b98);
    if (!ai_fix || (short)r != 1 || off % 0x2b4 || off / 0x2b4 >= 48) return r;
    if (dist != 2 * *(const int*)(rec + 0x218)) return r;   /* solo il controllo "un passo avanti" */
    int R = *(const int*)(rec + 0x214); if (R < 16 || R > 256) R = 48;
    int c = *(const int*)(0x6340c8 + (dir & 0x7ff)*4), s = *(const int*)(0x65c040 + (dir & 0x7ff)*4);
    int fs = R * 7 / 10 + 8, ls = R * 3 / 4;
    if (!ai_probe(rec, c, s, R + 24, 0) || !ai_probe(rec, c, s, fs, ls) || !ai_probe(rec, c, s, fs, -ls)) return 0;
    return r;
}
/* IA, stato 6 (0x405c70, thunk 0x4027a2): pattuglia su un percorso di punti fissato dal livello (entita' [0x6b70c0]+e*36+0x18:
   +2 numero di punti, punti da +8, 8 byte: x, y; punto corrente in rec+0x66, bit 15 = verso). Il nemico va dritto al punto
   senza guardare la mappa: se e' finito fuori percorso (dopo un inseguimento) e la linea retta passa sopra delle casse, ci
   cammina contro per sempre. Qui, prima di muoversi, controlla se la linea fino al punto e' libera per il corpo come lo
   controlla il movimento del gioco (0x41ec00 sui punti a +-raggio lungo x e y, ogni 16 unita'); se non lo e', passa al punto del suo
   percorso piu' vicino raggiungibile in linea retta e da li' riprende il giro. */
static DWORD patrol_orig;
static int ai_line_free_at(int x0, int y0, int z, int R, int tx, int ty)
{
    /* come il movimento del gioco (0x41ecf0 e le altre tre): spostandosi in x conta il punto a +-R sull'asse x, in y quello
       sull'asse y. Lungo la linea, ogni 16 unita', quei punti devono essere liberi. */
    float dx = (float)(tx - x0), dy = (float)(ty - y0);
    float len = sqrtf(dx*dx + dy*dy); if (len < 1) return 1; if (len > 2048) len = 2048;
    float ux = dx / len, uy = dy / len;
    int sx = ux > 0.2f ? R : ux < -0.2f ? -R : 0, sy = uy > 0.2f ? R : uy < -0.2f ? -R : 0;
    for (float t = 0; t <= len; t += 16) {
        int cx = x0 + (int)(ux*t), cy = y0 + (int)(uy*t);
        for (int k = R ? 0 : 2; k < 3; k++) {   /* 0: lato in x, 1: lato in y, 2: centro (solo con R = 0) */
            int ox = k == 0 ? sx : 0, oy = k == 1 ? sy : 0; if (k < 2 && !ox && !oy) continue;
            int P[3] = { (cx + ox) << 16, (cy + oy) << 16, z << 16 };
            if ((short)((int (__cdecl*)(int*))0x41ec00)(P) != 1) return 0;
        }
    }
    return 1;
}
static int ai_line_free(const BYTE *rec, int tx, int ty)
{
    int R = *(const int*)(rec + 0x214); if (R < 16 || R > 256) R = 48;
    return ai_line_free_at(((const int*)rec)[0] >> 16, ((const int*)rec)[1] >> 16, ((const int*)rec)[2] >> 16, R, tx, ty);
}
/* debug ([replay] percorsi=1): percorsi di pattuglia dei nemici (IA 0x401ebf) con tratti che passano sopra ostacoli.
   "sicuro" = anche la linea centrale e' bloccata; "corpo" = libera al centro ma non per il corpo (raggio 48) */
static void ai_check_paths(int level)
{
    BYTE *eb = *(BYTE**)0x6b70c0; int ne = 0, bad = 0, ns = 0;
    int nent = 0; { BYTE *gb = *(BYTE**)0x4bb080; (void)gb; }
    for (int e = 0; eb && e < 4096; e++) {
        BYTE *en = eb + e*36; if (IsBadReadPtr(en, 36)) break;
        if (en[4] == 0 || en[4] > 200) continue;
        BYTE *path = *(BYTE**)(en + 0x18); if (!path || IsBadReadPtr(path, 16)) continue;
        int np = ((short)(*(short*)(path + 2) << 1)) >> 1; if (np <= 0 || np > 200 || IsBadReadPtr(path + 8, np*8)) continue;
        { DWORD dsc = *(const DWORD*)(0x495db0 + en[4]*16 + 4); if (!dsc || IsBadReadPtr((void*)dsc, 12) || *(const DWORD*)(dsc + 8) != 0x401ebf) continue; }
        const short *wp = (const short*)(path + 8); int x = *(short*)(en + 6), y = *(short*)(en + 8), z = *(short*)(en + 0xa);
        if (z < -1024 || z > 4096 || x < 0 || y < 0) continue; ne++; nent++;
        char sure[300], body[300]; int ms = 0, mb = 0;
        for (int k = -1; k + 1 < np && ms < 250 && mb < 250; k++) {
            int ax = k < 0 ? x : wp[k*4], ay = k < 0 ? y : wp[k*4+1], bx = wp[(k+1)*4], by = wp[(k+1)*4+1];
            char nm[24]; if (k < 0) strcpy(nm, " partenza->0"); else sprintf(nm, " %d->%d", k, k + 1);
            if (!ai_line_free_at(ax, ay, z, 0, bx, by)) ms += sprintf(sure + ms, "%s", nm);
            else if (!ai_line_free_at(ax, ay, z, 48, bx, by)) mb += sprintf(body + mb, "%s", nm);
        }
        sure[ms] = 0; body[mb] = 0;
        if (ms || mb) { bad++; ns += ms > 0; mhlog("percorsi liv %d: entita %d tipo %d a %d,%d z%d, %d punti | sicuro:%s | corpo:%s", level, e, en[4], x, y, z, np, ms ? sure : " -", mb ? body : " -"); }
    }
    mhlog("percorsi liv %d: %d nemici con percorso, %d con tratti bloccati (%d sicuri)", level, ne, bad, ns);
}
static void __cdecl patrol_hook(BYTE *rec)
{
    DWORD off = (DWORD)(rec - (BYTE*)0x621b98); int e = *(const int*)(rec + 0x98);
    if (ai_fix && !(off % 0x2b4) && off / 0x2b4 < 48 && e >= 0 && e < 0x10000 && *(BYTE**)0x6b70c0) {
        const BYTE *path = *(BYTE* const*)(*(BYTE**)0x6b70c0 + e*36 + 0x18);
        if (path && !IsBadReadPtr(path, 8)) {
            int n = ((short)(*(const short*)(path + 2) << 1)) >> 1; WORD cur = *(WORD*)(rec + 0x66); int ci = cur & 0x7fff;
            static int tick[48]; int i = off / 0x2b4;
            if (n > 1 && n < 256 && ci < n && !IsBadReadPtr(path + 8, n*8) && (++tick[i] & 7) == 1) {   /* ogni 8 fotogrammi */
                const short *wp = (const short*)(path + 8);
                if (!ai_line_free(rec, wp[ci*4], wp[ci*4 + 1])) {
                    int x = ((const int*)rec)[0] >> 16, y = ((const int*)rec)[1] >> 16, best = -1; long long bd = 1LL << 62;
                    for (int k = 0; k < n; k++) {
                        if (k == ci) continue;
                        long long d = (long long)(wp[k*4] - x)*(wp[k*4] - x) + (long long)(wp[k*4+1] - y)*(wp[k*4+1] - y);
                        if (d < bd && ai_line_free(rec, wp[k*4], wp[k*4 + 1])) { bd = d; best = k; }
                    }
                    if (best >= 0) { *(WORD*)(rec + 0x66) = (WORD)((cur & 0x8000) | best);
                        static int nl; if (nl++ < 30) mhlog("IA: personaggio %d, strada per il punto %d bloccata: va al punto %d", i, ci, best); }
                }
            }
        }
    }
    ((void (__cdecl*)(BYTE*))patrol_orig)(rec);
}
typedef void (__cdecl *cellf_t)(BYTE*); static DWORD cellf_orig;
static void __cdecl cellf_hook(BYTE *rec)
{
    if (*(const int*)(rec + 0x250) && !((void *(__cdecl*)(BYTE*))0x41e9b0)(rec)) { static int n; if (n++ < 5) mhlog("personaggio %p fuori dalla mappa (%d,%d z%d): saltato", rec, ((int*)rec)[0] >> 16, ((int*)rec)[1] >> 16, ((int*)rec)[2] >> 16); return; }
    ((cellf_t)cellf_orig)(rec);
}
/* PROTOTIPO render nuovo: registra un fotogramma di poligoni (dopo la proiezione del gioco) in MHPOLY.bin.
   Agganciate le 12 routine finali del rasterizzatore (thunk senza argomenti): vertici a 0x62dad0 (passo 0x14: x, y, u, v, luce),
   texture [0x62dbf0], luce piatta [0x62db84], tabella luci [0x69d9a4], poligono B corrente [0x69dad8] (+5 tipo, +8 dimensione sprite). */
static const DWORD rz_thunk[12] = { 0x401320, 0x40135c, 0x4019dd, 0x4013b1, 0x402720, 0x401d61, 0x40286a, 0x401424, 0x40268a, 0x4018b1, 0x401104, 0x40190b };
static DWORD rz_orig[12]; static volatile int rz_rec; static BYTE *rz_buf; static int rz_n, rz_cap; static BYTE *rz_spr; static int rz_ns, rz_scap;   /* record da 128 byte; sprite copiati subito (buffer condiviso) */
/* render nuovo: sprite gia' visti. Chiave = sorgente del fotogramma (B+0) e lato; il gioco lo decodifica sempre uguale
   -> lo copio una volta sola (pend), la DLL lo carica in una texture e lo tiene. chk = controllo del contenuto della sorgente. */
typedef struct { DWORD key, chk; int side; BYTE *pend; unsigned ti, tr, ts; int pal, ver, dirty; } SprSlot;
static SprSlot spr_slot[4096]; static int spr_nslot; static short spr_map[8192]; static int spr_map_ok;
static int spr_side(int id) { return id == 6 || id == 9 ? 64 : id == 7 || id == 10 ? 128 : 256; }
static int spr_get(DWORD src, int side, DWORD buf)
{
    if (!spr_map_ok) { memset(spr_map, 0xff, sizeof spr_map); spr_map_ok = 1; }
    DWORD chk = 0; if (src && !IsBadReadPtr((void*)src, 16)) { const DWORD *q = (const DWORD*)src; chk = q[0] ^ q[1]*31 ^ q[2]*961 ^ q[3]*29791; }
    DWORD key = src ^ ((DWORD)side << 24); unsigned h = (key * 2654435761u) >> 19;
    int s = -1;
    while (spr_map[h] >= 0) { SprSlot *S = &spr_slot[spr_map[h]]; if (S->key == key && S->side == side) { s = spr_map[h]; break; } h = (h + 1) & 8191; }
    if (s >= 0 && spr_slot[s].chk == chk) return s;
    if (s < 0) { if (spr_nslot >= 4096) return -1; s = spr_nslot++; memset(&spr_slot[s], 0, sizeof spr_slot[s]); spr_slot[s].key = key; spr_slot[s].side = side; spr_map[h] = (short)s; }
    SprSlot *S = &spr_slot[s]; S->chk = chk; S->dirty = 1;
    if (!S->pend) S->pend = malloc((size_t)side*side);
    if (S->pend) { memset(S->pend, 0, (size_t)side*side); for (int o = 0; o < side*side; o += 4096) { int n = side*side - o < 4096 ? side*side - o : 4096; if (!IsBadReadPtr((void*)(buf + o), n)) memcpy(S->pend + o, (void*)(buf + o), n); } }
    return s;
}
static int rz_nok, rz_nbad, rz_nfix;   /* poligoni: coordinate verificate / non verificabili / corrette (rigirate) */
static void rz_record(int id)
{
    if (!rz_rec) return;
    if (rz_n >= rz_cap) { int nc = rz_cap ? rz_cap*2 : 4096; BYTE *nb = realloc(rz_buf, (size_t)nc*128); if (!nb) return; rz_buf = nb; rz_cap = nc; }
    DWORD *r = (DWORD*)(rz_buf + (size_t)rz_n*128); const BYTE *B = *(const BYTE**)0x69dad8;
    r[0] = id; r[1] = B ? B[5] : 0; r[2] = B ? *(const WORD*)(B + 8) : 0; r[3] = *(const BYTE*)0x62db84;
    r[4] = *(const DWORD*)0x69d9a4; r[5] = *(const DWORD*)0x62dbf0; r[6] = *(const DWORD*)0x62dbf4; r[7] = B ? B[4] : 0;
    for (int i = 0; i < 4; i++) memcpy(&r[8 + i*5], (const BYTE*)0x62dad0 + i*0x14, 20);
    r[28] = (DWORD)-1; r[29] = B ? *(const DWORD*)B : 0;
    {   /* profondita' dei 4 vertici dopo la proiezione (0x436dd0): per la prospettiva corretta nel render nuovo */
        static const DWORD za[4] = { 0x62dbd4, 0x62db38, 0x62dbb0, 0x62da9c }; DWORD z[4];
        for (int i = 0; i < 4; i++) { int v = *(const int*)za[i]; z[i] = v < 1 ? 1 : v > 65535 ? 65535 : v; }
        r[30] = z[0] | z[1] << 16; r[31] = z[2] | z[3] << 16;
    }
    {   /* coordinate a schermo vere: il gioco (0x4371e1) fa x = cx + (X*s)>>16, s = (F<<16)/z, col prodotto a 32 bit,
                       che si rigira per i poligoni vicinissimi alla telecamera (telecamera inclinata) -> poligoni enormi di traverso.
                       Rifatto a 64 bit con X, Y, z della telecamera rimasti in memoria; usato solo se torna col valore del gioco. */
        static const DWORD xa[4] = { 0x62da98, 0x62db58, 0x62dbec, 0x62db30 }, ya[4] = { 0x62dbcc, 0x62dbbc, 0x62db28, 0x62dbc0 },
                           za[4] = { 0x62dbd4, 0x62db38, 0x62dbb0, 0x62da9c };
        int nv = r[1] == 12 ? 3 : 4, F = *(const int*)0x48cdc4, ok = 1, ox = 0, oy = 0; long long tx[4], ty[4];
        for (int i = 0; i < nv && ok; i++) {
            int zz = *(const int*)za[i]; if (zz < 10) { ok = 0; break; }
            int s = (int)(((long long)F << 16) / zz), X = *(const int*)xa[i], Y = *(const int*)ya[i];
            int wx = (int)((unsigned)X * (unsigned)s) >> 16, wy = (int)((unsigned)Y * (unsigned)s) >> 16;
            int dx_ = (int)r[8 + i*5] - wx, dy_ = (int)r[9 + i*5] - wy;
            if (i == 0) { ox = dx_; oy = dy_; } else if (dx_ != ox || dy_ != oy) ok = 0;
            tx[i] = (((long long)X * s) >> 16) + ox; ty[i] = (((long long)Y * s) >> 16) + oy;
        }
        if (ok) rz_nok++; else rz_nbad++;
        if (!ok) {   /* non verificabile (es. sprite da un'altra routine): coordinate rigirate di 65536 rimesse vicine al primo vertice */
            for (int i = 1; i < nv; i++) for (int c = 0; c < 2; c++) {
                int *q = (int*)&r[8 + i*5 + c], ref = (int)r[8 + c];
                if (*q - ref > 32768) *q -= 65536; else if (ref - *q > 32768) *q += 65536;
            }
        }
        if (ok) { int fx = 0; for (int i = 0; i < nv; i++) if (tx[i] != (int)r[8 + i*5] || ty[i] != (int)r[9 + i*5]) fx = 1; rz_nfix += fx; }
        if (ok) for (int i = 0; i < nv; i++) {
            long long a = tx[i], b = ty[i]; if (a > 2000000000LL) a = 2000000000LL; if (a < -2000000000LL) a = -2000000000LL; if (b > 2000000000LL) b = 2000000000LL; if (b < -2000000000LL) b = -2000000000LL;
            r[8 + i*5] = (DWORD)(int)a; r[9 + i*5] = (DWORD)(int)b;
        }
    }
    if (id >= 6 && cfg_newr) { int sl = spr_get(r[29], spr_side(id), r[5]); r[28] = (DWORD)sl; }   /* render nuovo: sprite ricordati */
    else if (id >= 6) {   /* sprite: decodificato in un buffer condiviso -> copia ora (registrazione su file) */
        if (rz_ns >= rz_scap) { int nc = rz_scap ? rz_scap*2 : 64; BYTE *nb = realloc(rz_spr, (size_t)nc*65536); if (nb) { rz_spr = nb; rz_scap = nc; } }
        if (rz_ns < rz_scap) { BYTE *d = rz_spr + (size_t)rz_ns*65536; memset(d, 0, 65536);
            for (int o = 0; o < 65536; o += 4096) if (!IsBadReadPtr((void*)(r[5] + o), 4096)) memcpy(d + o, (void*)(r[5] + o), 4096);
            r[28] = rz_ns++; }
    }
    rz_n++;
}
static volatile int nr_skip;   /* render nuovo attivo e funzionante: il gioco non disegna piu' la sua scena a 256 colori */
#define RZH(n) static void __cdecl rz_h##n(void) { rz_record(n); if (!(nr_skip && rz_rec)) ((void (__cdecl*)(void))rz_orig[n])(); }
RZH(0) RZH(1) RZH(2) RZH(3) RZH(4) RZH(5) RZH(6) RZH(7) RZH(8) RZH(9) RZH(10) RZH(11)
static void (__cdecl *const rz_hook[12])(void) = { rz_h0, rz_h1, rz_h2, rz_h3, rz_h4, rz_h5, rz_h6, rz_h7, rz_h8, rz_h9, rz_h10, rz_h11 };
static void rz_write(const RGBQUAD *pal)
{
    char path[MAX_PATH]; path_in_dir(path, "MHPOLY.bin"); FILE *f = fopen(path, "wb"); if (!f) return;
    fwrite("MHP2", 1, 4, f); fwrite(&rz_n, 4, 1, f);
    for (int i = 0; i < 256; i++) { BYTE c[3] = { pal[i].rgbRed, pal[i].rgbGreen, pal[i].rgbBlue }; fwrite(c, 1, 3, f); }
    fwrite((const void*)0x6460d0, 1, 288*256, f);
    fwrite(rz_buf, 128, rz_n, f); fwrite(&rz_ns, 4, 1, f); fwrite(rz_spr, 65536, rz_ns, f);
    /* texture: ogni puntatore diverso, 65536 byte (o meno se non leggibili) */
    DWORD *seen = calloc(rz_n + 1, 4); int ns = 0;
    for (int i = 0; i < rz_n; i++) { DWORD p = ((DWORD*)(rz_buf + (size_t)i*128))[5]; int k; for (k = 0; k < ns && seen[k] != p; k++); if (k == ns) seen[ns++] = p; }
    fwrite(&ns, 4, 1, f);
    static BYTE blk[65536];
    for (int k = 0; k < ns; k++) { memset(blk, 0, sizeof blk); for (int o = 0; o < 65536; o += 4096) if (!IsBadReadPtr((void*)(seen[k] + o), 4096)) memcpy(blk + o, (void*)(seen[k] + o), 4096);
        fwrite(&seen[k], 4, 1, f); fwrite(blk, 1, 65536, f); }
    free(seen); fclose(f);
    { char bp[MAX_PATH]; path_in_dir(bp, "MHBLEND.bin"); FILE *g = fopen(bp, "wb"); if (g) { fwrite((const void*)0x6360d0, 1, 65536, g); fclose(g); } }   /* tabella di fusione (trasparenze) */
    mhlog("render nuovo (prototipo): %d poligoni e %d texture in MHPOLY.bin", rz_n, ns);
}
/* posto libero per un proiettile (0x401307 -> 0x447830, 64 da 0xdc a 0x6aa590): nessuno con "NPC verso la telecamera" */
typedef void *(__cdecl *proj_t)(void);
static DWORD proj_orig;
static void *__cdecl proj_hook(void)
{
    if (ghost_player && npc_aim) return NULL;
    return ((proj_t)proj_orig)();
}
typedef void (__cdecl *txt_t)(void);
static void __cdecl txt_hook(void)
{
    if (cfg_shot == 1) {   /* debug: record dei caratteri usati */
        static const BYTE *seen[512]; static int ns;
        for (BYTE *n = *(BYTE**)0x6b65b8; n; n = *(BYTE**)n) {
            const BYTE *g = *(const BYTE**)(n + 8); int k; if (!g) continue;
            const BYTE *key = g + (*(short*)(n+6) << 20); for (k = 0; k < ns && seen[k] != key; k++);
            if (k == ns && ns < 512) { seen[ns++] = key; mhlog("  glifo %p tipo %d car %02x x %d y %d: sx %d sy %d w %d h %d", g, *(short*)(n+6), n[0x14], *(short*)(n+0xc), *(short*)(n+0xe), g[0], g[1], g[2], g[3]); }
        }
    }
    {   /* finestra "ESCI?": il gioco mette "NO" e "SI'" come se tutte le lettere fossero larghe uguali -> con le lettere vere
           "SI'" risultava spostato. Li centro sulla riga del titolo (quella col "?"), misurando le lettere vere. */
        typedef struct { short y, ty; int x0, x1, n; char s[72]; BYTE *nd[72]; } TL; static TL L[48]; int nl = 0;
        for (BYTE *n = *(BYTE**)0x6b65b8; n; n = *(BYTE**)n) {
            const BYTE *g = *(const BYTE**)(n + 8); short y = *(short*)(n + 0xe), ty = *(short*)(n + 6); int x = *(short*)(n + 0xc);
            int k; for (k = 0; k < nl && !(L[k].y == y && L[k].ty == ty); k++);
            if (k == nl) { if (nl >= 48) continue; nl++; L[k].y = y; L[k].ty = ty; L[k].x0 = 99999; L[k].x1 = -99999; L[k].n = 0; }
            int w = g ? g[2] : 4; if (x < L[k].x0) L[k].x0 = x; if (x + w > L[k].x1) L[k].x1 = x + w;
            if (L[k].n < 71) { L[k].s[L[k].n] = (char)n[0x14]; L[k].nd[L[k].n] = n; L[k].n++; L[k].s[L[k].n] = 0; }
        }
        if (cfg_shot == 7) {   /* debug: righe di testo a schermo (una volta per riga diversa) */
            static char seen[400][32]; static int ns;
            for (int k = 0; k < nl; k++) { char key[32]; snprintf(key, 32, "%d|%s", L[k].ty, L[k].s); int f = 0; for (int j = 0; j < ns; j++) if (!strcmp(seen[j], key)) f = 1;
                if (!f && ns < 400) { strcpy(seen[ns++], key); mhlog("testo: tipo %d y %d x %d-%d \"%s\"", L[k].ty, L[k].y, L[k].x0, L[k].x1, L[k].s); } }
        }
        /* righe piu' larghe della finestra 320 (es. "FRECCIA SPOSTA, ... TAB TORNA AL GIOCO" nella mappa, tagliata ai bordi):
           spazi tra le lettere ridotti quanto basta, riga centrata dove stava */
        for (int k = 0; k < nl; k++) {
            int W = L[k].x1 - L[k].x0, n = L[k].n; if (W <= 284 || n < 2) continue;
            for (int i = 1; i < n; i++) for (int j = i; j > 0 && *(short*)(L[k].nd[j-1] + 0xc) > *(short*)(L[k].nd[j] + 0xc); j--) { BYTE *t_ = L[k].nd[j]; L[k].nd[j] = L[k].nd[j-1]; L[k].nd[j-1] = t_; }
            int E = W - 284, cl = 160, x0 = L[k].x0, gp[72], red[72], minp = 999, nsp = 0;
            for (int i = 1; i < n; i++) { gp[i] = *(short*)(L[k].nd[i] + 0xc) - *(short*)(L[k].nd[i-1] + 0xc); red[i] = 0; }
            for (int i = 1; i < n; i++) { const BYTE *g = *(const BYTE**)(L[k].nd[i-1] + 8); int adv = g ? g[2] : gp[i]; if (gp[i] - adv > 2) nsp++; }
            /* prima dagli spazi tra le parole (fino a meta' del loro spazio extra), poi un po' da tutte le lettere */
            int left = E;
            for (int i = 1; i < n && left > 0; i++) { const BYTE *g = *(const BYTE**)(L[k].nd[i-1] + 8); int adv = g ? g[2] : gp[i]; int ex = gp[i] - adv;
                if (ex > 2) { int r = ex / 2; int q = nsp ? (E + nsp - 1) / nsp : 0; if (r > q) r = q; if (r > left) r = left; red[i] = r; left -= r; } }
            for (int i = 1; i < n && left > 0; i++) { int r = (left + (n - i) - 1) / (n - i); red[i] += r; left -= r; }
            (void)minp; int acc = 0;
            for (int i = 0; i < n; i++) { if (i) acc += red[i]; short *px = (short*)(L[k].nd[i] + 0xc); int xi = *px; *px = (short)(cl - 142 + (xi - x0) - acc); }
        }
        /* "NO" / "SI'": stesso inizio d'inchiostro (S sotto la N). ink_left = colonne vuote a sinistra di ogni lettera nel foglio del font,
           misurate in blit_hook quando la lettera viene disegnata */
        int kno = -1; for (int k = 0; k < nl; k++) if (!strcmp(L[k].s, "NO")) kno = k;
        if (kno >= 0) {
            const BYTE *g0 = *(const BYTE**)(L[kno].nd[0] + 8); int ino = g0 ? ink_get(g0[0], g0[1], g0[3]) : -1; if (ino < 0) ino = 0;   /* font piccolo: lettere gia' tagliate all'inchiostro */
            {   /* "NO" centrato sotto il titolo col "?" (ESCI?) */
                int kt = -1; for (int k = 0; k < nl; k++) if (strchr(L[k].s, '?') && L[k].y < L[kno].y && L[kno].y - L[k].y < 80) kt = k;
                if (kt >= 0) { int d = (L[kt].x0 + L[kt].x1) / 2 - (L[kno].x0 + L[kno].x1) / 2;
                    for (int i = 0; i < L[kno].n; i++) *(short*)(L[kno].nd[i] + 0xc) += (short)d; L[kno].x0 += d; L[kno].x1 += d; }
            }
            if (ino >= 0) {
                int tno = *(short*)(L[kno].nd[0] + 0xc) + ino;
                for (int k = 0; k < nl; k++) {
                    if (k == kno || (strcmp(L[k].s, "SI'") && strcmp(L[k].s, "SI") && strcmp(L[k].s, "YES"))) continue;
                    if (abs(L[k].y - L[kno].y) > 40) continue;
                    const BYTE *g1 = *(const BYTE**)(L[k].nd[0] + 8); int i1 = g1 ? ink_get(g1[0], g1[1], g1[3]) : -1; if (i1 < 0) i1 = 0;
                    int d = tno - (*(short*)(L[k].nd[0] + 0xc) + i1);
                    for (int i = 0; i < L[k].n; i++) *(short*)(L[k].nd[i] + 0xc) += (short)d;
                }
            }
        }
    }
    {   /* "CONFERMA" fra i caratteri a schermo -> un menu aspetta CTRL (vedi pad_keys) */
        static const char w[] = "CONFERMA"; int k = 0;
        for (BYTE *n = *(BYTE**)0x6b65b8; n; n = *(BYTE**)n) { char ch = (char)n[0x14]; if (ch == ' ') continue; k = ch == w[k] ? k + 1 : (ch == w[0] ? 1 : 0); if (!w[k]) { menu_conf_t = GetTickCount(); break; } }
    }
    txt_on = 1; ((txt_t)0x44f6d0)(); txt_on = 0;
}
/* font piccolo (tabella sx,sy,w,h per carattere a 0x4948f8): celle fisse da 8 pixel -> spaziatura irregolare.
   Ogni lettera viene misurata nel foglio TEXT3 (stesso schema di PCPANEL) e la tabella corretta: inizio vero e larghezza + 1 pixel. */
static void fix_small_font(void)
{
    if (!hd_loaded) hd_load();
    int k = hd_find("TEXT3"); BYTE *t = (BYTE*)0x4948f8;
    if (k < 0 || hdi[k].w != 256 || hdi[k].h < 240 || memcmp(t + 0x41*4, "\x00\x30\x08\x08", 4)) return;
    const BYTE *I = hdi[k].idx[0]; DWORD old; int n = 0;
    /* entrambe le versioni del font piccolo: 0x4948f8 (tipi 1/5/6) e 0x494cf8 (tipo 0, l'altro colore: voci non selezionate dei menu) */
    BYTE *tabs[1] = { t };   /* solo 0x4948f8: correggere anche 0x494cf8 faceva comparire una barretta dopo la A */
    for (int ti = 0; ti < 1; ti++) {
        BYTE *tb = tabs[ti]; VirtualProtect(tb, 512, PAGE_READWRITE, &old);
        for (int c = 0x21; c < 0x80; c++) {
            BYTE *r = tb + c*4; if (r[3] != 8 || (r[1] != 48 && r[1] != 56) || r[2] < 3 || r[2] > 10 || r[0] + r[2] > 256) continue;
            int y0 = 192 + r[1] - 48, x0 = 99, x1 = -1;
            for (int y = y0; y < y0 + 8; y++) for (int x = 0; x < r[2]; x++) if (I[y*256 + r[0] + x]) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
            if (x1 < 0) continue;
            r[0] += x0; r[2] = x1 - x0 + 2; sf_bw[r[0]][(r[1] - 48) >> 3 & 1] = x1 - x0 + 1; n++;
        }
        VirtualProtect(tb, 512, old, &old);
    }
    /* testi tipo 5/6: avanzamento "larghezza - 3" (lettere sovrapposte) -> larghezza, sia nel disegno sia nel calcolo per centrare */
    static const DWORD at[2] = { 0x44faba, 0x404efa }; static const BYTE ex[2][4] = { {0x8d,0x6c,0x15,0xfd}, {0x8d,0x44,0x08,0xfd} };
    for (int i = 0; i < 2; i++) if (!memcmp((void*)at[i], ex[i], 4)) { VirtualProtect((void*)at[i], 4, PAGE_EXECUTE_READWRITE, &old); ((BYTE*)at[i])[3] = 0; VirtualProtect((void*)at[i], 4, old, &old); }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    mhlog("font piccolo: %d caratteri con spaziatura corretta", n);
}
static void srcmap_install(void)
{
    if (*(BYTE*)0x40210d != 0xE9 || *(BYTE*)0x4014e2 != 0xE9) return;
    if ((DWORD)(0x40210d + 5 + *(int*)(0x40210d+1)) != 0x437c40 || (DWORD)(0x4014e2 + 5 + *(int*)(0x4014e2+1)) != 0x4432d0) return;
    if (*(BYTE*)0x4011c2 == 0xE9 && (DWORD)(0x4011c2 + 5 + *(int*)(0x4011c2+1)) == 0x44f6d0) patch_tramp(0x4011c2, (void*)txt_hook);
    if (*(BYTE*)0x401e3d == 0xE9 && (DWORD)(0x401e3d + 5 + *(int*)(0x401e3d + 1)) == 0x443880) patch_tramp(0x401e3d, (void*)g6_hook);
    fix_small_font();
    if (*(BYTE*)0x40215d == 0xE9 && (DWORD)(0x40215d + 5 + *(int*)(0x40215d+1)) == 0x441d80) patch_tramp(0x40215d, (void*)rast_hook);
    if (*(BYTE*)0x401e2e == 0xE9 && (DWORD)(0x401e2e + 5 + *(int*)(0x401e2e + 1)) == 0x413ea0) patch_tramp(0x401e2e, (void*)cam_hook);
    if (*(BYTE*)0x401afa == 0xE9 && (DWORD)(0x401afa + 5 + *(int*)(0x401afa + 1)) == 0x420e00) patch_tramp(0x401afa, (void*)cam2_hook);
    if (*(BYTE*)0x4028d8 == 0xE9) { actor_orig = 0x4028d8 + 5 + *(int*)(0x4028d8 + 1); patch_tramp(0x4028d8, (void*)actor_hook); }
    if (*(BYTE*)0x402a86 == 0xE9) { pick_orig = 0x402a86 + 5 + *(int*)(0x402a86 + 1); if (pick_orig == 0x446b90) patch_tramp(0x402a86, (void*)pick_hook); }
    if (*(BYTE*)0x4010b9 == 0xE9) { cellf_orig = 0x4010b9 + 5 + *(int*)(0x4010b9 + 1); if (cellf_orig == 0x44d3b0) patch_tramp(0x4010b9, (void*)cellf_hook); }
    ai_fix = GetPrivateProfileIntA("video", "ia_ostacoli", 1, ini_path);
    if (*(BYTE*)0x4024ff == 0xE9) { ahead_orig = 0x4024ff + 5 + *(int*)(0x4024ff + 1); if (ahead_orig == 0x406070) patch_tramp(0x4024ff, (void*)ahead_hook); }
    if (*(BYTE*)0x4027a2 == 0xE9) { patrol_orig = 0x4027a2 + 5 + *(int*)(0x4027a2 + 1); if (patrol_orig == 0x405c70) patch_tramp(0x4027a2, (void*)patrol_hook); }
    for (int i = 0; i < 12; i++) if (*(BYTE*)rz_thunk[i] == 0xE9) { rz_orig[i] = rz_thunk[i] + 5 + *(int*)(rz_thunk[i] + 1); patch_tramp(rz_thunk[i], (void*)rz_hook[i]); }
    {   /* mappa (Select/TAB): centro = meta' della larghezza del livello 2D (854 a 32:9), ma il livello dei menu e' gia' spostato
           di (854-320)/2 per centrarli -> la mappa finiva a destra. Centro fisso su 320/2 come nell'originale. */
        static const BYTE e1[5] = { 0xa1, 0xc8, 0xcd, 0x48, 0x00 }, e2[6] = { 0x8b, 0x1d, 0xc8, 0xcd, 0x48, 0x00 };
        DWORD old_;
        static const BYTE nop6[6] = { 0xbb, 0x40, 0x01, 0x00, 0x00, 0x90 };   /* mov ebx, 320 */
        static const BYTE eax320[5] = { 0xb8, 0x40, 0x01, 0x00, 0x00 };   /* mov eax, 320 */
        {   /* forme della mappa: 0x42424d (centro in edi) e 0x424390 circa (centro in ebx) */
            static const BYTE e3[6] = { 0x8b, 0x3d, 0xc8, 0xcd, 0x48, 0x00 }, edi320[6] = { 0xbf, 0x40, 0x01, 0x00, 0x00, 0x90 };
            if (GetPrivateProfileIntA("video", "mappa_fix", 1, ini_path) && !memcmp((void*)0x42425c, e3, 6)) {
                VirtualProtect((void*)0x42425c, 6, PAGE_EXECUTE_READWRITE, &old_); memcpy((void*)0x42425c, edi320, 6); VirtualProtect((void*)0x42425c, 6, old_, &old_); }
            if (GetPrivateProfileIntA("video", "mappa_fix", 1, ini_path) && !memcmp((void*)0x4243a3, e2, 6)) {
                VirtualProtect((void*)0x4243a3, 6, PAGE_EXECUTE_READWRITE, &old_); memcpy((void*)0x4243a3, nop6, 6); VirtualProtect((void*)0x4243a3, 6, old_, &old_); }
        }
        if (GetPrivateProfileIntA("video", "mappa_fix", 1, ini_path) && !memcmp((void*)0x4235a0, e1, 5)) {   /* 0x423597: linee della mappa */
            VirtualProtect((void*)0x4235a0, 5, PAGE_EXECUTE_READWRITE, &old_); memcpy((void*)0x4235a0, eax320, 5); VirtualProtect((void*)0x4235a0, 5, old_, &old_); }
        if (GetPrivateProfileIntA("video", "mappa_fix", 1, ini_path))
        if (!memcmp((void*)0x423991, e2, 6)) { VirtualProtect((void*)0x423991, 6, PAGE_EXECUTE_READWRITE, &old_); memcpy((void*)0x423991, nop6, 6); VirtualProtect((void*)0x423991, 6, old_, &old_); }   /* 0x423820, linee della mappa (chiamata da 0x424b90) */
        if (GetPrivateProfileIntA("video", "mappa_fix", 1, ini_path) && !memcmp((void*)0x422d9b, e2, 6))   /* 0x422d40: freccia del giocatore sulla mappa */ { VirtualProtect((void*)0x422d9b, 6, PAGE_EXECUTE_READWRITE, &old_); memcpy((void*)0x422d9b, nop6, 6); VirtualProtect((void*)0x422d9b, 6, old_, &old_); }
        if (GetPrivateProfileIntA("video", "mappa_fix", 1, ini_path) && GetPrivateProfileIntA("video", "mappa_larga", 1, ini_path)) {
            /* mappa larga: cornice, linee, rettangoli rifatti; i segnalini (0x422d40) non vengono piu' scartati fuori da 0..320 */
            if (*(BYTE*)0x401325 == 0xE9 && (DWORD)(0x401325 + 5 + *(int*)(0x401325 + 1)) == 0x422b20) patch_tramp(0x401325, (void*)mw_frame);
            if (*(BYTE*)0x401514 == 0xE9 && (DWORD)(0x401514 + 5 + *(int*)(0x401514 + 1)) == 0x424a60) patch_tramp(0x401514, (void*)mw_tile);
            if (*(BYTE*)0x4010e6 == 0xE9 && (DWORD)(0x4010e6 + 5 + *(int*)(0x4010e6 + 1)) == 0x424bb0) patch_tramp(0x4010e6, (void*)mw_line);
            if (*(BYTE*)0x40154b == 0xE9 && (DWORD)(0x40154b + 5 + *(int*)(0x40154b + 1)) == 0x424840) patch_tramp(0x40154b, (void*)mw_rect);
            static const BYTE jl_[2] = { 0x7c, 0x31 }, jg_[2] = { 0x7f, 0x2a }, nop2[2] = { 0x90, 0x90 };
            if (!memcmp((void*)0x422dbe, jl_, 2) && !memcmp((void*)0x422dc5, jg_, 2)) {
                VirtualProtect((void*)0x422dbe, 9, PAGE_EXECUTE_READWRITE, &old_); memcpy((void*)0x422dbe, nop2, 2); memcpy((void*)0x422dc5, nop2, 2); VirtualProtect((void*)0x422dbe, 9, old_, &old_); }
            mw_on = 1;
            if (GetPrivateProfileIntA("video", "mappa_hd", 1, ini_path)) {   /* vertici delle linee x4 (centro x4, spostamenti 15 -> 13 bit) */
                static const BYTE s1[3] = { 0xc1, 0xf8, 0x0f }, s2[3] = { 0xc1, 0xf9, 0x0f }, h1[2] = { 0xd1, 0xeb };
                BYTE *p1 = (BYTE*)0x424438, *p2 = (BYTE*)0x42444a, *p3 = (BYTE*)0x4243b6, *p4 = (BYTE*)0x4243c8;
                if (!memcmp(p1, s1, 3) && !memcmp(p2, s2, 3) && !memcmp(p3, h1, 2) && !memcmp(p4, h1, 2) &&
                    *(BYTE*)0x4011f4 == 0xE9 && (DWORD)(0x4011f4 + 5 + *(int*)(0x4011f4 + 1)) == 0x436d00) {
                    VirtualProtect((void*)0x4243b0, 0xa0, PAGE_EXECUTE_READWRITE, &old_);
                    p1[2] = 0x0d; p2[2] = 0x0d; p3[1] = 0xe3; p4[1] = 0xe3;   /* sar 13; shl ebx,1 invece di shr ebx,1 */
                    VirtualProtect((void*)0x4243b0, 0xa0, old_, &old_);
                    patch_tramp(0x4011f4, (void*)mw_quad); mw_hd = 1; mhlog("mappa HD: vertici x4, disegno dalla scheda video");
                }
            }
        }
        FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    }
    if (*(BYTE*)0x401307 == 0xE9) { proj_orig = 0x401307 + 5 + *(int*)(0x401307 + 1); if (proj_orig == 0x447830) patch_tramp(0x401307, (void*)proj_hook); }
    patch_tramp(0x40210d, (void*)load_hook); patch_tramp(0x4014e2, (void*)blit_hook); sm_on = 1;
    *(volatile DWORD*)0x74b824 = (DWORD)(DWORD_PTR)bg_hook;
    if (cfg_shot == 1 && *(BYTE*)0x40122b == 0xE9) patch_tramp(0x40122b, (void*)spr_hook);
    mhlog("source-map: agganciati caricamento BMP e blitter");
}
/* schermate 4:3 allargate al rapporto d'aspetto: segmenti fissi (dettagli: rivetti, prese d'aria) e segmenti che si allungano */
typedef struct { const char *name; int n; short seg[8][3]; } WideSpec;     /* seg: x0, x1, 1 = si allunga */
static const WideSpec wide_specs[] = {
    { "MAINSCRN", 5, { {0, 20, 0}, {20, 128, 1}, {128, 224, 0}, {224, 298, 1}, {298, 320, 0} } },
    { "BIO_SCRN", 7, { {0, 32, 0}, {32, 56, 1}, {56, 70, 0}, {70, 165, 1}, {165, 185, 0}, {185, 232, 1}, {232, 256, 0} } },
};
static const WideSpec *wide_find(const HdImg *m)
{
    for (int i = 0; i < (int)(sizeof wide_specs / sizeof *wide_specs); i++) if (!_stricmp(m->name, wide_specs[i].name)) return &wide_specs[i];
    return NULL;
}
static void put_hd(DWORD *d, const BYTE *c)
{
    int a = c[3];
    if (a == 255) { *d = 0xFF000000u | c[0] | c[1] << 8 | c[2] << 16; return; }
    DWORD o = *d; int oa = o >> 24, ob = oa*(255 - a)/255, na = a + ob;
    if (!na) { *d = 0; return; }
    int r = (c[0]*a + (int)(o & 255)*ob) / na, g = (c[1]*a + (int)(o >> 8 & 255)*ob) / na, b = (c[2]*a + (int)(o >> 16 & 255)*ob) / na;
    *d = (DWORD)na << 24 | r | g << 8 | b << 16;
}
/* copia allargata di un'immagine intera: i pixel scritti sono marcati (alfa 0x80 in hd_ref) perche' nel livello
   originale li' c'e' altro (o niente): lo shader usa comunque la versione HD */
static void sm_wide(const BlitRec *r, const HdImg *m, const WideSpec *ws, int delta, int VW, int VH)
{
    int sw = 0; for (int i = 0; i < ws->n; i++) if (ws->seg[i][2]) sw += ws->seg[i][1] - ws->seg[i][0];
    int W4 = 4*(m->w + delta); static int map[4*1200]; if (W4 > 4*1200) return;
    { int X = 0, given = 0, last = -1;
      for (int i = 0; i < ws->n; i++) if (ws->seg[i][2]) last = i;
      for (int i = 0; i < ws->n; i++) {
        int w = ws->seg[i][1] - ws->seg[i][0], ex = 0;
        if (ws->seg[i][2]) { ex = i == last ? delta - given : delta*w/sw; given += ex; }
        int dw = 4*(w + ex);
        for (int k = 0; k < dw && X < W4; k++, X++) map[X] = 4*ws->seg[i][0] + (int)((long long)k*4*w/dw);
      } }
    int x0 = r->dx - delta/2;
    for (int y = r->dy; y < r->dy + r->h; y++) {
        if (y < 0 || y >= VH || y < r->y0 || y >= r->y1) continue;
        int v = y - r->dy;
        for (int x = x0; x < x0 + m->w + delta; x++) {
            if (x < 0 || x >= VW) continue;
            int X = 4*(x - x0), u = map[X + 2] / 4;
            BYTE t = r->src[(size_t)v*r->pitch + u]; if (!t) continue;
            BYTE fi = m->idx[0][v*m->w + u];
            hd_tmap[y*VW + x] = t;
            hd_ref[y*VW + x] = m->haspal ? (0x80000000u | m->pal[fi*3] | m->pal[fi*3+1] << 8 | m->pal[fi*3+2] << 16) : 0x80000000u;
            for (int j = 0; j < 4; j++) {
                DWORD *d = hd_comp + (size_t)(4*y + j)*VW*4 + 4*x; const BYTE *row = m->rgb[0] + (size_t)(4*v + j)*m->w*16;
                for (int i = 0; i < 4; i++) put_hd(d + i, row + (size_t)map[X + i]*4);
            }
        }
    }
}
/* immagine della missione (220x160) nel briefing allargato: versione larga NOMEW.raw (ripresa piu' ampia, ritagliata al centro)
   se esiste, altrimenti l'originale con i lati neri */
/* foto della missione (UI_HD\NOMEF.raw, fatta in gioco): riempie la finestra dell'anteprima, qualunque sia il rapporto
   d'aspetto, ritagliata al centro ("riempi e ritaglia"), con campionamento bilineare */
static int sm_mission_photo(const BlitRec *r, const HdImg *m, int delta, int VW, int VH)
{
    char fn[20]; snprintf(fn, sizeof fn, "%sF", m->name); int kf = hd_find(fn); if (kf < 0) return 0;
    const HdImg *f = &hdi[kf]; int PW = f->w*4, PH = f->h*4, WW = m->w + delta, x0 = r->dx - delta/2;
    double sc = (double)PW / (WW*4) < (double)PH / (m->h*4) ? (double)PW / (WW*4) : (double)PH / (m->h*4);   /* pixel foto per pixel HD */
    double sx0 = (PW - WW*4*sc) / 2, sy0 = (PH - m->h*4*sc) / 2;
    for (int y = r->dy; y < r->dy + r->h; y++) {
        if (y < 0 || y >= VH || y < r->y0 || y >= r->y1) continue;
        int v = y - r->dy;
        for (int x = x0; x < x0 + WW; x++) {
            if (x < 0 || x >= VW) continue;
            int u = x - r->dx, uc = u < 0 ? 0 : u >= m->w ? m->w - 1 : u;
            BYTE t = r->src[(size_t)v*r->pitch + uc]; if (!t) t = 1;
            BYTE fi = m->idx[0][v*m->w + uc];
            hd_tmap[y*VW + x] = t;
            (void)fi; hd_ref[y*VW + x] = 0x60000000u;   /* foto: nessuna ricolorazione per pixel (alfa 0x60 = marcato, colori della foto) */
            for (int j = 0; j < 4; j++) {
                DWORD *d = hd_comp + (size_t)(4*y + j)*VW*4 + 4*x;
                double py = sy0 + ((v*4 + j) + 0.5)*sc - 0.5; int iy = (int)py; double fy = py - iy; if (iy < 0) { iy = 0; fy = 0; } if (iy >= PH - 1) { iy = PH - 2; fy = 1; }
                for (int i = 0; i < 4; i++) {
                    double px = sx0 + (((x - x0)*4 + i) + 0.5)*sc - 0.5; int ix = (int)px; double fx = px - ix; if (ix < 0) { ix = 0; fx = 0; } if (ix >= PW - 1) { ix = PW - 2; fx = 1; }
                    const BYTE *a = f->rgb[0] + ((size_t)iy*PW + ix)*4, *b = a + 4, *c = a + (size_t)PW*4, *e = c + 4; BYTE o[4];
                    for (int k = 0; k < 3; k++) o[k] = (BYTE)((a[k]*(1-fx) + b[k]*fx)*(1-fy) + (c[k]*(1-fx) + e[k]*fx)*fy + 0.5);
                    o[3] = 255; put_hd(d + i, o);
                }
            }
        }
    }
    return 1;
}
static void sm_mission(const BlitRec *r, const HdImg *m, int delta, int VW, int VH)
{
    if (sm_mission_photo(r, m, delta, VW, VH)) return;
    char wn[20]; snprintf(wn, sizeof wn, "%sW", m->name); int kw = hd_find(wn); const HdImg *mw = kw >= 0 ? &hdi[kw] : NULL;
    int WW = m->w + delta, x0 = r->dx - delta/2, cx = mw ? (mw->w - WW) / 2 : 0;
    if (mw && (cx < 0 || mw->h != m->h)) mw = NULL;
    for (int y = r->dy; y < r->dy + r->h; y++) {
        if (y < 0 || y >= VH || y < r->y0 || y >= r->y1) continue;
        int v = y - r->dy;
        for (int x = x0; x < x0 + WW; x++) {
            if (x < 0 || x >= VW) continue;
            int u = x - r->dx; BYTE t; BYTE fi; const HdImg *s_ = m; int su = u;
            if (mw) { s_ = mw; su = cx + (x - x0); t = (u >= 0 && u < m->w) ? r->src[(size_t)v*r->pitch + u] : 0; fi = mw->idx[0][v*mw->w + su]; if (!t) t = fi ? fi : 1; }
            else if (u >= 0 && u < m->w) { t = r->src[(size_t)v*r->pitch + u]; if (!t) continue; fi = m->idx[0][v*m->w + u]; }
            else {   /* lati senza immagine: nero */
                hd_tmap[y*VW + x] = 1; hd_ref[y*VW + x] = 0x80000000u;
                for (int j = 0; j < 4; j++) { DWORD *d = hd_comp + (size_t)(4*y + j)*VW*4 + 4*x; d[0] = d[1] = d[2] = d[3] = 0xFF000000u; }
                continue;
            }
            hd_tmap[y*VW + x] = t;
            hd_ref[y*VW + x] = s_->haspal ? (0x80000000u | s_->pal[fi*3] | s_->pal[fi*3+1] << 8 | s_->pal[fi*3+2] << 16) : 0x80000000u;
            for (int j = 0; j < 4; j++) {
                DWORD *d = hd_comp + (size_t)(4*y + j)*VW*4 + 4*x; const BYTE *row = s_->rgb[0] + ((size_t)(4*v + j)*s_->w*4 + 4*su)*4;
                for (int i = 0; i < 4; i++) put_hd(d + i, row + i*4);
            }
        }
    }
}
/* immagine intera da 320 con versione larga NOMEW.raw (porte del menu: tools\door_wide.py, lati costruiti dall'originale):
   ne usa il centro largo quanto lo schermo; trasparenze dagli indici della versione larga */
static int sm_widew(const BlitRec *r, const HdImg *m, int delta, int VW, int VH)
{
    if (m->w != 320) return 0;
    char wn[20]; snprintf(wn, sizeof wn, "%sW", m->name); int kw = hd_find(wn); if (kw < 0) return 0;
    const HdImg *mw = &hdi[kw]; int WW = m->w + delta; if (mw->h != m->h || mw->w < WW) return 0;
    int cx = (mw->w - WW) / 2, x0 = r->dx - delta/2;
    if (cfg_shot == 7 && !_stricmp(m->name, "TOPDOR")) { static int s1, s2; if (r->dy == -64 && !s1) { s1 = 1; shot_req = 58; } if (r->dy == 0 && !s2) { s2 = 1; shot_req = 59; } }   /* prove: porte a meta' e chiuse */
    for (int y = r->dy; y < r->dy + r->h; y++) {
        if (y < 0 || y >= VH) continue;
        int v = y - r->dy;
        for (int x = x0; x < x0 + WW; x++) {
            if (x < 0 || x >= VW) continue;
            int su = cx + (x - x0), u = x - r->dx; BYTE fi = mw->idx[0][v*mw->w + su];
            BYTE t = (u >= 0 && u < m->w) ? r->src[(size_t)v*r->pitch + u] : fi; if (!t) continue;
            hd_tmap[y*VW + x] = t;
            hd_ref[y*VW + x] = mw->haspal ? (0x80000000u | mw->pal[fi*3] | mw->pal[fi*3+1] << 8 | mw->pal[fi*3+2] << 16) : 0x80000000u;
            for (int j = 0; j < 4; j++) {
                DWORD *d = hd_comp + (size_t)(4*y + j)*VW*4 + 4*x; const BYTE *row = mw->rgb[0] + ((size_t)(4*v + j)*mw->w*4 + 4*su)*4;
                for (int i = 0; i < 4; i++) put_hd(d + i, row + i*4);
            }
        }
    }
    return 1;
}
/* costruisce il composito HD rifacendo le copie registrate; ritorna 1 se e' cambiato */
static int sm_build(int VW, int VH)
{
    static unsigned smf; smf++;
    if (sm_dbg == 1 || (cfg_shot == 1 && smf == 1250)) { if (sm_dbg == 1) shot_req = 20; sm_dbg = 0;
        spr_log = 20; mhlog("sm debug: %d copie, livello %dx%d, %d buffer", n_brec, VW, VH, n_bmap);
        for (int j = 0; j < n_bmap; j++) mhlog("  buf %p -> %s", bmap[j].buf, bmap[j].k >= 0 ? hdi[bmap[j].k].name : "?");
        for (int n = 0; n < n_brec && n < 300; n++) { BlitRec *r = &brec[n];
            mhlog("  ra %06x/%06x src %p pitch %d s(%d,%d) %dx%d -> (%d,%d) clip %d,%d-%d,%d", r->ra, r->ra2, r->src, r->pitch, r->sx, r->sy, r->w, r->h, r->dx, r->dy, r->x0, r->y0, r->x1, r->y1); }
    }
    if (sm_dbg > 1) sm_dbg--;
    int changed = n_brec != n_bprev || memcmp(brec, bprev, n_brec*sizeof(BlitRec));
    if (!changed) { n_brec = 0; return 0; }
    if (hd_vw != VW || hd_vh != VH) { free(hd_tmap); free(hd_comp); free(hd_ref); hd_tmap = malloc(VW*VH); hd_comp = malloc((size_t)VW*VH*64); hd_ref = malloc((size_t)VW*VH*4); hd_vw = VW; hd_vh = VH; }
    memset(hd_tmap, 0, VW*VH); memset(hd_comp, 0, (size_t)VW*VH*64); memset(hd_ref, 0, (size_t)VW*VH*4);
    for (int n = 0; n < n_brec; n++) {
        BlitRec *r = &brec[n]; int k = -1; const BYTE *base = NULL;
        for (int j = 0; j < n_bmap; j++) if (bmap[j].k >= 0 && r->src >= bmap[j].buf && r->src < bmap[j].buf + hdi[bmap[j].k].w*hdi[bmap[j].k].h) { k = bmap[j].k; base = bmap[j].buf; break; }
        HdImg *m = k >= 0 ? &hdi[k] : NULL;
        if (m && r->pitch != m->w) m = NULL;
        int ox = 0, oy = 0;
        if (m) { int off = (int)(r->src - base); ox = off % m->w; oy = off / m->w; }
        if (m && VH == 240 && VW >= 320 && !ox && !oy && r->w == m->w && r->h == m->h && !r->sx && !r->sy) {
            const WideSpec *ws = VW > 320 ? wide_find(m) : NULL;
            if (ws) { sm_wide(r, m, ws, VW - 320, VW, VH); continue; }
            if (m->w == 220 && m->h == 160 && r->dx == 58 + (VW - 320)/2) { sm_mission(r, m, VW - 320, VW, VH); continue; }
            if (VW > 320 && sm_widew(r, m, VW - 320, VW, VH)) continue;
        }
        int xa = r->dx > r->x0 ? r->dx : r->x0, ya = r->dy > r->y0 ? r->dy : r->y0;
        int xb = r->dx + r->w < r->x1 ? r->dx + r->w : r->x1, yb = r->dy + r->h < r->y1 ? r->dy + r->h : r->y1;
        if (xa < 0) xa = 0; if (ya < 0) ya = 0; if (xb > VW) xb = VW; if (yb > VH) yb = VH;
        for (int y = ya; y < yb; y++) for (int x = xa; x < xb; x++) {
            int u = r->sx + ox + (x - r->dx), v = r->sy + oy + (y - r->dy);
            BYTE t = r->src[(size_t)(r->sy + (y - r->dy))*r->pitch + r->sx + (x - r->dx)];   /* indice originale copiato */
            if (!t) continue;                                                                 /* trasparente */
            if (!m || u < 0 || v < 0 || u >= m->w || v >= m->h) { hd_tmap[y*VW + x] = 0; continue; }
            hd_tmap[y*VW + x] = t;
            { BYTE fi = m->idx[0][v*m->w + u];   /* indice nel file: il gioco puo' aver rimappato quello in memoria */
              hd_ref[y*VW + x] = m->haspal ? (0xFF000000u | m->pal[fi*3] | m->pal[fi*3+1] << 8 | m->pal[fi*3+2] << 16) : 0; }
            const BYTE *srow = m->rgb[0] + ((size_t)(4*v)*m->w*4 + 4*u)*4;
            for (int j = 0; j < 4; j++) {
                DWORD *d = hd_comp + (size_t)(4*y + j)*VW*4 + 4*x; const BYTE *sp = srow + (size_t)j*m->w*16;
                for (int i = 0; i < 4; i++) {
                    const BYTE *c = sp + i*4; int a = c[3];
                    if (a == 255) { d[i] = 0xFF000000u | c[0] | c[1] << 8 | c[2] << 16; continue; }
                    DWORD o = d[i]; int oa = o >> 24, ob = oa*(255 - a)/255, na = a + ob;   /* sopra quello che c'era */
                    if (!na) { d[i] = 0; continue; }
                    int r = (c[0]*a + (int)(o & 255)*ob) / na, g = (c[1]*a + (int)(o >> 8 & 255)*ob) / na, b = (c[2]*a + (int)(o >> 16 & 255)*ob) / na;
                    d[i] = (DWORD)na << 24 | r | g << 8 | b << 16;
                }
            }
        }
    }
    memcpy(bprev, brec, n_brec*sizeof(BlitRec)); n_bprev = n_brec; n_brec = 0;
    return 1;
}
