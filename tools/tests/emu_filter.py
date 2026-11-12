"""Emulate the filter: module start-up on a fake main patched with our IPS (real code page around the icon routine),
a fake icon manager with three entries and a fake game state (flags, clock, area). Runs the hooked routine (the
watcher decides), then the patched instruction's trampoline for each entry, and reports where each one would go:
hide, the game's rule, or show."""
import struct, sys
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INTR, UcError
from unicorn import arm64_const as A
from emu_overlay_common import load_nso, assemble, relocate
sys.path.insert(0, '../..'); from names import murmur3
NSO, IPS, MAIN_IMG = sys.argv[1], sys.argv[2], sys.argv[3]
BASE, MAIN, STACK, DATA = 0x85A35000, 0x80AB5000, 0x71000000, 0x72000000
END = 0x70000000 + 0xFFF0
uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM, cpu=A.UC_CPU_ARM64_MAX); uc.reg_write(A.UC_ARM64_REG_CPACR_EL1, 0x300000)
img = load_nso(NSO); uc.mem_map(BASE, len(img)); uc.mem_write(BASE, bytes(img))
real = open(MAIN_IMG, 'rb').read()
for page in (0, 0x3C47000, 0x209000, 0x3C46000, 0x4E9E000, 0x4E7C000, 0x4E7E000, 0x4E84000, 0x4E91000, 0x43E000, 0x2D8000, 0x3C42000, 0x129E000, 0xE8000, 0x4C000): uc.mem_map(MAIN + page, 0x1000)
for page in (0x209000, 0x3C42000, 0x129E000, 0xE8000, 0x4C000): uc.mem_write(MAIN + page, real[page:page + 0x1000])
uc.mem_write(MAIN + 4, struct.pack('<I', 0x3C4705C)); uc.mem_write(MAIN + 0x3C4705C, b'MOD0')
d = open(IPS, 'rb').read(); p = 5
while d[p:p + 4] != b'EEOF':
    off, n = struct.unpack('>IH', d[p:p + 6]); p += 6; uc.mem_write(MAIN + off - 0x100, d[p:p + n]); p += n
uc.mem_write(MAIN + 0x209050, assemble('    retab\n'))                     # the routine's body: nothing
uc.mem_write(MAIN + 0x209650, assemble('    mov x0, xzr\n    ret\n'))      # placed-pane lookup: none here
for addr, v in ((0x2090B0, 1), (0x209260, 0), (0x2092D8, 2)):          # where the trampoline can go: report it
    uc.mem_write(MAIN + addr, assemble(f'    mov x0, #{v}\n    ret\n'))
uc.mem_map(0x70000000, 0x10000); uc.mem_map(STACK, 0x40000); uc.mem_map(DATA, 0x20000)
def w(a, fmt, *v): uc.mem_write(a, struct.pack(fmt, *v))
MGR, ENTRIES, GDM, IDX, ENT, WORLD, TMGR, CLOCK, SCENE, PATH = (DATA + o for o in (0, 0x1000, 0x3000, 0x3100, 0x3800, 0x4000, 0x4100, 0x5000, 0x6000, 0x6100))
entries = [(0x62c340f0b876c195, 7, 18.0, 18.0), (0x8186bcb07d6c2cf9, 7, -33.2, 31.4), (0x1234, 7, 0.0, 0.0)]   # KOK-3, LW-1, unknown
for i, (key, typ, x, z) in enumerate(entries): w(ENTRIES + i * 0xC8, '<Q3fi', key, x, 0.0, z, typ)
w(MGR + 0x1C8, '<Q', ENTRIES); w(MGR + 0x368, '<i', len(entries)); w(MGR + 0x45C, '<B', 0)
PARTS, PANES = DATA + 0x7000, DATA + 0x9000
w(MGR + 0xB8, '<I', len(entries)); w(MGR + 0xC0, '<Q', PARTS)
for i in range(len(entries)):
    part = PARTS + 0x100 + i * 0x80; lay = DATA + 0x8000 + i * 0x100
    w(PARTS + 8 * i, '<Q', part); w(part + 0x20, '<Q', lay); w(lay + 0x3A, '<B', 1)
from unicorn import UC_HOOK_CODE
def fake_calls(uc, addr, size, data):
    x0, x1 = uc.reg_read(A.UC_ARM64_REG_X0), uc.reg_read(A.UC_ARM64_REG_X1)
    if addr == MAIN + 0x43E1E0:
        name = bytes(uc.mem_read(x1, 16)).split(b'\0')[0]
        idx = (x0 - (DATA + 0x8000)) // 0x100
        uc.reg_write(A.UC_ARM64_REG_X0, PANES + idx * 0x200 + (0x100 if name == b'P_IconSh_00' else 0))
    elif addr == MAIN + 0x2D8AC0:
        s0 = struct.unpack('<f', struct.pack('<I', uc.reg_read(A.UC_ARM64_REG_S0)))[0]
        w(x0 + 0x74, '<f', s0)
    else: return
    uc.reg_write(A.UC_ARM64_REG_PC, uc.reg_read(A.UC_ARM64_REG_LR))
uc.hook_add(UC_HOOK_CODE, fake_calls, begin=MAIN + 0x2D8AC0, end=MAIN + 0x2D8AC4)
uc.hook_add(UC_HOOK_CODE, fake_calls, begin=MAIN + 0x43E1E0, end=MAIN + 0x43E1E4)
def alphas(i): return tuple(round(struct.unpack('<f', uc.mem_read(PANES + i * 0x200 + o + 0x74, 4))[0], 2) for o in (0, 0x100))
KEYS = DATA + 0xA000
flags = {}
def set_flags(**kv):
    names = {'child': 'Sequence_IsChildhood', 'hookshot': 'IsGet.Item_Hookshot'}
    hs = [(murmur3(names[k].encode()), v) for k, v in kv.items()]
    size = 64; uc.mem_write(IDX, bytes(8 * size)); uc.mem_write(ENT, bytes(24 * 8))
    for i, (h, v) in enumerate(hs):
        slot = h % size
        while struct.unpack('<I', uc.mem_read(IDX + 8 * slot, 4))[0]: slot = (slot + 1) % size
        w(IDX + 8 * slot, '<II', h, i); w(ENT + 24 * i + 0x16, '<B', v)
    w(GDM + 0x10, '<I', len(hs)); w(GDM + 0x18, '<Q', ENT); w(GDM + 0x20, '<Q', IDX); w(GDM + 0x2C, '<I', size)
    w(GDM + 0xC58, '<I', 0); w(GDM + 0xC60, '<Q', KEYS)
w(MAIN + 0x4E842A8, '<Q', GDM); w(MAIN + 0x4E7EAB0, '<Q', WORLD); w(WORLD + 0x578, '<Q', TMGR); w(TMGR + 0xD48, '<Q', CLOCK)
w(MAIN + 0x4E919D0, '<Q', SCENE); w(SCENE + 0x40, '<Q', PATH); uc.mem_write(PATH, b'Banc/KokiriForest/KokiriForest_Adult.bcett.byml.zs\0')
def set_hour(h): w(CLOCK + 9, '<B', 0); w(CLOCK + 0x28, '<i', h * 3600 + 5 * 60)
relocate(uc, img, BASE, {'nnMain': MAIN + 0x1202CD0})
log = []
def on_svc(uc, intno, data):
    pc = uc.reg_read(A.UC_ARM64_REG_PC); imm = (struct.unpack('<I', uc.mem_read(pc - 4, 4))[0] >> 5) & 0xFFFF
    if imm == 0x27: log.append(bytes(uc.mem_read(uc.reg_read(A.UC_ARM64_REG_X0), uc.reg_read(A.UC_ARM64_REG_X1))).decode())
    elif imm == 0x29: uc.reg_write(A.UC_ARM64_REG_X0, 0); uc.reg_write(A.UC_ARM64_REG_X1, 0x0400486024DEC000)
uc.hook_add(UC_HOOK_INTR, on_svc)
def call(addr, regs, label=''):
    for r, v in regs.items(): uc.reg_write(getattr(A, f'UC_ARM64_REG_{r.upper()}'), v)
    uc.reg_write(A.UC_ARM64_REG_SP, STACK + 0x3F000); uc.reg_write(A.UC_ARM64_REG_LR, END); uc.reg_write(A.UC_ARM64_REG_TPIDRRO_EL0, 0x1234000)
    try: uc.emu_start(addr, END, count=5_000_000)
    except UcError as e: log.append(f'!! {label}: {e} at pc {uc.reg_read(A.UC_ARM64_REG_PC):#x}')
    return uc.reg_read(A.UC_ARM64_REG_X0)
mod0 = struct.unpack_from('<I', img, 4)[0]; dyn = mod0 + struct.unpack_from('<i', img, mod0 + 4)[0]; tags = {}
while True:
    t, v = struct.unpack_from('<qQ', img, dyn); dyn += 16
    if t == 0: break
    tags[t] = v
call(BASE + tags[12], {}, 'start-up')
names = {0: 'hide', 1: "game's rule", 2: 'show'}
for label, fl, hour in (('adult, night, has Hookshot', dict(child=0, hookshot=1), 21), ('adult, day, has Hookshot', dict(child=0, hookshot=1), 12),
                        ('child, night, no Hookshot', dict(child=1, hookshot=0), 22)):
    set_flags(**fl); set_hour(hour)
    for frame in range(16): call(MAIN + 0x209040, {'x0': MGR}, 'icon update')
    res = [names.get(call(MAIN + 0x2090AC, {'x19': MGR, 'x23': i}, 'trampoline'), '?') for i in range(len(entries))]
    log.append(f'(harness) {label}: KOK-3 -> {res[0]}, LW-1 -> {res[1]}, unknown -> {res[2]}; KOK-3 alphas (plain, checked) {alphas(0)}')
for frame in range(250): call(MAIN + 0x209040, {'x0': MGR}, 'icon update')
log.append(f'(harness) after the preview switch: KOK-3 alphas (plain, checked) {alphas(0)}')
w(KEYS, '<QQ', 0x62c340f0b876c195, 1 << 32); w(GDM + 0xC58, '<I', 1)   # KOK-3 collected
for frame in range(16): call(MAIN + 0x209040, {'x0': MGR}, 'icon update')
log.append(f"(harness) KOK-3 collected: -> {names.get(call(MAIN + 0x2090AC, {'x19': MGR, 'x23': 0}, 'trampoline'))}")
for line in log: print(line)
