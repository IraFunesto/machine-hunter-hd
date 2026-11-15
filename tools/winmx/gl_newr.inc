/* Render nuovo ([video] render=nuovo): i poligoni del fotogramma, gia' proiettati dal gioco (registrati dalle 12 routine finali
   del rasterizzatore, rz_record in ui_hd.inc), vengono disegnati dalla scheda video in un'immagine W x H a colori veri.
   Colore = tabella "true" della DLL (texel, riga luce) -> RGB, luce continua (Gouraud) tra due righe.
   Ordine di disegno = quello del gioco (dal lontano al vicino), niente depth buffer. Lo shader finale usa questa immagine al posto della scena 8 bit. */
#include "gl_png.inc"
static const char *FS_NR =
"uniform sampler2D tidx; uniform sampler2D truec; uniform float spr; uniform float solid; uniform float alpha;\n"
"uniform float rgbm; uniform sampler2D gainT; uniform sampler2D tind; uniform sampler2D srcpal; uniform float itsc; uniform float isz; uniform float fadeU;\n"   /* rgbm: tidx contiene il PNG a colori veri; gainT: guadagno luce (32 x 9, /4) */

/* fattore per un texel di indici: colore del gioco (tabella true, luce) / luminanza della palette d'origine del PNG */
"vec3 corr(vec2 q, float bb, float l0, float l1, float f){\n"
"  q = clamp(q, vec2(0.0), vec2(isz-1.0));\n"
"  float ti = floor(texture2D(tind, (q+0.5)/isz).r*255.0+0.5);\n"
"  vec3 tc = mix(texture2D(truec, vec2((ti+0.5)/256.0, (bb+l0+0.5)/288.0)).rgb, texture2D(truec, vec2((ti+0.5)/256.0, (bb+l1+0.5)/288.0)).rgb, f);\n"
"  float ls = dot(texture2D(srcpal, vec2((ti+0.5)/256.0, 0.5)).rgb, vec3(0.30, 0.59, 0.11));\n"
"  return tc / max(ls, 0.06);\n"
"}\n"
"void main(){\n"
"  vec2 uv0 = gl_TexCoord[0].xy / gl_TexCoord[0].w;\n"   /* prospettiva corretta */

"  if (solid > 0.5) { gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }\n"   /* 0x435e80: poligono pieno col colore 0 */

"  if (rgbm > 0.5) {\n"
"    vec4 c = texture2D(tidx, uv0); float a = spr > 0.5 ? c.a : 1.0;\n"
"    if (spr > 0.5 && a < 0.02) discard;\n"
"    vec3 sh = texture2D(tind, uv0).rgb*(255.0/64.0);\n"   /* fattore colore del gioco (0-4) */
"    float sb = floor(gl_TexCoord[1].y/32.0 + 0.5); float l = clamp(gl_TexCoord[1].x, 0.0, 31.0); float l0 = floor(l), f = l - l0, l1 = min(l0 + 1.0, 31.0);\n"
"    float g = mix(texture2D(gainT, vec2((l0+0.5)/32.0, (sb+0.5)/9.0)).r, texture2D(gainT, vec2((l1+0.5)/32.0, (sb+0.5)/9.0)).r, f)*4.0;\n"
"    gl_FragColor = vec4(clamp(sh*dot(c.rgb, vec3(0.30, 0.59, 0.11))*g*fadeU, 0.0, 1.0), a*alpha); return;\n"   /* colore del gioco x chiaroscuro del PNG */
"  }\n"
"  float t = floor(texture2D(tidx, uv0).r*255.0+0.5);\n"
"  if (spr > 0.5 && t < 0.5) discard;\n"
"  float base = gl_TexCoord[1].y; float l = clamp(gl_TexCoord[1].x, 0.0, 31.0);\n"
"  float l0 = floor(l), f = l - l0, l1 = min(l0 + 1.0, 31.0);\n"
"  vec3 c0 = texture2D(truec, vec2((t+0.5)/256.0, (base+l0+0.5)/288.0)).rgb;\n"
"  vec3 c1 = texture2D(truec, vec2((t+0.5)/256.0, (base+l1+0.5)/288.0)).rgb;\n"
"  gl_FragColor = vec4(mix(c0, c1, f), alpha);\n"
"}\n";
typedef void (APIENTRY *glMultiTexCoord2f_t)(GLenum, GLfloat, GLfloat); static glMultiTexCoord2f_t p_glMultiTexCoord2f;
static GLuint nr_prog, nr_fbo, nr_scene; static GLint nr_u_tidx, nr_u_true, nr_u_spr, nr_u_solid, nr_u_alpha, nr_u_rgbm, nr_u_gain, nr_u_tind, nr_u_srcpal, nr_u_itsc, nr_u_isz, nr_u_fade; static GLuint nr_gain_t; static int nr_gain_up = -1; static int nr_w, nr_h, nr_ok = -1;
typedef struct { DWORD ptr, chk[4]; GLuint t, rgb, ts; int pal, ver; } NrTex; static NrTex nr_tex[1024]; static int nr_ntex;
static int nr_spr_pal[256]; static GLuint nr_spr_t[256], nr_spr_rgb[256]; static int nr_nspr; static int cfg_newr_png = 1;
/* lato della texture letta da ogni routine (a 4x): mappa 256; sprite 6/9 = 64, 7/10 = 128, 8/11 = 256 (maschere 0xfff/0x3fff/0xffff) */
static int nr_side(int id) { return id == 6 || id == 9 ? 64 : id == 7 || id == 10 ? 128 : 256; }

static int nr_init(void)
{
    p_glMultiTexCoord2f = (glMultiTexCoord2f_t)glp("glMultiTexCoord2f"); if (!p_glMultiTexCoord2f) { mhlog("render nuovo: manca glMultiTexCoord2f"); return 0; }
    GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER); p_glShaderSource(fs, 1, &FS_NR, NULL); p_glCompileShader(fs);
    GLint ok = 0; p_glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { char logb[1024]; p_glGetShaderInfoLog(fs, 1024, NULL, logb); mhlog("render nuovo: shader: %s", logb); return 0; }
    nr_prog = p_glCreateProgram(); p_glAttachShader(nr_prog, fs); p_glLinkProgram(nr_prog);
    p_glGetProgramiv(nr_prog, GL_LINK_STATUS, &ok); if (!ok) { mhlog("render nuovo: link fallito"); return 0; }
    nr_u_tidx = p_glGetUniformLocation(nr_prog, "tidx"); nr_u_true = p_glGetUniformLocation(nr_prog, "truec"); nr_u_spr = p_glGetUniformLocation(nr_prog, "spr"); nr_u_solid = p_glGetUniformLocation(nr_prog, "solid"); nr_u_alpha = p_glGetUniformLocation(nr_prog, "alpha"); nr_u_rgbm = p_glGetUniformLocation(nr_prog, "rgbm"); nr_u_tind = p_glGetUniformLocation(nr_prog, "tind"); nr_u_srcpal = p_glGetUniformLocation(nr_prog, "srcpal"); nr_u_itsc = p_glGetUniformLocation(nr_prog, "itsc"); nr_u_isz = p_glGetUniformLocation(nr_prog, "isz"); nr_u_fade = p_glGetUniformLocation(nr_prog, "fadeU"); nr_u_gain = p_glGetUniformLocation(nr_prog, "gainT");
    glGenTextures(1, &nr_gain_t);
    glGenTextures(1, &nr_scene); p_glGenFramebuffers(1, &nr_fbo);
    mhlog("render nuovo: pronto");
    return 1;
}
static void nr_tex_params(void)
{
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
/* texture della mappa: una per indirizzo, ricaricata se il contenuto cambia (nuovo livello) */
static GLuint nr_map_tex(DWORD p)
{
    if (!p || IsBadReadPtr((void*)p, 65536)) return 0;
    const DWORD *d = (const DWORD*)p; DWORD c[4] = { d[0], d[4096], d[8192], d[16383] };
    for (int i = 0; i < nr_ntex; i++) if (nr_tex[i].ptr == p) {
        if (memcmp(nr_tex[i].chk, c, 16)) { glBindTexture(GL_TEXTURE_2D, nr_tex[i].t); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_LUMINANCE, GL_UNSIGNED_BYTE, (void*)p); memcpy(nr_tex[i].chk, c, 16);
                                            nr_tex[i].ver = -1; }
        if (cfg_newr_png && nr_tex[i].ver != png_ver) { nr_tex[i].rgb = png_tex((const BYTE*)p, 256, 1, &nr_tex[i].pal, &nr_tex[i].ts); nr_tex[i].ver = png_last_pending ? -2 : png_ver; }
        return nr_tex[i].t;
    }
    int i = nr_ntex < 1024 ? nr_ntex++ : (int)(p >> 6) % 1024;   /* tabella piena: rimpiazza */
    if (i == nr_ntex - 1) glGenTextures(1, &nr_tex[i].t);
    nr_tex[i].ptr = p; memcpy(nr_tex[i].chk, c, 16);
    glBindTexture(GL_TEXTURE_2D, nr_tex[i].t); nr_tex_params();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, 256, 256, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, (void*)p);
    nr_tex[i].rgb = cfg_newr_png ? png_tex((const BYTE*)p, 256, 1, &nr_tex[i].pal, &nr_tex[i].ts) : 0; nr_tex[i].ver = cfg_newr_png && png_last_pending ? -2 : png_ver;
    return nr_tex[i].t;
}
/* livello nuovo: tutte le texture della mappa del livello, non solo quelle gia' viste. Nel gioco sono blocchi da 64 KB uno dietro
   l'altro: attorno a ogni texture gia' vista si provano i blocchi a passi di 64 KB e si prendono quelli che l'indice PNG riconosce. */
static int nr_preload_map(void)
{
    if (png_ready != 1) return 0;
    DWORD seen[1024]; int ns = 0, n0 = nr_ntex;
    for (int i = 0; i < nr_ntex && ns < 1024; i++) if (nr_tex[i].ptr) seen[ns++] = nr_tex[i].ptr;
    static DWORD tried[8192]; int nt = 0, found = 0;
    for (int s = 0; s < ns; s++) for (int dir = -1; dir <= 1; dir += 2) {
        int miss = 0;
        for (int k = 1; k <= 160 && miss < 4; k++) {
            DWORD p = seen[s] + (DWORD)(dir * k) * 65536u; int dup = 0;
            for (int t = 0; t < nt; t++) if (tried[t] == p) { dup = 1; break; }
            if (dup) break;
            if (nt < 8192) tried[nt++] = p;
            if (IsBadReadPtr((void*)p, 65536)) break;
            const PngEnt *e = png_find(png_fp((const BYTE*)p, 65536));
            if (!e || e->kind != 0) { miss++; continue; }
            miss = 0; if (nr_ntex < 1000) { nr_map_tex(p); found++; }
        }
    }
    mhlog("attesa: texture mappa gia' viste %d, trovate in memoria altre %d", n0, found);
    return found;
}
/* disegna i poligoni registrati in nr_scene (W x H); ritorna 1 se fatto */
static GLuint nr_map_rgb(DWORD p, int *pal, GLuint *ts) { for (int i = 0; i < nr_ntex; i++) if (nr_tex[i].ptr == p) { *pal = nr_tex[i].pal; *ts = nr_tex[i].ts; return nr_tex[i].rgb; } return 0; }
static const BYTE *nr_pal_now, *nr_brt_now;   /* palette (con la tuta) e tabella luci del fotogramma, da gl_present */
static void nr_colors(void)
{
    if (!nr_pal_now || !nr_brt_now) return;
    unsigned hv = 2166136261u;   /* impronta della tabella luci, senza le colonne della tuta (riscritte dalla DLL) */
    for (int i = 0; i < 288*256; i += 37) { int c = i & 255; if ((c >= 33 && c <= 38) || c == 42) continue; hv = (hv ^ nr_brt_now[i]) * 16777619u; }
    static unsigned last_hv; static int first = 1;
    double m = 0; for (int c = 1; c < 255; c++) { const BYTE *q = nr_pal_now + c*4; m += 0.30*q[0] + 0.59*q[1] + 0.11*q[2]; } png_curmean = (float)(m / 254);
    if (first || hv != last_hv) {   /* livello nuovo: fotografa i colori a luce media (riga 16) */
        for (int t = 0; t < 256; t++) { const BYTE *q = nr_pal_now + nr_brt_now[16*256 + t]*4; for (int c = 0; c < 3; c++) png_cur[t][c] = q[c]; }
        /* normalizzati alla luminanza media attuale (se si entra durante una dissolvenza) */
        last_hv = hv; first = 0; png_ver++; mhlog("render nuovo: colori del livello aggiornati (%d)", png_ver);
        if (cfg_newr_png && cfg_attesa) nr_hold = 1;   /* livello nuovo: attesa con caricamento delle texture (winmx.c) */
        static float ref_mean; ref_mean = png_curmean;
        for (int t = 0; t < 256; t++) for (int c = 0; c < 3; c++) png_cur[t][c] = ref_mean > 1e-3f ? png_cur[t][c] / ref_mean : 0;
    }
}
static int nr_draw(int W, int H)
{
    if (nr_ok < 0) nr_ok = nr_init();
    if (!nr_ok || rz_n <= 0) return 0;
    nr_colors();
    png_pump(nr_hold != 0);
    if (nr_w != W || nr_h != H) {
        glBindTexture(GL_TEXTURE_2D, nr_scene); nr_tex_params();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        p_glBindFramebuffer(GL_FRAMEBUFFER, nr_fbo); p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, nr_scene, 0);
        if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { mhlog("render nuovo: FBO incompleto"); nr_ok = 0; p_glBindFramebuffer(GL_FRAMEBUFFER, 0); return 0; }
        nr_w = W; nr_h = H;
    }
    /* sprite nuovi (visti per la prima volta): texture di indici della loro dimensione + PNG */
    p_glActiveTexture(GL_TEXTURE0);
    for (int i = 0; i < rz_n; i++) {
        const int *r = (const int*)(rz_buf + (size_t)i*128); int k = r[28]; if (r[0] < 6 || k < 0 || k >= spr_nslot) continue;
        SprSlot *S = &spr_slot[k]; if (!S->pend) continue;
        if (!S->dirty && S->ver == png_ver) continue;
        if (!S->ti) { glGenTextures(1, &S->ti); glBindTexture(GL_TEXTURE_2D, S->ti); nr_tex_params(); glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, S->side, S->side, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL); }
        glBindTexture(GL_TEXTURE_2D, S->ti); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, S->side, S->side, GL_LUMINANCE, GL_UNSIGNED_BYTE, S->pend);
        { GLuint ts_ = 0; png_last_pending = 0; S->tr = cfg_newr_png ? png_tex(S->pend, S->side, 0, &S->pal, &ts_) : 0; S->ts = ts_; }
        S->dirty = 0; S->ver = png_last_pending ? -2 : png_ver;
    }
    p_glBindFramebuffer(GL_FRAMEBUFFER, nr_fbo);
    glViewport(0, 0, W, H); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, W, H, 0, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    p_glUseProgram(nr_prog);
    p_glActiveTexture(GL_TEXTURE0+5); glBindTexture(GL_TEXTURE_2D, t_true); p_glUniform1i(nr_u_true, 5);
    p_glActiveTexture(GL_TEXTURE0+13); glBindTexture(GL_TEXTURE_2D, nr_gain_t); p_glUniform1i(nr_u_gain, 13);
    if (nr_gain_up != nr_gain_ver) { BYTE gb[9*32*4]; for (int s_ = 0; s_ < 9; s_++) for (int l = 0; l < 32; l++) { float g = nr_gain[s_][l] / 4.0f * 255.0f; BYTE v = (BYTE)(g < 0 ? 0 : g > 255 ? 255 : g + 0.5f); gb[(s_*32+l)*4] = gb[(s_*32+l)*4+1] = gb[(s_*32+l)*4+2] = v; gb[(s_*32+l)*4+3] = 255; }
        nr_tex_params(); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32, 9, 0, GL_RGBA, GL_UNSIGNED_BYTE, gb); nr_gain_up = nr_gain_ver; }
    p_glActiveTexture(GL_TEXTURE0); p_glUniform1i(nr_u_tidx, 0); p_glUniform1i(nr_u_tind, 2); p_glUniform1i(nr_u_srcpal, 3);
    static const float su[4] = { 0, 63, 63, 0 }, sv[4] = { 0, 0, 63, 63 };
    float last_spr = -1, last_solid = -1, last_rgbm = -1; GLuint last_t = 0; int last_bl = -1;
    p_glUniform1f(nr_u_alpha, 1.0f);
    for (int i = 0; i < rz_n; i++) {
        const int *r = (const int*)(rz_buf + (size_t)i*128); int id = r[0], n = r[1] == 12 ? 3 : 4;
        int row0 = (int)((DWORD)r[4] - 0x6460d0) / 256; if (row0 < 0 || row0 >= 288) continue;
        float base = (float)(row0 / 32 * 32), lflat = (float)(row0 % 32);
        /* trasparenze del gioco: id 3 e 6-8 = somma dei colori (tabella 0x6360d0), id 4 = un pixel si e uno no (50%) */
        int bl = id == 3 || (id >= 6 && id <= 8) ? 1 : id == 4 ? 2 : 0;
        if (bl != last_bl) {
            glEnable(GL_BLEND); if (bl == 1) glBlendFunc(GL_SRC_ALPHA, GL_ONE); else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            p_glUniform1f(nr_u_alpha, bl == 2 ? 0.5f : 1.0f); last_bl = bl;
        }
        if (id == 3) { base = 0; lflat = 7; }   /* 0x432f10: riga di luce fissa 0x6467d0 */
        float solid = id == 5 ? 1.0f : 0.0f;
        if (solid != last_solid) { p_glUniform1f(nr_u_solid, solid); last_solid = solid; }
        if (id == 5) {   /* poligono pieno nero, r[6] vertici (al massimo 4 registrati) */
            int nv = r[6] < 3 ? 3 : r[6] > 4 ? 4 : r[6]; glBegin(GL_TRIANGLE_FAN);
            for (int k = 0; k < nv; k++) glVertex2f((float)r[8 + k*5], (float)r[9 + k*5]);
            glEnd(); continue;
        }
        const SprSlot *S = id >= 6 && r[28] >= 0 && r[28] < spr_nslot ? &spr_slot[r[28]] : NULL;
        GLuint t = id >= 6 ? (S ? S->ti : 0) : nr_map_tex((DWORD)r[5]);
        if (!t) continue;
        int tpal = 0; GLuint tsh = 0; GLuint trgb = id >= 6 ? (S ? (tpal = S->pal, tsh = S->ts, S->tr) : 0) : nr_map_rgb((DWORD)r[5], &tpal, &tsh);
        float tsc = 1.0f;   /* sprite: texture della sua dimensione */
        if (trgb) {   /* PNG: lo sprite ha la sua dimensione; indici (unita' 2) e palette d'origine (unita' 3) per i colori veri del livello */
            p_glActiveTexture(GL_TEXTURE0+2); glBindTexture(GL_TEXTURE_2D, tsh); p_glActiveTexture(GL_TEXTURE0);
            p_glUniform1f(nr_u_fade, png_palmean[tpal] > 1e-3f ? png_curmean / png_palmean[tpal] : 1.0f);
            p_glUniform1f(nr_u_itsc, 1.0f); p_glUniform1f(nr_u_isz, id >= 6 ? (float)S->side : 256.0f); t = trgb;
        }
        float rgbm = trgb ? 1.0f : 0.0f; if (rgbm != last_rgbm) { p_glUniform1f(nr_u_rgbm, rgbm); last_rgbm = rgbm; }
        float sp = (id >= 6 || (r[7] & 1)) ? 1.0f : 0.0f;   /* sprite e poligoni "a buchi" (B+4 bit0, come 0x62dbdc): texel 0 trasparente */
        glBindTexture(GL_TEXTURE_2D, t); last_t = t;   /* sempre: il caricamento di un PNG cambia la texture legata */
        if (sp != last_spr) { p_glUniform1f(nr_u_spr, sp); last_spr = sp; }
        float X[4], Y[4], U[4], V[4], Lt[4], Q[4];
        for (int k = 0; k < n; k++) {
            const int *v = r + 8 + k*5;
            X[k] = (float)v[0]; Y[k] = (float)v[1];
            U[k] = (id >= 6 ? su[k] : v[2] / 65536.0f) / 64.0f * tsc; V[k] = (id >= 6 ? sv[k] : v[3] / 65536.0f) / 64.0f * tsc;
            Lt[k] = id == 2 ? v[4] / 65536.0f : lflat;
            { DWORD zz = (DWORD)(k < 2 ? r[30] : r[31]); int z = (int)((k & 1) ? zz >> 16 : zz & 0xffff); Q[k] = 1.0f / (float)(z < 1 ? 1 : z); }
        }
        glBegin(GL_TRIANGLE_FAN);
        if (n == 4 && id == 2) {   /* quadrato con luce per vertice: punto centrale con la media, niente giuntura diagonale */
            glTexCoord4f((U[0]*Q[0]+U[1]*Q[1]+U[2]*Q[2]+U[3]*Q[3])/4, (V[0]*Q[0]+V[1]*Q[1]+V[2]*Q[2]+V[3]*Q[3])/4, 0, (Q[0]+Q[1]+Q[2]+Q[3])/4); p_glMultiTexCoord2f(GL_TEXTURE0+1, (Lt[0]+Lt[1]+Lt[2]+Lt[3])/4, base);
            glVertex2f((X[0]+X[1]+X[2]+X[3])/4, (Y[0]+Y[1]+Y[2]+Y[3])/4);
        }
        for (int k = 0; k <= n; k++) {
            int kk = k % n; if (k == n && !(n == 4 && id == 2)) break;
            glTexCoord4f(U[kk]*Q[kk], V[kk]*Q[kk], 0, Q[kk]); p_glMultiTexCoord2f(GL_TEXTURE0+1, Lt[kk], base);
            glVertex2f(X[kk], Y[kk]);
        }
        glEnd();
    }
    glDisable(GL_BLEND); p_glUseProgram(0);
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return 1;
}
