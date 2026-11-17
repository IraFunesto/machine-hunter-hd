/*  WINMX.dll - compagno di Machine Hunter (EXE patchati da build_exe.py)
 *
 *  Sostituisce le 6 funzioni WINMM usate dal gioco e, al caricamento, aggancia:
 *   - musica:     emula il CD audio (mciSendString "cdaudio") con MUSIC\TrackNN.wav / .mp3
 *   - joypad:     DualSense / DualShock 4 via HID (USB e Bluetooth), pad XInput in alternativa
 *   - video:      presentazione del fotogramma (8 bit -> 32 bit) con filtro anti-banding,
 *                 finestra senza bordi / finestra, scaling con proporzioni corrette
 *   - avvio:      risposte automatiche alle domande "tutto schermo?" e "filmati?"
 *  Impostazioni in MHFIX.INI (creato al primo avvio).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ indirizzi nell'EXE */
#define G_W        (*(int*)0x48cdc8)
#define G_H        (*(int*)0x48cdcc)
#define G_HDC      (*(HDC*)0x6334f4)
#define G_HWND     (*(HWND*)0x67c4ac)
#define G_BITS     (*(BYTE**)0x65e444)
#define G_BMI      (*(BITMAPINFO**)0x65bb18)
#define PRESENT_VA 0x439950

/* ------------------------------------------------------------------ configurazione */
static char g_dir[MAX_PATH];
static int cfg_mode = 1;          /* 0 finestra, 1 senza bordi a schermo intero */
static int cfg_deband = 0;        /* filtro anti-banding (con la luce ricostruita non serve) */
static int cfg_deband_thr = 24;   /* soglia (0-255) */
static int cfg_vsync = 0;
static int cfg_stretch = 1;
static int cfg_shot = 0;
int rz_frame_t;
/* attesa all'inizio del livello ([video] attesa=1): 1 = da iniziare, 2 = in corso (hold_loop); pct/pronto per la scritta */
static volatile int poly_req;   /* F12: salva anche i poligoni del fotogramma (per i glitch del render nuovo) */
static volatile int nr_hold; static int cfg_attesa = 1, nr_hold_pct, nr_hold_ready, hold_mute;
static int cfg_newr;   /* [video] render=nuovo: scena 3D disegnata dalla scheda video (gl_newr.inc) */
static int cfg_player_hue = -1;   /* colore della tuta: -1 = arancione originale, 1000 = viola PS1, altrimenti tinta in gradi */
static BYTE suit_out[12]; static float suit_v[12]; static int suit_n;   /* indici liberi della palette per la tuta e loro luminosita' (0-1) */
static char cfg_aspect[32] = "auto"; static int cfg_start_level, aspect_auto = 1, cfg_photo_dist = 0, cfg_cull_extra;
static int cfg_hud_scale = 75;    /* % rispetto alla proporzione originale (100 = come PS1) */
static int cfg_hud_filter = 3, cfg_menu_filter = 3;   /* 0 pixel, 1 bilineare, 2 bicubico, 3 nitido */
static int cfg_menu_hd = 1;        /* menu con immagini HD (UI_HD) */   /* 0 pixel, 1 bilineare, 2 bicubico */          /* debug: salva alcuni fotogrammi in MHSHOT_n.BMP */
static int cfg_relight = 1;     /* luce Gouraud ricostruita a 32 bit (EXE con caverne RELIGHT) */      /* 1 = riempie tutto lo schermo, 0 = proporzioni esatte con bande nere */
static int cfg_deband_rad = 64;   /* raggio in pixel (alla risoluzione del gioco) */
static int cfg_dither = 1;        /* rumore leggero per nascondere le bande residue */
static int cfg_smooth = 1;        /* scaling morbido */
static int cfg_ask = 0;           /* 1 = mostra le domande all'avvio, 0 = risposta automatica */
static int cfg_movies = 0;        /* risposta automatica alla domanda sui filmati */
static int cfg_music = 1;
static int cfg_pad = 1;
static int cfg_btn[4] = {1, 3, 0, 2};  /* sparo: pulsante 1 sinistra, 2 su, 3 giu, 4 destra -> 0 croce,1 quadrato,2 cerchio,3 triangolo */
/* altri tasti del pad -> tasti della tastiera (codici VK): L1, R1, L2, R2, Options, Create */
static int cfg_keys[6] = {17, 87, 32, 83, 13, 9};
static const char *key_names[6] = {"key_l1", "key_r1", "key_l2", "key_r2", "key_options", "key_create"};
static int cfg_deadzone = 30;     /* zona morta stick (0-127) */

static void path_in_dir(char *out, const char *name) { snprintf(out, MAX_PATH, "%s\\%s", g_dir, name); }
static void mhlog(const char *fmt, ...)
{
    char p[MAX_PATH]; path_in_dir(p, "MHFIX.LOG"); FILE *f = fopen(p, "a"); if (!f) return;
    SYSTEMTIME t; GetLocalTime(&t); fprintf(f, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a); fputc(10, f); fclose(f);
}
static char ini_path[MAX_PATH];
static void load_config(void)
{
    char ini[MAX_PATH]; path_in_dir(ini, "MHFIX.INI"); strcpy(ini_path, ini);
    if (GetFileAttributesA(ini) == INVALID_FILE_ATTRIBUTES) {
        FILE *f = fopen(ini, "w");
        if (f) {
            fputs("; Machine Hunter - impostazioni WINMX.dll\n"
                  "[video]\n"
                  "; rapporto d'aspetto: auto (quello dello schermo), oppure 16:9, 21:9, 32:9, 16:10, 4:3, o un numero (es. 2.37)\n"
                  "aspect=auto\n"
                  "; 1 = senza bordi a schermo intero, 0 = finestra\n"
                  "mode=1\n"
                  "; filtro anti-banding (riduce gli aloni dei 256 colori)\n"
                  "deband=0\n"
                  "deband_threshold=8\n"
                  "deband_radius=32\n"
                  "; 1 = riempie tutto lo schermo, 0 = proporzioni esatte (bande nere)\n"
                  "stretch=1\n"
                  "; luce ricostruita a 32 bit (richiede ombreggiatura Gouraud nel menu): 1 = curva morbida, 2 = lineare, 0 = spenta\n"
                  "relight=1\n"
                  "; HUD: dimensione in percentuale rispetto all'originale (100 = proporzioni PS1)\n"
                  "hud_scale=75\n"
                  "; filtro per HUD e testi: 0 pixel, 1 bilineare, 2 bicubico, 3 nitido\n"
                  "hud_filter=3\n"
                  "menu_filter=3\n"
                  "; 1 = menu e schermate con immagini ingrandite (cartella UI_HD)\n"
                  "menu_hd=1\n"
                  "; 1 = sincronizzazione verticale\n"
                  "vsync=0\n"
                  "dither=1\n"
                  "; scaling morbido\n"
                  "smooth=1\n"
                  "[startup]\n"
                  "; 0 = niente domande all'avvio (finestra, filmati come sotto)\n"
                  "ask=0\n"
                  "movies=0\n"
                  "[audio]\n"
                  "; tracce in MUSIC\\Track02.wav (o .mp3), estratte dal CD\n"
                  "music=1\n"
                  "[input]\n"
                  "pad=1\n"
                  "; pulsanti del gioco 1-4: 0 croce, 1 quadrato, 2 cerchio, 3 triangolo, 4 L1, 5 R1, 6 L2, 7 R2\n"
                  "; sparo (come da manuale): button1 = sinistra, button2 = su, button3 = giu, button4 = destra\n"
                  "; valori: 0 croce, 1 quadrato, 2 cerchio, 3 triangolo\n"
                  "button1=1\nbutton2=3\nbutton3=0\nbutton4=2\n"
                  "; altri tasti -> tastiera (codice VK): L1 strafe, L2 arma secondaria, R1/R2 cambio armi, Options pausa (INVIO), Create mappa\n"
                  "; 17 CTRL, 32 SPAZIO, 87 W, 83 S, 27 ESC, 9 TAB, 13 INVIO\n"
                  "key_l1=17\nkey_r1=87\nkey_l2=32\nkey_r2=83\nkey_options=13\nkey_create=9\n"
                  "deadzone=30\n", f);
            fclose(f);
        }
    }
    cfg_mode = GetPrivateProfileIntA("video", "mode", 1, ini);
    if (!GetPrivateProfileStringA("video", "aspect", "", cfg_aspect, sizeof cfg_aspect, ini)) {   /* INI vecchio: aggiunge la voce */
        strcpy(cfg_aspect, "auto"); WritePrivateProfileStringA("video", "aspect", "auto", ini);
    }
    cfg_deband = GetPrivateProfileIntA("video", "deband", 0, ini);
    cfg_deband_thr = GetPrivateProfileIntA("video", "deband_threshold", 24, ini);
    cfg_stretch = GetPrivateProfileIntA("video", "stretch", 1, ini);
    cfg_shot = GetPrivateProfileIntA("debug", "shot", 0, ini);
    cfg_hud_scale = GetPrivateProfileIntA("video", "hud_scale", 75, ini); if (cfg_hud_scale < 25) cfg_hud_scale = 25;
    cfg_hud_filter = GetPrivateProfileIntA("video", "hud_filter", 3, ini); cfg_menu_filter = GetPrivateProfileIntA("video", "menu_filter", 3, ini);
    cfg_menu_hd = GetPrivateProfileIntA("video", "menu_hd", 1, ini);
    cfg_relight = GetPrivateProfileIntA("video", "relight", 1, ini);
    { char c[16]; GetPrivateProfileStringA("video", "render", "classico", c, sizeof c, ini); cfg_newr = !_stricmp(c, "nuovo"); }
    cfg_attesa = GetPrivateProfileIntA("video", "attesa", 1, ini);
    cfg_vsync = GetPrivateProfileIntA("video", "vsync", 0, ini);
    cfg_deband_rad = GetPrivateProfileIntA("video", "deband_radius", 64, ini);
    cfg_dither = GetPrivateProfileIntA("video", "dither", 1, ini);
    cfg_smooth = GetPrivateProfileIntA("video", "smooth", 1, ini);
    cfg_ask = GetPrivateProfileIntA("startup", "ask", 0, ini);
    cfg_movies = GetPrivateProfileIntA("startup", "movies", 0, ini);
    cfg_start_level = GetPrivateProfileIntA("startup", "level", 0, ini);
    cfg_photo_dist = GetPrivateProfileIntA("foto", "distanza_giocatore", 0, ini);
    { char c[32]; GetPrivateProfileStringA("giocatore", "colore", "arancione", c, sizeof c, ini);
      cfg_player_hue = !_stricmp(c, "viola") ? 1000 : !_stricmp(c, "blu") ? 225 : !_stricmp(c, "verde") ? 120 : !_stricmp(c, "rosso") ? 0 : !_stricmp(c, "arancione") ? -1 :
                       (c[0] >= '0' && c[0] <= '9') ? atoi(c) % 360 : -1; }
    cfg_cull_extra = GetPrivateProfileIntA("video", "culling_margine", 0, ini);
    { char t[8]; if (!GetPrivateProfileStringA("startup", "level", "", t, sizeof t, ini)) WritePrivateProfileStringA("startup", "level", "0", ini); }
    cfg_music = GetPrivateProfileIntA("audio", "music", 1, ini);
    cfg_pad = GetPrivateProfileIntA("input", "pad", 1, ini);
    for (int i = 0; i < 4; i++) { char k[16]; sprintf(k, "button%d", i+1); cfg_btn[i] = GetPrivateProfileIntA("input", k, cfg_btn[i], ini); }
    for (int i = 0; i < 6; i++) cfg_keys[i] = GetPrivateProfileIntA("input", key_names[i], cfg_keys[i], ini);
    cfg_deadzone = GetPrivateProfileIntA("input", "deadzone", 30, ini);
}

/* ------------------------------------------------------------------ WINMM reale */
static HMODULE real_winmm;
static FARPROC rp(const char *n)
{
    if (!real_winmm) { char p[MAX_PATH]; GetSystemDirectoryA(p, MAX_PATH); strcat(p, "\\winmm.dll"); real_winmm = LoadLibraryA(p); }
    return GetProcAddress(real_winmm, n);
}
typedef MCIERROR (WINAPI *mciSendStringA_t)(LPCSTR, LPSTR, UINT, HWND);
static MCIERROR real_mci(LPCSTR c, LPSTR r, UINT n, HWND h) { return ((mciSendStringA_t)rp("mciSendStringA"))(c, r, n, h); }

__declspec(dllexport) DWORD WINAPI timeGetTime(void) { return ((DWORD (WINAPI*)(void))rp("timeGetTime"))(); }
__declspec(dllexport) BOOL WINAPI mciGetErrorStringA(MCIERROR e, LPSTR s, UINT n)
{ return ((BOOL (WINAPI*)(MCIERROR, LPSTR, UINT))rp("mciGetErrorStringA"))(e, s, n); }

/* ------------------------------------------------------------------ musica (CD audio emulato) */
static int music_open = 0, music_track = 0, music_paused = 0;
static void music_stop(void)
{
    if (music_open) { real_mci("stop mhmusic", NULL, 0, NULL); real_mci("close mhmusic", NULL, 0, NULL); music_open = 0; }
    music_paused = 0;
}
static int music_play(int track, HWND notify)
{
    static const char *ext[] = {"wav", "mp3"};
    static const char *type[] = {"waveaudio", "mpegvideo"};
    char file[MAX_PATH], cmd[MAX_PATH+64];
    music_stop();
    if (!cfg_music) return 0;
    for (int i = 0; i < 2; i++) {
        char rel[64]; sprintf(rel, "MUSIC\\Track%02d.%s", track, ext[i]); path_in_dir(file, rel);
        if (GetFileAttributesA(file) == INVALID_FILE_ATTRIBUTES) continue;
        sprintf(cmd, "open \"%s\" type %s alias mhmusic", file, type[i]);
        if (real_mci(cmd, NULL, 0, NULL) == 0) {
            music_open = 1; music_track = track;
            real_mci(notify ? "play mhmusic notify" : "play mhmusic", NULL, 0, notify);
            return 1;
        }
    }
    return 0;
}
static void ret_str(LPSTR r, UINT n, const char *s) { if (r && n) { strncpy(r, s, n-1); r[n-1] = 0; } }

__declspec(dllexport) MCIERROR WINAPI mciSendStringA(LPCSTR cmd, LPSTR ret, UINT retlen, HWND cb)
{
    char c[256]; int i;
    for (i = 0; cmd[i] && i < 255; i++) c[i] = (char)tolower((unsigned char)cmd[i]); c[i] = 0;
    if (!strstr(c, "cdaudio")) return real_mci(cmd, ret, retlen, cb);
    ret_str(ret, retlen, "");
    if (strstr(c, "status")) {
        if (strstr(c, "ready") || strstr(c, "media present")) ret_str(ret, retlen, "true");
        else if (strstr(c, "number of tracks")) ret_str(ret, retlen, "20");
        else if (strstr(c, "mode")) ret_str(ret, retlen, music_open ? (music_paused ? "paused" : "playing") : "stopped");
        return 0;
    }
    if (strstr(c, "play")) {
        const char *f = strstr(c, "from");
        if (f) { int from = atoi(f+4); mhlog("musica: il gioco chiede la traccia %d (\"%s\")", from & 0xff, cmd); music_play(from & 0xff, strstr(c, "notify") ? cb : NULL); }
        else if (music_open && music_paused) { real_mci("resume mhmusic", NULL, 0, NULL); music_paused = 0; }
        return 0;
    }
    if (strstr(c, "pause")) { if (music_open) { real_mci("pause mhmusic", NULL, 0, NULL); music_paused = 1; } return 0; }
    if (strstr(c, "stop") || strstr(c, "close")) { music_stop(); return 0; }
    return 0;   /* open, set, lock, unlock ... */
}

/* ------------------------------------------------------------------ joypad */
typedef struct { int lx, ly, rx, ry; unsigned btn; int dpad; int ok; } PadState;   /* btn bit 10 = R3 */   /* btn: bit 0 croce,1 quadrato,2 cerchio,3 triangolo,4 L1,5 R1,6 L2,7 R2,8 options */
static PadState pad;
static CRITICAL_SECTION pad_cs;
static volatile LONG pad_thread_started = 0;

static void parse_ds(const BYTE *b, int len, int dualsense)
{
    PadState s = {0}; const BYTE *p; int hat; unsigned f, sh;
    if (dualsense) {
        if (b[0] == 0x01 && len >= 11 && len < 20) {   /* Bluetooth, report semplice */
            s.lx = b[1]; s.ly = b[2]; s.rx = b[3]; s.ry = b[4]; f = b[5]; sh = b[6];
            if (len >= 10) { if (b[8] > 100) sh |= 4; if (b[9] > 100) sh |= 8; }
        } else {
            p = (b[0] == 0x31) ? b+1 : b;              /* 0x31 Bluetooth esteso, 0x01 USB */
            s.lx = p[1]; s.ly = p[2]; s.rx = p[3]; s.ry = p[4]; f = p[8]; sh = p[9]; if (p[10] & 2) s.btn |= 2048;   /* touchpad = come L3 */
        }
    } else {                                           /* DualShock 4 */
        p = (b[0] == 0x11) ? b+2 : b;
        s.lx = p[1]; s.ly = p[2]; s.rx = p[3]; s.ry = p[4]; f = p[5]; sh = p[6]; if (p[7] & 2) s.btn |= 2048;
    }
    hat = f & 0x0f; s.dpad = hat;
    if (f & 0x20) s.btn |= 1;    /* croce */
    if (f & 0x10) s.btn |= 2;    /* quadrato */
    if (f & 0x40) s.btn |= 4;    /* cerchio */
    if (f & 0x80) s.btn |= 8;    /* triangolo */
    if (sh & 0x01) s.btn |= 16;  /* L1 */
    if (sh & 0x02) s.btn |= 32;  /* R1 */
    if (sh & 0x04) s.btn |= 64;  /* L2 */
    if (sh & 0x08) s.btn |= 128; /* R2 */
    if (sh & 0x20) s.btn |= 256; /* options */
    if (sh & 0x10) s.btn |= 512; /* create */
    if (sh & 0x80) s.btn |= 1024; /* R3 */
    if (sh & 0x40) s.btn |= 2048; /* L3 */
    s.ok = 1;
    EnterCriticalSection(&pad_cs); pad = s; LeaveCriticalSection(&pad_cs);
}

typedef DWORD (WINAPI *XInputGetState_t)(DWORD, void*);
typedef struct { WORD wButtons; BYTE bLT, bRT; SHORT sLX, sLY, sRX, sRY; } XGAMEPAD;
typedef struct { DWORD dwPacket; XGAMEPAD g; } XSTATE;

static int poll_xinput(void)
{
    static XInputGetState_t xget; static int tried;
    if (!tried) { HMODULE h = LoadLibraryA("xinput1_4.dll"); if (!h) h = LoadLibraryA("xinput1_3.dll"); if (h) xget = (XInputGetState_t)GetProcAddress(h, "XInputGetState"); tried = 1; }
    if (!xget) return 0;
    for (DWORD i = 0; i < 4; i++) {
        XSTATE st; if (xget(i, &st) != 0) continue;
        PadState s = {0}; WORD w = st.g.wButtons;
        s.lx = (st.g.sLX + 32768) >> 8; s.ly = 255 - ((st.g.sLY + 32768) >> 8); s.rx = (st.g.sRX + 32768) >> 8; s.ry = 255 - ((st.g.sRY + 32768) >> 8);
        int up = w & 1, dn = w & 2, lf = w & 4, rt = w & 8;
        s.dpad = up ? (rt ? 1 : lf ? 7 : 0) : dn ? (rt ? 3 : lf ? 5 : 4) : rt ? 2 : lf ? 6 : 8;
        if (w & 0x1000) s.btn |= 1; if (w & 0x4000) s.btn |= 2; if (w & 0x2000) s.btn |= 4; if (w & 0x8000) s.btn |= 8;
        if (w & 0x0100) s.btn |= 16; if (w & 0x0200) s.btn |= 32; if (st.g.bLT > 100) s.btn |= 64; if (st.g.bRT > 100) s.btn |= 128;
        if (w & 0x0010) s.btn |= 256; if (w & 0x0020) s.btn |= 512; if (w & 0x0080) s.btn |= 1024; if (w & 0x0040) s.btn |= 2048;
        s.ok = 1; EnterCriticalSection(&pad_cs); pad = s; LeaveCriticalSection(&pad_cs);
        return 1;
    }
    return 0;
}

static DWORD WINAPI pad_thread(LPVOID unused)
{
    GUID hid; HidD_GetHidGuid(&hid);
    for (;;) {
        HANDLE dev = INVALID_HANDLE_VALUE; int dualsense = 0;
        HDEVINFO set = SetupDiGetClassDevsA(&hid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        SP_DEVICE_INTERFACE_DATA ifd; ifd.cbSize = sizeof(ifd);
        for (DWORD i = 0; set != INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(set, NULL, &hid, i, &ifd); i++) {
            DWORD need = 0; SetupDiGetDeviceInterfaceDetailA(set, &ifd, NULL, 0, &need, NULL);
            SP_DEVICE_INTERFACE_DETAIL_DATA_A *det = malloc(need); det->cbSize = sizeof(*det);
            if (SetupDiGetDeviceInterfaceDetailA(set, &ifd, det, need, NULL, NULL)) {
                HANDLE h = CreateFileA(det->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                if (h == INVALID_HANDLE_VALUE) h = CreateFileA(det->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
                if (h != INVALID_HANDLE_VALUE) {
                    HIDD_ATTRIBUTES a; a.Size = sizeof(a);
                    if (HidD_GetAttributes(h, &a) && a.VendorID == 0x054C) mhlog("pad: trovato Sony %04x", a.ProductID);
                    if (HidD_GetAttributes(h, &a) && a.VendorID == 0x054C &&
                        (a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2 || a.ProductID == 0x05C4 || a.ProductID == 0x09CC)) {
                        PHIDP_PREPARSED_DATA pp; HIDP_CAPS caps;
                        if (HidD_GetPreparsedData(h, &pp)) {
                            int game = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 1 && (caps.Usage == 5 || caps.Usage == 4);
                            HidD_FreePreparsedData(pp);
                            mhlog("pad: usage page %x usage %x, input %d byte", caps.UsagePage, caps.Usage, caps.InputReportByteLength);
                            if (game) { dev = h; dualsense = (a.ProductID == 0x0CE6 || a.ProductID == 0x0DF2); free(det); break; }
                        }
                    }
                    CloseHandle(h);
                }
            }
            free(det);
        }
        if (set != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(set);
        if (dev != INVALID_HANDLE_VALUE) {
            BYTE buf[128]; DWORD got; int nrep = 0;
            mhlog("pad: lettura da %s", dualsense ? "DualSense" : "DualShock 4");
            while (ReadFile(dev, buf, sizeof(buf), &got, NULL) && got > 0) {
                if (nrep < 2) { mhlog("pad: report %lu byte: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", got, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9], buf[10]); nrep++; }
                parse_ds(buf, (int)got, dualsense);
            }
            mhlog("pad: lettura interrotta (%lu)", GetLastError());
            CloseHandle(dev);
            EnterCriticalSection(&pad_cs); pad.ok = 0; LeaveCriticalSection(&pad_cs);
        } else {
            DWORD t0 = GetTickCount();
            while (GetTickCount() - t0 < 2000) { if (!poll_xinput()) { EnterCriticalSection(&pad_cs); pad.ok = 0; LeaveCriticalSection(&pad_cs); Sleep(500); break; } Sleep(5); }
        }
    }
    return 0;
}
static void pad_wait_first(void)
{
    static int done; if (done) return; done = 1;
    for (int i = 0; i < 150; i++) { int ok; EnterCriticalSection(&pad_cs); ok = pad.ok; LeaveCriticalSection(&pad_cs); if (ok) { mhlog("pad: pronto dopo %d ms", i*10); return; } Sleep(10); }
    mhlog("pad: nessun dato entro 1,5 s");
}
static void pad_start(void)
{
    if (InterlockedExchange(&pad_thread_started, 1) == 0) { HANDLE t = CreateThread(NULL, 0, pad_thread, NULL, 0, NULL); if (t) CloseHandle(t); }
}

static volatile int photo_mode; static BYTE actor_free[64]; static BYTE actor_was[48];
static void cull_update(void); static void spawn_update(void); static double cull_asp;
static volatile DWORD menu_conf_t;   /* ultimo fotogramma con "... CTRL CONFERMA" a schermo (txt_hook): un menu aspetta CTRL */
static void pad_keys(unsigned btn)
{
    if (photo_mode) btn = 0;
    {   /* nei menu con "CTRL CONFERMA" (pausa, uscita...) anche la croce conferma (manda CTRL), come L1 */
        static int xdown; int menu = GetTickCount() - menu_conf_t < 400; HWND hw0 = G_HWND;
        int want = menu && (btn & 1);
        if (hw0 && want != xdown) { UINT sc = MapVirtualKeyA(VK_CONTROL, 0);
            if (want) PostMessageA(hw0, WM_KEYDOWN, VK_CONTROL, 1 | (sc << 16)); else PostMessageA(hw0, WM_KEYUP, VK_CONTROL, 1 | (sc << 16) | 0xC0000000u); xdown = want; }
    }
    static unsigned prev; static const unsigned bits[6] = {16, 32, 64, 128, 256, 512};
    HWND hw = G_HWND; if (!hw) { prev = btn; return; }
    for (int i = 0; i < 6; i++) {
        unsigned b = bits[i]; if (!cfg_keys[i] || ((btn ^ prev) & b) == 0) continue;
        UINT sc = MapVirtualKeyA(cfg_keys[i], 0);
        if (btn & b) PostMessageA(hw, WM_KEYDOWN, cfg_keys[i], 1 | (sc << 16));
        else PostMessageA(hw, WM_KEYUP, cfg_keys[i], 1 | (sc << 16) | 0xC0000000u);
    }
    prev = btn;
}
__declspec(dllexport) UINT WINAPI joyGetNumDevs(void) { mhlog("joyGetNumDevs"); return 16; }
__declspec(dllexport) MMRESULT WINAPI joyGetDevCapsA(UINT_PTR id, LPJOYCAPSA caps, UINT size)
{
    mhlog("joyGetDevCaps id=%u size=%u", (unsigned)id, size);
    if (!cfg_pad) return JOYERR_UNPLUGGED;
    pad_start(); pad_wait_first();
    if (id != 0 || !caps) return JOYERR_PARMS;
    memset(caps, 0, size);
    caps->wMid = 1; caps->wPid = 1; strcpy(caps->szPname, "PlayStation pad");
    caps->wXmax = caps->wYmax = caps->wZmax = 65535;
    caps->wNumButtons = caps->wMaxButtons = 4; caps->wPeriodMin = 10; caps->wPeriodMax = 1000;
    caps->wMaxAxes = caps->wNumAxes = 2;
    return JOYERR_NOERROR;
}
static volatile int dbg_ax, dbg_ay, dbg_on, shot_req, photo_req, dbg_jbtn;   /* debug: levetta simulata */
__declspec(dllexport) MMRESULT WINAPI joyGetPos(UINT id, LPJOYINFO ji)
{
    PadState s;
    if (!cfg_pad || !ji) return JOYERR_UNPLUGGED;
    if (id != 0) return JOYERR_UNPLUGGED;
    pad_start(); pad_wait_first();
    EnterCriticalSection(&pad_cs); s = pad; LeaveCriticalSection(&pad_cs);
    static int nj = 0;
    if (nj < 5 || (nj < 40 && s.btn)) { mhlog("joyGetPos id=%u ok=%d lx=%d ly=%d btn=%x", id, s.ok, s.lx, s.ly, s.btn); nj++; }
    if (!s.ok) return JOYERR_UNPLUGGED;
    int x = s.lx - 128, y = s.ly - 128;
    if (abs(x) < cfg_deadzone) x = 0; if (abs(y) < cfg_deadzone) y = 0;
    static const int hx[] = {0, 1, 1, 1, 0, -1, -1, -1}, hy[] = {-1, -1, 0, 1, 1, 1, 0, -1};
    if (s.dpad < 8) { x = hx[s.dpad]*127; y = hy[s.dpad]*127; }
    if (dbg_on) { x = dbg_ax; y = dbg_ay; }
    if (photo_mode) { x = 0; y = 0; ji->wButtons = 0; }
    ji->wXpos = (UINT)((x + 128) * 257 > 65535 ? 65535 : (x + 128) * 257);
    ji->wYpos = (UINT)((y + 128) * 257 > 65535 ? 65535 : (y + 128) * 257);
    ji->wZpos = 32768; ji->wButtons = 0;
    if (hold_mute) { if (s.btn & 1) s.btn &= ~1u; else hold_mute = 0; }   /* croce premuta per uscire dall'attesa: non spara */
    for (int i = 0; i < 4; i++) if (s.btn & (1u << cfg_btn[i])) ji->wButtons |= (1u << i);
    ji->wButtons |= dbg_jbtn;   /* prove automatiche: pulsante di conferma simulato nei menu */
    pad_keys(s.btn);
    return JOYERR_NOERROR;
}

/* ------------------------------------------------------------------ presentazione 8 -> 32 bit */
static DWORD *frame32; static int fw, fh;
/* modalita' UI (EXE con --ui): 3D in buffer proprio a piena risoluzione, HUD/menu nel DIB del gioco a VW x VH */
static int ui_on, ui_W, ui_H, ui_VW, ui_VH; static BYTE *ui_f3;
static double hd_ms; static DWORD fps_t0; static int fps_n;
static int hd_stable; static double hd_pal_ref, hd_fade = 1.0;
static BYTE noise[64*64];
static void make_noise(void)
{   /* matrice di Bayer 8x8 ripetuta e mescolata: dither ordinato leggero */
    static const int bayer[8][8] = {{0,32,8,40,2,34,10,42},{48,16,56,24,50,18,58,26},{12,44,4,36,14,46,6,38},{60,28,52,20,62,30,54,22},
                                    {3,35,11,43,1,33,9,41},{51,19,59,27,49,17,57,25},{15,47,7,39,13,45,5,37},{63,31,55,23,61,29,53,21}};
    unsigned r = 12345;
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { r = r*1103515245u + 12345u; noise[y*64+x] = (BYTE)((bayer[y&7][x&7]*4 + ((r >> 16) & 3)) & 0xff); }
}
static inline int iabs(int v) { return v < 0 ? -v : v; }

typedef struct { int y0, y1; const BYTE *src; const RGBQUAD *pal; } Job;
static void convert_rows(const Job *j)
{
    const int W = fw, H = fh, R = cfg_deband_rad, T = cfg_deband_thr;
    const BYTE *src = j->src; const RGBQUAD *pal = j->pal;
    for (int y = j->y0; y < j->y1; y++) {
        const BYTE *row = src + (size_t)y*W; DWORD *out = frame32 + (size_t)y*W;
        for (int x = 0; x < W; x++) {
            RGBQUAD c = pal[row[x]];
            int r = c.rgbRed, g = c.rgbGreen, b = c.rgbBlue;
            if (cfg_deband) {
                /* quattro campioni a distanza variabile: se tutti simili (zona sfumata), si fa la media */
                int n = noise[(y & 63)*64 + (x & 63)];
                int d = 1 + (n * R >> 8), d2 = 1 + (((n*7) & 0xff) * R >> 8);
                int xs[4] = {x-d, x+d2, x, x}, ys[4] = {y, y, y-d2, y+d};
                int sr = r, sg = g, sb = b, cnt = 1, okk = 1;
                for (int k = 0; k < 4; k++) {
                    int xx = xs[k] < 0 ? 0 : xs[k] >= W ? W-1 : xs[k];
                    int yy = ys[k] < 0 ? 0 : ys[k] >= H ? H-1 : ys[k];
                    RGBQUAD q = pal[src[(size_t)yy*W + xx]];
                    if (iabs(q.rgbRed-r) > T || iabs(q.rgbGreen-g) > T || iabs(q.rgbBlue-b) > T) { okk = 0; break; }
                    sr += q.rgbRed; sg += q.rgbGreen; sb += q.rgbBlue; cnt++;
                }
                if (okk) { r = sr/cnt; g = sg/cnt; b = sb/cnt; }
                if (cfg_dither) { int o = (n >> 6) - 1; r += o; g += o; b += o; }
                r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
            }
            out[x] = (DWORD)(r << 16 | g << 8 | b);
        }
    }
}

#define NTHREADS 4
static HANDLE th_go[NTHREADS], th_done[NTHREADS]; static Job jobs[NTHREADS];
static DWORD WINAPI worker(LPVOID p)
{
    int i = (int)(INT_PTR)p;
    for (;;) { WaitForSingleObject(th_go[i], INFINITE); convert_rows(&jobs[i]); SetEvent(th_done[i]); }
    return 0;
}
static void ensure_workers(void)
{
    static int started;
    if (started) return; started = 1;
    for (int i = 0; i < NTHREADS; i++) {
        th_go[i] = CreateEventA(NULL, FALSE, FALSE, NULL); th_done[i] = CreateEventA(NULL, FALSE, FALSE, NULL);
        HANDLE t = CreateThread(NULL, 0, worker, (LPVOID)(INT_PTR)i, 0, NULL); if (t) CloseHandle(t);
    }
}

static void fix_window(HWND hw)
{
    if (!hw) return;
    LONG st = GetWindowLongA(hw, GWL_STYLE), ex = GetWindowLongA(hw, GWL_EXSTYLE);
    const LONG EXB = WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE;
    if (cfg_mode == 1) {
        HMONITOR m = MonitorFromWindow(hw, MONITOR_DEFAULTTONEAREST); MONITORINFO mi; mi.cbSize = sizeof(mi); GetMonitorInfoA(m, &mi);
        RECT wr; GetWindowRect(hw, &wr);
        if ((st & (WS_CAPTION | WS_THICKFRAME | WS_BORDER)) || (ex & EXB) || !EqualRect(&wr, &mi.rcMonitor)) {
            SetWindowLongA(hw, GWL_EXSTYLE, ex & ~EXB);
            mhlog("finestra %p: stile %lx -> senza bordi %ldx%ld", hw, st, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top);
            SetWindowLongA(hw, GWL_STYLE, (st & ~(WS_OVERLAPPEDWINDOW | WS_BORDER | WS_DLGFRAME)) | WS_POPUP | WS_VISIBLE);
            SetWindowPos(hw, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        }
    }
}

#include "ui_hd.inc"
/* immagine originale della missione del livello corrente (per la sovrapposizione in modalita' foto) */
static float ovl_alpha;
static const BYTE *ovl_rgb(char *name_out)
{
    static const char *names[17] = { "MARS", "LABS", "DOCKS", "MINES", "ROOFTOPS", "STREETS", "HOSP", "SEWER", "JUNK", "POWER",
                                     "SWAMP", "BARRACKS", "COMPOUND", "ASTER", "NEST", "ACITY", "BOSS3" };
    static BYTE rgb[220*160*3]; static int last = -1;
    int lvl = *(volatile int*)0x4b1650; if (lvl < 0 || lvl > 40) return NULL;
    int im = ((const BYTE*)0x48a8b0)[16*lvl + 1]; if (im > 16) return NULL;
    if (!hd_loaded) hd_load();
    int k = hd_find(names[im]); if (k < 0 || hdi[k].w != 220 || hdi[k].h != 160 || !hdi[k].haspal) return NULL;
    if (im != last) {
        const HdImg *m = &hdi[k];
        for (int i = 0; i < 220*160; i++) { BYTE v = m->idx[0][i]; rgb[i*3] = m->pal[v*3]; rgb[i*3+1] = m->pal[v*3+1]; rgb[i*3+2] = m->pal[v*3+2]; }
        last = im;
    }
    strcpy(name_out, names[im]); return rgb;
}
/* foto in modalita' foto -> immagine dell'anteprima della missione corrente (UI_HD\NOMEF.raw), subito in uso */
static void photo_to_mission(const BYTE *bgr, int W, int H, size_t stride)
{
    char name[16]; if (!ovl_rgb(name)) return;
    int k = hd_find(name); if (k < 0) return;
    int w = W / 4, h = H / 4; if (w > 1024 || h > 480 || w < 1 || h < 1) return;
    int PW = w*4, PH = h*4, ox = (W - PW)/2, oy = (H - PH)/2;
    BYTE *rgba = malloc((size_t)PW*PH*4); if (!rgba) return;
    for (int y = 0; y < PH; y++) {   /* la foto e' dal basso verso l'alto (BMP) */
        const BYTE *s = bgr + (size_t)(H - 1 - (y + oy))*stride + ox*3; BYTE *d = rgba + (size_t)y*PW*4;
        for (int x = 0; x < PW; x++) { d[x*4] = s[x*3+2]; d[x*4+1] = s[x*3+1]; d[x*4+2] = s[x*3]; d[x*4+3] = 255; }
    }
    char fn[32], rel[64], path[MAX_PATH]; snprintf(fn, sizeof fn, "%sF", name); snprintf(rel, sizeof rel, "UI_HD\\%s.raw", fn); path_in_dir(path, rel);
    FILE *f = fopen(path, "wb");
    if (f) {
        DWORD wh[2] = { (DWORD)w, (DWORD)h }; fwrite("MHB3", 1, 4, f); fwrite(wh, 4, 2, f);
        BYTE *z = calloc((size_t)w*h, 1); if (z) { fwrite(z, 1, (size_t)w*h, f); free(z); }
        fwrite(rgba, 1, (size_t)PW*PH*4, f); fwrite(hdi[k].pal, 1, 768, f); fclose(f);
    }
    int kf = hd_find(fn);   /* aggiorna subito l'immagine in memoria */
    if (kf < 0 && n_hdi < 64) { kf = n_hdi++; memset(&hdi[kf], 0, sizeof hdi[kf]); strcpy(hdi[kf].name, fn); }
    if (kf >= 0) {
        HdImg *m = &hdi[kf]; free(m->rgb[0]); free(m->idx[0]);
        m->w = w; m->h = h; m->rgb[0] = rgba; m->idx[0] = calloc((size_t)w*h, 1); memcpy(m->pal, hdi[k].pal, 768); m->haspal = 1;
        n_bprev = -1;   /* ricostruisci il composito */
    } else free(rgba);
    mhlog("foto missione: anteprima di %s aggiornata (%s.raw, %dx%d)", name, fn, PW, PH);
}
#include "gl_present.inc"

static void __cdecl mh_present(void)
{
    HDC hdc = G_HDC; HWND hw = G_HWND; BITMAPINFO *bmi = G_BMI; BYTE *bits = G_BITS;
    static int nlog = 0;
    if (nlog < 3) { mhlog("present #%d hdc=%p hwnd=%p bmi=%p bits=%p W=%d H=%d", nlog, hdc, hw, bmi, bits, G_W, G_H); nlog++; }
    if (!hdc || !bmi || !bits) return;
    int W = G_W, H = G_H;
    fps_n++; if (GetTickCount() - fps_t0 >= 5000) { if (fps_t0) mhlog("fps %.1f (ricerca menu %.1f ms/fotogramma)", fps_n*1000.0/(GetTickCount()-fps_t0), fps_n ? hd_ms/fps_n : 0); fps_t0 = GetTickCount(); fps_n = 0; hd_ms = 0; }
    BYTE *uib = NULL; int has3d = 1; int UW = ui_VW, UH = ui_VH;
    if (ui_on) {
        uib = bits; bits = ui_f3; W = ui_W; H = ui_H;
        has3d = *(volatile DWORD*)0x74b810; *(volatile DWORD*)0x74b810 = 0;
        if (has3d) {   /* dimensioni del livello 2D usate in questo fotogramma (HUD) */
            UW = *(volatile int*)0x48cdc8; UH = *(volatile int*)0x48cdcc;
            if (UW < 320 || UW > ui_W || UH < 240 || UH > ui_H) { UW = ui_VW; UH = ui_VH; }
        }
        if (cfg_shot == 4 || cfg_start_level > 1) {   /* livello forzato (scansione o [startup] level): nuova partita (livello 0) -> livello scelto */
            static int forced; int want = cfg_shot == 4 ? GetPrivateProfileIntA("scan", "level", -1, ini_path) : cfg_start_level - 1;
            if (has3d) forced = 2;   /* in gioco: non toccare piu' */
            if (forced < 2 && want > 0 && *(volatile int*)0x4b1650 == 0) {
                *(volatile int*)0x4b1650 = want; if (forced) goto lv_done; forced = 1;
                char line[400]; int n = 0; for (int i = 0; i < 24; i++) n += sprintf(line + n, "%d:%d ", i, ((BYTE*)0x48a8b0)[16*i + 1]);
                mhlog("scansione: livello forzato %d (tabella immagini %s)", want, line);
            }
            lv_done:;
        }
        if (cfg_shot == 4 && has3d) {   /* scansione con telecamera libera: miniature della scena su una griglia (immagini delle missioni) */
            static int gp, si, x0s, y0s, z0; static char sdir[MAX_PATH];
            dbg_on = 1; dbg_ax = dbg_ay = 0; gp++;
            int NX = GetPrivateProfileIntA("scan", "nx", 8, ini_path), NY = GetPrivateProfileIntA("scan", "ny", 8, ini_path);
            int STEP = GetPrivateProfileIntA("scan", "step", 512, ini_path), SET = GetPrivateProfileIntA("scan", "settle", 3, ini_path);
            int CAP = GetPrivateProfileIntA("scan", "capture", 2, ini_path), DIV = GetPrivateProfileIntA("scan", "div", 8, ini_path);
            int NA = GetPrivateProfileIntA("scan", "nang", 1, ini_path), A0 = GetPrivateProfileIntA("scan", "angle", -1, ini_path), AS = GetPrivateProfileIntA("scan", "astep", 64, ini_path);
            if (gp == 150) {
                int px = *(volatile int*)0x621b98, py = *(volatile int*)0x621b9c; z0 = *(volatile int*)0x621ba0;
                x0s = GetPrivateProfileIntA("scan", "x0", (px >> 16) - STEP*NX/2, ini_path); y0s = GetPrivateProfileIntA("scan", "y0", (py >> 16) - STEP*NY/2, ini_path);
                path_in_dir(sdir, "SCAN"); CreateDirectoryA(sdir, NULL);
                mhlog("scansione: giocatore %d,%d z %d, origine %d,%d passo %d, %dx%d, angolo %d (ora %d)", px >> 16, py >> 16, z0 >> 16, x0s, y0s, STEP, NX, NY, cam_ang, *(volatile int*)0x4b164c);
            }
            if (gp >= 150 && si < NX*NY*NA) {
                int ph = (gp - 150) % SET, ai = si % NA, pi_ = si / NA, gx = pi_ % NX, gy = pi_ / NX;
                if (ph == 0) { cam_x = (x0s + gx*STEP) << 16; cam_y = (y0s + gy*STEP) << 16; cam_z = z0; cam_ang = A0 < 0 ? -1 : (A0 + ai*AS) & 2047; cam_ovr = 1;
                    { int ZF = GetPrivateProfileIntA("scan", "zoom", 0, ini_path); if (ZF) *(volatile int*)0x48aca0 = ZF; }
                    int ZS = GetPrivateProfileIntA("scan", "zoomstep", 0, ini_path); if (ZS) { cam_ang = -1; *(volatile int*)0x48aca0 = GetPrivateProfileIntA("scan", "zoom0", 2048, ini_path) + ai*ZS; } }
                if (ph == CAP) {
                    if (GetPrivateProfileIntA("scan", "photo", 0, ini_path)) photo_req = 1;
                    int w = W/DIV, h = H/DIV; BYTE *o = malloc((size_t)w*h*3);
                    if (o) {
                        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
                            int r = 0, g = 0, b = 0, n = 0;
                            for (int j = 0; j < DIV; j += 2) for (int i = 0; i < DIV; i += 2) { RGBQUAD c = bmi->bmiColors[bits[(size_t)(y*DIV+j)*W + x*DIV+i]]; r += c.rgbRed; g += c.rgbGreen; b += c.rgbBlue; n++; }
                            o[(y*w+x)*3] = r/n; o[(y*w+x)*3+1] = g/n; o[(y*w+x)*3+2] = b/n;
                        }
                        char nm[MAX_PATH]; sprintf(nm, "%s/s%04d.raw", sdir, si); FILE *f = fopen(nm, "wb");
                        if (f) { int hd[6] = { w, h, x0s + gx*STEP, y0s + gy*STEP, cam_ang, 0 }; fwrite(hd, 4, 6, f); fwrite(o, 1, (size_t)w*h*3, f); fclose(f); }
                        free(o);
                    }
                    si++; if (si == NX*NY*NA) { mhlog("scansione finita (%d scatti)", si); cam_ovr = 0; cam_ang = -1; }
                }
            }
        }
        static unsigned drf; if (cfg_shot == 3) {   /* debug: guida simulata + istantanee di .data per trovare la telecamera */
            drf++; dbg_on = 1; dbg_ax = 0; dbg_ay = 0;
            if (drf >= 1000 && drf < 1045) dbg_ay = -127;
            if (drf >= 1200 && drf < 1245) dbg_ax = 127;
            if (drf == 1390) shot_req = 10;
            if (drf == 1400) { *(volatile int*)0x621ba4 += 0x2000000; *(volatile int*)0x621b9c += 0x2000000; }
            if (drf == 1430) { shot_req = 11; photo_req = 1; }
            if (drf == 990 || drf == 1100 || drf == 1300) {
                char nm[32], path[MAX_PATH]; sprintf(nm, "DUMP%u.bin", drf); path_in_dir(path, nm); FILE *f = fopen(path, "wb");
                if (f) { fwrite((void*)0x478000, 1, 0x2d4000, f); fclose(f); mhlog("debug: %s", nm); }
            }
        }
        static unsigned dbgf; if (cfg_shot == 1 && ++dbgf == 400) {   /* debug: livello 2D grezzo + palette */
            char path[MAX_PATH]; path_in_dir(path, "MHUI2D.raw"); FILE *f = fopen(path, "wb");
            if (f) { fwrite(&UW, 4, 1, f); fwrite(&UH, 4, 1, f); fwrite(uib, 1, (size_t)UW*UH, f); fwrite(bmi->bmiColors, 4, 256, f); fclose(f); }
        }
        *(volatile DWORD*)0x48cdc8 = ui_VW; *(volatile DWORD*)0x48cdcc = ui_VH;   /* resta in modalita' 2D */
    }
    fix_window(hw);
    cull_update(); spawn_update();
    static int pm_test; if (cfg_shot == 6 && has3d) pm_test++;
    static int rp_t; if (cfg_shot == 7 && has3d) rp_t++; rz_frame_t = rp_t; if (cfg_shot == 7 && GetPrivateProfileIntA("replay", "pausa", 0, ini_path)) {   /* prova: pausa (INVIO) -> giu' fino a ESCI -> CTRL -> finestra ESCI? */
        static int pz; if (rp_t >= 480) pz++;
        struct { int t, vk, up; } ev[] = { {1, VK_RETURN, 0}, {3, VK_RETURN, 1}, {70, VK_CONTROL, 0}, {72, VK_CONTROL, 1} };
        dbg_on = 0; if ((pz >= 40 && pz < 46) || (pz >= 120 && pz < 126)) { dbg_on = 1; dbg_ax = 0; dbg_ay = 127; }   /* giu' col joystick */
        for (int i = 0; i < 4; i++) if (pz == ev[i].t) PostMessageA(hw, ev[i].up ? WM_KEYUP : WM_KEYDOWN, ev[i].vk, ev[i].up ? 0xC0000001 : 1);
        if (pz == 30) shot_req = 54; if (pz == 110) shot_req = 55;
        if (pz == 150) shot_req = 56;
    } if (cfg_shot == 7 && GetPrivateProfileIntA("replay", "mappa", 0, ini_path)) {   /* prova: apre la mappa (TAB) e fotografa la finestra */
        static int mp_t; if (rp_t >= 480) mp_t++;
        if (mp_t == 1) PostMessageA(hw, WM_KEYDOWN, VK_TAB, 1); if (mp_t == 3) PostMessageA(hw, WM_KEYUP, VK_TAB, 0xC0000001);
        if (mp_t == 60) shot_req = 51;
    }
    if (cfg_shot == 7) { static int mp; mp++;
        if (GetPrivateProfileIntA("replay", "esci", 0, ini_path)) {   /* prova: menu principale -> giu' fino a ESCI -> conferma -> finestra "ESCI?" */
            int ph = mp - 450; dbg_on = 0; dbg_jbtn = 0;
            if (ph >= 0 && ph < 4*24 && (ph % 24) < 6) { dbg_on = 1; dbg_ax = 0; dbg_ay = 127; }
            if (ph >= 4*24 + 10 && ph < 4*24 + 14) dbg_jbtn = 1;
            if (ph == 4*24 + 60) shot_req = 52;
            if (ph >= 4*24 + 70 && ph < 4*24 + 80) { dbg_on = 1; dbg_ax = 0; dbg_ay = 127; }
            if (ph == 4*24 + 110) shot_req = 53;
        } else dbg_jbtn = (rp_t == 0 && mp > 60 && (mp % 75) < 4) ? 1 : 0; }   /* menu e briefing: preme "conferma" sul pad finche' non parte la missione */
    if (cfg_shot == 7 && GetPrivateProfileIntA("replay", "nemico_casse", 0, ini_path) && rp_t >= 440 && rp_t < 1400) {   /* prova IA vicino alle casse (BARRACKS) */
        static int who = -1;
        if (who < 0 && rp_t == 440) { int best = -1, bd = 1 << 30; const int *pl = (const int*)0x621b98;
            for (int i = 8; i < 48; i++) { BYTE *r = (BYTE*)0x621b98 + 0x2b4*i; if (!*(int*)(r + 0x58) || *(int*)(r + 0x94) != 0x401ebf) continue;
                int d = abs((((int*)r)[0] >> 16) - (pl[0] >> 16)) + abs((((int*)r)[1] >> 16) - (pl[1] >> 16)); if (d < bd) { bd = d; best = i; } }
            who = best; if (who >= 0) { int *r = (int*)((BYTE*)0x621b98 + 0x2b4*who); r[0] = 7000 << 16; r[1] = 13200 << 16; r[2] = 512 << 16; *(short*)((BYTE*)r + 0xe) = 1536;
                mhlog("prova IA: nemico %d portato a 7000,13200 z512 verso la striscia", who); } }
        if (who >= 0 && (rp_t % 6) == 0) { BYTE *r = (BYTE*)0x621b98 + 0x2b4*who;
            mhlog("prova IA t%d: #%d pos %d,%d z%d dir %d stato %d vel %d tasti %04x", rp_t, who, ((int*)r)[0] >> 16, ((int*)r)[1] >> 16, ((int*)r)[2] >> 16,
                  *(short*)(r + 0xe) & 2047, r[0x6b], ((int*)r)[5], *(WORD*)(r + 0x64)); }
    }
    if (cfg_shot == 7 && rp_t == 200 && GetPrivateProfileIntA("replay", "percorsi", 0, ini_path)) ai_check_paths(*(volatile int*)0x4b1650);
    if (cfg_shot == 7 && rp_t == 200 && GetPrivateProfileIntA("replay", "entita_dump", 0, ini_path)) {   /* debug: entita' con percorso */
        BYTE *eb = *(BYTE**)0x6b70c0; int n = 0;
        for (int e = 0; eb && e < 4096 && n < 400; e++) {
            BYTE *en = eb + e*36; if (IsBadReadPtr(en, 36)) break;
            BYTE *path = *(BYTE**)(en + 0x18); if (!path || IsBadReadPtr(path, 16)) continue;
            int np = ((short)(*(short*)(path + 2) << 1)) >> 1; if (np <= 0 || np > 200) continue;
            char hx[120]; for (int k = 0; k < 36; k++) sprintf(hx + k*3, "%02x ", en[k]);
            mhlog("entita %d: %s| percorso %d punti, primo %d,%d", e, hx, np, ((short*)(path + 8))[0], ((short*)(path + 8))[1]); n++;
        }
        mhlog("entita' con percorso: %d", n);
    }
    if (cfg_shot == 7 && (rp_t == 520 || rp_t == 640)) { const BYTE *r_ = (const BYTE*)0x621b98 + 0x2b4 * *(volatile int*)0x629d5c; mhlog("replay %d: giocatore %d,%d z%d dir %d vita %d", rp_t, ((const int*)r_)[0] >> 16, ((const int*)r_)[1] >> 16, ((const int*)r_)[2] >> 16, *(const short*)(r_ + 0xe) & 2047, *(const int*)(r_ + 0x234)); if (rp_t == 640) { photo_req = 1; shot_req = 50; } }   /* debug: rifa' un'inquadratura ([replay] dal .txt) in modalita' foto */
    if (cfg_shot == 6 && has3d && (pm_test % 20) == 0) mhlog("poligoni: %d (zoom %d)", *(volatile int*)0x65a6f0, *(volatile int*)0x48aca0);
    if (cfg_shot == 6 && (pm_test == 210 || pm_test == 440 || pm_test == 760)) {
        char line[600]; int n = 0, k = 0;
        for (int i = 0; i < 48 && k < 8; i++) { const BYTE *r = (const BYTE*)0x621b98 + 0x2b4*i; if (!*(const int*)(r + 0x58)) continue; k++;
            n += sprintf(line + n, "[%d %d,%d z%d f%06x t%d] ", i, ((const int*)r)[0] >> 16, ((const int*)r)[1] >> 16, *(const short*)(r + 0x1a), *(const int*)(r + 0x94), *(const short*)(r + 0x10)); }
        mhlog("prova foto %d: personaggi %s", pm_test, line);
        const int *cm = (const int*)0x4b1930;
        mhlog("prova foto %d: zona %d  aca0 %d aca8 %d  cam +10 %d +1c %d +20 %d +24 %d +2c %d +30 %d +34 %d +38 %d +3c %d", pm_test, *(volatile int*)0x4b7bf4,
              *(volatile int*)0x48aca0, *(volatile int*)0x48aca8, cm[4], cm[7], cm[8], cm[9], cm[11], cm[12], cm[13], cm[14], cm[15]);
    }
    if (cfg_shot == 6 && (pm_test == 480 || pm_test == 750)) { shot_req = pm_test == 480 ? 44 : 45; BYTE *r_ = (BYTE*)0x621b98 + 0x2b4 * *(volatile int*)0x629d5c; mhlog("prova foto: giocatore %d,%d z %d vita %d/%d", ((int*)r_)[0] >> 16, ((int*)r_)[1] >> 16, ((int*)r_)[2] >> 16, *(int*)(r_ + 0x234), *(int*)(r_ + 0x238)); }
    {   /* modalita' foto: R3 / F10 entra-esce; il giocatore resta fermo, si muove solo la telecamera; croce / F12 scatta */
        int fg = GetForegroundWindow() == hw; PadState ps; EnterCriticalSection(&pad_cs); ps = pad; LeaveCriticalSection(&pad_cs);
        static unsigned pb; unsigned pressed = ps.ok ? ps.btn & ~pb : 0; pb = ps.ok ? ps.btn : 0;
        static int sv_pos[3], sv_hp, *hp, sv_zoom, sv_zone = -1, sv_angd; BYTE *rec = (BYTE*)0x621b98 + 0x2b4 * *(volatile int*)0x629d5c;
        static int f10, f12; int k10 = fg && (GetAsyncKeyState(VK_F10) & 0x8000), k12 = fg && (GetAsyncKeyState(VK_F12) & 0x8000);
        if ((k10 && !f10) || (pressed & 1024) || pm_test == 200 || pm_test == 450 || rp_t == 150 || rp_t == 420) {
            photo_mode = !photo_mode && ui_on;
            if (photo_mode) {
                memcpy(sv_pos, rec, 12); hp = (int*)(rec + 0x234); sv_hp = *hp; sv_angd = (*(int*)(rec + 0x258) - *(short*)(rec + 0xe)) & 2047;   /* vita (barra verde dell'HUD: +0x234 su massimo +0x238) */
                { char line[900]; int n = 0, k = 0;
                  for (int i = 0; i < 48 && k < 20; i++) { const BYTE *r = (const BYTE*)0x621b98 + 0x2b4*i; if (!*(const int*)(r + 0x58)) continue; k++;
                      n += sprintf(line + n, "[%d %d,%d pensa %06x tipo %d] ", i, ((const int*)r)[0] >> 16, ((const int*)r)[1] >> 16, *(const int*)(r + 0x94), *(const short*)(r + 0x10)); }
                  mhlog("modalita' foto: personaggi attivi %s", line); }
                sv_zoom = *(volatile int*)0x48aca0; sv_zone = *(volatile int*)0x4b7bf4;
                ghost_player = 1; *(volatile DWORD*)0x74b834 = (DWORD)(DWORD_PTR)rec; for (int i = 0; i < 48; i++) actor_was[i] = *(const int*)((const BYTE*)0x621b98 + 0x2b4*i + 0x58) != 0;
                memset(actor_free, 0, sizeof actor_free); *(volatile DWORD*)0x74b840 = (DWORD)(DWORD_PTR)actor_free;
                *(volatile DWORD*)0x74b838 = 0x40266c; *(volatile DWORD*)0x74b83c = 0x401253;   /* oggetti e ostaggi */ *(volatile DWORD*)0x74b830 = 1;   /* mondo congelato tranne ostaggi/oggetti (solo animazione) */
                const int *pr = (const int*)rec, *cm = (const int*)0x4b1930;
                cam_x = pr[0]; cam_y = pr[1]; cam_z = pr[2]; cam_ang = -1; cam_yaw_set = -1; cam_ovr = 1;
            } else { cam_ovr = 0; cam_ang = -1; cam_yaw_set = -1; cam_mode = 0;
                npc_aim = 0; memset(aim_on, 0, sizeof aim_on);   /* le posizioni sono gia' quelle salvate (rimesse a ogni fotogramma) */
                int keep = pl_placed;   /* personaggio teletrasportato: quella diventa la sua posizione (e direzione, e piano) */
                ghost_player = 0; pl_vis = pl_placed = 0; if (sv_zoom) *(volatile int*)0x48aca0 = sv_zoom;
                int zto = keep ? pl_zone : sv_zone;
                if (zto >= 0 && zto != *(volatile int*)0x4b7bf4) { BYTE *map = *(BYTE**)0x658118; if (map) ((void (__cdecl*)(BYTE*, int))0x41c960)(map, zto); }
                if (zto >= 0) *(short*)(rec + 0x1a) = (short)zto;
                sv_zone = -1; *(volatile DWORD*)0x74b830 = 0; memcpy(rec, keep ? pl_pos : sv_pos, 12); if (hp) *hp = sv_hp;
                if (keep) { *(short*)(rec + 0xe) = (short)pl_ang; *(int*)(rec + 0x258) = (pl_ang + sv_angd) & 2047;
                            mhlog("modalita' foto: nuova posizione del personaggio %d,%d z%d direzione %d", pl_pos[0] >> 16, pl_pos[1] >> 16, pl_pos[2] >> 16, pl_ang); } }
            mhlog("modalita' foto %s", photo_mode ? "attiva" : "spenta"); MessageBeep(photo_mode ? MB_ICONASTERISK : MB_OK);
        }
        { static int fn_; int kn = fg && (GetAsyncKeyState('N') & 0x8000);   /* piano della mappa (teletrasporti, ascensori): Cerchio / N */
          if (photo_mode && ((kn && !fn_) || (pressed & 4))) {
              BYTE *map = *(BYTE**)0x658118; int cnt = map ? *(int*)(map + 0x1c) : 0;
              if (cnt > 1 && cnt <= 16) { int z = (*(volatile int*)0x4b7bf4 + 1) % cnt; ((void (__cdecl*)(BYTE*, int))0x41c960)(map, z); *(short*)(rec + 0x1a) = (short)z;
                  mhlog("modalita' foto: piano %d di %d", z + 1, cnt); MessageBeep(MB_OK); }
          }
          fn_ = kn; }
        { static int fp; int kp = fg && (GetAsyncKeyState('P') & 0x8000);
          if (photo_mode && ((kp && !fp) || (pressed & 2))) { ovl_alpha = ovl_alpha == 0 ? 0.5f : ovl_alpha > 0.4f ? 0.25f : 0; mhlog("modalita' foto: sovrapposizione %d%%", (int)(ovl_alpha*100)); }
          fp = kp; if (!photo_mode) ovl_alpha = 0; }
        if ((k12 && !f12) || (photo_mode && (pressed & 1))) {
            photo_req = 1; poly_req = 1;
            char line[1600]; int n = 0, k = 0;   /* personaggi attivi vicino all'inquadratura (per riconoscere ostaggi e nemici) */
            for (int i = 0; i < 48 && k < 30; i++) { const BYTE *r = (const BYTE*)0x621b98 + 0x2b4*i; if (!*(const int*)(r + 0x58)) continue;
                int dx = (((const int*)r)[0] - cam_x) >> 16, dy = (((const int*)r)[1] - cam_y) >> 16; if (photo_mode && (abs(dx) > 4000 || abs(dy) > 4000)) continue; k++;
                n += sprintf(line + n, "[%d %+d,%+d pensa %06x] ", i, dx, dy, *(const int*)(r + 0x94)); }
            mhlog("foto: personaggi vicini (rispetto alla telecamera) %s", line);
        }
        { static int fm; int km = fg && (GetAsyncKeyState('M') & 0x8000);
          if (photo_mode && ((km && !fm) || (pressed & 8))) { cam_mode = (cam_mode + 1) % 3; mhlog("modalita' foto: telecamera %s", cam_mode == 0 ? "come nel gioco" : cam_mode == 1 ? "che ruota" : "fissa"); MessageBeep(MB_OK); }
          fm = km; }
        f10 = k10; f12 = k12;
        if (photo_mode) {
            double mx = 0, my = 0, rot = 0;
            if (ps.ok) {
                int lx = ps.lx - 128, ly = ps.ly - 128, rx = ps.rx - 128;
                if (abs(lx) > cfg_deadzone) mx += lx / 128.0; if (abs(ly) > cfg_deadzone) my -= ly / 128.0;
                if (abs(rx) > cfg_deadzone) rot += rx / 128.0;
                if (ps.btn & 16) rot -= 0.5; if (ps.btn & 32) rot += 0.5;
                static const int hx[] = {0, 1, 1, 1, 0, -1, -1, -1}, hy[] = {1, 1, 0, -1, -1, -1, 0, 1};
                if (ps.dpad < 8) { if (pl_placed && hx[ps.dpad]) pl_ang = (pl_ang + hx[ps.dpad]*16) & 2047;   /* sinistra/destra: gira il personaggio */
                                   else { mx += hx[ps.dpad]*0.15; my += hy[ps.dpad]*0.15; } }
            }
            if (cfg_shot == 6 && pm_test >= 232 && pm_test < 236) { cam_x = 2432 << 16; cam_y = 9344 << 16; }   /* prova: giocatore sopra l'oggetto 9 */
            if (cfg_shot == 6 && pm_test > 300 && pm_test < 330) *(volatile int*)0x48aca0 += 24;
            if (cfg_shot == 7 && rp_t >= 152 && rp_t < 400) {
                char b_[64]; GetPrivateProfileStringA("replay", "free_cam", "0,0,0", b_, sizeof b_, ini_path); if (b_[0] == 's') { cam_x = sv_pos[0]; cam_y = sv_pos[1]; cam_z = sv_pos[2]; } else sscanf(b_, "%d,%d,%d", (int*)&cam_x, (int*)&cam_y, (int*)&cam_z);   /* "start" = punto di partenza */
                cam_yaw_set = GetPrivateProfileIntA("replay", "free_yaw", -1, ini_path); cam_mode = GetPrivateProfileIntA("replay", "cam_mode", 0, ini_path);
                { int ru = GetPrivateProfileIntA("replay", "ruota", 0, ini_path);   /* prova: telecamera che gira piano (unita' 1/2048 di giro per fotogramma) e si sposta */
                  if (ru && cam_yaw_set != -1 && rp_t >= 260) { cam_yaw_set += ((rp_t - 260) * ru) << 16; cam_x += ((rp_t - 260) * 3) << 16; }
                  if (ru && (rp_t == 280 || rp_t == 300 || rp_t == 320 || rp_t == 340 || rp_t == 360)) { shot_req = 60 + (rp_t - 280) / 20; mhlog("replay ruota %d: imbardata %d", rp_t, (cam_yaw_set >> 16) & 2047); } }
                *(volatile int*)0x48aca0 = GetPrivateProfileIntA("replay", "zoom", 2048, ini_path);
                int zf = GetPrivateProfileIntA("replay", "zplus", 0, ini_path); cam_z += zf << 16;
                { int pz = GetPrivateProfileIntA("replay", "piano", -1, ini_path); BYTE *map = *(BYTE**)0x658118;   /* prova: piano della mappa (passerelle, ascensori) */
                  if (pz >= 0 && map && pz != *(volatile int*)0x4b7bf4 && pz < *(int*)(map + 0x1c)) { ((void (__cdecl*)(BYTE*, int))0x41c960)(map, pz); *(short*)(rec + 0x1a) = (short)pz; } }
                if (rp_t < 260) cam_x += GetPrivateProfileIntA("replay", "far", 0, ini_path) << 16;   /* prova: prima lontano, poi sul punto */
                if (rp_t == 330 || rp_t == 390) {
                    char line[3000]; int n = 0, k = 0;
                    for (int i = 0; i < 48 && k < 48; i++) { const BYTE *r = (const BYTE*)0x621b98 + 0x2b4*i; if (!*(const int*)(r + 0x58)) continue;
                        int dx = (((const int*)r)[0] - cam_x) >> 16, dy = (((const int*)r)[1] - cam_y) >> 16; k++;
                        n += sprintf(line + n, "[%d %+d,%+d z%d piano%d pensa %06x] ", i, dx, dy, ((const int*)r)[2] >> 16, *(const short*)(r + 0x1a), *(const int*)(r + 0x94)); }
                    mhlog("replay %d: poligoni %d, giocatore %d,%d z%d, personaggi %s", rp_t, *(volatile int*)0x65a6f0, ((int*)rec)[0] >> 16, ((int*)rec)[1] >> 16, ((int*)rec)[2] >> 16, line);
                    if (rp_t == 390) { photo_req = 1; mhlog("replay 390: coordinate verificate %d non verificabili %d corrette %d", rz_nok, rz_nbad, rz_nfix); }
                    for (int i = 8; i < 48; i++) { const BYTE *r = (const BYTE*)0x621b98 + 0x2b4*i; if (!*(const int*)(r + 0x58) || *(const int*)(r + 0x94) != 0x401460) continue;
                        mhlog("  torretta %d: %+d,%+d z%d ang %d stato %d 25c %d bersagli %08x %d %d %d / %08x %d %d %d", i, (((const int*)r)[0] - cam_x) >> 16, (((const int*)r)[1] - cam_y) >> 16, ((const int*)r)[2] >> 16,
                              *(const short*)(r + 0xe) & 2047, r[0x6b], *(const short*)(r + 0x25c), *(const int*)(r + 0xa0), *(const short*)(r + 0xa4), *(const int*)(r + 0xa8), *(const int*)(r + 0xac),
                              *(const int*)(r + 0xb8), *(const short*)(r + 0xbc), *(const int*)(r + 0xc0), *(const int*)(r + 0xc4)); }
                    if (rp_t == 390) {   /* debug: entita' della mappa vicine alla telecamera */
                        const BYTE *g = *(const BYTE**)0x4bb080; int ng = *(volatile int*)0x4b197c, ne = 0; const BYTE *eb = *(const BYTE**)0x6b70c0;
                        mhlog("entita': gruppi %d, rettangolo x+-%d", ng, *(int*)0x451488);
                        for (int gi = 0; gi < ng && gi < 16; gi++, g += 0x80) {
                            const BYTE *rows = *(const BYTE**)(g + 0x28); int r0 = *(const int*)(g + 0x10), nr = *(const int*)(g + 0x18);
                            mhlog("  gruppo %d: righe %d..%d", gi, r0, r0 + nr - 1);
                            for (int ri = 0; rows && ri < nr; ri++) for (const BYTE *e = *(const BYTE**)(rows + ri*36); e; e = *(const BYTE**)e) {
                                int dx = *(const short*)(e + 6) - (cam_x >> 16), dy = *(const short*)(e + 8) - (cam_y >> 16);
                                if (abs(dx) > 5000 || abs(dy) > 5000 || ne > 60) continue; ne++;
                                const DWORD *tt = (const DWORD*)(0x495db0 + e[4]*16);
                                mhlog("    n%d tipo %d (%lx %lx %x) %+d,%+d z%d fl %02x/%02x att %p", (int)((e - eb)/36), e[4], tt[0], tt[1], *(const short*)((const BYTE*)tt + 0xa), dx, dy, *(const short*)(e + 0xa), e[0xc], e[0x1e], *(void* const*)(e + 0x14));
                            }
                        }
                    }
                }
            }
            if (cfg_shot == 6 && pm_test == 335) { BYTE *map = *(BYTE**)0x658118; int cnt = map ? *(int*)(map + 0x1c) : 0; mhlog("prova: piani %d", cnt);
                if (cnt > 1) { ((void (__cdecl*)(BYTE*, int))0x41c960)(map, 1); *(short*)(rec + 0x1a) = 1; } }
            if (cfg_shot == 6) { if (pm_test == 230) shot_req = 40; if (pm_test > 240 && pm_test < 270) my = 1; if (pm_test > 395 && pm_test < 445) { mx = -1; my = 1; } if (pm_test == 280) shot_req = 41;
                                 if (pm_test > 290 && pm_test < 320) mx = 1; if (pm_test == 330) shot_req = 42; if (pm_test > 340 && pm_test < 380) rot = 1; if (pm_test == 390) { shot_req = 43; photo_req = 1; } }
            if (fg) {
                if (GetAsyncKeyState('J') & 0x8000) mx -= 1; if (GetAsyncKeyState('L') & 0x8000) mx += 1;
                if (GetAsyncKeyState('I') & 0x8000) my += 1; if (GetAsyncKeyState('K') & 0x8000) my -= 1;
                if (GetAsyncKeyState('U') & 0x8000) rot -= 0.5; if (GetAsyncKeyState('O') & 0x8000) rot += 0.5;
                if (pl_placed) { if (GetAsyncKeyState('Z') & 0x8000) pl_ang = (pl_ang - 16) & 2047; if (GetAsyncKeyState('X') & 0x8000) pl_ang = (pl_ang + 16) & 2047; }
            }
            if (rot != 0 && cam_yaw_set < 0) cam_yaw_set = ((((const int*)0x4b1930)[5] >> 16) - 512) & 2047;
            int cy = cam_yaw_set >= 0 ? cam_yaw_set : (((((const int*)0x4b1930)[5] >> 16) - 512) & 2047);
            double th = cy * 6.283185307 / 2048.0, sp = 28.0 * 65536;   /* velocita': unita' del mondo per fotogramma */
            /* tarato: "su" sullo schermo = (cos, sin) nel mondo, "destra" = (-sin, cos) */
            cam_x += (int)((my*cos(th) - mx*sin(th)) * sp);
            cam_y += (int)((my*sin(th) + mx*cos(th)) * sp);
            if (cam_yaw_set >= 0) cam_yaw_set = (cam_yaw_set + (int)(rot * 8)) & 2047;
            { static int ft, fv; int kt = fg && (GetAsyncKeyState('T') & 0x8000), kv = fg && (GetAsyncKeyState('V') & 0x8000);
              int rt = cfg_shot == 7 && rp_t == 300 && GetPrivateProfileIntA("replay", "teletrasporto", 0, ini_path);
              if ((kt && !ft) || (pressed & 256) || rt) {   /* Options / T: il personaggio si teletrasporta sotto la telecamera e si vede */
                  if (!((void *(__cdecl*)(BYTE*))0x41e9b0)(rec)) { mhlog("modalita' foto: qui non c'e' mappa, personaggio non spostato"); MessageBeep(MB_ICONHAND); }
                  else { if (!pl_placed) pl_ang = *(short*)(rec + 0xe) & 2047;
                         memcpy(pl_pos, rec, 12); pl_zone = *(volatile int*)0x4b7bf4; pl_placed = 1; pl_vis = 1;
                         if (rt) pl_ang = (pl_ang + GetPrivateProfileIntA("replay", "gira", 0, ini_path)) & 2047;
                         mhlog("modalita' foto: personaggio qui (%d,%d z%d)", pl_pos[0] >> 16, pl_pos[1] >> 16, pl_pos[2] >> 16); MessageBeep(MB_OK); } }
              if ((kv && !fv) || (pressed & 2048)) {   /* L3 / touchpad / V: personaggio visibile o no */
                  if (!pl_placed) { memcpy(pl_pos, sv_pos, 12); pl_zone = sv_zone; pl_ang = *(short*)(rec + 0xe) & 2047; pl_placed = 1; }   /* mai spostato: resta dov'era */
                  pl_vis = !pl_vis; mhlog("modalita' foto: personaggio %s", pl_vis ? "visibile" : "invisibile"); MessageBeep(MB_OK); }
              ft = kt; fv = kv; }
            { static int fb; int kb = fg && (GetAsyncKeyState('B') & 0x8000);   /* NPC verso la telecamera: Create / B */
              if ((kb && !fb) || (pressed & 512) || (rp_t == 200 && GetPrivateProfileIntA("replay", "npc_aim", 0, ini_path))) {
                  npc_aim = !npc_aim;
                  if (!npc_aim) for (int i = 0; i < 48; i++) if (aim_on[i]) { if (aim_same(i, (BYTE*)0x621b98 + 0x2b4*i)) memcpy((BYTE*)0x621b98 + 0x2b4*i, aim_pos[i], 12); aim_on[i] = 0; actor_free[i] = 0; }
                  mhlog("modalita' foto: NPC verso la telecamera %s", npc_aim ? "si'" : "no"); MessageBeep(npc_aim ? MB_ICONASTERISK : MB_OK);
              }
              fb = kb; }
            for (int i = 0; i < 48; i++) {   /* personaggi attivati durante la modalita' foto: 20 fotogrammi liberi per completarsi (es. torretta del carro) */
                BYTE *r = (BYTE*)0x621b98 + 0x2b4*i; int act = *(const int*)(r + 0x58) != 0;
                int actor_was_prev = actor_was[i];
                if (act && !actor_was[i]) actor_free[i] = 20; else if (actor_free[i]) actor_free[i]--;
                actor_was[i] = (BYTE)act;
                if (npc_aim && act && i >= 8 && r != rec) {   /* pensano (si girano) ma restano nel punto salvato */
                    int ent = *(const int*)(r + 0x98);
                    if (!aim_same(i, r) || (act && !actor_was_prev)) { memcpy(aim_pos[i], r, 12); aim_ent[i] = ent; aim_think[i] = *(const int*)(r + 0x94); aim_on[i] = 1; }
                    else memcpy(r, aim_pos[i], 12);
                    if (actor_free[i] < 2) actor_free[i] = 2;
                } else aim_on[i] = 0;
            }
            {   /* zoom (distanza della telecamera, 0x48aca0; il gioco usa 2048): L2/R2 o Pag giu/Pag su */
                int z = 0; if (ps.ok) { if (ps.btn & 64) z += 24; if (ps.btn & 128) z -= 24; }
                if (fg) { if (GetAsyncKeyState(VK_NEXT) & 0x8000) z += 24; if (GetAsyncKeyState(VK_PRIOR) & 0x8000) z -= 24; }
                if (z) { int v = *(volatile int*)0x48aca0 + z; if (v < 1024) v = 1024; if (v > 6144) v = 6144; *(volatile int*)0x48aca0 = v; }
            }
            {   /* il giocatore (invisibile) segue la telecamera ma resta sotto il bordo dello schermo: attiva gli NPC inquadrati senza toccarli */
                int cy2 = ((((const int*)0x4b1930)[5] >> 16) - 512) & 2047; double t2 = cy2 * 6.283185307 / 2048.0, d = cfg_photo_dist * 65536.0;
                ((int*)rec)[0] = cam_x - (int)(cos(t2) * d); ((int*)rec)[1] = cam_y - (int)(sin(t2) * d);
                ((int*)rec)[2] = sv_pos[2];   /* altezza fissa: non cade sotto la mappa (il gioco nasconde cio' che e' sopra il giocatore) */
                if (npc_aim) {   /* i nemici vedono solo chi e' alla loro altezza (fascia di 256): quella del personaggio piu' vicino alla telecamera */
                    double best = 1e18; int bz = 0, found = 0;
                    for (int i = 8; i < 48; i++) { const int *r = (const int*)((const BYTE*)0x621b98 + 0x2b4*i); if (!r[0x58/4]) continue;
                        double dx = (double)((r[0] - cam_x) >> 16), dy = (double)((r[1] - cam_y) >> 16), d = dx*dx + dy*dy;
                        if (d < best) { best = d; bz = aim_same(i, (const BYTE*)r) ? aim_pos[i][2] : r[2]; found = 1; } }
                    if (found) ((int*)rec)[2] = bz;
                }
            }
            if (hp) *hp = sv_hp;
        }
    }
    { static int f11; int k = (GetAsyncKeyState(VK_F11) & 0x8000) != 0; if (k && !f11 && GetForegroundWindow() == hw) { hide_player = !hide_player; mhlog("giocatore %s", hide_player ? "invisibile" : "visibile"); } f11 = k; }
    if (cfg_shot == 5) { static int hc; if (++hc == 1000) rast_log = 400; if (hc == 1100) { shot_req = 30; } if (hc == 1101) hide_player = 1; if (hc == 1110) shot_req = 31; }
    /* menu HD: ricerca delle immagini originali nel livello 2D (solo nei fotogrammi senza 3D) */
    int hd_state = 0;
    if (ui_on && cfg_menu_hd && sm_on) {              /* source-map: menu e HUD */
        if (!hd_loaded) hd_load();
        hd_state = sm_build(UW, UH) ? 2 : 1;
        hd_fade = 1.0;
    } else if (ui_on && cfg_menu_hd && !has3d) {
        static unsigned cnt; static int last_ok;
        if (!hd_loaded) hd_load();
        if (n_hdi) {
            LARGE_INTEGER q0, q1, qf; QueryPerformanceCounter(&q0);
            int ok = n_m > 0 && hd_verify(uib, ui_VW, ui_VH);
            if (!ok || (cnt % 8) == 0) {
                if (hd_match(uib, ui_VW, ui_VH)) {
                    hd_build(ui_VW, ui_VH); hd_state = 2; hd_stable = 0;
                    hd_pal_ref = 0; for (int i = 0; i < 256; i++) hd_pal_ref += bmi->bmiColors[i].rgbRed + bmi->bmiColors[i].rgbGreen + bmi->bmiColors[i].rgbBlue;
                } else { hd_state = 1; hd_stable++; }
            }
            else { hd_state = 1; hd_stable++; }
            /* dissolvenze: la versione HD segue la luminosita' della palette */
            double ps = 0; for (int i = 0; i < 256; i++) ps += bmi->bmiColors[i].rgbRed + bmi->bmiColors[i].rgbGreen + bmi->bmiColors[i].rgbBlue;
            hd_fade = hd_pal_ref > 0 ? ps / hd_pal_ref : 1.0; if (hd_fade > 1.0) { hd_fade = 1.0; hd_pal_ref = ps; }
            cnt++;
            QueryPerformanceCounter(&q1); QueryPerformanceFrequency(&qf); hd_ms += (q1.QuadPart - q0.QuadPart)*1000.0/qf.QuadPart;
            last_ok = 1;
        }
    }
    /* RELIGHT: buffer per texel e luce, indirizzati dalle caverne nell'EXE tramite 0x74b800.. */
    static BYTE *texbuf; static DWORD *litbuf; static int rw, rh;
    int has_rl = *(BYTE*)0x43060f == 0xE9 && *(BYTE*)0x4308a0 == 0xE9;
    volatile DWORD *G = (volatile DWORD*)0x74b800;
    if (has_rl && cfg_relight) {
        if (!texbuf || rw != W || rh != H) {
            G[0] = 0; free(texbuf); free(litbuf);
            texbuf = calloc((size_t)W*H + 64, 1); litbuf = calloc((size_t)W*H + 64, 4); rw = W; rh = H;
            mhlog("relight: buffer %dx%d", W, H);
        }
        G[1] = (DWORD)(DWORD_PTR)texbuf - (DWORD)(DWORD_PTR)bits;
        G[2] = (DWORD)(DWORD_PTR)litbuf - 4*(DWORD)(DWORD_PTR)bits;
        G[0] = 1;
    }
    {   /* tuta del giocatore: nello sprite DROID1 la tuta usa i texel 33-38 e 42 (mh_tex.py player_color), che nessuna texture usa.
           Gli sprite passano dalla tabella luci a 8 bit del gioco (0x6460d0, 288 righe x 256): per quelle colonne viene riscritta.
           - arancione: stessi indici dell'arancione 196-202 a ogni luce (come l'originale);
           - altro colore: indici della palette che in questo livello la tabella non produce mai (liberi), ognuno con una luminosita'
             (dal quasi nero al chiaro); a ogni luce la tuta prende quello con la luminosita' dell'arancione corrispondente.
             La DLL da' a quegli indici le tinte (gl_present), solo per la scena 3D: l'HUD usa la palette originale. */
        static const BYTE suit[7] = { 33, 34, 35, 36, 37, 38, 42 }; BYTE *T = (BYTE*)0x6460d0; const RGBQUAD *pq = bmi->bmiColors;
        static BYTE is_suit[256]; if (!is_suit[33]) for (int k = 0; k < 7; k++) is_suit[suit[k]] = 1;
        int prev_n = suit_n; suit_n = 0;
        if (cfg_player_hue >= 0) {
            static BYTE used[256]; memset(used, 0, 256); used[0] = used[255] = 1;
            for (int r = 0; r < 288; r++) { const BYTE *row = T + r*256; for (int c = 0; c < 256; c++) if (!is_suit[c]) used[row[c]] = 1; }
            for (int pass = 0; pass < 2 && suit_n < 12; pass++)
                for (int c = 1; c < 255 && suit_n < 12; c++) {
                    if (used[c]) continue; const RGBQUAD *q = pq + c; int mag = q->rgbRed > 90 && q->rgbBlue > 90 && q->rgbGreen < 30;
                    int dup = 0; for (int m = 0; m < suit_n; m++) if (suit_out[m] == c) dup = 1;
                    if (!dup && (pass == 1 || mag)) suit_out[suit_n++] = (BYTE)c;
                }
            for (int m = 0; m < suit_n; m++) suit_v[m] = suit_n == 1 ? 0.5f : 0.06f * powf(0.95f / 0.06f, (float)m / (suit_n - 1));
            if (suit_n != prev_n) mhlog("tuta del giocatore: %d indici liberi per le sfumature", suit_n);
        }
        for (int r = 0; r < 288; r++) {
            BYTE *row = T + r*256;
            for (int k = 0; k < 7; k++) {
                BYTE j = row[196 + k];
                if (!suit_n) { row[suit[k]] = j; continue; }
                const RGBQUAD *c = pq + j; int mxc = c->rgbRed > c->rgbGreen ? c->rgbRed : c->rgbGreen; if (c->rgbBlue > mxc) mxc = c->rgbBlue;
                float v = mxc / 255.0f; if (v < 0.03f) { row[suit[k]] = j; continue; }   /* nero */
                int bm = 0; float bd = 1e9f; for (int m = 0; m < suit_n; m++) { float d = fabsf(logf(suit_v[m] / v)); if (d < bd) { bd = d; bm = m; } }
                row[suit[k]] = suit_out[bm];
            }
        }
    }
    {   /* prototipo render nuovo: [replay] polydump=1 registra il fotogramma presentato a rp_t 390 */
        static int rp_dump = -1; if (rp_dump < 0) rp_dump = cfg_shot == 7 && GetPrivateProfileIntA("replay", "polydump", 0, ini_path);
        if (cfg_newr) rp_dump = 0;
        if (poly_req && cfg_newr && rz_n > 0) {   /* F12: poligoni del fotogramma -> FOTO\POLY_hhmmss.bin */
            poly_req = 0; rz_write(bmi->bmiColors);
            SYSTEMTIME st; GetLocalTime(&st); char nm[64], a[MAX_PATH], b[MAX_PATH]; sprintf(nm, "FOTO\\POLY_%02d%02d%02d.bin", st.wHour, st.wMinute, st.wSecond);
            path_in_dir(a, "MHPOLY.bin"); path_in_dir(b, nm); MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING);
            mhlog("F12: %s, poligoni %d, png_ver %d, texture mappa %d, rotazione %d, zoom %d, piano %d, texture in lavorazione %d, coordinate verificate %d non verificabili %d corrette %d",
                  nm, rz_n, png_ver, nr_ntex, ((((const int*)0x4b1930)[5]) >> 16) & 2047, *(volatile int*)0x48aca0, *(volatile int*)0x4b7bf4, (int)png_busy, rz_nok, rz_nbad, rz_nfix);
        } else if (poly_req && !cfg_newr) poly_req = 0;
        if (rp_dump && rz_rec) { rz_rec = 0; rz_write(bmi->bmiColors); rp_dump = 0; }
        if (rp_dump && rz_frame_t == 389) { rz_n = 0; rz_ns = 0; rz_rec = 1; }
    }
    int rel = has_rl && cfg_relight && texbuf;
    if (gl_present(hdc, hw, bits, bmi->bmiColors, W, H, rel ? texbuf : NULL, rel ? litbuf : NULL, rel ? (const BYTE*)0x6460d0 : NULL,
                   uib, UW, UH, has3d, hd_state)) {
        if (cfg_newr && has3d && nr_hold == 1) {
            /* livello nuovo (dopo il briefing): il gioco resta fermo sul primo fotogramma finche' le texture HD non sono pronte
               (percentuale + barra), poi aspetta la croce (o INVIO / SPAZIO / CTRL / X). Il gioco carica il livello solo dopo
               il briefing, quindi prima non si puo': i colori delle texture dipendono dal livello. */
            nr_hold = 2; nr_hold_ready = 0; nr_hold_pct = 0;

            int base_done = png_done, base_tot = png_tot - (int)png_busy;   /* gia' finiti prima dell'attesa: non contano */
            nr_preload_map();
            if (ui_on) { memset(ui_f3, 0, (size_t)W*H); memset(uib, 0, (size_t)UW*UH); }   /* primo fotogramma: livello 2D ancora sporco dal caricamento */
            DWORD t0 = GetTickCount(), tready = 0; unsigned pb_prev = ~0u; int released = 0, go = 0;
            while (!go && IsWindow(hw)) {
                MSG m;
                while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
                    if (m.message == WM_QUIT) { PostQuitMessage((int)m.wParam); go = 1; break; }
                    if (m.message == WM_KEYDOWN || m.message == WM_SYSKEYDOWN || m.message == WM_CHAR) {   /* tasti premuti: non al gioco */
                        if (m.message == WM_KEYDOWN && nr_hold_ready && !(m.lParam & 0x40000000) &&
                            (m.wParam == VK_RETURN || m.wParam == VK_SPACE || m.wParam == VK_CONTROL || m.wParam == 'X')) go = 1;
                        continue;
                    }
                    TranslateMessage(&m); DispatchMessageA(&m);
                }
                PadState ps; EnterCriticalSection(&pad_cs); ps = pad; LeaveCriticalSection(&pad_cs);
                unsigned b = ps.ok ? ps.btn : 0;
                if (!(b & 1)) released = 1;
                if (nr_hold_ready && released && (b & 1) && !(pb_prev & 1)) go = 1;
                pb_prev = b;
                int tot = png_tot - base_tot, dn = png_done - base_done;
                nr_hold_pct = tot > 0 ? dn * 100 / tot : 100; if (nr_hold_pct > 100) nr_hold_pct = 100;
                if (png_busy <= 0) { if (!nr_hold_ready) { tready = GetTickCount(); mhlog("attesa: %d texture pronte in %u ms", tot, tready - t0); } nr_hold_ready = 1; nr_hold_pct = 100; }
                if (cfg_shot == 7 && nr_hold_ready && GetTickCount() - tready > 1500) { shot_req = 57; go = 1; }   /* prove: foto della scritta e via */
                gl_present(hdc, hw, bits, bmi->bmiColors, W, H, rel ? texbuf : NULL, rel ? litbuf : NULL, rel ? (const BYTE*)0x6460d0 : NULL,
                           uib, UW, UH, has3d, hd_state);
                if (cfg_shot == 7 && GetTickCount() - tready <= 1500 && nr_hold_ready && GetTickCount() - tready > 1200) shot_req = 57;
                Sleep(nr_hold_ready ? 10 : 1);
            }
            hold_mute = 1; nr_hold = 0; nr_hold_ready = 0;
        }
        if (cfg_newr) { rz_n = 0; rz_ns = 0; rz_rec = 1; } if (ui_on && has3d) { memset(ui_f3, 0, (size_t)W*H); memset(uib, 0, (size_t)UW*UH); }
        if (ui_addm_on && ui_addm) { memset(ui_addm, 0, (size_t)ui_addm_w*ui_addm_h); ui_addm_on = 0; }
        return; }
    if (!frame32 || fw != W || fh != H) { free(frame32); frame32 = malloc((size_t)W*H*4); fw = W; fh = H; }
    fix_window(hw);
    ensure_workers();
    for (int i = 0; i < NTHREADS; i++) {
        jobs[i].y0 = H*i/NTHREADS; jobs[i].y1 = H*(i+1)/NTHREADS; jobs[i].src = bits; jobs[i].pal = bmi->bmiColors;
        SetEvent(th_go[i]);
    }
    WaitForMultipleObjects(NTHREADS, th_done, TRUE, INFINITE);

    RECT cr; GetClientRect(hw, &cr); int cw = cr.right, ch = cr.bottom; if (cw <= 0 || ch <= 0) return;
    int dw = cw, dh = (int)((double)cw * H / W);
    if (dh > ch) { dh = ch; dw = (int)((double)ch * W / H); }
    int dx = (cw - dw)/2, dy = (ch - dh)/2;
    BITMAPINFO bi; memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); bi.bmiHeader.biWidth = W; bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    HBRUSH blk = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (dy > 0) { RECT a = {0, 0, cw, dy}, b2 = {0, dy+dh, cw, ch}; FillRect(hdc, &a, blk); FillRect(hdc, &b2, blk); }
    if (dx > 0) { RECT a = {0, 0, dx, ch}, b2 = {dx+dw, 0, cw, ch}; FillRect(hdc, &a, blk); FillRect(hdc, &b2, blk); }
    SetStretchBltMode(hdc, COLORONCOLOR); SetBrushOrgEx(hdc, 0, 0, NULL);
    StretchDIBits(hdc, dx, dy, dw, dh, 0, 0, W, H, frame32, &bi, DIB_RGB_COLORS, SRCCOPY);
    GdiFlush();
}

/* ------------------------------------------------------------------ domande all'avvio */
typedef int (WINAPI *MessageBoxA_t)(HWND, LPCSTR, LPCSTR, UINT);
static MessageBoxA_t real_mb;
static int WINAPI my_mb(HWND h, LPCSTR text, LPCSTR cap, UINT type)
{
    mhlog("MessageBox: '%s' tipo %x", text ? text : "", type);
    if (!cfg_ask && text && (type & 0xf) == MB_YESNO) {
        if (strstr(text, "videata") || strstr(text, "full screen") || strstr(text, "Full Screen") || strstr(text, "fullscreen")) return IDNO;
        if (strstr(text, "filmati") || strstr(text, "movies") || strstr(text, "Movies")) return cfg_movies ? IDYES : IDNO;
    }
    return real_mb(h, text, cap, type);
}
static void hook_iat(const char *dll, const char *fn, void *repl, void **orig)
{
    BYTE *base = (BYTE*)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    IMAGE_IMPORT_DESCRIPTOR *imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (; imp->Name; imp++) {
        if (_stricmp((char*)(base + imp->Name), dll)) continue;
        IMAGE_THUNK_DATA *on = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        IMAGE_THUNK_DATA *ft = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        for (; on->u1.AddressOfData; on++, ft++) {
            if (on->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            IMAGE_IMPORT_BY_NAME *ibn = (IMAGE_IMPORT_BY_NAME*)(base + on->u1.AddressOfData);
            if (strcmp((char*)ibn->Name, fn)) continue;
            DWORD old; VirtualProtect(&ft->u1.Function, 4, PAGE_READWRITE, &old);
            if (orig) *orig = (void*)ft->u1.Function;
            ft->u1.Function = (DWORD)(DWORD_PTR)repl;
            VirtualProtect(&ft->u1.Function, 4, old, &old);
        }
    }
}
static void patch_jmp(DWORD at, void *to)
{
    DWORD old; BYTE *p = (BYTE*)at;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    p[0] = 0xE9; *(DWORD*)(p+1) = (DWORD)(DWORD_PTR)to - (at + 5);
    VirtualProtect(p, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), p, 5);
}


/* rapporto d'aspetto: riscrive nell'EXE tutti i valori che dipendono dalla larghezza (tabella MHRS creata da build_exe.py) */
/* limite poligoni del motore: 2047 (tabelle da 2048 nella .data). A 32:9 con la telecamera girata si superava e
   pezzi di mappa sparivano. Le 6 tabelle per poligono vengono spostate in memoria nuova da POLY_N e il limite alzato. */
#define POLY_N 8192
static void poly_expand(void)
{
    static const struct { DWORD start, elem; } arr[6] = {
        { 0x658528, 4 }, { 0x65e478, 48 }, { 0x676488, 12 }, { 0x6a1c10, 4 }, { 0x6a3c3c, 4 }, { 0x6a5c48, 4 } };
    static const struct { DWORD at, old; BYTE a; } ref[] = {
        {0x43e661,0x658528,0},{0x4427be,0x658528,0},{0x442ca1,0x658528,0},{0x442cb5,0x658528,0},
        {0x43e65b,0x65e478,1},{0x4425e4,0x65e48c,1},{0x4427d1,0x65e478,1},{0x442c39,0x65e478,1},
        {0x43e6b7,0x67648d,2},{0x43e6bd,0x676488,2},{0x43e6cb,0x67648c,2},{0x43e6ef,0x67648d,2},{0x43e6f5,0x676488,2},{0x43e703,0x67648c,2},
        {0x43e70c,0x67648c,2},{0x43e71b,0x67648c,2},{0x43e722,0x67648e,2},{0x43e814,0x67648d,2},{0x43e81a,0x676488,2},{0x43e877,0x67648d,2},
        {0x43e8d5,0x67648d,2},{0x43e8db,0x676488,2},{0x4425e9,0x67648d,2},{0x4427d7,0x676488,2},{0x442c32,0x676488,2},{0x442ed6,0x676488,2},
        {0x442ee0,0x67648d,2},{0x442ee7,0x676488,2},{0x442efb,0x676488,2},{0x442f00,0x67648c,2},{0x442f05,0x676490,2},
        {0x43e952,0x6a1c10,3},{0x442673,0x6a1c10,3},{0x442cee,0x6a1c10,3},
        {0x44270b,0x6a3c3c,4},{0x4427ad,0x6a3c40,4},
        {0x442696,0x6a5c48,5},{0x4426be,0x6a5c48,5},{0x4426fa,0x6a5c48,5} };
    static const DWORD cap[6] = { 0x43e641, 0x43e95a, 0x4429f5, 0x442a57, 0x442cf6, 0x4438fd };
    int n = sizeof ref / sizeof ref[0]; BYTE *where[64]; BYTE *capw[6];
    for (int i = 0; i < n; i++) {   /* tutto deve essere come previsto, altrimenti non si tocca niente */
        where[i] = NULL;
        for (int k = 1; k <= 7 && !where[i]; k++) if (*(DWORD*)(ref[i].at + k) == ref[i].old) where[i] = (BYTE*)(ref[i].at + k);
        if (!where[i]) { mhlog("poligoni: codice inatteso a %lx, limite 2047", ref[i].at); return; }
    }
    for (int i = 0; i < 6; i++) { capw[i] = NULL; for (int k = 1; k <= 2; k++) if (*(DWORD*)(cap[i] + k) == 0x7ff) capw[i] = (BYTE*)(cap[i] + k);
        if (!capw[i]) { mhlog("poligoni: limite inatteso a %lx", cap[i]); return; } }
    DWORD_PTR nb[6];
    for (int j = 0; j < 6; j++) { BYTE *m = VirtualAlloc(NULL, (SIZE_T)POLY_N * arr[j].elem + 64, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!m) { mhlog("poligoni: memoria esaurita"); return; } nb[j] = (DWORD_PTR)m + 16; }
    DWORD old; VirtualProtect((void*)0x43e000, 0x6000, PAGE_EXECUTE_READWRITE, &old);
    for (int i = 0; i < n; i++) *(DWORD*)where[i] = ref[i].old - arr[ref[i].a].start + (DWORD)nb[ref[i].a];
    for (int i = 0; i < 6; i++) *(DWORD*)capw[i] = POLY_N - 1;
    VirtualProtect((void*)0x43e000, 0x6000, old, &old); FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    mhlog("poligoni: limite %d (tabelle spostate)", POLY_N - 1);
}
static void apply_aspect(void)
{
    DWORD *tb = NULL;
    for (DWORD a = 0x402b00; a < 0x404400; a += 0x100) if (!memcmp((void*)a, "MHRS", 4)) { tb = (DWORD*)a; break; }
    if (!tb) return;
    int H = 960; double asp = 0;
    aspect_auto = !_stricmp(cfg_aspect, "auto") || !cfg_aspect[0];
    if (aspect_auto) {
        int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN); asp = sh > 0 ? (double)sw / sh : 0;
    } else {
        double x = 0, y = 0; char *c = strchr(cfg_aspect, ':');
        if (c) { x = atof(cfg_aspect); y = atof(c + 1); asp = y > 0 ? x / y : 0; } else asp = atof(cfg_aspect);
    }
    if (asp < 4.0/3) asp = 4.0/3; if (asp > 32.0/9) asp = 32.0/9;   /* da 4:3 a 32:9 */
    int W = (int)(H * asp / 8 + 0.5) * 8, VW = W / 4;
    int n = (int)tb[1];
    for (int i = 0; i < n; i++) {
        DWORD va = tb[2 + 2*i], k = tb[3 + 2*i], v = k == 1 ? W : k == 2 ? W/2 : k == 3 ? W-1 : VW, old;
        VirtualProtect((void*)va, 4, PAGE_EXECUTE_READWRITE, &old); *(DWORD*)va = v; VirtualProtect((void*)va, 4, old, &old);
    }
    FlushInstructionCache(GetCurrentProcess(), NULL, 0);
    mhlog("rapporto d'aspetto %s: %.3f -> 3D %dx%d, menu %dx240 (%d valori)", cfg_aspect, asp, W, H, VW, n);
    cull_asp = (double)W / H;
}
/* culling della mappa (EXE: finestra +-32 celle, tabelle 65x65 a 0x48efa0): ellisse larga quanto lo schermo,
   ruotata come la telecamera e ingrandita con lo zoom. I cerchi originali (21x21) danno il raggio di ogni altezza. */
#define CULL_R 32
#define CULL_K (2*CULL_R + 1)
static BYTE cull_tab[6][CULL_K*CULL_K]; static double cull_r[6]; static int cull_on;
static void cull_init(void)
{
    DWORD *pt = (DWORD*)0x48efa0;
    if (*(BYTE*)0x43ebfc != 0x8d || *(BYTE*)0x43ebfe != (BYTE)(256 - CULL_R)) return;   /* EXE non aggiornato */
    for (int i = 0; i < 6; i++) {
        const BYTE *o = (const BYTE*)(DWORD_PTR)pt[i]; double r = 0;
        if (!o || IsBadReadPtr(o, 441)) { r = 6; }
        else for (int y = 0; y < 21; y++) for (int x = 0; x < 21; x++) if (o[y*21 + x]) { double d = sqrt((double)(x-10)*(x-10) + (y-10)*(y-10)); if (d > r) r = d; }
        cull_r[i] = r + 0.75; pt[i] = (DWORD)(DWORD_PTR)cull_tab[i];
        memset(cull_tab[i], 0, sizeof cull_tab[i]);
    }
    cull_on = 1; mhlog("culling mappa: raggi %.1f %.1f %.1f %.1f %.1f %.1f", cull_r[0], cull_r[1], cull_r[2], cull_r[3], cull_r[4], cull_r[5]);
}
static void cull_update(void)
{
    if (!cull_on) return;
    static int last_rot = -1, last_zoom = -1;
    int rot = ((((const int*)0x4b1930)[5]) >> 16) & 2047, zoom = *(volatile int*)0x48aca0;
    if (rot == last_rot && zoom == last_zoom) return;
    last_rot = rot; last_zoom = zoom;
    double th = rot * 6.283185307 / 2048.0, c = cos(th), sn = sin(th);
    double kx = (cull_asp > 0 ? cull_asp : 4.0/3) / (4.0/3); if (kx < 1) kx = 1;
    double zf = zoom / 2048.0; if (zf < 1) zf = 1; zf *= 1.0 + cfg_cull_extra / 100.0;
    for (int i = 0; i < 6; i++) {
        /* raggio piu' grande per tutte le altezze: con la telecamera rotante (vista spostata in avanti) il pavimento lontano
           spariva mentre i muri alti (raggio maggiore) restavano -> buchi neri (verificato dall'utente) */
        double rmax = 0; for (int j = 0; j < 6; j++) if (cull_r[j] > rmax) rmax = cull_r[j];
        double rv = rmax * zf * 1.1 + 0.5, rh = rmax * zf * kx * 1.15 + 0.5;   /* margine */
        /* telecamera inclinata/ruotata: la vista si sposta in avanti e gli angoli dello schermo vanno piu' lontano ->
           quadrato uguale in tutte le direzioni, grande quanto il lato lungo (prima: rettangolo ruotato, angoli neri) */
        { double rq = rh > rv * 1.6 ? rh : rv * 1.6; rq *= 1.15; rv = rh = rq; }
        if (rv > CULL_R) rv = CULL_R; if (rh > CULL_R) rh = CULL_R;
        BYTE *t = cull_tab[i];
        for (int dy = -CULL_R; dy <= CULL_R; dy++) for (int dx = -CULL_R; dx <= CULL_R; dx++) {
            double u = -dx*sn + dy*c, v = dx*c + dy*sn;   /* destra / su dello schermo */
            double a2 = (u*u)/(rh*rh), b2 = (v*v)/(rv*rv);
            (void)a2; (void)b2; t[(dy + CULL_R)*CULL_K + dx + CULL_R] = abs(dx) <= rh && abs(dy) <= rv;   /* quadrato non ruotato: copre ogni rotazione */   /* rettangolo: copre anche gli angoli dello schermo (la superellisse li tagliava) */
        }
    }
}
/* rettangolo attorno al giocatore in cui il gioco crea le entita' della mappa (0x451440, pensato per il 4:3):
   allargato secondo il rapporto d'aspetto e, in modalita' foto, secondo lo zoom */
static void spawn_update(void)
{
    static const DWORD at[8] = { 0x45144e, 0x45145a, 0x451467, 0x451477, 0x451488, 0x451495, 0x4514a2, 0x4514b1 };
    static const int base_v[8] = { 0x700, 0x700, 0x680, 0x780, 0x600, 0x600, 0x580, 0x680 };
    static int ok = -1, last = -1, area = -1;
    if (area < 0) { area = GetPrivateProfileIntA("video", "area_nemici", 1, ini_path); if (!area) mhlog("entita': area dei nemici originale (area_nemici=0)"); }
    if (!area) return;   /* [video] area_nemici=0: rettangolo e distanze originali (per confronto col gioco originale) */
    if (ok < 0) { ok = 1; for (int i = 0; i < 8; i++) if (*(int*)at[i] != base_v[i]) ok = 0; if (!ok) mhlog("entita': codice inatteso, rettangolo non allargato"); }
    if (!ok) return;
    /* lo schermo e' largo k volte il 4:3; ruotato come la telecamera (destra = (-sin, cos), come nel culling):
       semiampiezze del rettangolo nel mondo = proiezioni dello schermo sugli assi x e y */
    double k = (cull_asp > 0 ? cull_asp : 4.0/3) / (4.0/3); if (k < 1) k = 1;
    double z = *(volatile int*)0x48aca0 / 2048.0; if (z < 1) z = 1;   /* lo zoom si cambia solo in modalita' foto */
    double th = ((((const int*)0x4b1930)[5] >> 16) & 2047) * 6.283185307 / 2048.0, c = fabs(cos(th)), s = fabs(sin(th));
    double mg = k > 1.01 ? 1.25 : 1.0;   /* schermo largo: margine (la telecamera non e' centrata sul giocatore); in 4:3 come l'originale */
    double sw = k * mg, sh = k > 1.01 ? 1.4 : 1.0;   /* schermo: larghezza e altezza (in alto la prospettiva allarga e la telecamera guarda avanti) */
    int fx = (int)((s*sw + c*sh) * z * 20 + 0.5) * 5, fy = (int)((c*sw + s*sh) * z * 20 + 0.5) * 5;   /* a passi del 5% */
    if (fx < 100) fx = 100; if (fy < 100) fy = 100; if (fx > 800) fx = 800; if (fy > 800) fy = 800;
    if (fx * 1000 + fy == last) return; last = fx * 1000 + fy;
    static int prot; DWORD old; if (!prot) { VirtualProtect((void*)0x451440, 0x80, PAGE_EXECUTE_READWRITE, &old); prot = 1; }
    for (int i = 0; i < 8; i++) *(int*)at[i] = base_v[i] * ((i & 2) ? fy : fx) / 100;   /* 0,1,4,5 = x; 2,3,6,7 = y */
    FlushInstructionCache(GetCurrentProcess(), (void*)0x451440, 0x80);
    /* distanza massima a cui si disegnano personaggi e oggetti (0x442990) e si sentono i loro suoni (0x44da40): 0x800 per asse,
       pensata per il 4:3 -> a 32:9 sparivano ai lati. Stessi fattori del rettangolo. */
    static const DWORD dr[4] = { 0x4429b1, 0x4429cb, 0x44da74, 0x44da8e }; static int dok = -1;
    if (dok < 0) { DWORD o2; dok = 1; for (int i = 0; i < 4; i++) if (*(BYTE*)(dr[i] - 1) != 0x3d || *(int*)dr[i] != 0x800) dok = 0;
                   if (dok) VirtualProtect((void*)0x442990, 0xb200, PAGE_EXECUTE_READWRITE, &o2); else mhlog("disegno personaggi: codice inatteso"); }
    if (dok) { for (int i = 0; i < 4; i++) *(int*)dr[i] = i < 2 ? (k > 1.01 ? 0x4000 : 0x800) : 0x800 * ((i & 1) ? fy : fx) / 100;   /* disegno: tutti i personaggi presenti (sono al massimo 40); suoni: in proporzione */ FlushInstructionCache(GetCurrentProcess(), (void*)0x442990, 0xb200); }
}
static HANDLE g_main;
static DWORD WINAPI watchdog(LPVOID u)
{
    int st = (int)(DWORD_PTR)u; if (st > 1) Sleep(st*1000);
    for (int i = 0; i < 15; i++) {
        Sleep(1000); CONTEXT c; memset(&c, 0, sizeof(c)); c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        SuspendThread(g_main);
        if (GetThreadContext(g_main, &c)) {
            DWORD *sp = (DWORD*)c.Esp; char buf[400]; int n = 0;
            for (int k = 0; k < 24; k++) { DWORD v = 0; if (!IsBadReadPtr(sp+k, 4)) v = sp[k]; if (v >= 0x401000 && v < 0x475000) n += sprintf(buf+n, " %lx", v); }
            HMODULE m = NULL; char mod[MAX_PATH] = "?";
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)c.Eip, &m);
            if (m) GetModuleFileNameA(m, mod, MAX_PATH);
            mhlog("thread principale: eip=%lx (%s) ritorni nell'exe:%s", c.Eip, strrchr(mod, 92) ? strrchr(mod, 92)+1 : mod, buf);
        }
        ResumeThread(g_main);
    }
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID unused)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        GetModuleFileNameA(NULL, g_dir, MAX_PATH); char *s = strrchr(g_dir, '\\'); if (s) *s = 0;
        InitializeCriticalSection(&pad_cs);
        load_config(); make_noise();
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_main, 0, FALSE, DUPLICATE_SAME_ACCESS);
        { int wd = GetPrivateProfileIntA("debug", "watchdog", 0, ".\MHFIX.INI"); if (wd) { HANDLE t = CreateThread(NULL, 0, watchdog, (LPVOID)(DWORD_PTR)wd, 0, NULL); if (t) CloseHandle(t); } }
        /* solo per l'EXE patchato: controlla che all'indirizzo ci sia la funzione di presentazione attesa */
        BYTE *base = (BYTE*)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
        if ((DWORD_PTR)base == 0x400000 && PRESENT_VA + 8 < 0x400000 + nt->OptionalHeader.SizeOfImage &&
            *(BYTE*)PRESENT_VA == 0xA1 && *(DWORD*)(PRESENT_VA+1) == 0x65bb18)
            { patch_jmp(PRESENT_VA, (void*)mh_present); mhlog("avvio: presentazione agganciata, exe %s", g_dir); }
        else mhlog("avvio: EXE non riconosciuto, presentazione originale");
        hook_iat("USER32.dll", "MessageBoxA", (void*)my_mb, (void**)&real_mb);
        if (cfg_pad) pad_start();
        if ((DWORD_PTR)base == 0x400000) { apply_aspect(); cull_init(); poly_expand(); }
        if ((DWORD_PTR)base == 0x400000)
            for (DWORD a = 0x402b00; a < 0x404400; a += 0x100)
                if (!memcmp((void*)a, "MHUI", 4)) {
                    DWORD *tb = (DWORD*)(a + 4); ui_W = tb[0]; ui_H = tb[1]; ui_VW = tb[2]; ui_VH = tb[3];
                    ui_f3 = calloc((size_t)ui_W*ui_H + 4096, 1);
                    if (ui_f3) {
                        *(volatile DWORD*)0x74b80c = (DWORD)(DWORD_PTR)ui_f3;
                        *(volatile DWORD*)0x48cdc8 = ui_VW; *(volatile DWORD*)0x48cdcc = ui_VH; ui_on = 1;
                        *(volatile DWORD*)0x74b828 = (ui_VW - 320) / 2;   /* menu centrati nel livello 2D largo */
                        {   /* HUD: il gioco disegna su un livello 2D piu' grande, poi lo ingrandiamo in modo uniforme */
                            double s = ui_H / 240.0 * cfg_hud_scale / 100.0; if (s < 1.0) s = 1.0;
                            int hw = ((int)(ui_W / s) + 3) & ~3, hh = (int)(ui_H / s);
                            if (hw < 320) hw = 320; if (hh < 240) hh = 240; if (hw > ui_W) hw = ui_W; if (hh > ui_H) hh = ui_H;
                            *(volatile DWORD*)0x74b814 = hw; *(volatile DWORD*)0x74b818 = hh;
                            mhlog("UI: livello HUD %dx%d (hud_scale %d%%)", hw, hh, cfg_hud_scale);
                        }
                        mhlog("UI: 3D %dx%d, HUD/menu %dx%d", ui_W, ui_H, ui_VW, ui_VH);
                        if (cfg_menu_hd) srcmap_install();
                    }
                    break;
                }
    }
    if (reason == DLL_PROCESS_DETACH) mhlog("uscita");
    return TRUE;
}
