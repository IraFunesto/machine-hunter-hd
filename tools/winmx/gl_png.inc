/* Render nuovo, colori veri: texture e sprite dai PNG HD (TEXTURES_4X, SPRITES_4X) invece degli indici a 256 colori.
   DATA\HDPNG.idx (tools\png_index.py): impronta del blocco di indici -> file PNG. Caricamento con WIC (Windows), cache per impronta.
   Mappa: il blocco in memoria e' letto dal gioco come immagine 256x256 lineare; per le tessere da 16/32 (piu' tessere in un blocco)
   il PNG viene riordinato nello stesso modo (come img_to_blk di mh_tex.py). Alfa: mappa = texel != 0 (buchi), sprite = alfa del PNG. */
#include <wincodec.h>
static float png_cur[256][3], png_curmean = 1, png_palmean[64]; static int png_ver;
typedef struct { unsigned long long h; BYTE kind, k, pal; WORD frame; char name[13]; } PngEnt;
static BYTE *png_pals; static int png_npal; static GLuint png_pal_t[64];   /* palette con cui sono stati salvati i PNG */
static PngEnt *png_ent; static int png_n, png_cap_sz; static int *png_tab;   /* tabella hash aperta */
static unsigned long long png_w[65536]; static int png_ready = -1;
static IWICImagingFactory *png_wic;
typedef struct { unsigned long long h; GLuint t, ts; int side, pal, ver, st; } PngTex; static PngTex png_cache[4096]; static int png_ncache;

static void png_init(void)
{
    png_ready = 0;
    unsigned long long x = 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 65536; i++) { png_w[i] = x; x = x * 6364136223846793005ULL + 1442695040888963407ULL; }
    char path[MAX_PATH]; path_in_dir(path, "DATA\\HDPNG.idx"); FILE *f = fopen(path, "rb"); if (!f) { mhlog("render nuovo: manca DATA\\HDPNG.idx (texture a indici)"); return; }
    char mg[4]; DWORD n = 0, np = 0; if (fread(mg, 1, 4, f) != 4 || memcmp(mg, "MHI2", 4) || fread(&n, 4, 1, f) != 1 || fread(&np, 4, 1, f) != 1 || n > 100000 || np > 64) { fclose(f); mhlog("render nuovo: HDPNG.idx vecchio, rifallo con tools\\png_index.py"); return; }
    png_pals = malloc(np*768); if (!png_pals || fread(png_pals, 768, np, f) != np) { fclose(f); return; } png_npal = (int)np;
    png_ent = calloc(n, sizeof *png_ent); png_cap_sz = 1; while (png_cap_sz < (int)n*2) png_cap_sz <<= 1; png_tab = malloc(png_cap_sz * sizeof(int));
    if (!png_ent || !png_tab) { fclose(f); return; }
    memset(png_tab, 0xff, png_cap_sz * sizeof(int));
    for (DWORD i = 0; i < n; i++) {
        BYTE rec[24]; if (fread(rec, 1, 24, f) != 24) break;
        PngEnt *e = &png_ent[png_n]; memcpy(&e->h, rec, 8); e->kind = rec[8]; e->k = rec[9]; memcpy(&e->frame, rec + 10, 2); memcpy(e->name, rec + 12, 11); e->name[11] = 0; e->pal = rec[23];
        int s = (int)(e->h & (png_cap_sz - 1)); while (png_tab[s] >= 0) s = (s + 1) & (png_cap_sz - 1); png_tab[s] = png_n++;
    }
    fclose(f);
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&png_wic)) || !png_wic) { mhlog("render nuovo: WIC non disponibile"); return; }
    for (int i = 0; i < png_npal; i++) {   /* palette -> texture 256x1 */
        BYTE rgba[1024]; for (int c = 0; c < 256; c++) { rgba[c*4] = png_pals[i*768 + c*3]; rgba[c*4+1] = png_pals[i*768 + c*3+1]; rgba[c*4+2] = png_pals[i*768 + c*3+2]; rgba[c*4+3] = 255; }
        glGenTextures(1, &png_pal_t[i]); glBindTexture(GL_TEXTURE_2D, png_pal_t[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    for (int i = 0; i < png_npal; i++) { double m = 0; for (int c = 1; c < 255; c++) { const BYTE *q = png_pals + i*768 + c*3; m += 0.30*q[0] + 0.59*q[1] + 0.11*q[2]; } png_palmean[i] = (float)(m / 254); }
    png_ready = 1; mhlog("render nuovo: indice PNG con %d voci, %d palette", png_n, png_npal);
}
static unsigned long long png_fp(const BYTE *b, int n)
{
    unsigned long long h = 0; for (int i = 0; i < n; i++) h += (unsigned long long)b[i] * png_w[i];
    return h + (unsigned long long)n * 0x100000001B3ULL;
}
static const PngEnt *png_find(unsigned long long h)
{
    int s = (int)(h & (png_cap_sz - 1));
    while (png_tab[s] >= 0) { if (png_ent[png_tab[s]].h == h) return &png_ent[png_tab[s]]; s = (s + 1) & (png_cap_sz - 1); }
    return NULL;
}
/* PNG -> BGRA (w x h) con WIC (ogni thread usa la sua fabbrica) */
static BYTE *png_load(IWICImagingFactory *wic, const char *rel, int *pw, int *ph)
{
    char path[MAX_PATH]; path_in_dir(path, rel); WCHAR wp[MAX_PATH]; MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH);
    IWICBitmapDecoder *dec = NULL; IWICBitmapFrameDecode *fr = NULL; IWICFormatConverter *cv = NULL; BYTE *px = NULL;
    *pw = *ph = 0;
    if (!wic || FAILED(wic->lpVtbl->CreateDecoderFromFilename(wic, wp, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return NULL;
    UINT w = 0, h = 0;
    if (SUCCEEDED(dec->lpVtbl->GetFrame(dec, 0, &fr)) && SUCCEEDED(wic->lpVtbl->CreateFormatConverter(wic, &cv)) &&
        SUCCEEDED(cv->lpVtbl->Initialize(cv, (IWICBitmapSource*)fr, &GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom)) &&
        SUCCEEDED(cv->lpVtbl->GetSize(cv, &w, &h)) && w && h && w <= 1024 && h <= 1024) {
        px = malloc((size_t)w*h*4);
        if (px && FAILED(cv->lpVtbl->CopyPixels(cv, NULL, w*4, w*h*4, px))) { free(px); px = NULL; }
    }
    if (cv) cv->lpVtbl->Release(cv); if (fr) fr->lpVtbl->Release(fr); dec->lpVtbl->Release(dec);
    *pw = (int)w; *ph = (int)h; return px;
}
/* colori del gioco in questo livello (impostati da nr_draw a ogni fotogramma): png_cur = colore a luce media di ogni indice
   (palette attuale, tuta compresa), png_curmean = luminanza media; png_ver cambia col livello (tabella luci diversa). */
/* Lavoro per una texture: lettura del PNG + "fattore colore" (colore del gioco / luminanza d'origine, sfumato 5x5 bilaterale).
   E' la parte lenta (circa 10 ms l'una): la fanno thread separati, cosi' il gioco non scatta; finche' non e' pronta
   si vede la texture a indici. Il caricamento sulla scheda video lo fa il thread del gioco (png_pump). */
typedef struct PngJob { struct PngJob *next; unsigned long long h; int ver, side, is_map, k, pal, ok; char rel[80]; BYTE *blk, *out, *osh; float cur[256][3]; } PngJob;
static CRITICAL_SECTION png_cs; static HANDLE png_sem; static PngJob *png_qh, *png_qt, *png_dh; static int png_nthr;
static volatile LONG png_busy;   /* lavori in coda o in corso */
static int png_tot, png_done, png_last_pending;   /* avanzamento (thread del gioco) */
static void png_process(PngJob *j, IWICImagingFactory *wic)
{
    int side = j->side, is_map = j->is_map, w, hh; const BYTE *blk = j->blk;
    BYTE *px = png_load(wic, j->rel, &w, &hh);
    if (px && w == side && hh == side) {
        int n = side*side; BYTE *out = malloc((size_t)n*4), *osh = malloc((size_t)n*4); float *df = calloc((size_t)n*4, sizeof(float)), *tmp = calloc((size_t)n*4, sizeof(float));
        int *pos = malloc((size_t)n*sizeof(int));
        if (out && osh && df && tmp && pos) {
            int K = is_map ? (j->k ? j->k*4 : 256) : side, nt = side / K;
            for (int py = 0; py < side; py++) for (int px_ = 0; px_ < side; px_++)   /* posizione nel blocco in memoria (tessere 16/32 della mappa) */
                pos[py*side + px_] = is_map ? ((py / K)*nt + px_ / K)*K*K + (py % K)*K + (px_ % K) : py*side + px_;
            const BYTE *sp = png_pals + j->pal*768; float kn = png_palmean[j->pal];   /* png_cur e' normalizzato (media 1) */
            for (int i = 0; i < n; i++) {   /* per pixel: colore del gioco e luminanza del colore d'origine (indice 0 = trasparente, saltato) */
                BYTE ix = blk[pos[i]]; if (!ix) continue;
                for (int c = 0; c < 3; c++) df[i*4+c] = j->cur[ix][c]*kn;
                df[i*4+3] = 0.30f*sp[ix*3] + 0.59f*sp[ix*3+1] + 0.11f*sp[ix*3+2];
            }
            /* sfocatura 5x5 "intelligente": ogni pixel prende la correzione solo dai vicini il cui colore d'origine somiglia
               al suo colore nel PNG (niente aloni dove si toccano zone di colori diversi) */
            for (int y = 0; y < side; y++) for (int x = 0; x < side; x++) {
                const BYTE *pc = px + ((size_t)y*side + x)*4; float cr = pc[2], cg = pc[1], cb = pc[0], acc[4] = {0, 0, 0, 0};
                for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
                    int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= side || yy >= side) continue;
                    int jj = yy*side + xx; if (!blk[pos[jj]]) continue; const float *q = df + (size_t)jj*4;
                    const BYTE *o = sp + blk[pos[jj]]*3; float er = cr - o[0], eg = cg - o[1], eb = cb - o[2];
                    float wt = 1.0f / (1.0f + (er*er + eg*eg + eb*eb) / 900.0f); wt *= wt;
                    for (int c = 0; c < 4; c++) acc[c] += wt*q[c];
                }
                /* fattore colore = colore del gioco / luminanza d'origine (sfumati): il PNG da' solo luce e ombra */
                float *t_ = tmp + ((size_t)y*side + x)*4;
                for (int c = 0; c < 3; c++) t_[c] = 1.0f;
                if (acc[3] > 1e-3f) { float ls = acc[3]; for (int c = 0; c < 3; c++) t_[c] = acc[c] / (ls > 4.0f ? ls : 4.0f); }
                t_[3] = 0;
            }
            for (int i = 0; i < n; i++) {
                const BYTE *s_ = px + (size_t)i*4; BYTE *d = out + (size_t)pos[i]*4, *o = osh + (size_t)pos[i]*4; const float *q = tmp + (size_t)i*4;
                d[0] = s_[2]; d[1] = s_[1]; d[2] = s_[0]; d[3] = is_map ? (blk[pos[i]] ? 255 : 0) : s_[3];
                for (int c = 0; c < 3; c++) { float v = q[c]*64.0f; o[c] = (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f); }   /* fattore 0-4 */
                o[3] = 255;
            }
            j->out = out; j->osh = osh; out = osh = NULL; j->ok = 1;
        }
        free(out); free(osh); free(df); free(tmp); free(pos);
    }
    free(px);
}
static DWORD WINAPI png_worker(LPVOID unused)
{
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    IWICImagingFactory *wic = NULL;
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&wic))) wic = NULL;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        WaitForSingleObject(png_sem, INFINITE);
        EnterCriticalSection(&png_cs); PngJob *j = png_qh; if (j) { png_qh = j->next; if (!png_qh) png_qt = NULL; } LeaveCriticalSection(&png_cs);
        if (!j) continue;
        png_process(j, wic);
        EnterCriticalSection(&png_cs); j->next = png_dh; png_dh = j; LeaveCriticalSection(&png_cs);
    }
    return 0;
}
static void png_threads(void)
{
    if (png_nthr) return;
    InitializeCriticalSection(&png_cs); png_sem = CreateSemaphoreA(NULL, 0, 1 << 30, NULL);
    SYSTEM_INFO si; GetSystemInfo(&si); int n = (int)si.dwNumberOfProcessors - 1; if (n < 1) n = 1; if (n > 6) n = 6;
    for (int i = 0; i < n; i++) { HANDLE t = CreateThread(NULL, 0, png_worker, NULL, 0, NULL); if (t) { CloseHandle(t); png_nthr++; } }
    mhlog("render nuovo: %d thread per le texture PNG", png_nthr);
}
/* thread del gioco: carica sulla scheda video le texture pronte (al massimo ~6 ms per fotogramma, tutte durante l'attesa) */
static void png_pump(int all)
{
    if (!png_nthr) return;
    LARGE_INTEGER q0, q1, qf; QueryPerformanceCounter(&q0); QueryPerformanceFrequency(&qf);
    for (;;) {
        EnterCriticalSection(&png_cs); PngJob *j = png_dh; if (j) png_dh = j->next; LeaveCriticalSection(&png_cs);
        if (!j) break;
        GLuint t = 0, tsh = 0;
        if (j->ok && j->ver == png_ver) {
            GLuint tt[2]; glGenTextures(2, tt); t = tt[0]; tsh = tt[1];
            for (int k = 0; k < 2; k++) {
                glBindTexture(GL_TEXTURE_2D, tt[k]);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, j->side, j->side, 0, GL_RGBA, GL_UNSIGNED_BYTE, k ? j->osh : j->out);
            }
        }
        for (int i = 0; i < png_ncache; i++) if (png_cache[i].h == j->h && png_cache[i].ver == j->ver && png_cache[i].st == 1) {
            png_cache[i].t = t; png_cache[i].ts = tsh; png_cache[i].st = 0; t = tsh = 0; break; }
        if (t) { GLuint old[2] = { t, tsh }; glDeleteTextures(2, old); }
        free(j->blk); free(j->out); free(j->osh); free(j);
        InterlockedDecrement(&png_busy); png_done++;
        QueryPerformanceCounter(&q1); if (!all && (double)(q1.QuadPart - q0.QuadPart) / qf.QuadPart > 0.006) break;
    }
}
/* texture a colori veri per un blocco di indici (mappa: 256x256; sprite: side x side). 0 = non trovata o non ancora pronta
   (png_last_pending = 1: richiamare nei fotogrammi seguenti). *ts = texture del "fattore colore". */
static GLuint png_tex(const BYTE *blk, int side, int is_map, int *ppal, GLuint *ts)
{
    *ppal = 0; *ts = 0; png_last_pending = 0;
    if (png_ready < 0) png_init();
    if (png_ready != 1) return 0;
    unsigned long long h = png_fp(blk, side*side);
    for (int i = 0; i < png_ncache; i++) if (png_cache[i].h == h && png_cache[i].ver == png_ver) {
        if (png_cache[i].st == 1) { png_last_pending = 1; return 0; }
        *ppal = png_cache[i].pal; *ts = png_cache[i].ts; return png_cache[i].t; }
    const PngEnt *e = png_find(h);
    int pal = e && e->pal < png_npal ? e->pal : 0, st = 0;
    if (e && (e->kind == 0) == !!is_map) {
        png_threads();
        PngJob *j = calloc(1, sizeof *j); BYTE *b = malloc((size_t)side*side);
        if (j && b) {
            memcpy(b, blk, (size_t)side*side); j->blk = b; j->h = h; j->ver = png_ver; j->side = side; j->is_map = is_map; j->k = e->k; j->pal = pal;
            snprintf(j->rel, sizeof j->rel, "%s\\%s\\%03d.png", e->kind ? "SPRITES_4X" : "TEXTURES_4X", e->name, e->frame);
            memcpy(j->cur, png_cur, sizeof j->cur);
            InterlockedIncrement(&png_busy); png_tot++;
            EnterCriticalSection(&png_cs); if (png_qt) png_qt->next = j; else png_qh = j; png_qt = j; LeaveCriticalSection(&png_cs);
            ReleaseSemaphore(png_sem, 1, NULL);
            st = 1; png_last_pending = 1;
        } else { free(j); free(b); }
    }
    *ppal = pal;
    int slot = -1;
    for (int i = 0; i < png_ncache; i++) if (png_cache[i].h == h) { slot = i; if (png_cache[i].t) { GLuint old[2] = { png_cache[i].t, png_cache[i].ts }; glDeleteTextures(2, old); } break; }
    if (slot < 0 && png_ncache < 4096) slot = png_ncache++;
    if (slot < 0) for (int i = 0; i < png_ncache; i++) if (png_cache[i].ver != png_ver && png_cache[i].st == 0) {   /* piena: riusa una di un livello precedente */
        slot = i; if (png_cache[i].t) { GLuint old[2] = { png_cache[i].t, png_cache[i].ts }; glDeleteTextures(2, old); } break; }
    if (slot >= 0) { png_cache[slot].h = h; png_cache[slot].t = 0; png_cache[slot].ts = 0; png_cache[slot].side = side; png_cache[slot].pal = pal; png_cache[slot].ver = png_ver; png_cache[slot].st = st; }
    return 0;
}
