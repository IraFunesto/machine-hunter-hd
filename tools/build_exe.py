r"""Genera un EXE di Machine Hunter patchato.

  python build_exe.py R T [larghezza_base] [uscita] [--sprites] [--nowinmx] [--ui]
    R  = fattore risoluzione interna (1, 2 o 4)   -> altezza 240*R
    T  = fattore texture/sprite (1, 2 o 4)       -> texture 64*T, archivio DATA\HD{T}X.ARC (T>1)
    --sprites = l'archivio contiene anche gli sprite ingranditi (SPRITES_xX)
    larghezza_base = larghezza a R=1 (default 560 = 21:9; 426 = 16:9; 320 = 4:3)

Parte da MACHHUNT_NOCD4.EXE (gia' senza CD e senza filmati). Richiede: pefile, keystone-engine, capstone.
"""
import sys, os, struct
import pefile, keystone, capstone

HERE = os.path.dirname(os.path.abspath(__file__)); GAME = os.path.dirname(HERE)
SRC = os.environ.get('MH_SRC', os.path.join(GAME, 'MACHHUNT_NOCD4.EXE'))
SPRHD = '--sprites' in sys.argv; args = [a for a in sys.argv[1:] if not a.startswith('--')]
R = int(args[0]); T = int(args[1]); BW = int(args[2]) if len(args) > 2 else 560
OUT = args[3] if len(args) > 3 else os.path.join(GAME, 'MACHHUNT_R%d_T%d%s.EXE' % (R, T, '_S' if SPRHD else ''))
assert R in (1, 2, 4) and T in (1, 2, 4)
W = BW*R; W -= W % 8; H = 240*R
LR = {1: 0, 2: 1, 4: 2}[R]; LT = {1: 0, 2: 1, 4: 2}[T]

pe = pefile.PE(SRC); BASE = pe.OPTIONAL_HEADER.ImageBase
b = bytearray(open(SRC, 'rb').read())
ks = keystone.Ks(keystone.KS_ARCH_X86, keystone.KS_MODE_32); md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
def off(va): return pe.get_offset_from_rva(va-BASE)
def dis(va): o = off(va); return next(md.disasm(bytes(b[o:o+15]), va))
def put(va, bs): o = off(va); b[o:o+len(bs)] = bs
def asm(src, va): return bytes(ks.asm(src, va)[0])
# versione inglese (stessa 1.10, compilata dopo): alcune funzioni sono altrove. Riconosciuta dal thunk 0x4024ff -> 0x4209f0.
_t = off(0x4024ff); EN = b[_t] == 0xE9 and 0x4024ff + 5 + struct.unpack('<i', bytes(b[_t+1:_t+5]))[0] == 0x4209f0
ENMAP = {0x40711e: 0x420c2e, 0x407125: 0x420c35, 0x407140: 0x420c50, 0x407147: 0x420c57, 0x44f73b: 0x44f73d}
def TR(va): return ENMAP.get(va, va) if EN else va
def imm(va, old, new):
    va = TR(va); i = dis(va); o = off(va)
    assert struct.unpack('<I', bytes(b[o+i.size-4:o+i.size]))[0] == old, (hex(va), i.op_str)
    b[o+i.size-4:o+i.size] = struct.pack('<I', new)
def var(va, old, new):
    o = off(va); assert struct.unpack('<I', b[o:o+4])[0] == old; b[o:o+4] = struct.pack('<I', new)
CAVES = iter(range(0x402b00, 0x404400, 0x100))
def cave(src):
    va = next(CAVES); code = asm(src, va); o = off(va)
    assert len(code) <= 0x100 and all(x == 0xCC for x in b[o:o+len(code)]), (hex(va), len(code))
    put(va, code); return va

# ---------------- CD: rilevamento sempre positivo (WINMX.dll emula il CD audio) ----------------
o = off(0x43bad0); assert bytes(b[o:o+2]) == bytes([0x6a, 0x00])
put(0x43bad0, asm('mov dword ptr [0x69c010], 1; mov eax, 1; ret', 0x43bad0))

# ---------------- risoluzione ----------------
# --ui: HUD e menu in un livello 2D separato (W/R x 240, coordinate originali), composto e ingrandito da WINMX.dll
UI = '--ui' in sys.argv and R > 1
VW = W // R; VH = 240
W2, H2 = (W, H)                           # limiti di ritaglio 2D (in modalita' UI il livello 2D ha dimensioni variabili)
var(0x48cdc8, 320, W); var(0x48cdcc, 240, H)
imm(0x43880b, 0x140, W); imm(0x43881f, 0xa0, W//2)
if UI:
    PT = cave("push eax\nmov eax, dword ptr [0x48cdc8]\nmov dword ptr [0x62dbe0], eax\npop eax\nret")
    for va in (0x436b53, 0x436ca1, 0x436d68, 0x436e10):
        o = off(va); assert bytes(b[o:o+2]) == b'\xc7\x05'
        put(va, asm('call %d' % PT, va) + b'\x90'*5)
else:
    for va in (0x436b53, 0x436ca1, 0x436d68, 0x436e10): imm(va, 0x140, W)
for va in (0x42d1cc, 0x40711e, 0x407125, 0x424ad3, 0x424adb, 0x422cf9, 0x422dc0, 0x44f724): imm(va, 0x140, W2)
for va in (0x424988, 0x424a9d, 0x42c9d9): imm(va, 0x13f, W2-1)
for va in (0x407140, 0x407147, 0x424ae5, 0x424aed, 0x44f73b): imm(va, 0xf0, H2)
for va in (0x424aa9, 0x42c9d4): imm(va, 0xef, H2-1)
for va in (0x443c5b, 0x437222, 0x437267, 0x4372a2, 0x4372f5):   # proiezione x R
    o = off(va); assert b[o] == 0xC1 and b[o+2] == 0x0e; b[o+2] = 0x0e-LR
# tabella righe (480 voci) -> moltiplicazione diretta, valida per qualsiasi altezza
o = off(0x441f76); assert bytes(b[o:o+3]) == b'\x8b\x0c\x9d'
if UI:
    RT = cave("mov ecx, ebx\nimul ecx, dword ptr [0x48cdc8]\njmp 0x441f7d")
    put(0x441f76, asm('jmp %d' % RT, 0x441f76) + b'\x90\x90')
else:
    put(0x441f76, asm('imul ecx, ebx, %d' % W, 0x441f76) + b'\x90')
# immagini 320x240 (menu, schermate): in modalita' UI copiate 1:1 nel livello 2D, altrimenti ingrandite xR
S = 1 if UI else R; PW = W2
def rep_store(o):
    if S == 1: return 'mov byte ptr [edi+%d], al' % o
    if S == 2: return 'mov word ptr [edi+%d], ax' % o
    return 'mov dword ptr [edi+%d], eax' % o
def widen():
    if S == 1: return ''
    if S == 2: return 'mov ah, al\n'
    return 'mov ah, al\nmov bx, ax\nshl eax, 16\nmov ax, bx\n'
stores = '\n'.join(rep_store(r*PW) for r in range(S))
CA = cave(f"""
  pushad
  mov edi, eax
  mov esi, ecx
  mov eax, dword ptr [0x74b824]
  test eax, eax
  jz LAh
  push 1
  push edi
  push esi
  call eax
  add esp, 12
LAh:
  push edi
  xor eax, eax
  mov ecx, dword ptr [0x48cdc8]
  imul ecx, dword ptr [0x48cdcc]
  shr ecx, 2
  rep stosd dword ptr es:[edi], eax
  pop edi
  mov edx, 240
LAy:
  mov ecx, 320
LAx:
  xor eax, eax
  lodsb al, byte ptr [esi]
  {widen()}
  {stores}
  add edi, {S}
  dec ecx
  jnz LAx
  add edi, dword ptr [0x48cdc8]
  sub edi, 320
  dec edx
  jnz LAy
  popad
  ret
""")
CB = cave(f"""
  pushad
  mov edi, ecx
  mov esi, edx
  mov eax, dword ptr [0x74b824]
  test eax, eax
  jz LBh
  push 0
  push edi
  push esi
  call eax
  add esp, 12
LBh:
  mov edx, 240
LBy:
  mov ecx, 320
LBx:
  xor eax, eax
  lodsb al, byte ptr [esi]
  test al, al
  jz LBs
  {widen()}
  {stores}
LBs:
  add edi, {S}
  dec ecx
  jnz LBx
  add edi, dword ptr [0x48cdc8]
  sub edi, 320
  dec edx
  jnz LBy
  popad
  ret
""")
for site, end in ((0x418f63, 0x418f71), (0x418fcd, 0x418fdb), (0x443bac, 0x443bba)):
    o = off(site); assert b[o] == 0xBE and struct.unpack('<I', b[o+1:o+5])[0] == 0x12c00
    put(site, asm('call %d' % CA, site)); put(site+5, asm('jmp %d' % end, site+5))
o = off(0x42d1e0); assert b[o] == 0xBE
put(0x42d1e0, asm('call %d' % CB, 0x42d1e0)); put(0x42d1e5, asm('jmp %d' % 0x42d1f2, 0x42d1e5))
if UI:
    # il DIB del gioco (ora livello 2D) resta allocato a piena risoluzione: margine di sicurezza
    o = off(0x4386b5); assert bytes(b[o:o+2]) == b'\x8b\x0d'; put(0x4386b5, asm('mov ecx, %d' % W, 0x4386b5) + b'\x90')
    o = off(0x4386c1); assert bytes(b[o:o+2]) == b'\x8b\x15'; put(0x4386c1, asm('mov edx, %d' % H, 0x4386c1) + b'\x90')
    # commutazione 3D <-> 2D nel fotogramma di gioco (variabili: 0x74b80c buffer 3D della DLL, 0x74b810 flag "c'e' 3D")
    ENTER = cave(f"""
  cmp dword ptr [0x74b80c], 0
  je E1
  mov dword ptr [0x48cdc8], {W}
  mov dword ptr [0x48cdcc], {H}
  push eax
  mov eax, dword ptr [0x74b80c]
  mov dword ptr [0x67ce38], eax
  pop eax
  mov dword ptr [0x74b810], 1
E1:
  jmp 0x4026bc
""")
    LEAVE = cave(f"""
  cmp dword ptr [0x74b80c], 0
  je L1
  pushad
  mov eax, dword ptr [0x74b814]
  test eax, eax
  jnz LW1
  mov eax, {VW}
LW1:
  mov dword ptr [0x48cdc8], eax
  mov eax, dword ptr [0x74b818]
  test eax, eax
  jnz LH1
  mov eax, {VH}
LH1:
  mov dword ptr [0x48cdcc], eax
  mov eax, dword ptr [0x65e444]
  mov dword ptr [0x67ce38], eax
  call 0x4026bc
  popad
L1:
  jmp 0x4013ac
""")
    i = dis(0x443be6); assert i.mnemonic == 'call' and i.op_str == '0x4026bc'; put(0x443be6, asm('call %d' % ENTER, 0x443be6))
    i = dis(0x443c7c); assert i.mnemonic == 'call' and i.op_str == '0x4013ac'; put(0x443c7c, asm('call %d' % LEAVE, 0x443c7c))   # prima dell'HUD
    # fondali dei menu: sempre la copia 1:1 in alto a sinistra (il confronto con 320 dipende dai limiti 2D)
    o = off(0x42d1d8); assert b[o] == 0x75, hex(b[o]); put(0x42d1d8, b'\x90'*2)
    # immagine della missione (220x160) e scheda BIO (256x189) copiate a mano nel briefing: passano dal blitter del gioco,
    # cosi' la DLL le riconosce e le sostituisce con la versione HD
    def blit_call(src_var, w, h, dx, dy):
        return (f"push {dy}\npush {dx}\npush {h}\npush {w}\npush 0\npush 0\npush {w}\npush dword ptr [{src_var:#x}]\n"
                f"call 0x4014e2\nadd esp, 32\n")
    o = off(0x42d250); assert bytes(b[o:o+5]) == b'\xa1\xc8\xcd\x48\x00'
    code = asm(blit_call(0x67c4c0, 220, 160, 58, 40) + "mov eax, dword ptr [0x48cdc8]\njmp 0x42d28c", 0x42d250); assert len(code) <= 0x3c; put(0x42d250, code)
    o = off(0x42d28c); assert bytes(b[o:o+2]) == b'\x8b\x15'
    code = asm(blit_call(0x658108, 256, 189, 32, 30) + "jmp 0x42d2c7", 0x42d28c); assert len(code) <= 0x3b; put(0x42d28c, code)
    o = off(0x42d363); assert bytes(b[o:o+5]) == b'\xa1\xc8\xcd\x48\x00'
    code = asm(blit_call(0x658108, 256, 189, 32, 30) + "jmp 0x42d3a4", 0x42d363); assert len(code) <= 0x41; put(0x42d363, code)
    # menu centrati nel livello 2D largo: l'inizio del buffer 2D nei fotogrammi senza 3D viene spostato di [0x74b828] (dalla DLL)
    MO = cave("test eax, eax\njz MO1\nadd eax, dword ptr [0x74b828]\nMO1:\nmov dword ptr [0x67ce38], eax\nret")
    for va in (0x4394fc, 0x4395bd, 0x4397c3):
        o = off(va); assert bytes(b[o:o+5]) == b'\xa3\x38\xce\x67\x00', hex(va); put(va, asm('call %d' % MO, va))
    # modalita' foto: con [0x74b830] != 0 il ciclo di aggiornamento dei personaggi salta tutti tranne il giocatore ([0x74b834])
    FZ = cave("""
  cmp dword ptr [0x74b830], 0
  je FZn
  cmp eax, dword ptr [0x74b834]
  je FZn
  push ecx
  mov ecx, dword ptr [eax+0x94]
  test ecx, ecx
  jz FZs
  cmp ecx, dword ptr [0x74b838]
  je FZa
  cmp ecx, dword ptr [0x74b83c]
  je FZa
  mov ecx, dword ptr [0x74b840]
  test ecx, ecx
  jz FZs
  add ecx, dword ptr [0x621b88]
  cmp byte ptr [ecx], 0
  jne FZa
FZs:
  pop ecx
  cmp dword ptr [eax+0x58], ebx
  je 0x42710e
  cmp dword ptr [0x621b88], 8
  jb 0x42710e
  test dword ptr [eax+0x58], 0x40000
  jnz 0x42710e
  movsx edx, word ptr [eax+0x1a]
  cmp edx, dword ptr [0x4b7bf4]
  jne FZd
  push 0x6bb158
  push eax
  call 0x4027d9
  add esp, 8
  test eax, eax
  mov eax, dword ptr [0x629d78]
  je 0x42710e
FZd:
  push eax
  call 0x4027ed
  add esp, 4
  mov eax, dword ptr [0x629d78]
  jmp 0x42710e
FZa:
  pop ecx
FZn:
  cmp dword ptr [eax+0x58], ebx
  je 0x4270fa
  jmp 0x426d95
""")
    o = off(0x426d8c); assert bytes(b[o:o+9]) == bytes.fromhex('3958580f8465030000')
    put(0x426d8c, asm('jmp %d' % FZ, 0x426d8c) + b'\x90'*4)
    # culling della mappa: finestra di celle attorno alla telecamera da +-10 (tabella 21x21) a +-32 (tabella 65x65);
    # le tabelle (6, una per altezza, puntatori a 0x48efa0) le calcola WINMX.dll secondo rapporto d'aspetto, zoom e rotazione
    CR = 32; CK = 2*CR + 1
    for va, old, new in ((0x43ebfc, '8d78f6', '8d78%02x' % (256 - CR)), (0x43ebff, '8d480a', '8d48%02x' % CR),
                         (0x43ec10, '8d550a', '8d55%02x' % CR), (0x43ec2d, '8d75f6', '8d75%02x' % (256 - CR)),
                         (0x43ecd7, '8d550a', '8d55%02x' % CR), (0x43eceb, '83c115', '83c1%02x' % CK)):
        o = off(va); assert bytes(b[o:o+3]).hex() == old, hex(va); b[o:o+3] = bytes.fromhex(new)
    o = off(0x43ec13); assert bytes(b[o:o+8]).hex() == 'c1e1032bcf8d0c49'; code = asm('imul ecx, ecx, %d' % CK, 0x43ec13); b[o:o+8] = code + b'\x90'*(8-len(code))
    o = off(0x43ec21); assert bytes(b[o:o+8]).hex() == 'c1e1032bc88d0449'; code = asm('imul eax, ecx, %d' % CK, 0x43ec21); b[o:o+8] = code + b'\x90'*(8-len(code))
    o = off(0x43ec55); assert bytes(b[o:o+7]).hex() == '8a8c02dc000000'; b[o+3:o+7] = struct.pack('<I', CR*CK + CR)
    # testi HUD su griglia 6x6 pensati per 320x240 in basso a destra (percentuali): agganciati all'angolo del livello HUD
    BR = cave("""
  push ecx
  push edx
  mov ecx, 6
  mov eax, dword ptr [0x48cdc8]
  sub eax, 320
  cdq
  idiv ecx
  add dword ptr [esp+12], eax
  mov eax, dword ptr [0x48cdcc]
  sub eax, 240
  cdq
  idiv ecx
  add dword ptr [esp+16], eax
  pop edx
  pop ecx
  jmp 0x401e3d
""")
    for va in (0x444423, 0x444782):
        i = dis(va); assert i.mnemonic == 'call' and i.op_str == '0x401e3d', hex(va); put(va, asm('call %d' % BR, va))
    # sprite a schermo (icona HUD): un risultato 0 diventerebbe "vuoto" nel livello 2D -> 1 (nero in tutte le palette)
    NZ_BL = cave("mov dl, byte ptr [eax+0x6360d0]\ntest dl, dl\njnz NB1\nmov dl, 1\nNB1:\nret")
    NZ_LT = cave("mov al, byte ptr [edx+0x6460d0]\ntest al, al\njnz NB2\nmov al, 1\nNB2:\nret")
    for va in (0x44202c, 0x4420e4, 0x44219c):
        o = off(va); assert bytes(b[o:o+6]) == bytes([0x8a, 0x90, 0xd0, 0x60, 0x63, 0x00]), hex(va)
        put(va, asm('call %d' % NZ_BL, va) + b'\x90')
    for va in (0x44225f, 0x4422fd, 0x44239b):
        o = off(va); assert bytes(b[o:o+6]) == bytes([0x8a, 0x82, 0xd0, 0x60, 0x64, 0x00]), hex(va)
        put(va, asm('call %d' % NZ_LT, va) + b'\x90')
    # tabella per la DLL: 'MHUI', W, H, VW, VH
    tv = next(CAVES); o = off(tv); assert all(x == 0xCC for x in b[o:o+20])
    b[o:o+20] = b'MHUI' + struct.pack('<4I', W, H, VW, VH)
    # tabella 'MHRS': tutti i valori che dipendono dalla larghezza, riscritti dalla DLL all'avvio secondo il rapporto d'aspetto
    # tipi: 1 = W, 2 = W/2, 3 = W-1, 4 = VW
    RS = []
    def rs_imm(va, kind):
        va = TR(va); i = dis(va); RS.append((va + i.size - 4, kind))
    def rs_find(start, size, val, kind, count=1):
        o0 = off(start); hits = [k for k in range(size - 3) if struct.unpack('<I', bytes(b[o0+k:o0+k+4]))[0] == val]
        assert len(hits) == count, (hex(start), val, hits); RS.extend((start + k, kind) for k in hits)
    RS.append((0x48cdc8, 1))
    rs_imm(0x43880b, 1); rs_imm(0x43881f, 2)
    for va in (0x42d1cc, 0x40711e, 0x407125, 0x424ad3, 0x424adb, 0x422cf9, 0x422dc0, 0x44f724): rs_imm(va, 1)
    for va in (0x424988, 0x424a9d, 0x42c9d9): rs_imm(va, 3)
    RS.append((0x4386b6, 1)); assert struct.unpack('<I', bytes(b[off(0x4386b6):off(0x4386b6)+4]))[0] == W
    rs_find(ENTER, 0x40, W, 1); rs_find(LEAVE, 0x40, VW, 4)
    RS.append((tv + 4, 1)); RS.append((tv + 12, 4))
    for va, k in RS: assert struct.unpack('<I', bytes(b[off(va):off(va)+4]))[0] == {1: W, 2: W//2, 3: W-1, 4: VW}[k], hex(va)
    tr = next(CAVES); o = off(tr); data = b'MHRS' + struct.pack('<I', len(RS)) + b''.join(struct.pack('<II', va, k) for va, k in RS)
    assert len(data) <= 0x100 and all(x == 0xCC for x in b[o:o+len(data)]); b[o:o+len(data)] = data

# ---------------- texture xT ----------------
if T > 1:
    ROLS = [0x42ea5c, 0x42f36c, 0x42fc7c, 0x430615, 0x4308a6, 0x431286, 0x4314aa, 0x431e09, 0x432074,
            0x4329d9, 0x432bfa, 0x433513, 0x433745, 0x4349cc, 0x4352cc, 0x435bcc]
    MASK = {4: 0xff, 5: 0x3ff, 6: 0xfff}
    for r in ROLS:
        i = dis(r); k = int(i.op_str.split(',')[1], 0); o = off(r); assert b[o:o+2] == b'\xC1\xC5' and b[o+2] == k
        shr = andi = None
        for va in range(r-12, r):
            try: j = dis(va)
            except StopIteration: continue
            if j.mnemonic == 'shr' and j.op_str == 'bp, %s' % hex(16-k) and va+j.size <= r: shr = va
        for va in range(r+3, r+16):
            try: j = dis(va)
            except StopIteration: continue
            if j.mnemonic == 'and' and j.op_str == 'ebp, %s' % hex(MASK[k]): andi = va; break
        assert shr and andi
        os_ = off(shr); b[os_+3] = 16-k-LT; b[o+2] = k+LT
        oa = off(andi); b[oa+2:oa+6] = struct.pack('<I', (1 << (2*(k+LT)))-1)
    # scorrimento texture: N x N, passo T
    N = 64*T; ROW = N*T
    SC = cave(f"""
  mov ecx,[esp+4]
  push ebx
  push esi
  push edi
  push ebp
  sub esp,{ROW}
  mov eax,[ecx]
  mov eax,[eax*4+0x67ce50]
  mov edx,[ecx+8]
  cmp dword ptr [ecx+4],0
  jne LSv
  mov ebp,{N}
  test edx,edx
  jle LSr
LSl:
  mov esi,eax
  mov edi,esp
  mov ecx,{T}
  rep movsb byte ptr es:[edi], byte ptr [esi]
  lea esi,[eax+{T}]
  mov edi,eax
  mov ecx,{N-T}
  rep movsb byte ptr es:[edi], byte ptr [esi]
  mov esi,esp
  mov ecx,{T}
  rep movsb byte ptr es:[edi], byte ptr [esi]
  add eax,{N}
  dec ebp
  jnz LSl
  jmp LSd
LSr:
  lea esi,[eax+{N-T}]
  mov edi,esp
  mov ecx,{T}
  rep movsb byte ptr es:[edi], byte ptr [esi]
  lea esi,[eax+{N-T-1}]
  lea edi,[eax+{N-1}]
  mov ecx,{N-T}
  std
  rep movsb byte ptr es:[edi], byte ptr [esi]
  cld
  mov esi,esp
  mov edi,eax
  mov ecx,{T}
  rep movsb byte ptr es:[edi], byte ptr [esi]
  add eax,{N}
  dec ebp
  jnz LSr
  jmp LSd
LSv:
  test edx,edx
  jle LSdn
  mov esi,eax
  mov edi,esp
  mov ecx,{ROW//4}
  rep movsd dword ptr es:[edi], dword ptr [esi]
  lea esi,[eax+{ROW}]
  mov edi,eax
  mov ecx,{(N*N-ROW)//4}
  rep movsd dword ptr es:[edi], dword ptr [esi]
  mov esi,esp
  mov ecx,{ROW//4}
  rep movsd dword ptr es:[edi], dword ptr [esi]
  jmp LSd
LSdn:
  lea esi,[eax+{N*N-ROW}]
  mov edi,esp
  mov ecx,{ROW//4}
  rep movsd dword ptr es:[edi], dword ptr [esi]
  lea esi,[eax+{N*N-ROW-4}]
  lea edi,[eax+{N*N-4}]
  mov ecx,{(N*N-ROW)//4}
  std
  rep movsd dword ptr es:[edi], dword ptr [esi]
  cld
  mov esi,esp
  mov edi,eax
  mov ecx,{ROW//4}
  rep movsd dword ptr es:[edi], dword ptr [esi]
LSd:
  add esp,{ROW}
  pop ebp
  pop edi
  pop esi
  pop ebx
  ret
""")
    put(0x444960, asm('jmp %d' % SC, 0x444960))
    # sprite: decompressi k x k -> ingranditi xT nel buffer
    if T == 2: wv = 'mov esi,ebp\nshl esi,8\nor esi,ebp'; st = 'mov word ptr [ebx+edx], si'
    else: wv = 'mov esi,ebp\nshl esi,8\nor esi,ebp\nmov ebp,esi\nshl esi,16\nor esi,ebp'; st = 'mov dword ptr [ebx+edx], esi'
    body_rows = []
    for r in range(T):
        body_rows.append(st)
        if r < T-1:
            body_rows += ['add edx, edi'] * T      # riga successiva = +T*k
    SP = cave(f"""
  pushad
  movzx ecx, word ptr [edx+8]
  mov edi,64
  cmp ecx,2
  jne LPc4
  mov edi,32
LPc4:
  cmp ecx,4
  jne LPgo
  mov edi,16
LPgo:
  mov eax,[edx]
  cmp dword ptr [eax],-1
  je LPraw
  mov esi,eax
  push dword ptr [0x632fa0]
  push eax
  call 0x402293
  add esp,8
  mov ecx,edi
  imul ecx,edi
  cmp dword ptr [esi],ecx
  jne LPdone
  jmp LPexp
LPraw:
  lea esi,[eax+4]
  mov ecx,edi
  imul ecx,edi
  mov ebx,edi
  mov edi,[0x632fa0]
  rep movsb byte ptr es:[edi], byte ptr [esi]
  mov edi,ebx
LPexp:
  mov ebx,[0x632fa0]
  lea ecx,[edi-1]
LPy:
  lea eax,[edi-1]
LPx:
  mov edx,ecx
  imul edx,edi
  add edx,eax
  movzx ebp, byte ptr [ebx+edx]
  {wv}
  mov edx,ecx
  imul edx,edi
  shl edx,{2*LT}
  lea edx,[edx+eax*{T}]
  {chr(10).join(body_rows)}
  dec eax
  jns LPx
  dec ecx
  jns LPy
LPdone:
  popad
  mov eax,[0x632fa0]
  mov [0x62dbf0],eax
  jmp 0x437397
""")
    o = off(0x437367); assert b[o:o+2] == b'\x8b\x02'; put(0x437367, asm('jmp %d' % SP, 0x437367))
    # secondo percorso sprite (quadrilateri a schermo, 0x441d80): passo righe/colonne xT,
    # per sprite gia' ingranditi nell'archivio (SPRITES_xX)
    for shr_va, and_va, k in (() if not SPRHD else ((0x441ff9, 0x442002, 16), (0x4420b1, 0x4420ba, 32), (0x442169, 0x442172, 64),
                                                    (0x442239, 0x442242, 16), (0x4422d7, 0x4422e0, 32), (0x442375, 0x44237e, 64))):
        o = off(shr_va); assert bytes(b[o:o+2]) == b'\xC1\xE8' and b[o+2] == 15-{16: 4, 32: 5, 64: 6}[k]
        b[o+2] = 15-LT-({16: 4, 32: 5, 64: 6}[k]+LT)
        o = off(and_va); assert bytes(b[o:o+2]) == b'\x83\xE0' and b[o+2] == (256-k) & 0xff
        m = (0x100000000-k*T) & 0xffffffff
        if m >= 0xffffff80: b[o+2] = m & 0xff                 # entra in imm8 con segno
        else: assert m == 0xffffff00; b[o:o+3] = b'\x30\xC0\x90'   # and eax,0xffffff00 == xor al,al
    for va in ((0x44201d, 0x4420d5, 0x44218d, 0x442258, 0x4422f6, 0x442394) if SPRHD else ()):
        o = off(va); assert bytes(b[o:o+3]) == b'\xC1\xED\x0f'; b[o+2] = 15-LT
    imm(0x438dbd, 0x1000, 0x1000*T*T)                       # buffer sprite
    imm(0x438dd4, 0x271000, 0x271000 + 0x80000*T*T*2)      # pool memoria livello
    o = off(0x48cd54); assert bytes(b[o:o+14]) == b'DATA\\DATA.ARC\x00'
    b[o:o+14] = ('DATA\\HD%d%s.ARC' % (T, 'S' if SPRHD else 'X')).encode() + b'\x00'

# ---------------- RELIGHT: i cicli Gouraud salvano texel e luce completa (per WINMX.dll) ----------------
# variabili nell'area libera della sezione .data (azzerata): G_ON, G_TEX (texbuf-fb), G_LIT (litbuf-4*fb)
G_ON, G_TEX, G_LIT = 0x74b800, 0x74b804, 0x74b808
for sec in pe.sections:
    if sec.Name.startswith(b'.data'):
        assert sec.VirtualAddress + BASE + sec.Misc_VirtualSize <= G_ON
        so = sec.get_file_offset() + 8; b[so:so+4] = struct.pack('<I', G_LIT + 4 - BASE - sec.VirtualAddress + 0x40)
TSH = 10 - LT; TROL = 6 + LT; TMASK = (1 << (2*TROL)) - 1
STORE = f"""
  test byte ptr [{G_ON}], 1
  jz {{n}}
  mov ebp, dword ptr [{G_TEX}]
  mov byte ptr [ebp+edi-1], al
  mov ebp, dword ptr [{G_LIT}]
  mov dword ptr [ebp+edi*4-4], ecx
{{n}}:"""
G1 = cave(f"""
RL1:
  shr bp, {TSH}
  mov eax, ecx
  rol ebp, {TROL}
  inc edi
  shr eax, 8
  and ebp, {TMASK}
  and eax, 0x1f00
  add ebx, edx
  mov al, byte ptr [esi+ebp]
  {STORE.format(n='RN1')}
  add ecx, dword ptr [0x62dac0]
  mov al, byte ptr [eax+0x6460d0]
  dec dword ptr [0x62db74]
  mov byte ptr [edi-1], al
  mov ebp, ebx
  jg RL1
  jmp 0x430645
""")
G2 = cave(f"""
RL2:
  shr bp, {TSH}
  mov eax, ecx
  rol ebp, {TROL}
  inc edi
  shr eax, 8
  and ebp, {TMASK}
  and eax, 0xff00
  add ebx, edx
  mov al, byte ptr [esi+ebp]
  test al, al
  je RS2
  {STORE.format(n='RN2')}
  add ecx, dword ptr [0x62dac0]
  mov al, byte ptr [eax+0x6460d0]
  dec dword ptr [0x62db74]
  mov byte ptr [edi-1], al
  mov ebp, ebx
  jg RL2
  jmp 0x4308ec
RS2:
  add ecx, dword ptr [0x62dac0]
  dec dword ptr [0x62db74]
  mov ebp, ebx
  jg RL2
  jmp 0x4308ec
""")
assert bytes(b[off(0x43060d):off(0x43060d)+2]) == b'\x8b\xeb' and bytes(b[off(0x43089e):off(0x43089e)+2]) == b'\x8b\xeb'
put(0x43060f, asm('jmp %d' % G1, 0x43060f)); put(0x4308a0, asm('jmp %d' % G2, 0x4308a0))

# import WINMM.dll -> WINMX.dll (musica, joypad, presentazione: vedi tools\winmx)
if '--nowinmx' not in sys.argv:
    i = b.find(b'WINMM.dll\x00'); assert i > 0 and b.find(b'WINMM.dll\x00', i+1) < 0
    b[i:i+9] = b'WINMX.dll'
open(OUT, 'wb').write(b)
print('scritto %s  (%dx%d, texture %dx%d)' % (OUT, W, H, 64*T, 64*T))
