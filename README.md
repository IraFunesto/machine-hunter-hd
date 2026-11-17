# Machine Hunter HD

**[Italiano](#italiano) | [English](#english)**

---

<a name="italiano"></a>
## Italiano

Mod non ufficiale per **Machine Hunter** (PC, 1997, versione 1.10): alta risoluzione, schermi larghi fino a 32:9, texture e sprite HD, menu e HUD ridisegnati, joypad, musica dal CD e varie correzioni.

> Il repository contiene **solo il codice e gli strumenti del mod**. Non contiene file del gioco originale, né musica, né texture. Ognuno si genera le texture HD, i menu HD e la musica dalla **propria copia** del gioco con `tools\installa.py`.

> ⚠️ **Compatibilità:** il mod è stato provato con la versione **italiana** e quella **inglese** del gioco (`MACHHUNT.EXE` v1.10; MD5 italiana `e651c3d6a3658aa47fc647621876acae`, inglese `a1f47a8ca1aa627081a53b33b9d861db`). Altre versioni o lingue non sono state provate. `installa.py` riconosce la versione e si ferma se l'EXE non corrisponde nei punti da modificare.

### Cosa fa

**Grafica**
- Risoluzione interna 4x (960 righe) e rapporto d'aspetto libero: auto, 16:9, 21:9, 32:9, 16:10, 4:3 o un numero. A schermo intero senza bordi o in finestra.
- Render nuovo su scheda video (OpenGL), con:
  - colori veri a 32 bit e luce continua;
  - prospettiva corretta;
  - trasparenze del gioco (additive e al 50%);
  - texture e sprite HD ricavati con Real-ESRGAN, ricolorati con i colori del livello.
- Render classico migliorato (a 256 colori) sempre disponibile.
- Texture HD a caricamento rapido: precaricate su più thread, con la schermata "CARICAMENTO TEXTURE HD" all'inizio del livello.
- Correzioni grafiche:
  - glitch con la telecamera inclinata o che ruota;
  - pezzi di mappa che sparivano ai lati;
  - limite di poligoni alzato.

**Menu e HUD**
- Menu, HUD, caratteri e logo in HD, con la dimensione dell'HUD regolabile.
- Il menu principale e le porte della transizione si allargano allo schermo ricostruendo i lati, senza stirare.
- Mappa (Select) larga e in alta risoluzione.
- Colonna degli oggetti a sinistra riordinata, con le icone luminose trasparenti.

**Gioco**
- Joypad (XInput/HID) con tasti configurabili e conferma con la croce nei menu.
- Musica dalle tracce del CD (`MUSIC\TrackNN.wav` o `.mp3`).
- IA dei nemici:
  - non spingono più contro casse e muri sottili;
  - le pattuglie evitano i tratti bloccati.
- Area di comparsa dei nemici allargata per lo schermo largo (disattivabile).
- Colore della tuta del giocatore a scelta: viola come su PS1, arancione come su PC, o altri.
- **Modalità foto** (R3 / F10):
  - telecamera libera, rotazione e zoom;
  - mondo congelato;
  - personaggio visibile o nascosto;
  - F12 / croce salva la foto in `FOTO\`.

Tutte le opzioni sono in `MHFIX.INI`, commentate.

### Installazione

Serve il gioco **Machine Hunter** installato dal CD originale: cartella con `MACHHUNT.EXE` (v1.10) e `DATA\DATA.ARC`.

1. **Python 3.10+** e i pacchetti:
   ```bash
   pip install -r tools/requirements.txt
   ```
2. **Real-ESRGAN ncnn-vulkan** (scheda video con Vulkan):
   - scarica `realesrgan-ncnn-vulkan-...-windows.zip` da [Real-ESRGAN releases](https://github.com/xinntao/Real-ESRGAN/releases);
   - estrai il contenuto in `tools\realesrgan\` (deve esserci `tools\realesrgan\realesrgan-ncnn-vulkan.exe` con la cartella `models\`).
3. Copia la cartella `tools\` e `MHFIX.INI` di questo repository **dentro la cartella del gioco**.
4. Con il CD nel lettore (per la musica), dalla cartella del gioco:
   ```bash
   python tools/installa.py
   ```
   Opzioni:
   - `--cd E` per scegliere il lettore;
   - `--senza-musica` per saltare la musica;
   - `--solo-exe` per rifare solo l'EXE.

   Ci vogliono circa 15-20 minuti, soprattutto per Real-ESRGAN, e circa 1,5 GB di spazio.
5. Metti `WINMX.dll` (dalla [release](../../releases), oppure compilala, vedi sotto) nella cartella del gioco.
6. Facoltativo: scarica `anteprime_missioni.zip` dalla [release](../../releases) ed estrailo nella cartella del gioco. Sono le immagini delle 17 missioni rifatte in HD con foto in gioco (cartella `UI_HD\`); senza, si vedono quelle originali ingrandite.
7. Avvia **`MACHHUNT_HD.EXE`**. Nella finestra "Machine Hunter V 1.10" premi *Nuova partita* (*New Game* nella versione inglese).

`installa.py` esegue in ordine:
1. EXE senza CD;
2. tracce audio del CD;
3. texture e sprite (esporta, Real-ESRGAN, correzione delle texture scure, archivio `DATA\HD4S.ARC`, indice `DATA\HDPNG.idx`);
4. menu HD (`UI_HD\`), caratteri, logo e porte larghe;
5. `MACHHUNT_HD.EXE` (patch con `build_exe.py`).

Ogni passo si può lanciare anche a mano: vedi la descrizione in cima a ogni script.

**Texture proprie:** dopo il passo 3 le texture HD sono normali PNG in `TEXTURES_4X\` e `SPRITES_4X\`. Si possono ritoccare o sostituire con le proprie e poi ricostruire l'archivio:
```bash
python tools/mh_tex.py build TEXTURES_4X SPRITES_4X
python tools/png_index.py
```

### Compilare WINMX.dll

`WINMX.dll` sostituisce `WINMM.dll` per l'EXE patchato. È a 32 bit e si compila con clang o gcc mingw-w64 (i686):

```bash
cd tools/winmx
i686-w64-mingw32-clang -O2 -shared -o WINMX.dll winmx.c -lsetupapi -lhid -lgdi32 -luser32 -lopengl32 -lwindowscodecs -lole32 -luuid -Wl,--kill-at
```

### File

| File | Cosa fa |
|---|---|
| `tools/winmx/` | la DLL: presentazione OpenGL, render nuovo, HUD/menu HD, joypad, musica, correzioni (`winmx.c` + `*.inc`) |
| `tools/build_exe.py` | patch dell'EXE: risoluzione, texture 4x, livello HUD separato, aggancio a WINMX.dll |
| `tools/mh_tex.py` | texture e sprite: esportazione, Real-ESRGAN, ricostruzione degli archivi |
| `tools/png_index.py` | indice dei PNG HD per il render nuovo |
| `tools/rip_cd.py` | estrae le tracce audio dal CD (lettura CDDA diretta, nessun programma esterno) |
| `tools/font_hd.py`, `make_logo.py`, `door_wide.py` | caratteri HD, logo, porte del menu larghe |
| `tools/mission_hd.py` | immagini delle missioni dalle proprie foto in gioco (facoltativo) |
| `tools/installa.py` | tutto in un comando |
| `MHFIX.INI` | impostazioni del mod |

### Note

- Mod amatoriale, non affiliato con gli autori o i detentori dei diritti di Machine Hunter.
- Le immagini delle missioni si possono rifare con le proprie foto:
  1. in `MHFIX.INI` metti `[foto] anteprima=1`;
  2. in modalità foto scatta con F12.
- I commenti nel codice e in `MHFIX.INI` sono in italiano.

### Licenza

Codice del mod sotto licenza MIT (vedi `LICENSE`). Machine Hunter e i suoi contenuti appartengono ai rispettivi proprietari.

---

<a name="english"></a>
## English

Unofficial mod for **Machine Hunter** (PC, 1997, version 1.10): high resolution, widescreen up to 32:9, HD textures and sprites, redrawn menus and HUD, gamepad, CD music and various fixes.

> This repository contains **only the mod's code and tools**. It contains no original game files, music or textures. Everyone generates the HD textures, HD menus and music from **their own copy** of the game with `tools\installa.py`.

> ⚠️ **Compatibility:** the mod has been tested with the **Italian** and **English** versions of the game (`MACHHUNT.EXE` v1.10; Italian MD5 `e651c3d6a3658aa47fc647621876acae`, English `a1f47a8ca1aa627081a53b33b9d861db`). Other versions and languages are untested. `installa.py` detects the version and stops if the EXE doesn't match at the patched locations.

### Features

**Graphics**
- 4x internal resolution (960 lines) and any aspect ratio: auto, 16:9, 21:9, 32:9, 16:10, 4:3 or a number. Borderless fullscreen or windowed.
- New GPU renderer (OpenGL), with:
  - 32-bit true colour and smooth lighting;
  - perspective-correct texturing;
  - the game's transparencies (additive and 50%);
  - HD textures and sprites made with Real-ESRGAN, recoloured with each level's colours.
- Improved classic renderer (256 colours) still available.
- Fast HD texture loading: preloaded on several threads, with a loading screen ("CARICAMENTO TEXTURE HD") at the start of each level.
- Graphics fixes:
  - glitches with the tilted or rotating camera;
  - map pieces disappearing at the sides;
  - higher polygon limit.

**Menus and HUD**
- HD menus, HUD, fonts and logo, with adjustable HUD size.
- The main menu and the door transition widen to the screen by rebuilding the sides, not by stretching.
- Wide, high-resolution map (Select).
- Left item column rearranged, with the glowing icons drawn transparent.

**Gameplay**
- Gamepad (XInput/HID) with configurable buttons and cross-to-confirm in menus.
- Music from the CD tracks (`MUSIC\TrackNN.wav` or `.mp3`).
- Enemy AI:
  - enemies no longer push into crates and thin walls;
  - patrols avoid blocked path segments.
- Enemy spawn area widened for widescreen (can be turned off).
- Player suit colour: purple (as on PS1), orange (as on PC) or others.
- **Photo mode** (R3 / F10):
  - free camera, rotation and zoom;
  - frozen world;
  - character shown or hidden;
  - F12 / cross saves the picture to `FOTO\`.

All options are in `MHFIX.INI`, with comments (in Italian).

### Installation

You need **Machine Hunter** installed from the original CD: a folder with `MACHHUNT.EXE` (v1.10) and `DATA\DATA.ARC`.

1. **Python 3.10+** and the packages:
   ```bash
   pip install -r tools/requirements.txt
   ```
2. **Real-ESRGAN ncnn-vulkan** (needs a Vulkan graphics card):
   - download `realesrgan-ncnn-vulkan-...-windows.zip` from [Real-ESRGAN releases](https://github.com/xinntao/Real-ESRGAN/releases);
   - extract it into `tools\realesrgan\` (there must be `tools\realesrgan\realesrgan-ncnn-vulkan.exe` with the `models\` folder).
3. Copy this repository's `tools\` folder and `MHFIX.INI` **into the game folder**.
4. With the CD in the drive (for the music), from the game folder:
   ```bash
   python tools/installa.py
   ```
   Options:
   - `--cd E` to choose the drive;
   - `--senza-musica` to skip the music;
   - `--solo-exe` to rebuild only the EXE.

   It takes about 15-20 minutes, mostly for Real-ESRGAN, and about 1.5 GB of disk space.
5. Put `WINMX.dll` (from the [release](../../releases), or build it, see below) in the game folder.
6. Optional: download `anteprime_missioni.zip` from the [release](../../releases) and extract it into the game folder. It contains the 17 mission images remade in HD from in-game photos (`UI_HD\` folder); without it you get the original images upscaled.
7. Start **`MACHHUNT_HD.EXE`**. In the "Machine Hunter V 1.10" window, press *New Game*.

`installa.py` runs, in order:
1. no-CD EXE;
2. CD audio tracks;
3. textures and sprites (export, Real-ESRGAN, fix for dark textures, `DATA\HD4S.ARC` archive, `DATA\HDPNG.idx` index);
4. HD menus (`UI_HD\`), fonts, logo and wide doors;
5. `MACHHUNT_HD.EXE` (patched by `build_exe.py`).

Each step can also be run by hand: see the description at the top of each script.

**Your own textures:** after step 3 the HD textures are plain PNG files in `TEXTURES_4X\` and `SPRITES_4X\`. You can edit them or replace them with your own, then rebuild the archive:
```bash
python tools/mh_tex.py build TEXTURES_4X SPRITES_4X
python tools/png_index.py
```

### Building WINMX.dll

`WINMX.dll` replaces `WINMM.dll` for the patched EXE. It is 32-bit and builds with clang or gcc mingw-w64 (i686):

```bash
cd tools/winmx
i686-w64-mingw32-clang -O2 -shared -o WINMX.dll winmx.c -lsetupapi -lhid -lgdi32 -luser32 -lopengl32 -lwindowscodecs -lole32 -luuid -Wl,--kill-at
```

### Files

| File | What it does |
|---|---|
| `tools/winmx/` | the DLL: OpenGL presentation, new renderer, HD HUD/menus, gamepad, music, fixes (`winmx.c` + `*.inc`) |
| `tools/build_exe.py` | EXE patch: resolution, 4x textures, separate HUD layer, hook to WINMX.dll |
| `tools/mh_tex.py` | textures and sprites: export, Real-ESRGAN, archive rebuild |
| `tools/png_index.py` | index of the HD PNGs for the new renderer |
| `tools/rip_cd.py` | extracts the audio tracks from the CD (direct CDDA reading, no external programs) |
| `tools/font_hd.py`, `make_logo.py`, `door_wide.py` | HD fonts, logo, wide menu doors |
| `tools/mission_hd.py` | mission images from your own in-game photos (optional) |
| `tools/installa.py` | everything in one command |
| `MHFIX.INI` | mod settings |

### Notes

- Fan-made mod, not affiliated with the authors or rights holders of Machine Hunter.
- You can remake the mission images from your own photos:
  1. in `MHFIX.INI` set `[foto] anteprima=1`;
  2. take the shot with F12 in photo mode.
- Comments in the code and in `MHFIX.INI` are in Italian.

### License

Mod code under the MIT license (see `LICENSE`). Machine Hunter and its content belong to their respective owners.
