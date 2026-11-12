"""Collectible table for the in-game mod (build/items.inc), from the tracker's data (tracker_data.json, cats.json).

Each map point gets its area (banc), age, position, what it needs (age, night, tool groups: any flag in a group
counts) and a short description for hints. Gold Skulltulas also carry the save key the game's map icons use, so an
icon is matched by key; hearts are matched by area and position.
usage: gen_mod_items.py <data dir> <output .inc>"""
import json, os, sys
SP, OUT = sys.argv[1], sys.argv[2]
sys.path.insert(0, SP)
from names import murmur3

T = json.load(open(os.path.join(SP, 'tracker_data.json')))
C = json.load(open(os.path.join(SP, 'cats.json')))
CATS = {c['id']: c for c in C['cats']}
h32 = lambda n: murmur3(n.encode())
ITEMFLAG = {   # tool -> IsGet flags (any)   (same table as the overlay's gen_items.py)
    'Hookshot': ['IsGet.Item_Hookshot', 'IsGet.Item_Longshot'], 'Longshot': ['IsGet.Item_Longshot'],
    'Boomerang': ['IsGet.Item_Boomerang'], 'Bombs': ['IsGet.Item_BombBag_Small'], 'Slingshot': ['IsGet.Weapon_Range_SlingShot'],
    'Megaton Hammer': ['IsGet.Item_Hammer'], 'Bolero of Fire': ['IsGet.Item_Melody_FireBolero'],
    'Requiem of Spirit': ['IsGet.Item_Melody_SpiritRequiem'], 'Song of Storms': ['IsGet.Item_Melody_StormSong'],
    'Song of Time': ['IsGet.Item_Melody_TimeSong'], "Scarecrow's Song": ['IsGet.Item_Melody_ScarecrowSong'],
    "Zelda's Lullaby": ['IsGet.Item_Melody_ZeldaLullaby'], "Saria's Song": ['IsGet.Item_Melody_SariaSong'],
    "Sun's Song": ['IsGet.Item_Melody_SunSong'], 'Silver Gauntlets': ['IsGet.Armor_Adult_Gauntlet_Silver'],
    'Lens of Truth': ['IsGet.Armor_GlassTruth'], 'Iron Boots': ['IsGet.Armor_Adult_HeavyBoot'],
    'Fire Arrows': ['IsGet.Item_Arrow_Fire'], 'Fairy Bow': ['IsGet.Weapon_Range_Bow'],
    'Ocarina': ['IsGet.Item_Ocarina_Fairy', 'IsGet.Item_Ocarina_Time'], 'Deku Stick': ['IsGet.Item_DekuStick'],
    'Magic Bean': ['IsGet.Item_MagicBean'],
}
ALWAYS = {'Cucco', 'Small Key', 'Boss Key', 'Sword', 'Baby Dodongo blast', 'Bomb Flower'}
BOTTLE_HS = CATS['it']['rows'][[r['desc'] for r in CATS['it']['rows']].index('Bottle 1')]['det']['hs']
AGE = {'Child': 1, 'Adult': 2, 'Either': 0, 'Child then Adult': 3, 'Child (plant) / Adult (ride)': 4}

def groups_of(tools):
    out = []
    for grp in (tools or '').split(';'):
        grp = grp.strip()
        if not grp: continue
        alts = [a.strip() for a in grp.split('|')]
        if any(a in ALWAYS for a in alts): continue
        hs = []
        for a in alts:
            if a.startswith('Bottle'): hs += BOTTLE_HS
            elif a in ITEMFLAG: hs += [h32(n) for n in ITEMFLAG[a]]
            else: raise SystemExit('unknown tool ' + a)
        out.append((grp.replace('|', ' or '), sorted(set(hs))))
    return out

def split_key(mapkey):
    parts = mapkey.split('_'); age = 0
    if 'Child' in parts[1:]: age = 1
    if 'Adult' in parts[1:]: age = 2
    return parts[0], age

def ascii(s):
    s = (s or '').replace('’', "'").replace('‘', "'").replace('“', '"').replace('”', '"').replace('–', '-').replace('—', '-').replace('é', 'e').replace('…', '...')
    return s.encode('ascii', 'replace').decode()
def cstr(s): return '"' + ascii(s).replace('\\', '\\\\').replace('"', '\\"').replace('{B}', '\\x84" "').replace('\n', '\\n') + '"'
AGE_TEXT = {'Child': 'Child', 'Adult': 'Adult', 'Either': '', 'Child then Adult': 'Child, then adult',
            'Child (plant) / Adult (ride)': 'Plant as child, ride as adult'}
def need_text(age, time, tools):
    parts = [AGE_TEXT.get(age or '', age or '')]
    if 'Night' in (time or ''): parts.append('Night')
    parts += [t.strip().replace('|', ' or ') for t in (tools or '').split(';') if t.strip()]
    return ' {B} '.join(p for p in parts if p)   # {B}: the font's bullet (byte 0x84)

items = []
for r in T['rows']:                                  # Gold Skulltulas (cat 0)
    for k in r['keys']:
        g = T['gold'].get(k)
        if not g: continue
        for mk, x, y, z in g['maps']:
            banc, age = split_key(mk)
            items.append(dict(cat=0, id=r['id'], banc=banc, age=age, x=x, y=y, z=z, reqAge=AGE[r['age']],
                              night=1 if r['time'] == 'Night' else 0, groups=groups_of(r['tools']), key=int(k, 16), desc=r['desc'],
                              op=2, val=int(k, 16), area=r['area'], need=need_text(r['age'], r['time'], r['tools'])))
for r in CATS['hp']['rows']:                         # Pieces of Heart (cat 1)
    seen = set()
    for mk, x, y, z in r.get('pts') or []:
        banc, age = split_key(mk); sk = (banc, age, round(x, 1), round(z, 1))
        if sk in seen: continue
        seen.add(sk)
        det = r['det']; op, val = {'f': (1, det.get('h', 0)), 'k': (2, int(str(det.get('h', '0')), 16) if det['t'] == 'k' else 0)}.get(det['t'], (5, 0))
        items.append(dict(cat=1, id=r['id'], banc=banc, age=age, x=x, y=y, z=z, reqAge=AGE.get(r['age'], 0),
                          night=1 if 'Night' in (r.get('time') or '') else 0, groups=groups_of(r.get('tools')), key=0, desc=r['desc'],
                          op=op, val=val, area=r.get('area', ''), need=need_text(r.get('age'), r.get('time'), r.get('tools'))))

MP = json.load(open(os.path.join(SP, 'mapparam.json')))
def floor_index(banc, name):
    """The game's floor number for a dungeon floor name, as the map uses it (seen in-game): B2 -2, B1 -1, 1F 0, 2F 1."""
    return -int(name[1:]) if name.startswith('B') else int(name[:-1]) - 1
NO_FLOOR = -128
for r in CATS['poe']['rows']:                        # Big Poes (cat 2): the defeat flag
    for mk, x, y, z in r.get('pts') or []:
        banc, age = split_key(mk)
        items.append(dict(cat=2, id=r['id'], banc=banc, age=age, x=x, y=y, z=z, reqAge=AGE.get(r['age'], 0), night=0,
                          groups=groups_of(r.get('tools')), key=0, desc=r['desc'], op=1, val=r['det']['h'], area=r['area'],
                          need=need_text(r.get('age'), r.get('time'), r.get('tools')), floor=NO_FLOOR, toks=[], notes=r.get('notes', '')))
for r in CATS['sr']['rows']:                         # Silver Rupee rooms (cat 3): all five rupees' save keys
    for mk, x, y, z in r.get('pts') or []:
        banc, age = split_key(mk)
        fl = (r.get('fl') or {}).get(mk) or []
        items.append(dict(cat=3, id=r['id'], banc=banc, age=age, x=x, y=y, z=z, reqAge=AGE.get(r['age'], 0), night=0,
                          groups=groups_of(r.get('tools')), key=0, desc=r['desc'], op=3, val=0, area=r['area'],
                          need=need_text(r.get('age'), r.get('time'), r.get('tools')),
                          floor=floor_index(banc, fl[0]) if fl and 'FloorNum' in MP.get(banc, {}) else NO_FLOOR,
                          toks=[int(h, 16) for h in r['det']['hs']], notes=r.get('notes', '')))
STEPS = json.load(open(os.path.join(SP, 'mod', 'data', 'steps.json'))) if os.path.exists(os.path.join(SP, 'mod', 'data', 'steps.json')) else {}
missing_steps = sorted(set(i['id'] for i in items if i['id'] not in STEPS))

bancs = sorted(set(i['banc'] for i in items))
flags, gdefs, rows, toks = [], [], [], []
for it in items:
    g0 = len(gdefs)
    for name, hs in it['groups']:
        gdefs.append((name, len(flags), len(hs))); flags += hs
    t0 = len(toks); toks += it.get('toks', [])
    steps = STEPS.get(it['id'], [])
    rows.append('    {%d, %d, %d, %d, %d, %d, %d, 0x%016xULL, %d, 0x%016xULL, %.2ff, %.2ff, %.2ff, %s, %s, %s, %s, %d, %d, %d, %s},' % (
        it['cat'], bancs.index(it['banc']), it['age'], it['reqAge'], it['night'], g0, len(it['groups']), it['key'],
        it['op'], it['val'], it['x'], it['y'], it['z'], cstr(it['id']), cstr(it['desc']), cstr(it['area']), cstr(it['need']),
        it.get('floor', NO_FLOOR), t0, len(it.get('toks', [])), cstr('\n'.join(steps))))
with open(OUT, 'w') as f:
    f.write('// generated by tools/gen_mod_items.py from the tracker data - do not edit\n#pragma once\n')
    f.write('namespace mm::data {\n')
    f.write('constexpr const char* kBancs[] = {%s};\n' % ', '.join(cstr(b) for b in bancs))
    f.write('constexpr int kBancCount = %d;\n' % len(bancs))
    f.write('constexpr u32 kReqFlags[] = {%s};\n' % ', '.join('0x%08x' % h for h in flags))
    f.write('struct ReqGroup { const char* name; u16 first, count; };\n')
    f.write('constexpr ReqGroup kReqGroups[] = {%s};\n' % ', '.join('{%s, %d, %d}' % (cstr(n), a, c) for n, a, c in gdefs))
    f.write('// cat: 0 Gold Skulltula, 1 Piece of Heart; age: 0 any, 1 child map, 2 adult map;\n'
            '// reqAge: 0 either, 1 child, 2 adult, 3 child then adult, 4 plant as child / ride as adult\n'
            '// collected when: op 1 the flag with hash val is set, op 2 the save key val is set, op 5 unknown (chest)\n')
    f.write('constexpr u64 kTokenKeys[] = {%s};\n' % (', '.join('0x%016xULL' % t for t in toks) or '0'))
    f.write('// cat 2 Big Poe, 3 Silver Rupee room; op 3: collected when all the token save keys (t0, tn) are set;\n'
            '// floor: the dungeon floor number the map uses (B1 -1, 1F 0; -128: none); steps: how to get it, one step per line\n')
    f.write('struct Item { u8 cat, banc, age, reqAge, night; u16 g0; u8 gn; u64 key; u8 op; u64 val; float x, y, z; const char* id; const char* desc;\n'
            '              const char* area; const char* need; s8 floor; u16 t0; u8 tn; const char* steps; };   // need: what it takes, e.g. "Adult (bullet) Night (bullet) Hookshot"\n')
    f.write('constexpr Item kItems[] = {\n' + '\n'.join(rows) + '\n};\n')
    f.write('constexpr int kItemCount = %d;\n' % len(items))
    f.write('}  // namespace mm::data\n')
from collections import Counter
print('steps missing for', len(missing_steps), missing_steps[:8]); print('items', len(items), dict(Counter(i['cat'] for i in items)), 'bancs', len(bancs), 'groups', len(gdefs), 'flags', len(flags))
