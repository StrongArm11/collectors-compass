"""Emulate step 2 end to end on a fake game: the module's start-up (hooks installed into a main patched with our IPS),
then the game's graphics-table load (which hands our wrapped lookup to the game) and one "UI 2D" layer draw (which
binds the screen through the wrapped render-target call). Python stands in for the graphics library (NVN): it answers
the lookups with stubs and records the copy. Prints the log, the copy and a sample of the painted pixels."""
import struct, subprocess, sys, os, tempfile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_INTR, UC_HOOK_CODE, UcError
from unicorn.arm64_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
NSO, IPS = sys.argv[1], sys.argv[2]
SCREEN_FORMAT = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x3F
BASE, MAIN, NVN, STACK, DATA = 0x85A35000, 0x80AB5000, 0x70000000, 0x71000000, 0x72000000
END = NVN + 0xFFF0

def load_nso(path):
    s = open(path, 'rb').read()
    segs = [struct.unpack_from('<III', s, 0x10 + i * 0x10) for i in range(3)]
    bss = struct.unpack_from('<I', s, 0x3C)[0]
    img = bytearray((segs[2][1] + segs[2][2] + bss + 0xFFF) & ~0xFFF)
    for fo, mo, sz in segs: img[mo:mo + sz] = s[fo:fo + sz]
    return img

def assemble(src):
    with tempfile.TemporaryDirectory() as d:
        open(f'{d}/a.S', 'w').write(src)
        subprocess.run(['aarch64-linux-gnu-as', '-march=armv8.3-a', f'{d}/a.S', '-o', f'{d}/a.o'], check=True)
        subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', '-j', '.text', f'{d}/a.o', f'{d}/a.bin'], check=True)
        return open(f'{d}/a.bin', 'rb').read()

from unicorn import arm64_const as A
uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM, cpu=A.UC_CPU_ARM64_MAX)   # with pointer authentication, like the Switch 2 CPU
uc.reg_write(UC_ARM64_REG_CPACR_EL1, 0x300000)
img = load_nso(NSO)
uc.mem_map(BASE, len(img)); uc.mem_write(BASE, bytes(img))
for page in (0, 0x3C47000, 0x129E000, 0xE8000, 0x3E000, 0x4C000, 0x4D000, 0x3C46000, 0x4E9E000):   # the parts of main we touch
    uc.mem_map(MAIN + page, 0x1000)
uc.mem_write(MAIN + 4, struct.pack('<I', 0x3C4705C)); uc.mem_write(MAIN + 0x3C4705C, b'MOD0')
for site, words in ((0x129E290, (0xD503237F, 0xA9BE7BFD, 0xA9014FF4, 0x910003FD)), (0x4CD60, (0xD503237F, 0xA9BA7BFD, 0xA9016FFC, 0xA90267FA)), (0xE8DA0, (0xD503237F, 0xD10403FF, 0xA90A7BFD, 0xA90B6FFC))):
    uc.mem_write(MAIN + site, struct.pack('<4I', *words))
# apply our IPS patch to the fake main
d = open(IPS, 'rb').read(); assert d[:5] == b'IPS32'; p = 5
while d[p:p + 4] != b'EEOF':
    off, n = struct.unpack('>IH', d[p:p + 6]); p += 6
    uc.mem_write(MAIN + off - 0x100, d[p:p + n]); p += n
uc.mem_map(NVN, 0x10000); uc.mem_map(STACK, 0x40000); uc.mem_map(DATA, 0x10000)
PROCSLOT, COLORS, LAYER, LAYERNAME, SCREEN, CMD, DEVICE = DATA, DATA + 0x10, DATA + 0x100, DATA + 0x400, DATA + 0x800, DATA + 0x900, DATA + 0xA00
uc.mem_write(COLORS, struct.pack('<Q', SCREEN)); uc.mem_write(LAYER + 0xE0, struct.pack('<Q', LAYERNAME)); uc.mem_write(LAYER + 0x94, struct.pack('<I', 7))
uc.mem_write(LAYERNAME, b'UI 2D\0')
# the rest of the two hooked routines, as small fakes that do what matters to us:
# LoadGraphicsProcs: asks the lookup it was given for the render-target bind and keeps it in the game's table.
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
'''))   # after the routine's own first four words (pacibsp; stp x29,x30,[sp,#-0x20]!; stp x20,x19,[sp,#0x10]; mov x29,sp)
# DrawLayer: binds the screen as the drawing target through the game's table entry.
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
'''))   # after the routine's own first four words (pacibsp; stp x29,x30,[sp,#-0x60]!; stp x28,x27,[sp,#0x10]; stp x26,x25,[sp,#0x20])

# relocate the module (what rtld does)
mod0 = struct.unpack_from('<I', img, 4)[0]; dyn = mod0 + struct.unpack_from('<i', img, mod0 + 4)[0]
tags = {}
while True:
    t, v = struct.unpack_from('<qQ', img, dyn); dyn += 16
    if t == 0: break
    tags[t] = v
strtab, symtab = tags[5], tags[6]
for off in range(tags[7], tags[7] + tags[8], 24):
    r_off, info, add = struct.unpack_from('<QQq', img, off)
    if info & 0xffffffff == 1027: uc.mem_write(BASE + r_off, struct.pack('<Q', BASE + add)); continue
    nm = struct.unpack_from('<I', img, symtab + (info >> 32) * 24)[0]
    name = img[strtab + nm:img.index(b'\0', strtab + nm)].decode()
    uc.mem_write(BASE + r_off, struct.pack('<Q', {'nnMain': MAIN + 0x1202CD0}[name] + add))

# fake graphics library
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
state = {'stride': 0, 'h': 0, 'storage': 0, 'copies': [], 'barriers': 0, 'binds': 0, 'tex': {}, 'pool_storage': 0}
log = []

def ret(uc, value=None):
    if value is not None: uc.reg_write(UC_ARM64_REG_X0, value)
    uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_LR))

def cstr(uc, a):
    b = bytes(uc.mem_read(a, 64)); return b[:b.index(0)].decode()

def on_code(uc, addr, size, data):
    x = [uc.reg_read(getattr(__import__('unicorn.arm64_const', fromlist=['x']), f'UC_ARM64_REG_X{i}')) for i in range(8)]
    if addr == LOOKUP:
        ret(uc, BYNAME.get(cstr(uc, x[1]), 0)); return
    n = STUB.get(addr)
    if n is None: return
    v = None
    if n == 'nvnTextureGetWidth': v = 3840 if x[0] == SCREEN else 0
    elif n == 'nvnTextureGetHeight': v = 2160 if x[0] == SCREEN else 0
    elif n == 'nvnTextureGetFormat': v = SCREEN_FORMAT
    elif n in ('nvnTextureGetTarget', 'nvnTextureGetSamples'): v = 1
    elif n == 'nvnCommandBufferSetRenderTargets': state['binds'] += 1
    elif n == 'nvnCommandBufferBarrier': state['barriers'] += 1
    elif n == 'nvnCommandBufferCopyTextureToTexture':
        src = struct.unpack('<6i', uc.mem_read(x[3], 24)); dst = struct.unpack('<6i', uc.mem_read(x[6], 24))
        state['copies'].append((x[1], src, x[4], dst, x[7]))
    elif n == 'nvnMemoryPoolBuilderSetStorage': state['pool_storage'] = x[1]
    elif n == 'nvnMemoryPoolInitialize': v = 1
    elif n == 'nvnMemoryPoolMap': v = 0
    elif n == 'nvnTextureBuilderSetStride': state['stride'] = x[1]
    elif n == 'nvnTextureBuilderSetSize2D': state['w'], state['h'] = x[1], x[2]
    elif n == 'nvnTextureBuilderGetStorageSize': v = state['stride'] * state['h']
    elif n == 'nvnTextureBuilderGetStorageAlignment': v = 4096
    elif n == 'nvnTextureBuilderSetStorage': state['storage'] = x[2]
    elif n == 'nvnTextureInitialize': state['tex'][x[0]] = state['storage']; v = 1
    elif n == 'nvnDeviceGetInteger': uc.mem_write(x[2], struct.pack('<i', 256))
    ret(uc, v)

def on_svc(uc, intno, data):
    pc = uc.reg_read(UC_ARM64_REG_PC); imm = (struct.unpack('<I', uc.mem_read(pc - 4, 4))[0] >> 5) & 0xFFFF
    if imm == 0x27: log.append(bytes(uc.mem_read(uc.reg_read(UC_ARM64_REG_X0), uc.reg_read(UC_ARM64_REG_X1))).decode())
    elif imm == 0x29: uc.reg_write(UC_ARM64_REG_X0, 0); uc.reg_write(UC_ARM64_REG_X1, 0x0400486024DEC000)
    else: raise SystemExit(f'unexpected exception {intno} at pc {pc:#x} (module+{pc - BASE:#x}, main+{pc - MAIN:#x}), word {struct.unpack("<I", uc.mem_read(pc, 4))[0]:#010x}')

uc.hook_add(UC_HOOK_CODE, on_code, begin=NVN, end=NVN + 0x1000)
uc.hook_add(UC_HOOK_INTR, on_svc)

def call(addr, *args, label=''):
    for i, a in enumerate(args): uc.reg_write(getattr(__import__('unicorn.arm64_const', fromlist=['x']), f'UC_ARM64_REG_X{i}'), a)
    uc.reg_write(UC_ARM64_REG_SP, STACK + 0x3F000); uc.reg_write(UC_ARM64_REG_LR, END)
    uc.reg_write(UC_ARM64_REG_TPIDRRO_EL0, 0x1234000)
    try:
        uc.emu_start(addr, END, count=20_000_000)
    except UcError as e:
        log.append(f'!! {label}: emulation error {e} at pc {uc.reg_read(UC_ARM64_REG_PC):#x} (module+{uc.reg_read(UC_ARM64_REG_PC) - BASE:#x})')

call(BASE + tags[12], label='start-up')
table = struct.unpack('<2Q', uc.mem_read(MAIN + 0x4E9E000, 16))
log.append(f'(harness) hook table: {[hex(t - BASE) + " in module" if t else "empty" for t in table]}')
call(MAIN + 0x129E290, DEVICE, LOOKUP, label='graphics table load')
log.append(f'(harness) the game\'s table now holds {"our wrapper" if BASE <= struct.unpack("<Q", uc.mem_read(PROCSLOT, 8))[0] < BASE + len(img) else "something else"} for the render-target bind')
for frame in range(3):
    call(MAIN + 0x4CD60, 0xD15, LAYER, 5, label=f'UI 2D draw {frame}')
for line in log: print(line)
print(f"(harness) real render-target binds: {state['binds']}, barriers: {state['barriers']}, copies: {len(state['copies'])}")
for src_tex, src, dst_tex, dst, flags in state['copies'][:2]:
    print(f'(harness) copy: source region {src} -> screen region {dst}, flags {flags:#x}')
if state['copies']:
    tex = state['copies'][-1][0]; storage = state['tex'][tex]; base = state['pool_storage'] + storage
    w = state['w']; stride = state['stride']
    def px(x, y): return struct.unpack('<I', uc.mem_read(base + y * stride + 4 * x, 4))[0]
    print(f'(harness) painted pixels (format {SCREEN_FORMAT:#x}): corner {px(1, 1):#010x}, between frame and centre {px(w // 4, w // 4):#010x}, centre {px(w // 2, w // 2):#010x}')
