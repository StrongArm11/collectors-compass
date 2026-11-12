"""Shared pieces of the emulation harnesses: load an NSO image, assemble a snippet, relocate a module like rtld."""
import struct, subprocess, tempfile

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

def relocate(uc, img, base, imports):
    mod0 = struct.unpack_from('<I', img, 4)[0]; dyn = mod0 + struct.unpack_from('<i', img, mod0 + 4)[0]
    tags = {}
    while True:
        t, v = struct.unpack_from('<qQ', img, dyn); dyn += 16
        if t == 0: break
        tags[t] = v
    strtab, symtab = tags[5], tags[6]
    for off in range(tags[7], tags[7] + tags[8], 24):
        r_off, info, add = struct.unpack_from('<QQq', img, off)
        if info & 0xffffffff == 1027: uc.mem_write(base + r_off, struct.pack('<Q', base + add)); continue
        nm = struct.unpack_from('<I', img, symtab + (info >> 32) * 24)[0]
        name = img[strtab + nm:img.index(b'\0', strtab + nm)].decode()
        uc.mem_write(base + r_off, struct.pack('<Q', imports.get(name, 0) + add if name in imports else 0))
    return tags

FS_NAMES = ['_ZN2nn2fs11MountSdCardEPKc', '_ZN2nn2fs8OpenFileEPNS0_10FileHandleEPKci', '_ZN2nn2fs8ReadFileENS0_10FileHandleElPvm',
            '_ZN2nn2fs9WriteFileENS0_10FileHandleElPKvmRKNS0_11WriteOptionE', '_ZN2nn2fs11GetFileSizeEPlNS0_10FileHandleE',
            '_ZN2nn2fs11SetFileSizeENS0_10FileHandleEl', '_ZN2nn2fs9CloseFileENS0_10FileHandleE', '_ZN2nn2fs10CreateFileEPKcl',
            '_ZN2nn2fs15CreateDirectoryEPKc', '_ZN2nn2fs19SetEnabledAutoAbortEb']

class FakeFs:
    """nn::fs for the harnesses: files in a dict (path -> bytes), with Horizon-like results."""
    PATH_NOT_FOUND, ALREADY_EXISTS = 0x202, 0x402
    def __init__(self, uc, at, files=None):
        from unicorn import UC_HOOK_CODE
        from unicorn import arm64_const as A
        self.uc, self.A, self.files, self.handles, self.calls, self.mounts, self.abort = uc, A, dict(files or {}), {}, [], set(), [True]
        self.addr = {n: at + 16 * i for i, n in enumerate(FS_NAMES)}
        self.byaddr = {a: n for n, a in self.addr.items()}
        uc.hook_add(UC_HOOK_CODE, self.on_code, begin=at, end=at + 16 * len(FS_NAMES))
    def imports(self): return dict(self.addr)
    def reg(self, i): return self.uc.reg_read(getattr(self.A, f'UC_ARM64_REG_X{i}'))
    def cstr(self, a): b = bytes(self.uc.mem_read(a, 128)); return b[:b.index(0)].decode()
    def on_code(self, uc, addr, size, data):
        n = self.byaddr.get(addr)
        if n is None: return
        x = [self.reg(i) for i in range(6)]
        rc = 0
        short = n.split('2fs')[1]
        if 'MountSdCard' in n: self.mounts.add(self.cstr(x[0]))
        elif 'OpenFile' in n:
            path = self.cstr(x[1])
            if path not in self.files: rc = self.PATH_NOT_FOUND
            else:
                h = 0x1000 + len(self.handles); self.handles[h] = path; uc.mem_write(x[0], h.to_bytes(8, 'little'))
        elif 'ReadFile' in n: uc.mem_write(x[2], self.files[self.handles[x[0]]][x[1]:x[1] + x[3]])
        elif 'WriteFile' in n:
            path = self.handles[x[0]]; d = bytearray(self.files[path]); data = bytes(uc.mem_read(x[2], x[3]))
            d[x[1]:x[1] + len(data)] = data; self.files[path] = bytes(d)
        elif 'GetFileSize' in n: uc.mem_write(x[0], len(self.files[self.handles[x[1]]]).to_bytes(8, 'little'))
        elif 'SetFileSize' in n:
            path = self.handles[x[0]]; self.files[path] = (self.files[path] + bytes(x[1]))[:x[1]]
        elif 'CloseFile' in n: self.handles.pop(x[0], None)
        elif 'CreateFile' in n:
            path = self.cstr(x[0])
            if path in self.files: rc = self.ALREADY_EXISTS
            else: self.files[path] = bytes(x[1])
        elif 'CreateDirectory' in n: rc = 0
        elif 'SetEnabledAutoAbort' in n: self.abort.append(bool(x[0] & 0xFF))
        self.calls.append((short, rc))
        uc.reg_write(self.A.UC_ARM64_REG_X0, rc)
        uc.reg_write(self.A.UC_ARM64_REG_PC, uc.reg_read(self.A.UC_ARM64_REG_LR))
