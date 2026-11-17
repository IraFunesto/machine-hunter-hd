r"""Crea il pacchetto per chi non ha Python: MachineHunterHD.zip con
  MachineHunterHD.exe   (launcher, PyInstaller: Python + script del mod dentro)
  WINMX.dll, MHFIX.INI
  tools\realesrgan\     (Real-ESRGAN ncnn-vulkan, licenza BSD-3: solo i modelli usati)

  python make_launcher.py [cartella_uscita]      (serve: pip install pyinstaller)
"""
import os, sys, glob, shutil, subprocess, tempfile, zipfile

HERE = os.path.dirname(os.path.abspath(__file__)); GAME = os.path.dirname(HERE)
OUT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(GAME, 'dist_launcher')
ESR = os.path.join(HERE, 'realesrgan')
ESR_FILES = ['realesrgan-ncnn-vulkan.exe', 'vcomp140.dll', 'vcomp140d.dll',
             'models/realesrgan-x4plus.bin', 'models/realesrgan-x4plus.param',
             'models/realesr-animevideov3-x4.bin', 'models/realesr-animevideov3-x4.param']
ESR_LICENSE = """Real-ESRGAN ncnn-vulkan (https://github.com/xinntao/Real-ESRGAN)

BSD 3-Clause License

Copyright (c) 2021, Xintao Wang
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""

def main():
    os.makedirs(OUT, exist_ok=True); work = tempfile.mkdtemp(prefix='mh_pyi_')
    scripts = [f for f in glob.glob(os.path.join(HERE, '*.py')) if os.path.basename(f) not in ('launcher.py', 'make_launcher.py')]
    cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--onefile', '--windowed', '--name', 'MachineHunterHD',
           '--distpath', work, '--workpath', os.path.join(work, 'build'), '--specpath', work,
           '--collect-all', 'keystone', '--collect-all', 'capstone', '--hidden-import', 'scipy.ndimage', '--hidden-import', 'pefile',
           '--exclude-module', 'matplotlib', '--exclude-module', 'pandas']
    for f in scripts: cmd += ['--add-data', f + os.pathsep + 'scripts']
    cmd.append(os.path.join(HERE, 'launcher.py'))
    subprocess.run(cmd, check=True)
    exe = os.path.join(work, 'MachineHunterHD.exe')
    z = os.path.join(OUT, 'MachineHunterHD.zip')
    with zipfile.ZipFile(z, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        zf.write(exe, 'MachineHunterHD.exe')
        zf.write(os.path.join(HERE, 'winmx', 'WINMX.dll'), 'WINMX.dll')
        ini = os.path.join(GAME, 'machine-hunter-hd', 'MHFIX.INI')
        zf.write(os.environ.get('MH_INI', ini), 'MHFIX.INI')
        for f in ESR_FILES: zf.write(os.path.join(ESR, f), 'tools/realesrgan/' + f)
        zf.writestr('tools/realesrgan/LICENSE.txt', ESR_LICENSE)
    shutil.copy(exe, os.path.join(OUT, 'MachineHunterHD.exe'))
    shutil.rmtree(work, ignore_errors=True)
    print('scritto', z, '%.1f MB' % (os.path.getsize(z) / 1e6))

if __name__ == '__main__':
    main()
