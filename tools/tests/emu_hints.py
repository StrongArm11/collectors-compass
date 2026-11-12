"""Emulate the pause-map hints (and the settings menu around them) on a fake game: module start-up on a fake main patched with our IPS, the
graphics-table load, then frames of: icon update (pause map open or not), controller read through the patched
import stub (Python plays nn::hid::GetNpadStates with a 16-sample history, newest first), and a "UI 2D" layer draw.
Checks what the game sees of the buttons (chord held back, tap replayed, menu blanks input, B hidden after close),
the settings the menu changes, and writes the painted panel to PNG files.
usage: emu_menu.py <subsdk9> <ips> <main image> <out dir> [screen format] [width height]"""
import struct, sys, os
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INTR, UC_HOOK_CODE, UC_HOOK_INSN, UcError
from unicorn import arm64_const as A
from emu_overlay_common import load_nso, assemble, relocate

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
ENTRIES, PARTS, LAYOUTS, PANES = DATA + 0x4000, DATA + 0x5000, DATA + 0x6000, DATA + 0x8000
entries = [(0x62c340f0b876c195, 7, 18.0, 18.9, (0.0, 0.0)), (0x8186bcb07d6c2cf9, 7, -33.2, 31.4, (200.0, 50.0)),
           (0x1234, 7, 0.0, 0.0, (-150.0, -100.0))]   # KOK-3, LW-1, not in the tracker; last: where the map put them
for i, (key, typ, x, z, pos) in enumerate(entries):
    w(ENTRIES + i * 0xC8, '<Q3fi', key, x, 0.0, z, typ)
    part = PARTS + 0x100 + i * 0x80; lay = LAYOUTS + i * 0x100
    w(PARTS + 8 * i, '<Q', part); w(part + 0x20, '<Q', lay); w(lay + 0x3A, '<B', 2)
    w(PANES + i * 0x400 + 0x200 + 0x6C, '<2f', *pos)            # the placed pane
w(MGR + 0x1C8, '<Q', ENTRIES); w(MGR + 0x368, '<i', len(entries)); w(MGR + 0xB8, '<I', len(entries)); w(MGR + 0xC0, '<Q', PARTS)
uc.mem_write(MAIN + 0x209650, assemble('    ret\n'))   # placed pane: faked below
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
            s = history[-1 - i] if i < len(history) else (0, 0, 0, 0, 0, 0)
            w(x[0] + 0x28 * i, '<qQiiiiII', s[0], s[1], s[2], s[3], s[4], s[5], 1, 0)
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
alphas = {}
def fake_game(uc, addr, size, data):
    x0 = reg(0)
    if addr == MAIN + 0x43E1E0:                       # FindPane(layout, name)
        idx = (x0 - LAYOUTS) // 0x100; ret(PANES + idx * 0x400)
    elif addr == MAIN + 0x209650:                     # placed pane (layout + 0x40)
        idx = (x0 - 0x40 - LAYOUTS) // 0x100; ret(PANES + idx * 0x400 + 0x200)
    elif addr == MAIN + 0x2D8AC0:                     # SetAlpha(pane, s0)
        a = struct.unpack('<f', struct.pack('<I', uc.reg_read(A.UC_ARM64_REG_S0)))[0]
        alphas[(x0 - PANES) // 0x400] = round(a, 2); ret()
for a in (0x43E1E0, 0x209650, 0x2D8AC0): uc.hook_add(UC_HOOK_CODE, fake_game, begin=MAIN + a, end=MAIN + a + 4)
uc.hook_add(UC_HOOK_INTR, on_svc)
uc.hook_add(UC_HOOK_INSN, on_mrs, None, 1, 0, A.UC_ARM64_INS_MRS)
tags = relocate(uc, img, BASE, {'nnMain': MAIN + 0x1202CD0})

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
def frame(held='', lx=0, ly=0, pause=True, rx=0, ry=0):
    """One game frame: returns the newest buttons the game sees."""
    frame_no[0] += 1; clock[0] += 520_833                      # 1/60 s at 31.25 MHz
    history.append((frame_no[0] * 4, mask(held), lx, ly, rx, ry))
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


call(BASE + tags[12], label='start-up')
call(MAIN + 0x129E290, DEVICE, LOOKUP, label='graphics table load')
for _ in range(3): frame()
def picked(): return [l for l in log if l.startswith('[oot-minimap] hint: picked')]
frame('', rx=30000); frame(''); frame('')
check(f'first tilt picks the icon nearest the middle: {picked()[-1:]}', len(picked()) == 1 and 'icon #0 KOK-3' in picked()[-1])
check(f'the hint panel is copied while an icon is picked', len(st['copies']) > 0)
dump(f'hint_{SW}x{SH}_{FORMAT:#x}_kok3.png')
blink = set()
for _ in range(30): frame(); blink.add(alphas.get(0))
check(f'the picked icon blinks (alphas {sorted(blink)[:3]}..{sorted(blink)[-2:]})', len(blink) > 5)
frame('', rx=30000); frame('')
check(f'tilt right picks the icon to the right: {picked()[-1:]}', 'icon #1' in picked()[-1])
dump(f'hint_{SW}x{SH}_{FORMAT:#x}_lw1.png')
frame('', rx=30000); frame('')
check('tilt right again: nothing further right, the pick stays', len(picked()) == 2)
frame('', rx=-30000, ry=-30000); frame('')
check(f'tilt down-left picks the nearest one down-left: {picked()[-1:]}', 'icon #0' in picked()[-1])
frame('', rx=-30000, ry=-30000); frame('')
check(f'and again: the next one down-left: {picked()[-1:]}', 'icon #2' in picked()[-1])
dump(f'hint_{SW}x{SH}_{FORMAT:#x}_unknown.png')
# the settings menu takes the panel; the hint comes back when it closes
frame('L+R'); frame(''); n_menu = sum('menu: opened' in l for l in log)
dump(f'hint_{SW}x{SH}_{FORMAT:#x}_menu_over_hint.png')
frame('B'); frame(''); frame('')
tex, src, dst = st['copies'][-1]
check(f'menu opens over the hint ({n_menu}), and the hint is back after B (panel {src[3]}x{src[4]} at {dst[0]},{dst[1]})', n_menu == 1 and src[3] > 1000)
# world map view: no hint
w(MGR + 0x458, '<I', 0x100); n = len(st['copies']); frame(''); frame('')
check(f'no hint on the world map ({len(st["copies"]) - n} copies)', len(st['copies']) == n)
w(MGR + 0x458, '<I', 0x01000001); frame('', rx=30000); frame('')
n_picks = len(picked()); frame('', lx=30000); frame(''); frame('', lx=-30000, ly=30000); frame('')
check('the left stick does not pick (it changes floors on dungeon maps)', len(picked()) == n_picks)
check(f'back on the area map a tilt picks again: {picked()[-1:]}', len(picked()) >= 4)
# the in-game layout of the Forest Temple test: FOR-5 (-4,-118), FOR-2 (125,313), FOR-3 (-372,108)
for i, pos in enumerate(((-4.0, -118.0), (125.0, 313.0), (-372.0, 108.0))): w(PANES + i * 0x400 + 0x200 + 0x6C, '<2f', *pos)
w(MGR + 0x1C8, '<Q', ENTRIES + 0x1000); frame(''); w(MGR + 0x1C8, '<Q', ENTRIES)   # a new list: the pick resets
frame(''); frame('', rx=30000); frame('')
check(f'cone: first pick nearest the middle {picked()[-1:]}', 'icon #0' in picked()[-1])
frame('', rx=30000); frame(''); n = len(picked())
check(f'cone: right from (-4,-118): (125,313) is 72 degrees off, so it is only taken as a fallback {picked()[-1:]}', 'icon #1' in picked()[-1])
frame('', rx=-30000); frame('')
check(f'cone: left from (125,313) goes to (-372,108) (within 60 degrees) {picked()[-1:]}', 'icon #2' in picked()[-1])
for line in log: print(line)
print(f"(harness) {sum(ok for _, ok in checks)}/{len(checks)} checks passed; {len(st['copies'])} panel copies")
