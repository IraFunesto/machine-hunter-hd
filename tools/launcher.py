r"""Machine Hunter HD - launcher (finestra): installa il mod e avvia il gioco, senza bisogno di Python installato.

Impacchettato con PyInstaller in MachineHunterHD.exe (vedi make_launcher.py), da mettere nella cartella del gioco insieme a
tools\realesrgan\, WINMX.dll e MHFIX.INI. Gli script del mod sono dentro l'EXE e girano nello stesso processo.
Si puo' anche lanciare con Python:  python tools\launcher.py
"""
import os, sys, threading, queue, locale, subprocess, string, ctypes
import tkinter as tk
from tkinter import ttk, messagebox

FROZEN = getattr(sys, 'frozen', False)
if FROZEN:
    GAME = os.path.dirname(os.path.abspath(sys.executable))
    SCRIPTS = os.path.join(sys._MEIPASS, 'scripts')
else:
    SCRIPTS = os.path.dirname(os.path.abspath(__file__)); GAME = os.path.dirname(SCRIPTS)
TOOLS = os.path.join(GAME, 'tools')
os.environ['MH_GAME'] = GAME; os.environ['MH_TOOLS'] = TOOLS
sys.path.insert(0, SCRIPTS)

if False:   # solo per PyInstaller: librerie usate dagli script impacchettati
    import numpy, json, struct, hashlib, shutil, tempfile, random, runpy, traceback
    import PIL.Image, PIL.ImageDraw, PIL.ImageFont, PIL.ImageFilter
    import scipy.ndimage, pefile, keystone, capstone

T = {
 'it': {'title': 'Machine Hunter HD', 'game': 'Cartella del gioco', 'ver': 'Versione del gioco', 'state': 'Mod HD',
        'nogame': 'non trovato: metti MachineHunterHD.exe nella cartella del gioco (con MACHHUNT.EXE e DATA\\DATA.ARC)',
        'it': 'italiana 1.10', 'en': 'inglese 1.10', 'unk': 'non riconosciuta (non provata)',
        'inst': 'installato', 'noinst': 'non installato', 'music': 'musica', 'yes': 'si\'', 'no': 'no',
        'cd': 'Musica dal CD', 'nocd': '(nessuno: salta la musica)',
        'install': 'Installa', 'reinstall': 'Reinstalla', 'play': 'Gioca', 'settings': 'Impostazioni', 'quit': 'Esci', 'lang': 'English',
        'busy': 'Installazione in corso: ci vogliono circa 15-20 minuti (Real-ESRGAN usa la scheda video).',
        'done': 'Installazione completata. Premi Gioca.', 'fail': 'Installazione non riuscita: guarda il registro qui sotto.',
        'noesr': 'Manca tools\\realesrgan\\realesrgan-ncnn-vulkan.exe: estrai tutto lo zip del mod nella cartella del gioco.',
        'nodll': 'Manca WINMX.dll nella cartella del gioco: estrai tutto lo zip del mod.',
        'confirm': 'L\'installazione crea texture, sprite e menu HD dalla tua copia del gioco (circa 1,5 GB, 15-20 minuti). Continuare?',
        'running': 'Attendi la fine dell\'installazione.'},
 'en': {'title': 'Machine Hunter HD', 'game': 'Game folder', 'ver': 'Game version', 'state': 'HD mod',
        'nogame': 'not found: put MachineHunterHD.exe in the game folder (with MACHHUNT.EXE and DATA\\DATA.ARC)',
        'it': 'Italian 1.10', 'en': 'English 1.10', 'unk': 'not recognised (untested)',
        'inst': 'installed', 'noinst': 'not installed', 'music': 'music', 'yes': 'yes', 'no': 'no',
        'cd': 'Music from CD', 'nocd': '(none: skip music)',
        'install': 'Install', 'reinstall': 'Reinstall', 'play': 'Play', 'settings': 'Settings', 'quit': 'Quit', 'lang': 'Italiano',
        'busy': 'Installing: it takes about 15-20 minutes (Real-ESRGAN uses the graphics card).',
        'done': 'Installation complete. Press Play.', 'fail': 'Installation failed: see the log below.',
        'noesr': 'tools\\realesrgan\\realesrgan-ncnn-vulkan.exe is missing: extract the whole mod zip into the game folder.',
        'nodll': 'WINMX.dll is missing from the game folder: extract the whole mod zip.',
        'confirm': 'Installing creates HD textures, sprites and menus from your copy of the game (about 1.5 GB, 15-20 minutes). Continue?',
        'running': 'Please wait for the installation to finish.'},
}
MD5 = {'e651c3d6a3658aa47fc647621876acae': 'it', 'a1f47a8ca1aa627081a53b33b9d861db': 'en'}

def cd_drives():
    k = ctypes.windll.kernel32
    return [L for L in string.ascii_uppercase if k.GetDriveTypeW(L + ':\\') == 5]

class Out:   # stdout/stderr -> finestra
    def __init__(self, q): self.q = q
    def write(self, s):
        if s: self.q.put(s)
    def flush(self): pass

class App:
    def __init__(self, root):
        self.root = root; self.q = queue.Queue(); self.busy = False
        self.lang = 'it' if (locale.getdefaultlocale()[0] or '').lower().startswith('it') else 'en'
        sys.stdout = sys.stderr = Out(self.q)
        root.resizable(True, True); root.minsize(640, 420)
        f = ttk.Frame(root, padding=12); f.pack(fill='both', expand=True)
        self.info = ttk.Label(f, justify='left', font=('Segoe UI', 10)); self.info.pack(anchor='w')
        r = ttk.Frame(f); r.pack(fill='x', pady=(10, 4))
        self.cdl = ttk.Label(r); self.cdl.pack(side='left')
        self.cd = ttk.Combobox(r, state='readonly', width=34); self.cd.pack(side='left', padx=8)
        b = ttk.Frame(f); b.pack(fill='x', pady=6)
        self.bi = ttk.Button(b, command=self.install); self.bi.pack(side='left')
        self.bp = ttk.Button(b, command=self.play); self.bp.pack(side='left', padx=6)
        self.bs = ttk.Button(b, command=self.settings); self.bs.pack(side='left')
        self.bl = ttk.Button(b, command=self.toggle); self.bl.pack(side='right')
        self.bq = ttk.Button(b, command=self.quit); self.bq.pack(side='right', padx=6)
        self.msg = ttk.Label(f, foreground='#a05000'); self.msg.pack(anchor='w', pady=(4, 2))
        self.pb = ttk.Progressbar(f, maximum=13); self.pb.pack(fill='x', pady=2)
        self.log = tk.Text(f, height=14, font=('Consolas', 9), wrap='word'); self.log.pack(fill='both', expand=True, pady=(4, 0))
        root.protocol('WM_DELETE_WINDOW', self.quit)
        self.refresh(); self.pump()
        self.auto = '--auto' in sys.argv   # prova automatica: installa senza domande, salva il registro ed esce
        if self.auto: root.after(500, lambda: self.install(auto=True))

    def t(self, k): return T[self.lang][k]

    def state(self):
        exe = os.path.join(GAME, 'MACHHUNT.EXE'); arc = os.path.join(GAME, 'DATA', 'DATA.ARC')
        if not (os.path.exists(exe) and os.path.exists(arc)): return None, False, False
        import hashlib; v = MD5.get(hashlib.md5(open(exe, 'rb').read()).hexdigest(), 'unk')
        hd = os.path.exists(os.path.join(GAME, 'MACHHUNT_HD.EXE')) and os.path.exists(os.path.join(GAME, 'DATA', 'HD4S.ARC'))
        mus = os.path.exists(os.path.join(GAME, 'MUSIC', 'Track02.wav')) or os.path.exists(os.path.join(GAME, 'MUSIC', 'Track02.mp3'))
        return v, hd, mus

    def refresh(self):
        self.root.title(self.t('title'))
        v, hd, mus = self.state(); self.ok = v is not None; self.hd = hd
        lines = ['%s: %s' % (self.t('game'), GAME)]
        if v is None: lines.append(self.t('nogame'))
        else:
            lines.append('%s: %s' % (self.t('ver'), self.t(v)))
            lines.append('%s: %s   (%s: %s)' % (self.t('state'), self.t('inst') if hd else self.t('noinst'), self.t('music'), self.t('yes') if mus else self.t('no')))
        self.info.config(text='\n'.join(lines))
        self.cdl.config(text=self.t('cd'))
        vals = [L + ':' for L in cd_drives()] + [self.t('nocd')]; cur = self.cd.get()
        self.cd.config(values=vals); self.cd.set(cur if cur in vals else vals[0])
        self.bi.config(text=self.t('reinstall') if hd else self.t('install'))
        self.bp.config(text=self.t('play')); self.bs.config(text=self.t('settings')); self.bq.config(text=self.t('quit')); self.bl.config(text=self.t('lang'))
        st = 'disabled' if self.busy else 'normal'
        self.bi.config(state='normal' if self.ok and not self.busy else 'disabled')
        self.bp.config(state='normal' if hd and not self.busy else 'disabled')

    def toggle(self): self.lang = 'en' if self.lang == 'it' else 'it'; self.refresh()

    def pump(self):
        try:
            while True:
                s = self.q.get_nowait()
                if s.startswith('\n=== ') or s.startswith('=== '): self.pb.step(1)
                for i, part in enumerate(s.split('\r')):   # '\r' = riscrive la riga (avanzamento delle tracce del CD)
                    if i: self.log.delete('end-1c linestart', 'end-1c')
                    self.log.insert('end', part)
                self.log.see('end')
        except queue.Empty: pass
        self.root.after(100, self.pump)

    def install(self, auto=False):
        if not os.path.exists(os.path.join(TOOLS, 'realesrgan', 'realesrgan-ncnn-vulkan.exe')): self.msg.config(text=self.t('noesr')); return
        if not auto and not messagebox.askyesno(self.t('title'), self.t('confirm')): return
        args = []
        c = self.cd.get()
        if len(c) == 2 and c[1] == ':': args += ['--cd', c[0]]
        else: args += ['--senza-musica']
        self.busy = True; self.pb.config(value=0, maximum=13 if '--senza-musica' in args else 14); self.log.delete('1.0', 'end'); self.msg.config(text=self.t('busy')); self.refresh()
        threading.Thread(target=self.worker, args=(args,), daemon=True).start()

    def worker(self, args):
        ok = False
        try:
            import installa; installa.main(args); ok = True
        except SystemExit as e:
            ok = e.code in (None, 0)
            if not ok and e.code: print(e.code)
        except Exception:
            import traceback; traceback.print_exc()
        self.root.after(0, self.finished, ok)

    def finished(self, ok):
        self.busy = False; self.pb.config(value=self.pb['maximum'] if ok else self.pb['value'])
        self.msg.config(text=self.t('done') if ok else self.t('fail'))
        if ok and not os.path.exists(os.path.join(GAME, 'WINMX.dll')): self.msg.config(text=self.t('nodll'))
        self.refresh()
        if self.auto:
            def save():
                with open(os.path.join(GAME, 'launcher_auto.log'), 'w', encoding='utf-8') as f:
                    f.write(self.log.get('1.0', 'end') + ('\nOK\n' if ok else '\nFALLITO\n'))
                self.root.destroy()
            self.root.after(300, save)

    def play(self):
        if not os.path.exists(os.path.join(GAME, 'WINMX.dll')): self.msg.config(text=self.t('nodll')); return
        subprocess.Popen([os.path.join(GAME, 'MACHHUNT_HD.EXE')], cwd=GAME)

    def settings(self):
        p = os.path.join(GAME, 'MHFIX.INI')
        if os.path.exists(p): os.startfile(p)

    def quit(self):
        if self.busy: messagebox.showinfo(self.t('title'), self.t('running')); return
        self.root.destroy()

if __name__ == '__main__':
    root = tk.Tk(); App(root); root.mainloop()
