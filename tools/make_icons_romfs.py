"""Build the mod's map icon layout: a copy of the game's UI/Layout2Archive/Common...blarc.zs in which the
Skulltula/heart map icon (layout PaMapIconCollect_00) shows the plain heart and Skulltula pictures instead of the
checked ones (the vanilla map only shows what you've collected; the mod shows what's missing, and fades what's
collected).

The layout picks its picture from a two-entry list of texture names by the icon's "Type" frame (0 heart, 1 Skulltula).
The names are rewritten in the string pool where the old picture names were; nothing moves and no size changes: the
archive keeps its exact decompressed size (the game budgets memory per file) and is recompressed with the game's own
zstd dictionary (zs.zsdic, id 1).

(Tried and dropped: growing the list to four pictures for Big Poes and Silver Rupees. The game only ever uses the
first two entries of the list, whatever the list's count, key frames or animation length say - tested in-game with
six variants. Big Poes and Silver Rupees use the facility icons instead: see make_icons_bntx.py and icons.cpp.)

usage: make_icons_romfs.py <original blarc.zs> <ZsDic.pack.zs> <output blarc.zs>"""
import struct, sys, zstandard, oead

SRC, DICPACK, OUT = sys.argv[1:4]
LAYOUT = b'PaMapIconCollect_00.buil'
PICTURES = [b'MapIconCollectHeartPiece_00', b'MapIconCollectSkullSign_00']
POOL_FIRST = b'MapIconCollectHeartPieceCheckSh_00'       # where the old picture names start in the string pool
POOL_NEXT = b'FacilityIconNoise_00'                      # the string after them (kept)
GLOW_LIST, ICON_LIST = 0x580, 0x608                      # list records: {u64 offset of names, u32 count, u32 stride}

dicts = {f.name: bytes(f.data) for f in oead.Sarc(zstandard.ZstdDecompressor().decompressobj().decompress(open(DICPACK, 'rb').read())).get_files()}
zdict = zstandard.ZstdCompressionDict(dicts['zs.zsdic'])
raw = open(SRC, 'rb').read()
assert zstandard.get_frame_parameters(raw).dict_id == zdict.dict_id() == 1
sarc = bytearray(zstandard.ZstdDecompressor(dict_data=zdict).decompressobj().decompress(raw))
assert sarc[:4] == b'SARC'

def patch(lyt):
    assert lyt[:4] == b'BFFH' and lyt[0x28:0x4c] == b'Nintendo.Ui.ResModels.LayoutResData\0'
    q = lambda o: struct.unpack_from('<Q', lyt, o)[0]
    glow_names, icon_names = q(GLOW_LIST), q(ICON_LIST)
    assert struct.unpack_from('<II', lyt, GLOW_LIST + 8) == (2, 8) and struct.unpack_from('<II', lyt, ICON_LIST + 8) == (2, 8)
    start = lyt.find(POOL_FIRST + b'\0'); end = lyt.find(POOL_NEXT + b'\0')
    assert start > 0 and end > start and q(glow_names) == start, (hex(start), hex(end))
    # the old names, and nothing else, point into [start, end)
    users = [o for o in range(0, start, 8) if start <= q(o) < end]
    lists = (glow_names, glow_names + 8, icon_names, icon_names + 8)
    assert all(o in users for o in lists)
    extra = [o for o in users if o not in lists]   # the panes' default pictures (materials): the plain heart
    names = {}
    lyt[start:end] = bytes(end - start)
    at = start
    for p in PICTURES:
        names[p] = at
        lyt[at:at + len(p)] = p
        at += len(p) + 1
    assert at <= end, 'names do not fit'
    for o in extra: lyt[o:o + 8] = struct.pack('<Q', names[PICTURES[0]])
    lyt[glow_names:glow_names + 16] = struct.pack('<QQ', names[PICTURES[0]], names[PICTURES[1]])
    lyt[icon_names:icon_names + 16] = struct.pack('<QQ', names[PICTURES[0]], names[PICTURES[1]])
    return extra

header_len = struct.unpack_from('<H', sarc, 4)[0]; data_start = struct.unpack_from('<I', sarc, 0xC)[0]
count = struct.unpack_from('<H', sarc, header_len + 6)[0]
names_at = header_len + 12 + 16 * count + 8
copies = 0
for i in range(count):
    _, attr, s, e = struct.unpack_from('<IIII', sarc, header_len + 12 + 16 * i)
    o = names_at + (attr & 0xFFFFFF) * 4
    if sarc[o:sarc.index(b'\0', o)] != LAYOUT: continue
    a, b = data_start + s, data_start + e
    lyt = bytearray(sarc[a:b])
    extra = patch(lyt)
    assert len(lyt) == b - a
    sarc[a:b] = lyt; copies += 1
    print(f'patched copy {copies} of {LAYOUT.decode()} ({b - a} bytes at {a:#x}); other name users {[hex(x) for x in extra]}')
assert copies >= 1
out = zstandard.ZstdCompressor(level=19, dict_data=zdict, write_content_size=True, write_checksum=False, write_dict_id=True).compress(bytes(sarc))
check = zstandard.ZstdDecompressor(dict_data=zdict).decompressobj().decompress(out)
assert check == bytes(sarc) and len(check) == len(zstandard.ZstdDecompressor(dict_data=zdict).decompressobj().decompress(raw))
open(OUT, 'wb').write(out)
print(f'wrote {OUT}: {len(out)} bytes compressed, {len(check)} decompressed (unchanged size)')
