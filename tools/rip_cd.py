r"""Estrae le tracce audio del CD di Machine Hunter in ..\MUSIC\TrackNN.wav (per WINMX.dll).
  python rip_cd.py [lettera]      es.  python rip_cd.py D
Legge direttamente i settori audio (CDDA) del lettore; nessun programma esterno.
"""
import ctypes, ctypes.wintypes as wt, os, struct, sys, string

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileW.restype = wt.HANDLE
IOCTL_CDROM_READ_TOC = 0x24000
IOCTL_CDROM_RAW_READ = 0x2403E
CDDA = 2
GAME = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def find_drive():
    for L in string.ascii_uppercase:
        if k32.GetDriveTypeW(L + ':\\') == 5: yield L

def open_drive(L):
    h = k32.CreateFileW('\\\\.\\%s:' % L, 0x80000000, 3, None, 3, 0, None)
    if h in (None, wt.HANDLE(-1).value): raise OSError('impossibile aprire %s: (%d)' % (L, ctypes.get_last_error()))
    return h

def ioctl(h, code, inbuf, outsize):
    out = ctypes.create_string_buffer(outsize); got = wt.DWORD()
    inb = ctypes.create_string_buffer(inbuf, len(inbuf)) if inbuf else None
    ok = k32.DeviceIoControl(h, code, inb, len(inbuf) if inbuf else 0, out, outsize, ctypes.byref(got), None)
    if not ok: raise OSError('DeviceIoControl %x errore %d' % (code, ctypes.get_last_error()))
    return out.raw[:got.value]

def toc(h):
    t = ioctl(h, IOCTL_CDROM_READ_TOC, None, 804)
    first, last = t[2], t[3]; tracks = []
    for i in range(last - first + 2):
        e = t[4 + 8*i: 12 + 8*i]; ctrl = e[1] & 0x0f; num = e[2]; m, s, f = e[5], e[6], e[7]
        tracks.append((num, ctrl, (m*60 + s)*75 + f - 150))
    return tracks

def rip(L):
    h = open_drive(L); tr = toc(h); out = os.path.join(GAME, 'MUSIC'); os.makedirs(out, exist_ok=True); n = 0
    for i in range(len(tr) - 1):
        num, ctrl, start = tr[i]; end = tr[i+1][2]
        if ctrl & 4: print('traccia %d: dati, saltata' % num); continue
        if i + 1 < len(tr) - 1 and tr[i+1][1] & 4: end -= 11400     # spazio prima di una sessione dati
        sectors = end - start; path = os.path.join(out, 'Track%02d.wav' % num)
        with open(path, 'wb') as f:
            f.write(b'RIFF' + struct.pack('<I', 36 + sectors*2352) + b'WAVEfmt ' +
                    struct.pack('<IHHIIHH', 16, 1, 2, 44100, 44100*4, 4, 16) + b'data' + struct.pack('<I', sectors*2352))
            pos = start
            while pos < end:
                cnt = min(20, end - pos)
                req = struct.pack('<qII', pos*2048, cnt, CDDA)
                for attempt in range(5):
                    try: data = ioctl(h, IOCTL_CDROM_RAW_READ, req, cnt*2352); break
                    except OSError:
                        if attempt == 4: data = bytes(cnt*2352)
                f.write(data.ljust(cnt*2352, b'\0')); pos += cnt
                print('\rtraccia %d: %3d%%' % (num, 100*(pos-start)//sectors), end='')
        print('\rtraccia %d: %s (%.1f MB)' % (num, path, os.path.getsize(path)/1e6)); n += 1
    k32.CloseHandle(h); print('estratte', n, 'tracce audio in', out)

if __name__ == '__main__':
    drives = [sys.argv[1].upper()[0]] if len(sys.argv) > 1 else list(find_drive())
    if not drives: sys.exit('nessun lettore CD trovato')
    rip(drives[0])
