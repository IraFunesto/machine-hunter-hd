/* Presentazione su GPU (OpenGL 2): palette + anti-banding in uno shader, poi scaling lineare a schermo. */
#include <GL/gl.h>
typedef char GLchar; typedef ptrdiff_t GLsizeiptr;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_TEXTURE0 0x84C0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GLF(ret, name, args) typedef ret (APIENTRY *name##_t) args; static name##_t p_##name;
GLF(GLuint, glCreateShader, (GLenum))
GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
GLF(void, glCompileShader, (GLuint))
GLF(void, glGetShaderiv, (GLuint, GLenum, GLint*))
GLF(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
GLF(GLuint, glCreateProgram, (void))
GLF(void, glAttachShader, (GLuint, GLuint))
GLF(void, glLinkProgram, (GLuint))
GLF(void, glGetProgramiv, (GLuint, GLenum, GLint*))
GLF(void, glUseProgram, (GLuint))
GLF(GLint, glGetUniformLocation, (GLuint, const GLchar*))
GLF(void, glUniform1i, (GLint, GLint))
GLF(void, glUniform1f, (GLint, GLfloat))
GLF(void, glUniform2f, (GLint, GLfloat, GLfloat))
GLF(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLF(void, glActiveTexture, (GLenum))
GLF(void, glGenFramebuffers, (GLsizei, GLuint*))
GLF(void, glBindFramebuffer, (GLenum, GLuint))
GLF(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
GLF(GLenum, glCheckFramebufferStatus, (GLenum))
typedef BOOL (WINAPI *wglSwapIntervalEXT_t)(int);

static int gl_state = 0;          /* 0 da inizializzare, 1 pronto, -1 non disponibile */
static HGLRC gl_rc; static HDC gl_dc;
static GLuint t_idx, t_pal, t_fb, t_tex, t_lit, t_brt, t_true, t_ui, t_hd, t_tmap, t_href, t_ovl, t_uipal, fbo, prog; static int gl_uw, gl_uh;
static BYTE true_tab[288*256*4]; static BYTE pal_cache[1024];
static GLint u_uim, u_uimsz, u_uiadd, u_uiaddon; static GLuint t_uim, t_uiadd;
static GLint u_mapon, u_maprect, u_mapbg, u_mapl; static GLuint mw_tex, mw_fbo; static int mw_tw, mw_th;
static GLint u_newr, u_nscene, u_uipal, u_idx, u_pal, u_src, u_thr, u_rad, u_frame, u_on, u_dith, u_tex, u_lit, u_brt, u_rel, u_smm, u_true, u_ui, u_uion, u_uisz, u_uir, u_has3d, u_hs, u_hf, u_mf, u_hdon, u_hdt, u_tmap, u_osz, u_ooff, u_sm3, u_hdf, u_href, u_ovl, u_ovlrect, u_ovla;
static BYTE brt_cache[288*256];
static int gl_w, gl_h; static unsigned gl_frame;

static const char *FS =
"uniform sampler2D idx; uniform sampler2D pal; uniform vec2 src; uniform float thr; uniform float rad;\n"
"uniform float frame; uniform float on; uniform float dith;\n"
"uniform sampler2D tex; uniform sampler2D lit; uniform sampler2D brt; uniform float relight; uniform float smoothmode;\n"
"float u8(float v){ return floor(v*255.0+0.5); }\n"
"uniform sampler2D uipal;\n"
"vec3 palc(float i){ return texture2D(uipal, vec2((i+0.5)/256.0, 0.5)).rgb; }\n"   /* HUD/menu: palette originale */
"float brtv(float row, float t){ return u8(texture2D(brt, vec2((t+0.5)/256.0, (row+0.5)/288.0)).r); }\n"
"uniform sampler2D truec;\n"
"uniform sampler2D ui; uniform float uion; uniform vec2 uisz; uniform float uir; uniform float has3d;\n"
"uniform float hudscale; uniform float hudfilt; uniform float menufilt;\n"
"uniform float hdon; uniform sampler2D hdt; uniform sampler2D tmap; uniform float hdfade; uniform sampler2D href;\n"
"uniform vec2 outsz; uniform vec2 outoff; uniform float smooth3d;\n"
"uniform sampler2D ovl; uniform vec4 ovlrect; uniform float ovla;\n"
"uniform sampler2D uim; uniform vec2 uimsz;\n"
"uniform sampler2D uiadd; uniform float uiaddon;\n"   /* icone HUD additive */
"uniform float mapon; uniform vec4 maprect; uniform float mapbg; uniform sampler2D mapl;\n"   /* mappa HD */   /* tessere 8x8 del livello HUD con qualcosa disegnato (dilatate di 1) */
"float uidx(vec2 c){ if (c.x < 0.0 || c.y < 0.0 || c.x >= uisz.x || c.y >= uisz.y) return 0.0; return u8(texture2D(ui, (c+0.5)/uisz).r); }\n"
"vec4 uitap(vec2 c){ float i = uidx(c); return i > 0.0 ? vec4(palc(i), 1.0) : vec4(0.0); }\n"
"vec4 mapc(vec2 uv){ if (mapon < 0.5) return vec4(0.0); vec2 q = floor(uv); if (q.x < maprect.x || q.y < maprect.y || q.x > maprect.z || q.y > maprect.w || uidx(q) != mapbg) return vec4(0.0);\n"
"  vec4 L = texture2D(mapl, vec2(uv.x/uisz.x, 1.0 - uv.y/uisz.y)); return vec4(L.rgb + palc(mapbg)*(1.0 - L.a), 1.0); }\n"
"float tmv(vec2 q){ return (q.x >= 0.0 && q.y >= 0.0 && q.x < uisz.x && q.y < uisz.y) ? u8(texture2D(tmap, (q+0.5)/uisz).r) : 0.0; }\n"
"float hdok(vec2 q){ float e = tmv(q); if (e <= 0.0) return 0.0; float u = uidx(q); if (u == e) return 1.0;\n"
"  float fa = texture2D(href, (q+0.5)/uisz).a; if (fa > 0.25 && fa < 0.75) return 1.0;\n"
"  vec3 d = abs(palc(u) - palc(e)); return (u > 0.0 && max(d.r, max(d.g, d.b)) < 0.10) ? 1.0 : 0.0; }\n"
"vec4 hdcol(vec2 uv, vec2 q){ vec4 c4 = texture2D(hdt, uv/uisz); vec3 c = c4.rgb; vec4 r = texture2D(href, (q+0.5)/uisz);\n"
"  if (r.a < 0.45) return vec4(c*hdfade, c4.a); vec3 cur = palc(tmv(q)); return vec4(clamp(c * clamp(cur/max(r.rgb, vec3(0.03)), 0.0, 4.0), 0.0, 1.0), c4.a); }\n"
"vec4 cr(float x){ float x2 = x*x, x3 = x2*x; return vec4(-0.5*x3+x2-0.5*x, 1.5*x3-2.5*x2+1.0, -1.5*x3+2.0*x2+0.5*x, 0.5*x3-0.5*x2); }\n"
"vec4 uisample(vec2 uv, float mode){\n"
"  if (mode < 0.5) return uitap(floor(uv));\n"
"  vec2 b = floor(uv - 0.5); vec2 f = uv - 0.5 - b;\n"
"  if (mode > 2.5) { vec2 k = 1.0/max(fwidth(uv), vec2(1e-4)); f = clamp((f - 0.5)*k + 0.5, 0.0, 1.0); }\n"
"  if (mode < 1.5 || mode > 2.5) return mix(mix(uitap(b), uitap(b+vec2(1.0,0.0)), f.x), mix(uitap(b+vec2(0.0,1.0)), uitap(b+vec2(1.0,1.0)), f.x), f.y);\n"
"  vec4 wx = cr(f.x), wy = cr(f.y); vec4 s = vec4(0.0);\n"
"  for (int j = 0; j < 4; j++) { vec4 r = vec4(0.0);\n"
"    for (int i = 0; i < 4; i++) r += uitap(b + vec2(float(i)-1.0, float(j)-1.0)) * (i==0?wx.x:i==1?wx.y:i==2?wx.z:wx.w);\n"
"    s += r * (j==0?wy.x:j==1?wy.y:j==2?wy.z:wy.w); }\n"
"  return clamp(s, 0.0, 1.0);\n"
"}\n"
"vec3 tc(float row, float t){ return texture2D(truec, vec2((t+0.5)/256.0, (row+0.5)/288.0)).rgb; }\n"
"vec3 col(vec2 p){ p = clamp(p, vec2(0.0), src-1.0); float i = texture2D(idx, (floor(p)+0.5)/src).r;\n"
"  return texture2D(pal, vec2((i*255.0+0.5)/256.0, 0.5)).rgb; }\n"
"float h(vec2 p){ return fract(sin(dot(p, vec2(12.9898, 78.233)))*43758.5453); }\n"
"vec3 scene(vec2 p);\n"
"vec3 sceneS(vec2 p){ if (smooth3d < 0.5) return scene(p); vec2 q = p - 0.5; vec2 b = floor(q); vec2 f = q - b;\n"
"  return mix(mix(scene(b+vec2(0.5,0.5)), scene(b+vec2(1.5,0.5)), f.x), mix(scene(b+vec2(0.5,1.5)), scene(b+vec2(1.5,1.5)), f.x), f.y); }\n"
"void main0(){\n"
"  vec2 fc = gl_FragCoord.xy - outoff; vec2 p = vec2(fc.x, outsz.y - fc.y) * (src/outsz);\n"
"  if (uion > 0.5) {\n"
"    vec2 uv; vec4 o;\n"
"    if (has3d < 0.5) {\n"
"      float m = src.y/uisz.y; uv = (p - src*0.5)/m + uisz*0.5;\n"
"      { vec4 mc = mapc(uv); if (mc.a > 0.5) { gl_FragColor = vec4(mc.rgb, 1.0); return; } }\n"
"      if (hdon > 0.5) { vec2 q = floor(uv); if (hdok(q) > 0.5) { vec4 hc = hdcol(uv, q); gl_FragColor = vec4(hc.rgb*hc.a, 1.0); return; } }\n"
"      o = uisample(uv, menufilt); gl_FragColor = vec4(o.rgb, 1.0); return;\n"
"    }\n"
"    uv = p * (uisz/src);\n"
"    { vec4 mc = mapc(uv); if (mc.a > 0.5) { gl_FragColor = vec4(mc.rgb, 1.0); return; } }\n"
"    if (texture2D(uim, (floor(uv/8.0)+0.5)/uimsz).r < 0.5) { gl_FragColor = vec4(sceneS(p), 1.0); return; }\n"   /* niente HUD qui: solo la scena */
"    if (hdon > 0.5) { vec2 q = floor(uv); if (hdok(q) > 0.5) { vec4 hc = hdcol(uv, q); gl_FragColor = vec4(hc.a > 0.999 ? hc.rgb : mix(sceneS(p), hc.rgb, hc.a), 1.0); return; } }\n"
"    o = uisample(uv, hudfilt);\n"
"    if (uiaddon > 0.5 && texture2D(uiadd, (floor(uv)+0.5)/uisz).r > 0.5) { gl_FragColor = vec4(min(sceneS(p) + o.rgb, vec3(1.0)), 1.0); return; }\n"
"    if (o.a > 0.999) { gl_FragColor = vec4(o.rgb, 1.0); return; }\n"
"    gl_FragColor = vec4(o.rgb + sceneS(p)*(1.0 - o.a), 1.0); return;\n"
"  }\n"
"  gl_FragColor = vec4(sceneS(p), 1.0);\n"
"}\n"
"void main(){\n"
"  main0();\n"
"  if (ovla > 0.0) {   /* modalita' foto: immagine originale della missione sovrapposta nel riquadro del ritaglio */\n"
"    vec2 fc = gl_FragCoord.xy - outoff; vec2 p = vec2(fc.x, outsz.y - fc.y) * (src/outsz);\n"
"    vec2 e = 2.0*src/outsz;\n"
"    if (p.x >= ovlrect.x && p.y >= ovlrect.y && p.x < ovlrect.z && p.y < ovlrect.w) {\n"
"      vec2 uv = (p - ovlrect.xy)/(ovlrect.zw - ovlrect.xy); gl_FragColor.rgb = mix(gl_FragColor.rgb, texture2D(ovl, uv).rgb, ovla);\n"
"      if (p.x < ovlrect.x + e.x || p.y < ovlrect.y + e.y || p.x >= ovlrect.z - e.x || p.y >= ovlrect.w - e.y) gl_FragColor.rgb = vec3(1.0, 0.85, 0.2);\n"
"    } }\n"
"}\n"
"uniform float newr; uniform sampler2D nscene;\n"
"vec3 scene(vec2 p){\n"
"  if (newr > 0.5) return texture2D(nscene, vec2((floor(p.x)+0.5)/src.x, 1.0 - (floor(p.y)+0.5)/src.y)).rgb;   /* render nuovo */\n"
"  if (relight > 0.5) {\n"
"    vec2 uv = (floor(p)+0.5)/src; float i = u8(texture2D(idx, uv).r); float t = u8(texture2D(tex, uv).r);\n"
"    vec4 L = texture2D(lit, uv); float row = u8(L.b); float fr = u8(L.g)/256.0; float r2 = -1.0;\n"
"    if (brtv(row, t) == i) r2 = row; else { float rr = mod(row, 32.0); if (brtv(rr, t) == i) r2 = rr; }\n"
"    if (r2 >= 0.0) {\n"
"      float lev = mod(r2, 32.0); float base = r2 - lev;\n"
"      float x = fr, x2 = x*x, x3 = x2*x;\n"
"      vec4 w = vec4((1.0-3.0*x+3.0*x2-x3), (4.0-6.0*x2+3.0*x3), (1.0+3.0*x+3.0*x2-3.0*x3), x3)/6.0;\n"
"      vec3 cm = tc(base + max(lev-1.0, 0.0), t), c0 = tc(r2, t);\n"
"      vec3 c1 = tc(base + min(lev+1.0, 31.0), t), c2 = tc(base + min(lev+2.0, 31.0), t);\n"
"      vec3 c = cm*w.x + c0*w.y + c1*w.z + c2*w.w;\n"
"      if (smoothmode < 0.5) c = mix(c0, c1, fr);\n"
"      c += (h(p*0.73 + frame*0.013) - 0.5)/255.0;\n"
"      return c;\n"
"    }\n"
"  }\n"
"  vec3 c = col(p); vec3 s = c; float n = 1.0;\n"
"  if (on > 0.5) {\n"
"    float a0 = h(p + frame*0.0)*6.2831;\n"
"    for (int k = 0; k < 16; k++) {\n"
"      float a = a0 + float(k)*2.3999; float r = rad*sqrt((float(k)+0.5)/16.0);\n"
"      vec3 q = col(p + r*vec2(cos(a), sin(a)));\n"
"      vec3 d = abs(q - c);\n"
"      if (max(d.r, max(d.g, d.b)) < thr) { s += q; n += 1.0; }\n"
"    }\n"
"    c = s/n;\n"
"    c += (h(p*1.37 + 0.5) - 0.5)*dith/255.0;\n"
"  }\n"
"  return c;\n"
"}\n";


static int cmp_f(const void *a, const void *b) { float x = *(const float*)a, y = *(const float*)b; return x < y ? -1 : x > y; }
static float nr_gain[9][32]; static int nr_gain_ver;   /* guadagno della luce per set/livello (render nuovo a colori veri) */
static void build_true_table(const BYTE *brtb, const BYTE *palrgba)
{
    static float ratios[256];
    for (int s = 0; s < 9; s++) {
        const BYTE *ref = brtb + (s*32 + 16)*256;
        float gain[32];
        for (int l = 0; l < 32; l++) {
            const BYTE *row = brtb + (s*32 + l)*256; int n = 0;
            for (int t = 1; t < 256; t++) {
                const BYTE *a = palrgba + ref[t]*4, *b = palrgba + row[t]*4;
                float la = 0.30f*a[0] + 0.59f*a[1] + 0.11f*a[2], lb = 0.30f*b[0] + 0.59f*b[1] + 0.11f*b[2];
                if (la > 24.0f) ratios[n++] = lb / la;
            }
            if (n) { qsort(ratios, n, sizeof(float), cmp_f); gain[l] = ratios[n/2]; } else gain[l] = l/16.0f;
        }
        for (int l = 1; l < 32; l++) if (l <= 16 && gain[l] < gain[l-1]) gain[l] = gain[l-1];   /* monotona */
        for (int l = 0; l < 32; l++) nr_gain[s][l] = gain[l]; nr_gain_ver++;
        for (int l = 0; l < 32; l++) {
            const BYTE *row = brtb + (s*32 + l)*256; BYTE *out = true_tab + (s*32 + l)*256*4;
            for (int t = 0; t < 256; t++) {
                const BYTE *a = palrgba + ref[t]*4, *p = palrgba + row[t]*4;
                for (int c = 0; c < 3; c++) {
                    float v = a[c]*gain[l];
                    if (l > 16) v = 0.5f*v + 0.5f*p[c];          /* sovraesposizione: mezza via tra modello e colore del gioco */
                    out[t*4+c] = (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
                }
                out[t*4+3] = 255;
            }
        }
    }
}

static void save_shot(int cw, int ch, int n)
{
    int w = cw, h = ch; size_t stride = ((size_t)w*3 + 3) & ~3; BYTE *px = malloc(stride*h); if (!px) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 4); glReadPixels(0, 0, w, h, 0x80E0 /*GL_BGR*/, GL_UNSIGNED_BYTE, px);
    char name[64], path[MAX_PATH]; sprintf(name, "MHSHOT_%d.BMP", n); path_in_dir(path, name);
    FILE *f = fopen(path, "wb");
    if (f) {
        BITMAPFILEHEADER bf = {0}; BITMAPINFOHEADER bh = {0};
        bf.bfType = 0x4D42; bf.bfOffBits = sizeof(bf) + sizeof(bh); bf.bfSize = bf.bfOffBits + (DWORD)(stride*h);
        bh.biSize = sizeof(bh); bh.biWidth = w; bh.biHeight = h; bh.biPlanes = 1; bh.biBitCount = 24;
        fwrite(&bf, sizeof(bf), 1, f); fwrite(&bh, sizeof(bh), 1, f); fwrite(px, stride, h, f); fclose(f);
        mhlog("debug: salvato %s", name);
    }
    free(px);
}
static void *glp(const char *n) { void *p = (void*)wglGetProcAddress(n); return p; }
/* scritta dell'attesa a inizio livello: testo disegnato con GDI in una texture (bianco su nero -> alfa) */
static GLuint ho_tex[2]; static char ho_str[2][64]; static int ho_w[2], ho_h[2], ho_px;
static void ho_text(int k, const char *s, int px)
{
    if (ho_tex[k] && !strcmp(ho_str[k], s) && ho_px == px) return;
    int W = 2048, H = px * 2; BITMAPINFO bi; memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H; bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32;
    void *bits = NULL; HDC mdc = CreateCompatibleDC(NULL); HBITMAP bm = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bm || !bits) { if (bm) DeleteObject(bm); DeleteDC(mdc); return; }
    HGDIOBJ ob = SelectObject(mdc, bm);
    HFONT f = CreateFontA(-px, 0, 0, 0, FW_BOLD, 0, 0, 0, ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, "Arial");
    HGDIOBJ of = SelectObject(mdc, f); memset(bits, 0, (size_t)W*H*4);
    SetBkMode(mdc, TRANSPARENT); SetTextColor(mdc, RGB(255, 255, 255));
    SIZE sz; GetTextExtentPoint32A(mdc, s, (int)strlen(s), &sz); TextOutA(mdc, 0, 0, s, (int)strlen(s)); GdiFlush();
    int tw = sz.cx + 2 < W ? sz.cx + 2 : W, th = sz.cy < H ? sz.cy : H;
    BYTE *rgba = malloc((size_t)tw*th*4);
    if (rgba) {
        for (int y = 0; y < th; y++) for (int x = 0; x < tw; x++) { BYTE a = ((BYTE*)bits)[((size_t)y*W + x)*4 + 1]; BYTE *d = rgba + ((size_t)y*tw + x)*4; d[0] = 255; d[1] = 214; d[2] = 140; d[3] = a; }
        if (!ho_tex[k]) glGenTextures(1, &ho_tex[k]);
        glBindTexture(GL_TEXTURE_2D, ho_tex[k]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, tw, th, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba); glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        free(rgba); ho_w[k] = tw; ho_h[k] = th; strcpy(ho_str[k], s);
    }
    SelectObject(mdc, of); DeleteObject(f); SelectObject(mdc, ob); DeleteObject(bm); DeleteDC(mdc);
}
static void ho_quad(float x0, float y0, float x1, float y1) { glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(x0, y0); glTexCoord2f(1, 0); glVertex2f(x1, y0); glTexCoord2f(1, 1); glVertex2f(x1, y1); glTexCoord2f(0, 1); glVertex2f(x0, y1); glEnd(); }
static void hold_overlay(int cw, int ch)
{
    int px = ch / 22; if (px < 14) px = 14; if (ho_px != px) { ho_str[0][0] = ho_str[1][0] = 0; }
    char s[64]; sprintf(s, "CARICAMENTO TEXTURE HD  %d%%", nr_hold_pct);
    ho_text(0, s, px); ho_text(1, "PREMI  X  PER INIZIARE", px); ho_px = px;
    p_glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, cw, ch); glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, cw, ch, 0, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_TEXTURE_2D);
    glColor4f(0, 0, 0, 0.6f); ho_quad(0, 0, (float)cw, (float)ch);   /* scena scurita dietro */
    float cy = ch * 0.5f, bw = cw * 0.36f > ch * 0.9f ? ch * 0.9f : cw * 0.36f, bh = px * 0.7f;
    glColor4f(0.35f, 0.3f, 0.25f, 1); ho_quad(cw/2.0f - bw/2 - 3, cy - 3, cw/2.0f + bw/2 + 3, cy + bh + 3);   /* barra */
    glColor4f(0.05f, 0.05f, 0.05f, 1); ho_quad(cw/2.0f - bw/2, cy, cw/2.0f + bw/2, cy + bh);
    glColor4f(1.0f, 0.62f, 0.2f, 1); ho_quad(cw/2.0f - bw/2, cy, cw/2.0f - bw/2 + bw * nr_hold_pct / 100.0f, cy + bh);
    glEnable(GL_TEXTURE_2D); glColor4f(1, 1, 1, 1);
    if (ho_tex[0]) { glBindTexture(GL_TEXTURE_2D, ho_tex[0]); float x0 = (cw - ho_w[0]) / 2.0f, y0 = cy - px * 1.9f; ho_quad(x0, y0, x0 + ho_w[0], y0 + ho_h[0]); }
    if (nr_hold_ready && ho_tex[1]) {
        float a = 0.6f + 0.4f * sinf(GetTickCount() / 180.0f); glColor4f(1, 1, 1, a);
        glBindTexture(GL_TEXTURE_2D, ho_tex[1]); float x0 = (cw - ho_w[1]) / 2.0f, y0 = cy + bh + px * 0.9f; ho_quad(x0, y0, x0 + ho_w[1], y0 + ho_h[1]);
    }
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glColor4f(1, 1, 1, 1);
}
static int gl_init(HDC dc)
{
    PIXELFORMATDESCRIPTOR pfd; memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1; pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA; pfd.cColorBits = 32; pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) { mhlog("OpenGL: SetPixelFormat fallito (%lu)", GetLastError()); return 0; }
    gl_rc = wglCreateContext(dc); if (!gl_rc || !wglMakeCurrent(dc, gl_rc)) { mhlog("OpenGL: contesto non creato"); return 0; }
    gl_dc = dc;
#define L(n) p_##n = (n##_t)glp(#n); if (!p_##n) { mhlog("OpenGL: manca " #n); return 0; }
    L(glCreateShader) L(glShaderSource) L(glCompileShader) L(glGetShaderiv) L(glGetShaderInfoLog) L(glCreateProgram)
    L(glAttachShader) L(glLinkProgram) L(glGetProgramiv) L(glUseProgram) L(glGetUniformLocation) L(glUniform1i)
    L(glUniform1f) L(glUniform2f) L(glUniform4f) L(glActiveTexture) L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D)
    L(glCheckFramebufferStatus)
#undef L
    wglSwapIntervalEXT_t swi = (wglSwapIntervalEXT_t)glp("wglSwapIntervalEXT"); if (swi) swi(cfg_vsync);
    GLuint fs = p_glCreateShader(GL_FRAGMENT_SHADER); p_glShaderSource(fs, 1, &FS, NULL); p_glCompileShader(fs);
    GLint ok = 0; p_glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { char logb[1024]; p_glGetShaderInfoLog(fs, 1024, NULL, logb); mhlog("OpenGL: shader: %s", logb); return 0; }
    prog = p_glCreateProgram(); p_glAttachShader(prog, fs); p_glLinkProgram(prog);
    p_glGetProgramiv(prog, GL_LINK_STATUS, &ok); if (!ok) { mhlog("OpenGL: link fallito"); return 0; }
    u_idx = p_glGetUniformLocation(prog, "idx"); u_pal = p_glGetUniformLocation(prog, "pal"); u_uipal = p_glGetUniformLocation(prog, "uipal"); u_newr = p_glGetUniformLocation(prog, "newr"); u_nscene = p_glGetUniformLocation(prog, "nscene"); u_src = p_glGetUniformLocation(prog, "src");
    u_thr = p_glGetUniformLocation(prog, "thr"); u_rad = p_glGetUniformLocation(prog, "rad"); u_frame = p_glGetUniformLocation(prog, "frame");
    u_on = p_glGetUniformLocation(prog, "on"); u_dith = p_glGetUniformLocation(prog, "dith");
    u_tex = p_glGetUniformLocation(prog, "tex"); u_lit = p_glGetUniformLocation(prog, "lit");
    u_brt = p_glGetUniformLocation(prog, "brt"); u_rel = p_glGetUniformLocation(prog, "relight");
    u_smm = p_glGetUniformLocation(prog, "smoothmode"); u_true = p_glGetUniformLocation(prog, "truec");
    u_ui = p_glGetUniformLocation(prog, "ui"); u_uion = p_glGetUniformLocation(prog, "uion"); u_uisz = p_glGetUniformLocation(prog, "uisz");
    u_uir = p_glGetUniformLocation(prog, "uir"); u_has3d = p_glGetUniformLocation(prog, "has3d");
    u_osz = p_glGetUniformLocation(prog, "outsz"); u_ooff = p_glGetUniformLocation(prog, "outoff"); u_sm3 = p_glGetUniformLocation(prog, "smooth3d");
    u_hdf = p_glGetUniformLocation(prog, "hdfade"); u_href = p_glGetUniformLocation(prog, "href");
    u_ovl = p_glGetUniformLocation(prog, "ovl"); u_ovlrect = p_glGetUniformLocation(prog, "ovlrect"); u_ovla = p_glGetUniformLocation(prog, "ovla");
    u_hdon = p_glGetUniformLocation(prog, "hdon"); u_hdt = p_glGetUniformLocation(prog, "hdt"); u_tmap = p_glGetUniformLocation(prog, "tmap");
    u_mapon = p_glGetUniformLocation(prog, "mapon"); u_maprect = p_glGetUniformLocation(prog, "maprect"); u_mapbg = p_glGetUniformLocation(prog, "mapbg"); u_mapl = p_glGetUniformLocation(prog, "mapl");
    u_uiadd = p_glGetUniformLocation(prog, "uiadd"); u_uiaddon = p_glGetUniformLocation(prog, "uiaddon");
    u_uim = p_glGetUniformLocation(prog, "uim"); u_uimsz = p_glGetUniformLocation(prog, "uimsz");
    u_hs = p_glGetUniformLocation(prog, "hudscale"); u_hf = p_glGetUniformLocation(prog, "hudfilt"); u_mf = p_glGetUniformLocation(prog, "menufilt");
    GLuint t[13]; glGenTextures(13, t); t_href = t[10]; t_ovl = t[11]; t_uipal = t[12]; t_idx = t[0]; t_pal = t[1]; t_fb = t[2]; t_tex = t[3]; t_lit = t[4]; t_brt = t[5]; t_true = t[6]; t_ui = t[7]; t_hd = t[8]; t_tmap = t[9];
    p_glGenFramebuffers(1, &fbo);
    mhlog("OpenGL pronto: %s / %s", (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));
    return 1;
}
static void gl_setup_tex(GLuint t, int filter, int w, int h, GLenum fmt, GLenum ifmt)
{
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, NULL);
}
#include "gl_newr.inc"
static int gl_present(HDC dc, HWND hw, const BYTE *bits, const RGBQUAD *palq, int W, int H, const BYTE *texb, const DWORD *litb, const BYTE *brtb,
                      const BYTE *uib, int UW, int UH, int has3d, int hd_state)
{
    if (gl_state == 0) gl_state = gl_init(dc) ? 1 : -1;
    if (gl_state != 1) return 0;
    if (dc != gl_dc) { if (!wglMakeCurrent(dc, gl_rc)) return 0; gl_dc = dc; }
    if (gl_w != W || gl_h != H) {
        gl_setup_tex(t_idx, GL_NEAREST, W, H, GL_LUMINANCE, GL_LUMINANCE8);
        gl_setup_tex(t_pal, GL_NEAREST, 256, 1, GL_RGBA, GL_RGBA8); gl_setup_tex(t_uipal, GL_NEAREST, 256, 1, GL_RGBA, GL_RGBA8);
        gl_setup_tex(t_fb, GL_LINEAR, W, H, GL_RGBA, GL_RGBA8);
        gl_setup_tex(t_tex, GL_NEAREST, W, H, GL_LUMINANCE, GL_LUMINANCE8);
        gl_setup_tex(t_lit, GL_NEAREST, W, H, GL_RGBA, GL_RGBA8);
        gl_setup_tex(t_brt, GL_NEAREST, 256, 288, GL_LUMINANCE, GL_LUMINANCE8); memset(brt_cache, 0xAB, sizeof(brt_cache));
        gl_setup_tex(t_true, GL_NEAREST, 256, 288, GL_RGBA, GL_RGBA8); memset(pal_cache, 0xAB, sizeof(pal_cache));
        p_glBindFramebuffer(GL_FRAMEBUFFER, fbo); p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t_fb, 0);
        if (p_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { mhlog("OpenGL: FBO incompleto"); gl_state = -1; return 0; }
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0); gl_w = W; gl_h = H;
    }
    /* carica indici e palette (RGBQUAD e' BGRA) */
    static BYTE palrgba[1024];
    for (int i = 0; i < 256; i++) { palrgba[i*4] = palq[i].rgbRed; palrgba[i*4+1] = palq[i].rgbGreen; palrgba[i*4+2] = palq[i].rgbBlue; palrgba[i*4+3] = 255; }
    static BYTE palui[1024]; memcpy(palui, palrgba, 1024);   /* HUD e menu: palette originale, senza i colori della tuta */
    /* tuta del giocatore: colori degli indici liberi (suit_out) per luminosita'; seguono le dissolvenze dell'arancione (198) */
    static float o_ref; float fade = 1.0f;
    { const BYTE *o = palrgba + 198*4; float vo = o[0] > o[1] ? (o[0] > o[2] ? o[0] : o[2]) : (o[1] > o[2] ? o[1] : o[2]); if (vo > o_ref) o_ref = vo; if (o_ref > 0) fade = vo / o_ref; }
    for (int m = 0; m < suit_n && cfg_player_hue >= 0; m++) {
        BYTE *d = palrgba + suit_out[m]*4; float r, g, bl, vv = suit_v[m];
        if (cfg_player_hue == 1000) {   /* viola della versione PS1 (dump DuckStation dello sprite del giocatore) */
            static const float pv[4] = { 0.26f, 0.45f, 0.61f, 0.71f }, pc[4][3] = { {66,0,66}, {115,0,115}, {156,41,148}, {181,74,173} };
            float v = vv > 0.71f ? 0.71f : vv; int i = v < pv[1] ? 0 : v < pv[2] ? 1 : 2;   /* non oltre il viola piu' chiaro della PS1 (prima: puntini quasi bianchi) */
            if (v < pv[0]) { float f = v / pv[0]; r = pc[0][0]*f; g = pc[0][1]*f; bl = pc[0][2]*f; }
            else { float f = (v - pv[i]) / (pv[i+1] - pv[i]); r = pc[i][0] + (pc[i+1][0]-pc[i][0])*f; g = pc[i][1] + (pc[i+1][1]-pc[i][1])*f; bl = pc[i][2] + (pc[i+1][2]-pc[i][2])*f; }
        } else {
            float mx = vv * 255.0f, sat = 0.85f, h = cfg_player_hue / 60.0f, f = h - (int)h, p = mx*(1 - sat), q = mx*(1 - sat*f), t = mx*(1 - sat*(1 - f));
            switch ((int)h % 6) { case 0: r = mx; g = t; bl = p; break; case 1: r = q; g = mx; bl = p; break; case 2: r = p; g = mx; bl = t; break;
                                  case 3: r = p; g = q; bl = mx; break; case 4: r = t; g = p; bl = mx; break; default: r = mx; g = p; bl = q; }
        }
        r *= fade; g *= fade; bl *= fade; if (r > 255) r = 255; if (g > 255) g = 255; if (bl > 255) bl = 255; if (r < 0) r = 0; if (g < 0) g = 0; if (bl < 0) bl = 0;
        d[0] = (BYTE)(r + 0.5f); d[1] = (BYTE)(g + 0.5f); d[2] = (BYTE)(bl + 0.5f);
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    /* render nuovo attivo: indici, texture e luce del vecchio render (3 immagini a schermo intero, ~20 MB a fotogramma) non servono */
    int skip8 = cfg_newr && has3d && nr_ok == 1 && rz_n > 0;
    p_glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t_idx);
    if (!skip8) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_LUMINANCE, GL_UNSIGNED_BYTE, bits);
    p_glActiveTexture(GL_TEXTURE0+1); glBindTexture(GL_TEXTURE_2D, t_pal);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, palrgba);
    p_glActiveTexture(GL_TEXTURE0+11); glBindTexture(GL_TEXTURE_2D, t_uipal);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, palui);
    if (uib) {
        p_glActiveTexture(GL_TEXTURE0+6);
        if (gl_uw != UW || gl_uh != UH) { gl_setup_tex(t_ui, GL_NEAREST, UW, UH, GL_LUMINANCE, GL_LUMINANCE8); gl_uw = UW; gl_uh = UH; }
        glBindTexture(GL_TEXTURE_2D, t_ui);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UW, UH, GL_LUMINANCE, GL_UNSIGNED_BYTE, uib);
        static int hd_w, hd_h;
        if (hd_state == 2) {   /* composito HD cambiato: ricarica */
            if (hd_w != UW || hd_h != UH) { gl_setup_tex(t_hd, GL_LINEAR, UW*4, UH*4, GL_RGBA, GL_RGBA8); gl_setup_tex(t_tmap, GL_NEAREST, UW, UH, GL_LUMINANCE, GL_LUMINANCE8); gl_setup_tex(t_href, GL_NEAREST, UW, UH, GL_RGBA, GL_RGBA8); hd_w = UW; hd_h = UH; }
            p_glActiveTexture(GL_TEXTURE0+7); glBindTexture(GL_TEXTURE_2D, t_hd);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UW*4, UH*4, GL_RGBA, GL_UNSIGNED_BYTE, hd_comp);
            p_glActiveTexture(GL_TEXTURE0+8); glBindTexture(GL_TEXTURE_2D, t_tmap);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UW, UH, GL_LUMINANCE, GL_UNSIGNED_BYTE, hd_tmap);
            p_glActiveTexture(GL_TEXTURE0+9); glBindTexture(GL_TEXTURE_2D, t_href);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, UW, UH, GL_RGBA, GL_UNSIGNED_BYTE, hd_ref);
        }
    }
    int uim_w = 1, uim_h = 1;
    {   /* maschera HUD: tessera 8x8 accesa se c'e' qualcosa (indice o immagine HD), allargata di una tessera */
        static BYTE *raw, *msk; static int cap; static BYTE one = 255;
        if (!t_uim) { glGenTextures(1, &t_uim); }
        p_glActiveTexture(GL_TEXTURE0+14); glBindTexture(GL_TEXTURE_2D, t_uim);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (uib && UW > 0 && UH > 0) {
            int MW = (UW + 7) / 8, MH = (UH + 7) / 8;
            if (MW*MH > cap) { free(raw); free(msk); cap = MW*MH; raw = malloc(cap); msk = malloc(cap); }
            if (raw && msk) {
                memset(raw, 0, (size_t)MW*MH);
                const BYTE *tm = (hd_state && hd_tmap && hd_vw == UW && hd_vh == UH) ? hd_tmap : NULL;
                for (int y = 0; y < UH; y++) {
                    const BYTE *a = uib + (size_t)y*UW, *b = tm ? tm + (size_t)y*UW : NULL; BYTE *rr = raw + (size_t)(y >> 3)*MW;
                    for (int x = 0; x < UW; x++) if (a[x] || (b && b[x])) rr[x >> 3] = 1;
                }
                for (int ty = 0; ty < MH; ty++) for (int tx = 0; tx < MW; tx++) {
                    int on = 0; for (int dy = -1; dy <= 1 && !on; dy++) for (int dx = -1; dx <= 1; dx++) { int xx = tx + dx, yy = ty + dy; if (xx >= 0 && yy >= 0 && xx < MW && yy < MH && raw[yy*MW + xx]) { on = 1; break; } }
                    msk[ty*MW + tx] = on ? 255 : 0;
                }
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, MW, MH, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, msk); uim_w = MW; uim_h = MH;
            } else glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, 1, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &one);
        } else glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, 1, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, &one);
    }
    int mapon = 0, mapbg = 0; float mrect[4] = { 0, 0, 0, 0 };
    mw_gl_ok = 1;
    if (mw_ncmd > 0 && uib && UW > 0 && UH > 0) {   /* mappa HD: linee e tappi nel livello 2D a S volte la risoluzione */
        RECT cr0; GetClientRect(hw, &cr0); int S = cr0.bottom / UH; if (S < 1) S = 1; if (S > 8) S = 8;
        int TW = UW * S, TH = UH * S;
        if (!mw_fbo) { glGenTextures(1, &mw_tex); p_glGenFramebuffers(1, &mw_fbo); }
        p_glActiveTexture(GL_TEXTURE0+15); glBindTexture(GL_TEXTURE_2D, mw_tex);
        if (mw_tw != TW || mw_th != TH) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, TW, TH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL); mw_tw = TW; mw_th = TH;
            p_glBindFramebuffer(GL_FRAMEBUFFER, mw_fbo); p_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mw_tex, 0);
        }
        p_glBindFramebuffer(GL_FRAMEBUFFER, mw_fbo); p_glUseProgram(0);
        glViewport(0, 0, TW, TH); glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        int b = map_wb(); mrect[0] = 17; mrect[1] = 17; mrect[2] = 302 + 2*b; mrect[3] = 222;
        glEnable(GL_SCISSOR_TEST); glScissor((int)(mrect[0]*S), (int)((UH - mrect[3] - 1)*S), (int)((mrect[2] - mrect[0] + 1)*S), (int)((mrect[3] - mrect[1] + 1)*S));
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, UW, UH, 0, -1, 1); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glDisable(GL_TEXTURE_2D); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
        typedef void (WINAPI *bfs_t)(GLenum, GLenum, GLenum, GLenum); static bfs_t bfs; if (!bfs) bfs = (bfs_t)glp("glBlendFuncSeparate");
        glEnable(GL_LINE_SMOOTH); glHint(GL_LINE_SMOOTH_HINT, GL_NICEST); glLineWidth(S * 0.75f < 1.5f ? 1.5f : S * 0.75f);
        int cur = -1;
        for (int i = 0; i < mw_ncmd; i++) {
            const MwCmd *m = &mw_cmd[i];
            if (m->kind != cur) {
                if (cur >= 0) glEnd();
                if (m->kind == 1) { glDisable(GL_BLEND); glColor4f(0, 0, 0, 0); glBegin(GL_QUADS); }
                else { glEnable(GL_BLEND); if (bfs) bfs(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA); else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glBegin(GL_LINES); }
                cur = m->kind;
            }
            if (m->kind == 1) { for (int k = 0; k < 4; k++) glVertex2f(m->v[k*2] + 0.5f, m->v[k*2+1] + 0.5f); }
            else { const BYTE *q = palui + m->c*4; glColor4ub(q[0], q[1], q[2], 255); glVertex2f(m->v[0] + 0.5f, m->v[1] + 0.5f); glVertex2f(m->v[2] + 0.5f, m->v[3] + 0.5f); }
        }
        if (cur >= 0) glEnd();
        glDisable(GL_LINE_SMOOTH); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glLineWidth(1);
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
        {   /* sfondo della finestra della mappa: l'indice piu' frequente dentro il riquadro */
            static int hist[256]; memset(hist, 0, sizeof hist);
            for (int y = (int)mrect[1]; y <= (int)mrect[3] && y < UH; y += 2) for (int x = (int)mrect[0]; x <= (int)mrect[2] && x < UW; x += 2) hist[uib[(size_t)y*UW + x]]++;
            for (int i = 1; i < 256; i++) if (hist[i] > hist[mapbg]) mapbg = i;
        }
        mapon = 1;
    }
    mw_ncmd = 0;
    int rel = texb && litb && brtb;
    if (rel) {
        p_glActiveTexture(GL_TEXTURE0+2); glBindTexture(GL_TEXTURE_2D, t_tex);
        if (!skip8) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_LUMINANCE, GL_UNSIGNED_BYTE, texb);
        p_glActiveTexture(GL_TEXTURE0+3); glBindTexture(GL_TEXTURE_2D, t_lit);
        if (!skip8) glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, litb);
        p_glActiveTexture(GL_TEXTURE0+4); glBindTexture(GL_TEXTURE_2D, t_brt);
        int brt_changed = memcmp(brt_cache, brtb, sizeof(brt_cache)) != 0;
        if (brt_changed) { memcpy(brt_cache, brtb, sizeof(brt_cache)); glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 288, GL_LUMINANCE, GL_UNSIGNED_BYTE, brt_cache); }
        if (brt_changed || memcmp(pal_cache, palrgba, 1024)) {
            memcpy(pal_cache, palrgba, 1024); build_true_table(brt_cache, pal_cache);
            p_glActiveTexture(GL_TEXTURE0+5); glBindTexture(GL_TEXTURE_2D, t_true);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 288, GL_RGBA, GL_UNSIGNED_BYTE, true_tab);
        }
    }
    nr_pal_now = palrgba; nr_brt_now = brtb;
    int newr = cfg_newr && rel && has3d && nr_draw(W, H);   /* render nuovo: scena 3D dalla scheda video */
    nr_skip = cfg_newr && nr_ok == 1;
    /* disegno diretto nella finestra, alla risoluzione dello schermo */
    RECT cr; GetClientRect(hw, &cr); int cw = cr.right, ch = cr.bottom; if (cw <= 0 || ch <= 0) return 1;
    int dw = cw, dh = (int)((double)cw * H / W); if (dh > ch) { dh = ch; dw = (int)((double)ch * W / H); }
    if (cfg_stretch && aspect_auto) { dw = cw; dh = ch; }   /* rapporto scelto a mano: proporzioni esatte (bande nere) */
    p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, cw, ch); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
    glViewport((cw-dw)/2, (ch-dh)/2, dw, dh);
    p_glUseProgram(prog);
    p_glUniform2f(u_osz, (float)dw, (float)dh); p_glUniform2f(u_ooff, (float)((cw-dw)/2), (float)((ch-dh)/2)); p_glUniform1f(u_sm3, cfg_smooth ? 1.0f : 0.0f);
    p_glUniform1i(u_idx, 0); p_glUniform1i(u_pal, 1); p_glUniform1i(u_uipal, 11); p_glUniform1f(u_newr, newr ? 1.0f : 0.0f); p_glUniform1i(u_nscene, 12);
    p_glActiveTexture(GL_TEXTURE0+12); glBindTexture(GL_TEXTURE_2D, nr_scene); p_glUniform1i(u_tex, 2); p_glUniform1i(u_lit, 3); p_glUniform1i(u_brt, 4);
    p_glUniform1f(u_rel, rel ? 1.0f : 0.0f);
    p_glUniform1f(u_smm, cfg_relight >= 2 ? 0.0f : 1.0f); p_glUniform1i(u_true, 5);
    p_glUniform1i(u_ui, 6); p_glUniform1f(u_uion, (uib && !(photo_mode && has3d)) ? 1.0f : 0.0f); p_glUniform2f(u_uisz, (float)UW, (float)UH);
    p_glUniform1f(u_uir, uib ? (float)H/UH : 1.0f); p_glUniform1f(u_has3d, has3d ? 1.0f : 0.0f);
    p_glUniform1i(u_uim, 14); p_glUniform2f(u_uimsz, (float)uim_w, (float)uim_h);
    {   /* icone HUD additive (ui_hd.inc addm_mark) */
        int on = ui_addm_on && ui_addm && uib && ui_addm_w == UW && ui_addm_h == UH && has3d;
        if (on) {
            if (!t_uiadd) glGenTextures(1, &t_uiadd);
            p_glActiveTexture(GL_TEXTURE0+16); glBindTexture(GL_TEXTURE_2D, t_uiadd);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE8, UW, UH, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, ui_addm);
        }
        p_glUniform1i(u_uiadd, 16); p_glUniform1f(u_uiaddon, on ? 1.0f : 0.0f);
    }
    p_glUniform1f(u_mapon, (float)mapon); p_glUniform4f(u_maprect, mrect[0], mrect[1], mrect[2], mrect[3]); p_glUniform1f(u_mapbg, (float)mapbg); p_glUniform1i(u_mapl, 15);
    if (mapon) { p_glActiveTexture(GL_TEXTURE0+15); glBindTexture(GL_TEXTURE_2D, mw_tex); }
    p_glUniform1f(u_hs, uib ? (float)H/UH * cfg_hud_scale / 100.0f : 1.0f); p_glUniform1f(u_hf, (float)cfg_hud_filter); p_glUniform1f(u_mf, (float)cfg_menu_filter);
    if (uib) { p_glActiveTexture(GL_TEXTURE0+6); glBindTexture(GL_TEXTURE_2D, t_ui); }
    p_glUniform1f(u_hdon, (hd_state && (sm_on || (n_m > 0 && !has3d))) ? 1.0f : 0.0f); p_glUniform1i(u_href, 9);
    p_glActiveTexture(GL_TEXTURE0+9); glBindTexture(GL_TEXTURE_2D, t_href); p_glUniform1f(u_hdf, (float)hd_fade); p_glUniform1i(u_hdt, 7); p_glUniform1i(u_tmap, 8);
    p_glActiveTexture(GL_TEXTURE0+7); glBindTexture(GL_TEXTURE_2D, t_hd); p_glActiveTexture(GL_TEXTURE0+8); glBindTexture(GL_TEXTURE_2D, t_tmap);
    p_glActiveTexture(GL_TEXTURE0+5); glBindTexture(GL_TEXTURE_2D, t_true); p_glUniform2f(u_src, (float)W, (float)H);
    p_glUniform1f(u_thr, cfg_deband_thr/255.0f); p_glUniform1f(u_rad, (float)cfg_deband_rad);
    p_glUniform1f(u_frame, (float)(gl_frame++ & 255)); p_glUniform1f(u_on, cfg_deband ? 1.0f : 0.0f); p_glUniform1f(u_dith, cfg_dither ? 1.5f : 0.0f);
    {   /* sovrapposizione dell'immagine originale (modalita' foto) */
        static char ovl_cur[16]; const BYTE *rgb = (photo_mode && ovl_alpha > 0 && has3d) ? ovl_rgb(ovl_cur) : NULL;
        p_glActiveTexture(GL_TEXTURE0 + 10); glBindTexture(GL_TEXTURE_2D, t_ovl); p_glUniform1i(u_ovl, 10);
        if (rgb) {
            static char up[16]; static int tex_ok;
            if (!tex_ok) { glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                           glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); tex_ok = 1; }
            if (strcmp(up, ovl_cur)) { glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 220, 160, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb); strcpy(up, ovl_cur); }
        }
        float x0 = W*0.5f - 440.0f*(H/960.0f), y0 = 192.0f*(H/960.0f);
        p_glUniform4f(u_ovlrect, x0, y0, x0 + 880.0f*(H/960.0f), y0 + 640.0f*(H/960.0f)); p_glUniform1f(u_ovla, rgb ? ovl_alpha : 0.0f);
    }
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); glEnd();
    if (photo_req && has3d) {   /* F12: foto della sola scena 3D alla risoluzione interna (per le immagini delle missioni) */
        photo_req = 0;
        p_glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, W, H);
        p_glUniform1f(u_ovla, 0.0f); p_glUniform1f(u_uion, 0.0f); p_glUniform2f(u_osz, (float)W, (float)H); p_glUniform2f(u_ooff, 0.0f, 0.0f); p_glUniform1f(u_sm3, 0.0f);
        glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); glEnd();
        size_t stride = ((size_t)W*3 + 3) & ~3; BYTE *px = malloc(stride*H);
        if (px) {
            glPixelStorei(GL_PACK_ALIGNMENT, 4); glReadPixels(0, 0, W, H, 0x80E0 /*GL_BGR*/, GL_UNSIGNED_BYTE, px);
            char dir[MAX_PATH], path[MAX_PATH], name[64]; path_in_dir(dir, "FOTO"); CreateDirectoryA(dir, NULL);
            for (int n = 1; n < 1000; n++) { sprintf(name, "FOTO\\FOTO_%03d.BMP", n); path_in_dir(path, name); if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) break; }
            FILE *f = fopen(path, "wb");
            if (f) {
                BITMAPFILEHEADER bf = {0}; BITMAPINFOHEADER bh = {0};
                bf.bfType = 0x4D42; bf.bfOffBits = sizeof(bf) + sizeof(bh); bf.bfSize = bf.bfOffBits + (DWORD)(stride*H);
                bh.biSize = sizeof(bh); bh.biWidth = W; bh.biHeight = H; bh.biPlanes = 1; bh.biBitCount = 24;
                fwrite(&bf, sizeof(bf), 1, f); fwrite(&bh, sizeof(bh), 1, f); fwrite(px, stride, H, f); fclose(f);
                mhlog("foto missione: %s", name); MessageBeep(MB_OK);
                if (photo_mode && cfg_shot != 7 && cfg_shot != 6 && GetPrivateProfileIntA("foto", "anteprima", 0, ini_path)) photo_to_mission(px, W, H, stride);   /* solo con [foto] anteprima=1 (le anteprime sono fatte: di norma la foto resta solo in FOTO) */
                {   /* dati per rifare l'inquadratura con la telecamera libera: livello, giocatore, telecamera */
                    char tp[MAX_PATH]; strcpy(tp, path); strcpy(tp + strlen(tp) - 4, ".txt"); FILE *t = fopen(tp, "w");
                    if (t) {
                        const int *pr = (const int*)0x621b98, *cm = (const int*)0x4b1930;
                        fprintf(t, "level=%d\nplayer=%d,%d,%d\nyaw_rec=%d\ncamera=%d,%d,%d\ncam_yaw=%d\naspect_w=%d\nfoto=%d\nfree_cam=%d,%d,%d\nfree_yaw=%d\ncam_mode=%d\nzoom=%d\nrect=%d,%d,%d,%d\npiano=%d\n",
                                *(volatile int*)0x4b1650, pr[0], pr[1], pr[2], *(const int*)((const BYTE*)pr + 0x258), cm[0], cm[1], cm[2], cm[5], W, photo_mode, cam_x, cam_y, cam_z, cam_yaw_set, cam_mode, *(volatile int*)0x48aca0, W/2 - 440*H/960, 192*H/960, W/2 + 440*H/960, 832*H/960, *(volatile int*)0x4b7bf4);
                        fclose(t);
                    }
                }
            }
            free(px);
        }
        p_glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    p_glUseProgram(0);
    if (nr_hold == 2) hold_overlay(cw, ch);
    if (cfg_shot == 1) { static int sn; unsigned f = gl_frame; if (f == 60 || f == 400 || f == 800 || f == 1200 || f == 1600 || f == 2400) save_shot(cw, ch, sn++); }
    if (cfg_shot == 2) {   /* debug: sequenza di fotogrammi ridotti (1/4) */
        unsigned f = gl_frame;
        if (f >= 60 && f < 700) {
            int w = cw/4, h = ch/4; BYTE *full = malloc((size_t)cw*ch*3), *sm = malloc((size_t)w*h*3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, cw, ch, 0x80E0, GL_UNSIGNED_BYTE, full);
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) memcpy(sm + ((size_t)y*w + x)*3, full + ((size_t)(y*4)*cw + x*4)*3, 3);
            char name[64], path[MAX_PATH]; sprintf(name, "SEQ\\%04u.raw", f); path_in_dir(path, name);
            FILE *fo = fopen(path, "wb"); if (fo) { fwrite(&w, 4, 1, fo); fwrite(&h, 4, 1, fo); fwrite(sm, 1, (size_t)w*h*3, fo); fclose(fo); }
            free(full); free(sm);
        }
    }
    if (shot_req) { save_shot(cw, ch, shot_req); shot_req = 0; }
    if (nr_hold == 1) return 1;   /* primo fotogramma del livello: livello 2D ancora sporco dal caricamento -> non mostrato, lo ridisegna l'attesa pulito */
    SwapBuffers(dc);
    return 1;
}
