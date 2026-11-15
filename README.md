# Machine Hunter HD

Mod non ufficiale per **Machine Hunter** (PC, 1997, versione 1.10): alta risoluzione, schermi larghi fino a 32:9, texture e sprite HD, menu e HUD ridisegnati, joypad, musica dal CD e varie correzioni.

*English summary below.*

> Il repository contiene **solo il codice e gli strumenti del mod**. Non contiene file del gioco originale, né musica, né texture: tutto viene generato dalla **propria copia** del gioco con `tools\installa.py`.

## Cosa fa

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

## Installazione

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

   Ci vogliono alcuni minuti, soprattutto per Real-ESRGAN, e circa 1,5 GB di spazio.
5. Metti `WINMX.dll` (dalla [release](../../releases), oppure compilala, vedi sotto) nella cartella del gioco.
6. Avvia **`MACHHUNT_HD.EXE`**. Nella finestra "Machine Hunter V 1.10" premi *Nuova partita*.

`installa.py` esegue in ordine:
1. EXE senza CD;
2. tracce audio del CD;
3. texture e sprite (esporta, Real-ESRGAN, correzione delle texture scure, archivio `DATA\HD4S.ARC`, indice `DATA\HDPNG.idx`);
4. menu HD (`UI_HD\`), caratteri, logo e porte larghe;
5. `MACHHUNT_HD.EXE` (patch con `build_exe.py`).

Ogni passo si può lanciare anche a mano: vedi la descrizione in cima a ogni script.

## Compilare WINMX.dll

`WINMX.dll` sostituisce `WINMM.dll` per l'EXE patchato. È a 32 bit e si compila con clang o gcc mingw-w64 (i686):

```bash
cd tools/winmx
i686-w64-mingw32-clang -O2 -shared -o WINMX.dll winmx.c -lsetupapi -lhid -lgdi32 -luser32 -lopengl32 -lwindowscodecs -lole32 -luuid -Wl,--kill-at
```

## File

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

## Note

- Mod amatoriale, non affiliato con gli autori o i detentori dei diritti di Machine Hunter.
- Le immagini delle missioni si possono rifare con le proprie foto:
  1. in `MHFIX.INI` metti `[foto] anteprima=1`;
  2. in modalità foto scatta con F12.

---

## English summary

Unofficial mod for **Machine Hunter** (PC, 1997, v1.10).

- **Graphics:**
  - 4x internal resolution and any aspect ratio up to 32:9;
  - a new OpenGL renderer with true colour, perspective-correct texturing and HD textures and sprites (Real-ESRGAN);
  - fixes for the tilted or rotating camera.
- **Menus and HUD:** HD menus, HUD, fonts and logo; a widescreen menu and map.
- **Gameplay:**
  - gamepad support and CD music;
  - enemy AI fixes (enemies no longer walk into crates);
  - a photo mode.

This repository contains **only the mod's code and tools**: no game files, music or textures. Everything is generated from your own copy of the game:

1. Install the requirements:
   ```bash
   pip install -r tools/requirements.txt
   ```
2. Put Real-ESRGAN ncnn-vulkan in `tools/realesrgan/`.
3. Copy `tools/` and `MHFIX.INI` into the game folder.
4. Run:
   ```bash
   python tools/installa.py
   ```
5. Add `WINMX.dll` from the release.
6. Start `MACHHUNT_HD.EXE`.

Settings are in `MHFIX.INI`. Comments in the code and in the INI are in Italian.

## Licenza

Codice del mod sotto licenza MIT (vedi `LICENSE`). Machine Hunter e i suoi contenuti appartengono ai rispettivi proprietari.
