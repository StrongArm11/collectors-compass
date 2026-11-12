"""Convert the linked module (ELF, laid out by link.ld) into an NSO the game's loader accepts.
Segments are stored uncompressed with SHA-256 hashes, like the Hyrule Cheat Menu's working subsdk8."""
import hashlib, struct, sys
from elftools.elf.elffile import ELFFile

def main(src, dst):
    with open(src, 'rb') as f:
        elf = ELFFile(f)
        loads = [s for s in elf.iter_segments() if s['p_type'] == 'PT_LOAD']
        if len(loads) != 3: raise SystemExit(f'expected 3 PT_LOAD segments (text, rodata, data), got {len(loads)}')
        text, ro, data = sorted(loads, key=lambda s: s['p_vaddr'])
        for seg, want in ((text, 5), (ro, 4), (data, 6)):
            if seg['p_flags'] != want: raise SystemExit(f"segment at {seg['p_vaddr']:#x} has flags {seg['p_flags']}, expected {want}")
            if seg['p_vaddr'] & 0xFFF: raise SystemExit(f"segment at {seg['p_vaddr']:#x} is not page aligned")
        build_id = b''
        note = elf.get_section_by_name('.note.gnu.build-id')
        if note is not None:
            for n in note.iter_notes():
                if n['n_type'] == 'NT_GNU_BUILD_ID': build_id = bytes.fromhex(n['n_desc'])
        segs = [(s['p_vaddr'], s.data()) for s in (text, ro, data)]
        bss = data['p_memsz'] - data['p_filesz']
    if text[0] if False else segs[0][0] != 0: raise SystemExit('text must start at 0')
    hdr = bytearray(0x100)
    hdr[0:4] = b'NSO0'
    struct.pack_into('<I', hdr, 0xC, 0x38)                  # check the three segment hashes
    off = 0x100
    for i, (vaddr, blob) in enumerate(segs):
        struct.pack_into('<III', hdr, 0x10 + i * 0x10, off, vaddr, len(blob))
        struct.pack_into('<I', hdr, 0x60 + i * 4, len(blob))
        hdr[0xA0 + i * 0x20:0xC0 + i * 0x20] = hashlib.sha256(blob).digest()
        off += len(blob)
    struct.pack_into('<I', hdr, 0x3C, bss)
    hdr[0x40:0x40 + len(build_id[:32])] = build_id[:32]
    with open(dst, 'wb') as f:
        f.write(hdr)
        for _, blob in segs: f.write(blob)
    print(f'{dst}: text {len(segs[0][1]):#x}, rodata {len(segs[1][1]):#x} at {segs[1][0]:#x}, data {len(segs[2][1]):#x} at {segs[2][0]:#x}, bss {bss:#x}, build id {build_id.hex()}')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
