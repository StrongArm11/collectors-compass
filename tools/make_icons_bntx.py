"""Put the mod's Silver Rupee map icon into the game's UI texture archive (UI/Layout2Archive/Common...bntx.zs).

The map's facility icons (shops, mini-games, ...; layout PaMapIconFacility_00) pick their picture from a fixed list of
31 textures by icon type. The mod shows Big Poes with the game's own Poe icon (MapIconGhostShop_00) and Silver Rupees
with this picture, painted over the facility picture MapIconTraining_00 (the Gerudo Training Ground sign, which only
appears once, on the adult Gerudo's Fortress map - it shows the silver rupee there too, fitting for that dungeon) and
its glow MapIconTrainingSh_00. Same sizes and formats (128x128 ASTC 4x4 sRGB and 128x128 BC4), so the archive keeps its
exact size; it is recompressed with plain zstd like the original.
usage: make_icons_bntx.py <original bntx.zs> <rupee png> <output bntx.zs>"""
import struct, sys
import numpy as np, zstandard, texture2ddecoder
import astc_encoder as A
from PIL import Image, ImageFilter

SRC, PNG, OUT = sys.argv[1:4]
PICTURE, GLOW = 'MapIconTraining_00', 'MapIconTrainingSh_00'

def textures(d):
    out, i = {}, 0
    while True:
        i = d.find(b'BRTI', i)
        if i < 0: return out
        u = lambda o, f: struct.unpack_from(f, d, i + o)[0]
        nameptr = u(0x60, '<Q')
        name = d[nameptr + 2:nameptr + 2 + struct.unpack_from('<H', d, nameptr)[0]].decode(errors='replace') if nameptr < len(d) else ''
        out[name] = dict(off=i, fmt=u(0x1C, '<I'), w=u(0x24, '<I'), h=u(0x28, '<I'), mips=u(0x16, '<H'), layout=u(0x34, '<I'),
                         size=u(0x50, '<I'), data=struct.unpack_from('<Q', d, u(0x70, '<Q'))[0])
        i += 4

def block_addresses(wb, hb, bpp, gobs):
    """Byte offset of each block's first byte in the block-linear (swizzled) layout (as the game's GPU stores it)."""
    xs = np.arange(wb)[None, :] * bpp
    ys = np.arange(hb)[:, None]
    xb = np.broadcast_to(xs, (hb, wb)); y = np.broadcast_to(ys, (hb, wb))
    wgob = -(-(wb * bpp) // 64)
    gob = (y // (8 * gobs)) * 512 * gobs * wgob + (xb // 64) * 512 * gobs + ((y % (8 * gobs)) // 8) * 512
    xx = xb % 64; yy = y % 8
    return gob + (yy % 2) * 16 + ((yy // 2) % 2) * 32 + ((xx % 32) // 16) * 64 + (yy // 4) * 128 + ((xx % 64) // 32) * 256 + (xx % 16)

def bc4_encode(img):
    """Plain BC4 (one channel, 4x4 blocks of 8 bytes): endpoints max/min of the block, 8-step palette."""
    h, w = img.shape
    out = bytearray()
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            b = img[by:by + 4, bx:bx + 4].astype(np.int32).reshape(-1)
            hi, lo = int(b.max()), int(b.min())
            if hi == lo:
                out += bytes([hi, lo]) + bytes(6)   # every index 0 -> hi
                continue
            pal = [hi, lo] + [((7 - k) * hi + k * lo) // 7 for k in range(1, 7)]   # hi > lo: 8-value mode
            idx = [int(np.argmin([abs(v - p) for p in pal])) for v in b]
            bits = 0
            for k, ix in enumerate(idx): bits |= ix << (3 * k)
            out += bytes([hi, lo]) + bits.to_bytes(6, 'little')
    return bytes(out)

def put(d, t, blocks_linear, wb, hb, bpp):
    addr = block_addresses(wb, hb, bpp, 1 << (t['layout'] & 7))
    assert addr.max() + bpp <= t['size']
    region = np.frombuffer(bytes(d[t['data']:t['data'] + t['size']]), dtype=np.uint8).copy()
    blocks = np.frombuffer(blocks_linear, dtype=np.uint8).reshape(hb * wb, bpp)
    for k, a in enumerate(addr.reshape(-1)): region[a:a + bpp] = blocks[k]
    d[t['data']:t['data'] + t['size']] = region.tobytes()
    return np.stack([region[a:a + bpp] for a in addr.reshape(-1)]).tobytes()   # read back

d = bytearray(zstandard.ZstdDecompressor().decompressobj().decompress(open(SRC, 'rb').read()))
size0 = len(d)
tex = textures(d)
im = Image.open(PNG).convert('RGBA')
assert im.size == (128, 128)

# the picture: ASTC 4x4 sRGB
t = tex[PICTURE]
assert t['fmt'] >> 8 == 0x2d and t['w'] == 128 and t['h'] == 128 and t['mips'] == 1, t
ctx = A.ASTCContext(A.ASTCConfig(A.ASTCProfile.LDR_SRGB, 4, 4, 1, A.ASTCQualityPreset.EXHAUSTIVE))
back = put(d, t, ctx.compress(A.ASTCImage(A.ASTCType.U8, 128, 128, 1, im.tobytes()), A.ASTCSwizzle.from_str('RGBA')), 32, 32, 16)
rgba = np.array(Image.frombytes('RGBA', (128, 128), texture2ddecoder.decode_astc(back, 128, 128, 4, 4), 'raw', 'BGRA')).astype(int)
err = np.abs(rgba - np.array(im).astype(int))[np.array(im)[..., 3] > 0].mean()
assert err < 3, err

# the glow: the picture's outline grown a little and softened, like the game's own (BC4, one channel)
t = tex[GLOW]
assert t['fmt'] >> 8 == 0x1d and t['w'] == 128 and t['h'] == 128 and t['mips'] == 1, t
alpha = im.getchannel('A').point(lambda v: 255 if v > 40 else 0)
glow = np.array(alpha.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.GaussianBlur(1.6)))
back = put(d, t, bc4_encode(glow), 32, 32, 8)
g = np.array(Image.frombytes('RGBA', (128, 128), texture2ddecoder.decode_bc4(back, 128, 128), 'raw', 'BGRA'))[..., :3].max(-1)
gerr = np.abs(g.astype(int) - glow.astype(int)).mean()
assert gerr < 4, gerr

assert len(d) == size0
out = zstandard.ZstdCompressor(level=6, write_content_size=True, write_checksum=False, threads=-1).compress(bytes(d))
assert zstandard.get_frame_parameters(out).content_size == size0
open(OUT, 'wb').write(out)
print(f'{PICTURE} and {GLOW} repainted (round-trip error {err:.2f} / {gerr:.2f}); wrote {OUT} '
      f'({len(out)} bytes compressed, {size0} decompressed, unchanged)')
