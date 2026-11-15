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
EXE_MD5 = 'e651c3d6a3658aa47fc647621876acae'   # versione 1.10 (1997)
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
    if hashlib.md5(b).hexdigest() != EXE_MD5:
        print('ATTENZIONE: MACHHUNT.EXE non e\' la versione attesa (1.10); provo lo stesso')
    for o, old, new in NOCD_PATCH:
        old, new = bytes.fromhex(old), bytes.fromhex(new)
        if b[o:o+len(old)] != old: sys.exit('MACHHUNT.EXE diverso dall\'atteso a 0x%x: interrotto' % o)
        b[o:o+len(new)] = new
    open(NOCD, 'wb').write(b); print('scritto', NOCD)

def main():
    a = sys.argv[1:]
    if not os.path.exists(EXE) or not os.path.exists(os.path.join(GAME, 'DATA', 'DATA.ARC')):
        sys.exit('Metti tools\\ nella cartella del gioco installato (servono MACHHUNT.EXE e DATA\\DATA.ARC).')
    step('EXE senza CD'); make_nocd()
    if '--solo-exe' not in a:
        if '--senza-musica' not in a:
            step('Musica dal CD -> MUSIC\\TrackNN.wav')
            cd = a[a.index('--cd') + 1] if '--cd' in a else None
            try: run('rip_cd.py', *([cd] if cd else []))
            except subprocess.CalledProcessError: print('    musica non estratta (CD non trovato?): il gioco funziona lo stesso, riprova con --cd LETTERA')
        if not os.path.exists(ESRGAN): sys.exit('Manca ' + ESRGAN + ' (vedi le istruzioni in cima a questo file).')
        step('Texture originali -> TEXTURES');        run('mh_tex.py', 'export')
        step('Texture HD (Real-ESRGAN, alcuni minuti)'); run('mh_tex.py', 'esrgan', '4')
        step('Texture scure appiattite: rifatte');     run('mh_tex.py', 'esrgan_fix')
        step('Sprite originali -> SPRITES');           run('mh_tex.py', 'sprite_export')
        step('Sprite HD (Real-ESRGAN)');               run('mh_tex.py', 'sprite_esrgan')
        step('Archivio HD -> DATA\\HD4S.ARC');         run('mh_tex.py', 'build', os.path.join(GAME, 'TEXTURES_4X'), os.path.join(GAME, 'SPRITES_4X'))
        step('Indice PNG per il render nuovo');        run('png_index.py')
        step('Menu e HUD HD -> UI_HD');                run('mh_tex.py', 'ui_hd')
        step('Caratteri HD');                          run('font_hd.py')
        step('Logo');                                  run('make_logo.py')
        step('Porte del menu per schermi larghi');     run('door_wide.py')
    step('EXE HD -> MACHHUNT_HD.EXE');                 run('build_exe.py', '4', '4', '560', OUT, '--sprites', '--ui')
    if not os.path.exists(os.path.join(GAME, 'WINMX.dll')):
        print('\nManca WINMX.dll: scaricala dalla release (o compilala da tools\\winmx) e mettila accanto a MACHHUNT_HD.EXE.')
    print('\nFatto. Avvia MACHHUNT_HD.EXE (impostazioni in MHFIX.INI).')

if __name__ == '__main__':
    main()
