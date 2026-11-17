r"""Machine Hunter HD - installazione completa dalla propria copia del gioco.

Mettere la cartella tools\ (e MHFIX.INI) nella cartella del gioco installato (quella con MACHHUNT.EXE e DATA\DATA.ARC), poi:

  python tools\installa.py                 tutto (musica dal CD se inserito, texture/sprite/menu HD, EXE)
  python tools\installa.py --cd E          lettera del lettore CD (default: il primo trovato)
  python tools\installa.py --senza-musica  salta l'estrazione delle tracce audio
  python tools\installa.py --solo-exe      rifa' solo l'EXE (MACHHUNT_HD.EXE)

Serve Real-ESRGAN (ncnn-vulkan, scheda video con Vulkan) in tools\realesrgan\realesrgan-ncnn-vulkan.exe con la cartella models\:
  https://github.com/xinntao/Real-ESRGAN/releases  (realesrgan-ncnn-vulkan-...-windows.zip)
Pacchetti Python: pip install -r tools\requirements.txt
Alla fine si avvia MACHHUNT_HD.EXE (WINMX.dll accanto, dalla release o compilata da tools\winmx).
"""
import os, sys, hashlib, subprocess, time

HERE = os.path.dirname(os.path.abspath(__file__)); GAME = os.path.dirname(HERE)
EXE = os.path.join(GAME, 'MACHHUNT.EXE'); NOCD = os.path.join(GAME, 'MACHHUNT_NOCD4.EXE'); OUT = os.path.join(GAME, 'MACHHUNT_HD.EXE')
EXE_MD5 = {'e651c3d6a3658aa47fc647621876acae': 'italiana 1.10', 'a1f47a8ca1aa627081a53b33b9d861db': 'inglese 1.10'}   # versioni provate
# EXE senza CD e senza filmati obbligatori: 4 punti (offset nel file, byte originali, byte nuovi)
NOCD_PATCH = [(0x247d0, '83ec08535556', 'b801000000c3'), (0x2c327, '7503', '9090'), (0x380c9, '75', 'eb'), (0x3b781, '75', 'eb')]
ESRGAN = os.path.join(HERE, 'realesrgan', 'realesrgan-ncnn-vulkan.exe')

def step(msg): print('\n=== ' + msg, flush=True)

def run(*args):
    t = time.time(); subprocess.run([sys.executable] + list(args), cwd=HERE, check=True)
    print('    (%.0f s)' % (time.time() - t))

def make_nocd():
    if os.path.exists(NOCD): return
    b = bytearray(open(EXE, 'rb').read())
    v = EXE_MD5.get(hashlib.md5(b).hexdigest())
    print('MACHHUNT.EXE: versione ' + v if v else 'ATTENZIONE: MACHHUNT.EXE non e\' una versione provata (italiana o inglese 1.10); provo lo stesso')
    for o, old, new in NOCD_PATCH:
        old, new = bytes.fromhex(old), bytes.fromhex(new); cur = bytes(b[o:o+len(old)])
        if cur != old and cur != new: sys.exit('MACHHUNT.EXE diverso dall\'atteso a 0x%x: interrotto' % o)   # gia' modificato = va bene
        b[o:o+len(new)] = new
    open(NOCD, 'wb').write(b); print('scritto', NOCD)

def main():
    a = sys.argv[1:]
    if not os.path.exists(EXE) or not os.path.exists(os.path.join(GAME, 'DATA', 'DATA.ARC')):
        sys.exit('Metti tools\\ nella cartella del gioco (servono MACHHUNT.EXE e DATA\\DATA.ARC).\n'
                 'Put tools\\ in the game folder (MACHHUNT.EXE and DATA\\DATA.ARC are needed).')
    step('EXE senza CD / no-CD EXE'); make_nocd()
    if '--solo-exe' not in a:
        if '--senza-musica' not in a:
            step('Musica dal CD / CD music -> MUSIC\\TrackNN.wav')
            cd = a[a.index('--cd') + 1] if '--cd' in a else None
            try: run('rip_cd.py', *([cd] if cd else []))
            except subprocess.CalledProcessError: print('    musica non estratta (CD non trovato?), il gioco funziona lo stesso: riprova con --cd LETTERA / music not extracted (CD not found?), the game still works: retry with --cd LETTER')
        if not os.path.exists(ESRGAN): sys.exit('Manca / missing: ' + ESRGAN + ' (vedi README / see README)')
        step('Texture originali / original textures -> TEXTURES');        run('mh_tex.py', 'export')
        step('Texture HD / HD textures (Real-ESRGAN, alcuni minuti / a few minutes)'); run('mh_tex.py', 'esrgan', '4')
        step('Correzione texture scure / dark texture fix');     run('mh_tex.py', 'esrgan_fix')
        step('Sprite originali / original sprites -> SPRITES');           run('mh_tex.py', 'sprite_export')
        step('Sprite HD / HD sprites (Real-ESRGAN)');               run('mh_tex.py', 'sprite_esrgan')
        step('Archivio HD / HD archive -> DATA\\HD4S.ARC');         run('mh_tex.py', 'build', os.path.join(GAME, 'TEXTURES_4X'), os.path.join(GAME, 'SPRITES_4X'))
        step('Indice PNG / PNG index');        run('png_index.py')
        step('Menu e HUD HD / HD menus and HUD -> UI_HD');                run('mh_tex.py', 'ui_hd')
        step('Caratteri HD / HD fonts');                          run('font_hd.py')
        step('Logo');                                  run('make_logo.py')
        step('Porte del menu larghe / wide menu doors');     run('door_wide.py')
    step('EXE HD -> MACHHUNT_HD.EXE');                 run('build_exe.py', '4', '4', '560', OUT, '--sprites', '--ui')
    if not os.path.exists(os.path.join(GAME, 'WINMX.dll')):
        print('\nManca WINMX.dll: scaricala dalla release (o compilala da tools\\winmx) e mettila accanto a MACHHUNT_HD.EXE.'
              '\nWINMX.dll is missing: download it from the release (or build it from tools\\winmx) and put it next to MACHHUNT_HD.EXE.')
    print('\nFatto. Avvia MACHHUNT_HD.EXE (impostazioni in MHFIX.INI).\nDone. Start MACHHUNT_HD.EXE (settings in MHFIX.INI).')

if __name__ == '__main__':
    main()
