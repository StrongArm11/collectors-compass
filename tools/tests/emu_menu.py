"""Emulate the settings menu end to end on a fake game: module start-up on a fake main patched with our IPS, the
graphics-table load, then frames of: icon update (pause map open or not), controller read through the patched
import stub (Python plays nn::hid::GetNpadStates with a 16-sample history, newest first), and a "UI 2D" layer draw.
Checks what the game sees of the buttons (chord held back, tap replayed, menu blanks input, B hidden after close),
the settings the menu changes, and writes the painted panel to PNG files.
usage: emu_menu.py <subsdk9> <ips> <main image> <out dir> [screen format] [width height]"""
import struct, sys, os
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_INSN, UcError
from unicorn import arm64_const as A
from emu_overlay_common import load_nso, assemble, relocate, FakeFs

NSO, IPS, MAIN_IMG, OUTDIR = sys.argv[1:5]
FORMAT = int(sys.argv[5], 0) if len(sys.argv) > 5 else 0x25
SW, SH = (int(sys.argv[6]), int(sys.argv[7])) if len(sys.argv) > 7 else (1920, 1080)
BASE, MAIN, NVN, STACK, DATA = 0x85A35000, 0x80AB5000, 0x70000000, 0x71000000, 0x72000000
END = NVN + 0xFFF0
uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM, cpu=A.UC_CPU_ARM64_MAX); uc.reg_write(A.UC_ARM64_REG_CPACR_EL1, 0x300000)
img = load_nso(NSO); uc.mem_map(BASE, len(img)); uc.mem_write(BASE, bytes(img))
real = open(MAIN_IMG, 'rb').read()
for page in (0, 0x3C47000, 0x209000, 0x3C46000, 0x4E9E000, 0x4E7C000, 0x4E7E000, 0x4E84000, 0x4E91000, 0x43E000,
             0x2D8000, 0x129E000, 0xE8000, 0x3E000, 0x4C000, 0x4D000, 0x3C42000, 0x4CBF000, 0x4BE1000):
    uc.mem_map(MAIN + page, 0x1000)
for page in (0x209000, 0x3C42000): uc.mem_write(MAIN + page, real[page:page + 0x1000])
uc.mem_write(MAIN + 4, struct.pack('<I', 0x3C4705C)); uc.mem_write(MAIN + 0x3C4705C, b'MOD0')
for site, words in ((0x129E290, (0xD503237F, 0xA9BE7BFD, 0xA9014FF4, 0x910003FD)), (0x4CD60, (0xD503237F, 0xA9BA7BFD, 0xA9016FFC, 0xA90267FA)), (0xE8DA0, (0xD503237F, 0xD10403FF, 0xA90A7BFD, 0xA90B6FFC))):
    uc.mem_write(MAIN + site, struct.pack('<4I', *words))
d = open(IPS, 'rb').read(); p = 5
while d[p:p + 4] != b'EEOF':
    off, n = struct.unpack('>IH', d[p:p + 6]); p += 6; uc.mem_write(MAIN + off - 0x100, d[p:p + n]); p += n
GAME_HEAP = 0x76000000; uc.mem_map(GAME_HEAP, 16 << 20)   # the game's malloc heap (aligned_alloc stub)
uc.mem_write(MAIN + 0x3ECA0, assemble('    movz x0, #0x7600, lsl #16\n    ret\n'))
uc.mem_write(MAIN + 0x209050, assemble('    retab\n'))           # icon routine body: nothing
uc.mem_map(NVN, 0x10000); uc.mem_map(STACK, 0x40000); uc.mem_map(DATA, 0x20000)
def w(a, fmt, *v): uc.mem_write(a, struct.pack(fmt, *v))
PROCSLOT, COLORS, LAYER, LAYERNAME, SCREEN, CMD, DEVICE = DATA, DATA + 0x10, DATA + 0x100, DATA + 0x400, DATA + 0x800, DATA + 0x900, DATA + 0xA00
MGR, STATES, IDPTR = DATA + 0x2000, DATA + 0x3000, DATA + 0x3400
w(COLORS, '<Q', SCREEN); w(LAYER + 0xE0, '<Q', LAYERNAME); w(LAYER + 0x94, '<I', 7); uc.mem_write(LAYERNAME, b'UI 2D\0')
w(MGR + 0x368, '<i', 0); w(MGR + 0x1C8, '<Q', 0)
uc.mem_write(MAIN + 0x129E2A0, assemble(f'''
    mov x9, x1
    adr x1, 1f
    blr x9
    ldr x9, 2f
    str x0, [x9]
    ldp x20, x19, [sp, #0x10]
    ldp x29, x30, [sp], #0x20
    retab
1:  .asciz "nvnCommandBufferSetRenderTargets"
    .balign 8
2:  .quad {PROCSLOT}
'''))
uc.mem_write(MAIN + 0x4CD70, assemble(f'''
    ldr x9, 1f
    ldr x9, [x9]
    ldr x0, 2f
    mov x1, #1
    ldr x2, 3f
    mov x3, xzr
    mov x4, xzr
    mov x5, xzr
    blr x9
    ldp x26, x25, [sp, #0x20]
    ldp x28, x27, [sp, #0x10]
    ldp x29, x30, [sp], #0x60
    retab
    .balign 8
1:  .quad {PROCSLOT}
2:  .quad {CMD}
3:  .quad {COLORS}
'''))
# the import table: GetNpadStates for each style -> Python
HID = NVN + 0x800
for i, got in enumerate((0x4CBF5C0, 0x4CBF5C8, 0x4CBF5D0, 0x4CBF5D8, 0x4CBF5E0)): w(MAIN + got, '<Q', HID + 16 * i)

NAMES = ['nvnCommandBufferSetRenderTargets', 'nvnTextureGetWidth', 'nvnTextureGetHeight', 'nvnTextureGetFormat', 'nvnTextureGetTarget',
         'nvnTextureGetSamples', 'nvnCommandBufferCopyTextureToTexture', 'nvnCommandBufferBarrier', 'nvnMemoryPoolBuilderSetDefaults',
         'nvnMemoryPoolBuilderSetDevice', 'nvnMemoryPoolBuilderSetStorage', 'nvnMemoryPoolBuilderSetFlags', 'nvnMemoryPoolInitialize',
         'nvnTextureBuilderSetDefaults', 'nvnTextureBuilderSetDevice', 'nvnTextureBuilderSetTarget', 'nvnTextureBuilderSetSize2D',
         'nvnTextureBuilderSetFormat', 'nvnTextureBuilderSetFlags', 'nvnTextureBuilderSetStride', 'nvnTextureBuilderGetStorageSize',
         'nvnTextureBuilderGetStorageAlignment', 'nvnTextureBuilderSetStorage', 'nvnTextureInitialize', 'nvnTextureFinalize',
         'nvnMemoryPoolMap', 'nvnMemoryPoolFlushMappedRange', 'nvnDeviceGetInteger']
LOOKUP = NVN
STUB = {NVN + 0x100 + 16 * i: n for i, n in enumerate(NAMES)}
BYNAME = {n: a for a, n in STUB.items()}
st = {'stride': 0, 'h': 0, 'w': 0, 'storage': 0, 'copies': [], 'tex': {}, 'pool': 0, 'hidcalls': 0}
history = []          # (sampling, buttons, lx, ly), oldest first
clock = [1_000_000]
log = []

def reg(i): return uc.reg_read(getattr(A, f'UC_ARM64_REG_X{i}'))
def ret(value=None):
    if value is not None: uc.reg_write(A.UC_ARM64_REG_X0, value)
    uc.reg_write(A.UC_ARM64_REG_PC, uc.reg_read(A.UC_ARM64_REG_LR))
def cstr(a): b = bytes(uc.mem_read(a, 64)); return b[:b.index(0)].decode()

def on_code(uc, addr, size, data):
    x = [reg(i) for i in range(8)]
    if addr == LOOKUP: ret(BYNAME.get(cstr(x[1]), 0)); return
    if HID <= addr < HID + 0x50:
        st['hidcalls'] += 1
        n = min(x[1], 16)
        for i in range(n):
            s = history[-1 - i] if i < len(history) else (0, 0, 0, 0)
            w(x[0] + 0x28 * i, '<qQiiiiII', s[0], s[1], s[2], s[3], 0, 0, 1, 0)
        ret(n); return
    n = STUB.get(addr)
    if n is None: return
    v = None
    if n == 'nvnTextureGetWidth': v = SW if x[0] == SCREEN else 0
    elif n == 'nvnTextureGetHeight': v = SH if x[0] == SCREEN else 0
    elif n == 'nvnTextureGetFormat': v = FORMAT
    elif n in ('nvnTextureGetTarget', 'nvnTextureGetSamples'): v = 1
    elif n == 'nvnCommandBufferCopyTextureToTexture':
        src = struct.unpack('<6i', uc.mem_read(x[3], 24)); dst = struct.unpack('<6i', uc.mem_read(x[6], 24))
        st['copies'].append((x[1], src, dst))
    elif n == 'nvnMemoryPoolBuilderSetStorage': st['pool'] = x[1]
    elif n == 'nvnMemoryPoolInitialize': v = 1
    elif n == 'nvnMemoryPoolMap': v = 0
    elif n == 'nvnTextureBuilderSetStride': st['stride'] = x[1]
    elif n == 'nvnTextureBuilderSetSize2D': st['w'], st['h'] = x[1], x[2]
    elif n == 'nvnTextureBuilderGetStorageSize': v = st['stride'] * st['h']
    elif n == 'nvnTextureBuilderGetStorageAlignment': v = 4096
    elif n == 'nvnTextureBuilderSetStorage': st['storage'] = x[2]
    elif n == 'nvnTextureInitialize': st['tex'][x[0]] = st['storage']; v = 1
    elif n == 'nvnDeviceGetInteger': w(x[2], '<i', 256)
    ret(v)

def on_svc(uc, intno, data):
    pc = uc.reg_read(A.UC_ARM64_REG_PC); imm = (struct.unpack('<I', uc.mem_read(pc - 4, 4))[0] >> 5) & 0xFFFF
    if imm == 0x27: log.append(bytes(uc.mem_read(reg(0), reg(1))).decode())
    elif imm == 0x29: uc.reg_write(A.UC_ARM64_REG_X0, 0); uc.reg_write(A.UC_ARM64_REG_X1, 0x0400486024DEC000)
    else: raise SystemExit(f'unexpected exception {intno} at pc {pc:#x} (module+{pc - BASE:#x})')

def on_mrs(uc, r, cp, data):
    if (cp.op0, cp.op1, cp.crn, cp.crm, cp.op2) == (3, 3, 14, 0, 1): uc.reg_write(r, clock[0]); return True    # cntpct_el0
    if (cp.op0, cp.op1, cp.crn, cp.crm, cp.op2) == (3, 3, 13, 0, 3): return False                                # tpidrro_el0
    raise SystemExit(f'(harness) FAIL: system register read {(cp.op0, cp.op1, cp.crn, cp.crm, cp.op2)} that Lichii may not emulate')

uc.hook_add(UC_HOOK_CODE, on_code, begin=NVN, end=NVN + 0x1000)
uc.hook_add(UC_HOOK_INTR, on_svc)
uc.hook_add(UC_HOOK_INSN, on_mrs, None, 1, 0, A.UC_ARM64_INS_MRS)
import ast
START_FILES = ast.literal_eval(os.environ.get('MM_FILES', '{}'))
fs = FakeFs(uc, NVN + 0xC00, START_FILES)
tags = relocate(uc, img, BASE, {'nnMain': MAIN + 0x1202CD0, **fs.imports()})

def call(addr, *args, label=''):
    for i, a in enumerate(args): uc.reg_write(getattr(A, f'UC_ARM64_REG_X{i}'), a)
    uc.reg_write(A.UC_ARM64_REG_SP, STACK + 0x3F000); uc.reg_write(A.UC_ARM64_REG_LR, END)
    uc.reg_write(A.UC_ARM64_REG_TPIDRRO_EL0, 0x1234000)
    try: uc.emu_start(addr, END, count=200_000_000)
    except UcError as e: log.append(f'!! {label}: {e} at pc {uc.reg_read(A.UC_ARM64_REG_PC):#x} (module+{uc.reg_read(A.UC_ARM64_REG_PC) - BASE:#x})')
    return reg(0)

BTN = {'A': 1, 'B': 2, 'L': 1 << 6, 'R': 1 << 7, 'Left': 1 << 12, 'Up': 1 << 13, 'Right': 1 << 14, 'Down': 1 << 15}
def mask(names): return sum(BTN[n] for n in names.split('+')) if names else 0
frame_no = [0]
def frame(held='', ly=0, pause=True):
    """One game frame: returns the newest buttons the game sees."""
    frame_no[0] += 1; clock[0] += 520_833                      # 1/60 s at 31.25 MHz
    history.append((frame_no[0] * 4, mask(held), 0, ly))
    w(MGR + 0x45C, '<B', 1 if pause else 0)
    call(MAIN + 0x209040, MGR, label='icon update')
    w(IDPTR, '<I', 0)
    call(MAIN + 0x3C42800, STATES, 16, IDPTR, label='controller read')
    seen = struct.unpack('<Q', uc.mem_read(STATES + 8, 8))[0]
    newest_two = [struct.unpack('<Q', uc.mem_read(STATES + 0x28 * i + 8, 8))[0] for i in range(2)]
    call(MAIN + 0x4CD60, 0xD15, LAYER, 5, label='UI 2D draw')
    return seen, newest_two

names = {v: k for k, v in BTN.items()}
def show(b): return '+'.join(n for n, m in BTN.items() if b & m) or '-'
checks = []
def check(what, ok): checks.append((what, ok)); log.append(f'(harness) {"PASS" if ok else "FAIL"}: {what}')

call(BASE + tags[12], label='start-up')
call(MAIN + 0x129E290, DEVICE, LOOKUP, label='graphics table load')
for _ in range(3): frame()
# 1. a quick L tap on the pause map: held back, then replayed when let go
s1, _ = frame('L'); s2, two = frame('')
check(f'L tap on the map: held back ({show(s1)}), then replayed ({show(s2)})', s1 == 0 and s2 == BTN['L'])
s3, _ = frame('')
check(f'after the replay the game sees L let go ({show(s3)})', s3 == 0)
# 2. L held: the game gets it after the chord window (125 ms = 7.5 frames)
seen = [frame('L')[0] for _ in range(12)]
check(f'L held: hidden at first, then passed on: {[show(s) for s in seen]}', seen[0] == 0 and seen[-1] == BTN['L'] and seen[6] == 0)
frame(''); frame('')
# 3. L away from the pause map: straight through
seen_off = frame('L', pause=False)[0]
for _ in range(15): frame('', pause=False)   # the map 'closes' (no pause-map updates for 1/4 s)
seen_off2 = frame('L', pause=False)[0]
check(f'L off the map goes straight through ({show(seen_off2)})', seen_off2 == BTN['L'])
frame('', pause=False); frame(); frame()
# 4. L then R: the menu opens, the game sees nothing
copies_before = len(st['copies'])
a, _ = frame('L'); b, _ = frame('L+R')
c, _ = frame('L+R'); dd, _ = frame('')
check(f'L then R opens the menu; game sees {show(a)}, {show(b)}, {show(c)}, {show(dd)}', a == b == c == dd == 0 and any('menu: opened' in l for l in log))
check(f'the panel is copied to the screen while open ({len(st["copies"]) - copies_before} copies)', len(st['copies']) > copies_before)

def dump(name):
    if not st['copies']:
        for line in log: print(line)
        raise SystemExit('(harness) no panel copy to dump')
    tex, src, dst = st['copies'][-1]
    base = st['pool'] + st['tex'][tex]; W, H = src[3], src[4]; stride = st['stride']
    raw = bytes(uc.mem_read(base, stride * H))
    from PIL import Image
    im = Image.new('RGB', (W, H))
    px = im.load()
    def dec(v, mbits):   # unsigned small float -> 0..1 linear
        e, m = v >> mbits, v & ((1 << mbits) - 1)
        if e == 0: return (m / (1 << mbits)) * 2 ** -14
        return (1 + m / (1 << mbits)) * 2 ** (e - 15)
    for y in range(H):
        row = raw[y * stride:y * stride + 4 * W]
        for x in range(W):
            v = struct.unpack_from('<I', row, 4 * x)[0]
            if FORMAT == 0x3F:
                lin = (dec(v & 0x7FF, 6), dec((v >> 11) & 0x7FF, 6), dec(v >> 22, 5))
                px[x, y] = tuple(min(255, round(255 * max(0, c) ** (1 / 2.2))) for c in lin)
            else:
                px[x, y] = (v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF)
    path = os.path.join(OUTDIR, name); im.save(path); log.append(f'(harness) wrote {path} ({W}x{H} at screen {dst[0]},{dst[1]})')

dump(f'menu_{SW}x{SH}_{FORMAT:#x}_open.png')
# 5. A opens the Gold Skulltulas page; Down twice, A: its "Show collected" on
frame('A'); frame(''); frame('Down'); frame(''); frame('Down'); frame(''); frame('A'); frame('')
check('A opens the Skulltula page; Down, Down, A turns its "Show collected" on', any('Gold Skulltulas: Show collected (faded) on' in l for l in log))
dump(f'menu_{SW}x{SH}_{FORMAT:#x}_page.png')
# 6. B back to the list; stick down, Right opens Pieces of Heart; A turns "Show on the maps" off; B back
frame('B'); frame(''); frame('', ly=-30000); frame(''); frame('Right'); frame(''); frame('A'); frame(''); frame('B'); frame('')
check('B, stick down, Right, A turns "Pieces of Heart: Show on the maps" off', any('Pieces of Heart: Show on the maps off' in l for l in log))
dump(f'menu_{SW}x{SH}_{FORMAT:#x}_changed.png')
# 7. B closes; B stays hidden from the game until let go
b1, _ = frame('B'); b2, _ = frame('B'); b3, _ = frame(''); b4, _ = frame('B')
check(f'B closes the menu; held B hidden ({show(b1)}, {show(b2)}), new B press reaches the game ({show(b4)})',
      b1 == 0 and b2 == 0 and b4 == BTN['B'] and any('menu: closed' in l for l in log))
n = len(st['copies']); frame(''); frame('')
check(f'no panel copies after closing ({len(st["copies"]) - n})', len(st['copies']) == n)
# 8. chord pressed in one frame opens; leaving the map closes the menu
frame(''); x1, _ = frame('L+R'); frame('')
opened = sum('menu: opened' in l for l in log) == 2
for _ in range(20): frame('', pause=False)
check(f'L+R in one frame opens ({opened}); leaving the pause map closes it', opened and any('pause map closed; menu closed' in l for l in log))
path = 'mmsd:/OoTMinimapMod/settings.ini'
saved = fs.files.get(path, b'').decode()
if not START_FILES:
    check(f'settings saved when the menu closed: {saved.splitlines()[1:]}', 'skulltulas_show_collected=1' in saved and 'hearts=0' in saved and 'skulltulas_only_now=1' in saved)
    check(f'file errors never stop the game (auto-abort off during our calls, back on after): {fs.abort[-4:]}', fs.abort[-1] is True and False in fs.abort)
    open(os.path.join(OUTDIR, 'settings_saved.txt'), 'w').write(repr({path: saved.encode()}))
else:
    check('settings loaded at start: ' + ' '.join(l for l in log if 'settings: loaded' in l), any('settings: loaded 13' in l and 'Skulltulas 111' in l and 'hearts 01' in l for l in log))
for line in log: print(line)
print(f"(harness) {sum(ok for _, ok in checks)}/{len(checks)} checks passed; {st['hidcalls']} controller reads, {len(st['copies'])} panel copies; fs calls {fs.calls[:12]}")
