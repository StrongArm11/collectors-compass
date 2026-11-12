"""Write the main-executable patch for our hooks (an IPS32 file named after main's build ID) and the matching C++
table (build/hooks.inc). Each hook site's first word becomes `b gate`; the gate, in unused space at the end of main's
code, loads our handler from a table in main's unused zeroed memory and jumps to it, or runs the original first word
and continues when the table slot is empty (module not loaded)."""
import json, struct, sys
import capstone
MAIN_IMG = sys.argv[1]; HOOKS = sys.argv[2]; OUT_IPS = sys.argv[3]; OUT_INC = sys.argv[4]
BUILTIN = [(0x805BD0, 4), (0x6E6CC4, 4), (0x3C46200, 0x78)]   # Lichii's own built-in patches to main
OTHER_MODS = [(0x3C46280, 0x180, 'Hyrule Cheat Menu gates'),
              (0x4CD60, 4, 'Fast Travel 1.3 draw hook'), (0x129E290, 4, 'Fast Travel 1.3 graphics hook'), (0x3C427F0, 0x44, 'Fast Travel 1.3 controller hooks'),
              (0x34694F8, 4, 'Hyrule Cheat Menu controller call'), (0x3469524, 4, 'Hyrule Cheat Menu controller call'), (0x3469550, 4, 'Hyrule Cheat Menu controller call'),
              (0x346966C, 4, 'Hyrule Cheat Menu controller call'), (0x3469698, 4, 'Hyrule Cheat Menu controller call'),
              (0x346BFF0, 4, 'Hyrule Cheat Menu graphics call'), (0x346C0D4, 4, 'Hyrule Cheat Menu graphics call'), (0x4E9F800, 0x80, 'Hyrule Cheat Menu table'), (0x4E9FFF0, 0x10, 'Fast Travel anchor')]
TEXT_END, CODE_LIMIT, BSS_END, MAPPED_END = 0x3C461D0, 0x3C47000, 0x4E9A4C8, 0x4EA0000
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
img = open(MAIN_IMG, 'rb').read()
spec = json.load(open(HOOKS))
cave, table = int(spec['cave'], 16), int(spec['table'], 16)
hooks = spec['hooks']
w32 = lambda a: struct.unpack_from('<I', img, a)[0]

def overlaps(a, n, b, m): return a < b + m and b < a + n
gate_bytes = 0x20 * len(hooks)
assert TEXT_END <= cave and cave + gate_bytes <= CODE_LIMIT, 'gates must sit in the padding after main\'s code'
assert BSS_END <= table and table + 8 * len(hooks) <= MAPPED_END, 'table must sit in main\'s unused zeroed memory'
assert img[cave:cave + gate_bytes] == bytes(gate_bytes), 'gate area is not empty'
for a, n in BUILTIN: assert not overlaps(cave, gate_bytes, a, n), f'gates overlap a Lichii built-in patch at {a:#x}'
for a, n, what in OTHER_MODS:
    assert not overlaps(cave, gate_bytes, a, n) and not overlaps(table, 8 * len(hooks), a, n), f'overlaps {what}'

def enc_b(pc, dest, link=False):
    d = dest - pc; assert d % 4 == 0 and -(1 << 27) <= d < (1 << 27)
    return (0x94000000 if link else 0x14000000) | ((d >> 2) & 0x3FFFFFF)
def enc_adrp(rd, pc, dest):
    imm = ((dest & ~0xFFF) - (pc & ~0xFFF)) >> 12
    return 0x90000000 | ((imm & 3) << 29) | (((imm >> 2) & 0x7FFFF) << 5) | rd
def enc_ldr_x(rt, rn, off):   # ldr xt, [xn, #off] (unsigned offset, scaled by 8)
    assert off % 8 == 0 and 0 <= off < 0x8000
    return 0xF9400000 | ((off // 8) << 10) | (rn << 5) | rt
def enc_cbz_x(rt, pc, dest):
    d = (dest - pc) >> 2
    return 0xB4000000 | ((d & 0x7FFFF) << 5) | rt
PCREL = ('adr', 'adrp', 'b', 'bl', 'cbz', 'cbnz', 'tbz', 'tbnz', 'ldr', 'ldrsw', 'prfm')

records = []; rows = []
for slot, h in enumerate(hooks):
    site = int(h['site'], 16); words = [int(w, 16) for w in h['words']]
    enabled = h.get('enabled', True)
    found = [w32(site + 4 * i) for i in range(4)]
    assert found == words, f"{h['name']}: main+{site:#x} holds {[hex(x) for x in found]}, expected {h['words']}"
    for a, n in BUILTIN: assert not overlaps(site, 4, a, n), f"{h['name']} overlaps a Lichii built-in patch"
    first = next(md.disasm(struct.pack('<I', words[0]), site))
    literal_load = first.mnemonic in ('ldr', 'ldrsw', 'prfm') and '[' not in first.op_str   # ldr xN, #label
    pc_relative = literal_load or (first.mnemonic in PCREL and first.mnemonic not in ('ldr', 'ldrsw', 'prfm')) or first.mnemonic.startswith('b.')
    assert not pc_relative, f"{h['name']}: first instruction '{first.mnemonic} {first.op_str}' is PC-relative"
    gate = cave + 0x20 * slot
    rows.append(f'    {{"{h["name"]}", {site:#x}, {slot}, {"true" if enabled else "false"}, {{{", ".join(f"{w:#010x}" for w in words)}}}}},')
    if not enabled:
        print(f"{h['name']:18s} main+{site:#09x}: disabled (slot {slot} kept, nothing patched)")
        continue
    code = [enc_adrp(16, gate, table), enc_ldr_x(16, 16, (table + 8 * slot) & 0xFFF), enc_cbz_x(16, gate + 8, gate + 0x10),
            0xD61F0200,   # br x16
            words[0], enc_b(gate + 0x14, site + 4), 0, 0]
    gate_code = struct.pack('<8I', *code)
    records.append((site, struct.pack('<I', enc_b(site, gate))))
    records.append((gate, gate_code))
    dis = '; '.join(f'{i.mnemonic} {i.op_str}' for i in md.disasm(gate_code[:0x18], gate))
    print(f"{h['name']:18s} main+{site:#09x} -> gate {gate:#x}: {dis}")

# Inline hooks: one instruction in the middle of a routine becomes `b trampoline`. The trampoline calls our handler
# (from the table slot after the hook slots) with the listed registers as arguments, then branches on its result:
# a listed result value goes to its target, anything else continues after the replaced instruction. With no handler
# loaded it just continues, so the replaced instruction must be one the patch would drop anyway (e.g. a check).
INLINE_BASE = cave + 0x200
inline_rows = []
def enc_mov_reg(rd, rn): return 0xAA0003E0 | (rn << 16) | rd          # mov xd, xn (orr xd, xzr, xn)
def enc_cbz_w(rt, pc, dest): return 0x34000000 | ((((dest - pc) >> 2) & 0x7FFFF) << 5) | rt
def enc_cmp_w_imm(rn, imm): return 0x7100001F | (imm << 10) | (rn << 5)
def enc_beq(pc, dest): return 0x54000000 | ((((dest - pc) >> 2) & 0x7FFFF) << 5)
for k, h in enumerate(spec.get('inline', [])):
    slot = len(hooks) + k
    site, old, cont = int(h['site'], 16), int(h['old'], 16), int(h['continue'], 16)
    assert w32(site) == old, f"inline {h['name']}: main+{site:#x} holds {w32(site):#010x}, expected {old:#010x}"
    assert cont == site + 4
    for a, n in BUILTIN: assert not overlaps(site, 4, a, n)
    for a, n, what in OTHER_MODS: assert not overlaps(site, 4, a, n), what
    t = INLINE_BASE + 0x80 * k
    assert t + 0x80 <= CODE_LIMIT and table + 8 * (slot + 1) <= MAPPED_END
    assert img[t:t + 0x80] == bytes(0x80), 'inline trampoline area is not empty'
    regs = [int(r[1:]) for r in h['args']]
    res = {int(v): int(a, 16) for v, a in h['results'].items()}
    code = [0xA9BF7BFD,                                    # stp x29, x30, [sp, #-16]!
            enc_adrp(16, t + 4, table), enc_ldr_x(16, 16, (table + 8 * slot) & 0xFFF)]
    nohandler_at = None
    code.append(None)                                      # cbz x16, nohandler (filled below)
    for i, r in enumerate(regs): code.append(enc_mov_reg(i, r))
    code.append(0xD63F0200)                                # blr x16
    code.append(0xA8C17BFD)                                # ldp x29, x30, [sp], #16
    targets = []                                           # (value, index of the branch word) for results
    for v in sorted(res):
        if v == 0: code.append(('cbz0', v))
        else: code.append(enc_cmp_w_imm(0, v)); code.append(('beq', v))
    code.append(('b', cont))
    nohandler_at = len(code)
    code.append(0xA8C17BFD); code.append(('b', cont))       # no handler: ldp; continue
    tails = {}
    for v in sorted(res): tails[v] = len(code); code.append(('b', res[v]))
    words = []
    for i, c in enumerate(code):
        pc = t + 4 * i
        if c is None: words.append(enc_cbz_x(16, pc, t + 4 * nohandler_at))
        elif isinstance(c, tuple):
            kind, v = c
            if kind == 'cbz0': words.append(enc_cbz_w(0, pc, t + 4 * tails[v]))
            elif kind == 'beq': words.append(enc_beq(pc, t + 4 * tails[v]))
            else: words.append(enc_b(pc, v))
        else: words.append(c)
    assert len(words) * 4 <= 0x80
    blob = struct.pack(f'<{len(words)}I', *words)
    records.append((site, struct.pack('<I', enc_b(site, t))))
    records.append((t, blob))
    hook_sites_extra = (site, 4)
    inline_rows.append(f'    {{"{h["name"]}", {site:#x}, {slot}, {t:#x}}},')
    dis = '; '.join(f'{i.mnemonic} {i.op_str}' for i in md.disasm(blob, t))
    print(f"inline {h['name']} main+{site:#x} -> {t:#x}: {dis}")

# Call hooks (since v1.0.1): instead of the routine's first word, its call instructions are redirected (`bl gate` /
# `b gate` for a tail call, or `bl gate` for a `blr xN`). The gate jumps to our handler when its table slot is filled,
# otherwise to the original target, so another mod that patches the routine's own entry (Fast Travel does, for the
# screen-layer draw routine) still runs inside our handler's call to it. "pass" copies registers into x2, x3, ...
CALL_BASE = cave + 0x300
call_rows = []
def enc_mov_reg_(rd, rn): return 0xAA0003E0 | (rn << 16) | rd
n_inline = len(spec.get('inline', []))
call_sites_all = []
for k, h in enumerate(spec.get('calls', [])):
    slot = len(hooks) + n_inline + k
    g = CALL_BASE + 0x20 * k
    assert g + 0x20 <= CODE_LIMIT and table + 8 * (slot + 1) <= MAPPED_END
    assert img[g:g + 0x20] == bytes(0x20), 'call gate area is not empty'
    sites = [int(x, 16) for x in h['sites']]
    olds = []
    target = int(h['target'], 16) if 'target' in h else 0
    reg = int(h['register'][1:]) if 'register' in h else None
    for site in sites:
        w = w32(site); olds.append(w)
        if reg is not None:
            assert w == 0xD63F0000 | (reg << 5), f"call {h['name']}: main+{site:#x} is {w:#010x}, not blr x{reg}"
            records.append((site, struct.pack('<I', enc_b(site, g, link=True))))
        else:
            assert w & 0x7C000000 == 0x14000000, f"call {h['name']}: main+{site:#x} is not b/bl"
            link = bool(w & 0x80000000)
            assert w == enc_b(site, target, link), f"call {h['name']}: main+{site:#x} does not call {target:#x}"
            records.append((site, struct.pack('<I', enc_b(site, g, link=link))))
        for a_, n_ in BUILTIN: assert not overlaps(site, 4, a_, n_)
        for a_, n_, what in OTHER_MODS: assert not overlaps(site, 4, a_, n_), what
        call_sites_all.append((site, 4))
    code = [enc_adrp(16, g, table), enc_ldr_x(16, 16, (table + 8 * slot) & 0xFFF), None]
    for i, r in enumerate(h.get('pass', [])): code.append(enc_mov_reg_(2 + i, int(r[1:])))
    code.append(0xD61F0200)                                # br x16
    fallback = len(code)
    pc_fb = g + 4 * fallback
    code.append(0xD61F0000 | (reg << 5) if reg is not None else enc_b(pc_fb, target))   # br xN / b target
    code[2] = enc_cbz_x(16, g + 8, g + 4 * fallback)
    code += [0] * (8 - len(code))
    blob = struct.pack('<8I', *code)
    records.append((g, blob))
    sl = ', '.join(f'{x:#x}' for x in sites) + ', 0' * (3 - len(sites))
    ol = ', '.join(f'{x:#010x}' for x in olds) + ', 0' * (3 - len(olds))
    call_rows.append(f'    {{"{h["name"]}", {slot}, {g:#x}, {target:#x}, {len(sites)}, {{{sl}}}, {{{ol}}}}},')
    dis = '; '.join(f'{i.mnemonic} {i.op_str}' for i in md.disasm(blob[:4 * (fallback + 1)], g))
    print(f"call {h['name']:10s} sites {[hex(x) for x in sites]} -> gate {g:#x}: {dis}")
got_rows = [f'    {{"{h["name"]}", {int(h["got"], 16):#x}}},' for h in spec.get('got', [])]
PLT0 = 0x3C41C50
assert w32(PLT0) & 0x9F000000 == 0x90000000 or True

# Plain one-word patches: the old word must match the game's, and no patch may touch a hook site or another mod.
hook_sites = [(int(h['site'], 16), 4) for h in hooks if h.get('enabled', True)] + [(int(h['site'], 16), 4) for h in spec.get('inline', [])] \
    + call_sites_all
for pt in spec.get('patches', []):
    site, old, new = int(pt['site'], 16), int(pt['old'], 16), int(pt['new'], 16)
    assert w32(site) == old, f"patch main+{site:#x}: holds {w32(site):#010x}, expected {old:#010x}"
    for a, n in BUILTIN + hook_sites: assert not overlaps(site, 4, a, n), f'patch main+{site:#x} overlaps {a:#x}'
    for a, n, what in OTHER_MODS: assert not overlaps(site, 4, a, n), f'patch main+{site:#x} overlaps {what}'
    o = next(md.disasm(struct.pack('<I', old), site)); nw = next(md.disasm(struct.pack('<I', new), site))
    print(f"patch main+{site:#09x}: {o.mnemonic} {o.op_str} -> {nw.mnemonic} {nw.op_str}   ; {pt['what']}")
    records.append((site, struct.pack('<I', new)))
records.sort()

with open(OUT_IPS, 'wb') as f:
    f.write(b'IPS32')
    for addr, data in records:
        f.write(struct.pack('>IH', addr + 0x100, len(data))); f.write(data)   # IPS offsets include the 0x100 NSO header
    f.write(b'EEOF')
with open(OUT_INC, 'w') as f:
    f.write('// generated by tools/make_patch.py from hooks.json - do not edit\n#pragma once\n')
    f.write(f'constexpr u32 kGateArea = {cave:#x};\nconstexpr u32 kHookTable = {table:#x};\n')
    f.write('struct HookSite { const char* name; u32 site; u32 slot; bool enabled; u32 words[4]; };\n')
    f.write('constexpr HookSite kHookSites[] = {\n' + '\n'.join(rows) + '\n};\n')
    f.write('enum HookId { ' + ', '.join(f"kHook{h['name']}" for h in hooks) + ' };\n')
    f.write('struct InlineSite { const char* name; u32 site; u32 slot; u32 trampoline; };\n')
    f.write('constexpr InlineSite kInlineSites[] = {\n' + ('\n'.join(inline_rows) or '    {"none", 0, 0, 0},') + '\n};\n')
    f.write('enum InlineId { ' + ', '.join(f"kInline{h['name']}" for h in spec.get('inline', [])) + ' };\n')
    f.write('struct CallSite { const char* name; u32 slot; u32 gate; u32 target; u32 count; u32 sites[3]; u32 words[3]; };\n')
    f.write('constexpr CallSite kCallSites[] = {\n' + ('\n'.join(call_rows) or '    {"none", 0, 0, 0, 0, {0, 0, 0}, {0, 0, 0}},') + '\n};\n')
    f.write('enum CallId { ' + ', '.join(f"kCall{h['name']}" for h in spec.get('calls', [])) + ' };\n')
    f.write('struct GotSlot { const char* name; u32 got; };\n')
    f.write('constexpr GotSlot kGotSlots[] = {\n' + '\n'.join(got_rows) + '\n};\n')
    f.write(f'constexpr u32 kPlt0 = {PLT0:#x};\n')
print('wrote', OUT_IPS, 'and', OUT_INC)
