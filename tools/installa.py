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

SCRIPTS = os.path.dirname(os.path.abspath(__file__))   # dove sono gli script (dentro MachineHunterHD.exe se impacchettato)
HERE = os.environ.get('MH_TOOLS') or SCRIPTS; GAME = os.environ.get('MH_GAME') or os.path.dirname(HERE)
INPROC = getattr(sys, 'frozen', False)   # launcher senza Python installato: gli script girano dentro lo stesso processo
EXE = os.path.join(GAME, 'MACHHUNT.EXE'); NOCD = os.path.join(GAME, 'MACHHUNT_NOCD4.EXE'); OUT = os.path.join(GAME, 'MACHHUNT_HD.EXE')
EXE_MD5 = {'e651c3d6a3658aa47fc647621876acae': 'italiana 1.10', 'a1f47a8ca1aa627081a53b33b9d861db': 'inglese 1.10'}   # versioni provate
# EXE senza CD e senza filmati obbligatori: 4 punti (offset nel file, byte originali, byte nuovi)
NOCD_PATCH = [(0x247d0, '83ec08535556', 'b801000000c3'), (0x2c327, '7503', '9090'), (0x380c9, '75', 'eb'), (0x3b781, '75', 'eb')]
# EXE gia' "senza CD" di terze parti (es. l'inglese a1f47a8c...): la funzione 0x40fea0 che apre il CD audio e ne controlla lo stato
# e' sostituita da "flag = 1, ritorna" -> il gioco non apre mai il CD audio e non chiede la musica. Si rimettono i byte originali
# (uguali nell'italiana): al CD ci pensa gia' il mod (build_exe.py + WINMX.dll che emula il CD audio con MUSIC\).
UNCRACK = [(0xf2a0, 'b801000000a3bced4900c3', 'a1bced490081ec00010000')]
ESRGAN = os.path.join(HERE, 'realesrgan', 'realesrgan-ncnn-vulkan.exe')

def step(msg): print('\n=== ' + msg, flush=True)

def run(*args):
    t = time.time()
    if INPROC:
        import runpy
        old = sys.argv; sys.argv = [os.path.join(SCRIPTS, args[0])] + list(args[1:])
        try: runpy.run_path(sys.argv[0], run_name='__main__')
        except SystemExit as e:
            if e.code not in (None, 0): raise subprocess.CalledProcessError(e.code if isinstance(e.code, int) else 1, args)
        except Exception as e:
            import traceback; traceback.print_exc(); raise subprocess.CalledProcessError(1, args)
        finally: sys.argv = old
    else:
        subprocess.run([sys.executable] + list(args), cwd=HERE, check=True)
    print('    (%.0f s)' % (time.time() - t), flush=True)

def make_nocd():   # rifatto ogni volta (pochi byte): cosi' una reinstallazione prende anche le correzioni nuove
    b = bytearray(open(EXE, 'rb').read())
    v = EXE_MD5.get(hashlib.md5(b).hexdigest())
    print('MACHHUNT.EXE: versione ' + v if v else 'ATTENZIONE: MACHHUNT.EXE non e\' una versione provata (italiana o inglese 1.10); provo lo stesso')
    for o, cracked, orig in UNCRACK:
        if bytes(b[o:o+11]) == bytes.fromhex(cracked):
            b[o:o+11] = bytes.fromhex(orig); print('ripristinata la funzione del CD audio a 0x%x (EXE gia\' modificato da altri: la musica non partiva)' % (o + 0x400c00))
    for o, old, new in NOCD_PATCH:
        old, new = bytes.fromhex(old), bytes.fromhex(new); cur = bytes(b[o:o+len(old)])
        if cur != old and cur != new: sys.exit('MACHHUNT.EXE diverso dall\'atteso a 0x%x: interrotto' % o)   # gia' modificato = va bene
        b[o:o+len(new)] = new
    open(NOCD, 'wb').write(b); print('scritto', NOCD)

def main(a=None):
    a = sys.argv[1:] if a is None else a
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
