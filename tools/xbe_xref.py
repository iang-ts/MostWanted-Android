#!/usr/bin/env python3
import os
import pickle
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "generated", "game")
XBE = os.path.join(ROOT, "assets", "default.xbe")
CACHE = os.path.join(ROOT, "out", "build", "xref.pickle")

FUNC = re.compile(r"^X86_FUNC\(__imp__sub_([0-9A-F]{8})\)")
INSN = re.compile(r"^\t// ([0-9A-F]{8}) (.*)$")
HEX = re.compile(r"0x([0-9A-Fa-f]+)")


class Image:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        u32 = lambda o: struct.unpack_from("<I", self.data, o)[0]
        self.base = u32(0x104)
        count, table = u32(0x11C), u32(0x120)
        self.sections = []
        for i in range(count):
            o = table - self.base + i * 56
            flags, va, vsize, raw, rsize, name = struct.unpack_from("<6I", self.data, o)
            n = self.data[name - self.base:self.data.index(b"\0", name - self.base)].decode("latin-1")
            self.sections.append((n, flags, va, vsize, raw, rsize))

    def offset(self, va):
        for _, _, sva, _, raw, rsize in self.sections:
            if sva <= va < sva + rsize:
                return raw + va - sva
        return None

    def u32(self, va):
        o = self.offset(va)
        return None if o is None or o + 4 > len(self.data) else struct.unpack_from("<I", self.data, o)[0]

    def cstr(self, va, limit=256):
        o = self.offset(va)
        if o is None:
            return None
        end = self.data.find(b"\0", o, o + limit)
        if end < 0:
            return None
        raw = self.data[o:end]
        if not raw or any(c < 0x20 and c not in (9, 10, 13) or c > 0x7E for c in raw):
            return None
        return raw.decode("latin-1")

    def data_sections(self):
        return [s for s in self.sections if s[0] != ".text"]


def build():
    funcs, insns = {}, []
    for name in sorted(os.listdir(GEN)):
        if not name.startswith("x86_recomp.") or not name.endswith(".cpp"):
            continue
        current = None
        with open(os.path.join(GEN, name), encoding="latin-1") as f:
            for line in f:
                m = FUNC.match(line)
                if m:
                    current = int(m.group(1), 16)
                    funcs[current] = []
                    continue
                m = INSN.match(line)
                if m and current is not None:
                    addr = int(m.group(1), 16)
                    funcs[current].append((addr, m.group(2)))
    for f, body in funcs.items():
        for addr, text in body:
            insns.append((addr, f, text))
    insns.sort()
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "wb") as f:
        pickle.dump((funcs, insns), f, protocol=pickle.HIGHEST_PROTOCOL)
    return funcs, insns


def load():
    if os.path.exists(CACHE) and os.path.getmtime(CACHE) >= os.path.getmtime(os.path.join(GEN, "x86_func_mapping.cpp")):
        with open(CACHE, "rb") as f:
            return pickle.load(f)
    return build()


def values(text):
    return {int(v, 16) for v in HEX.findall(text)}


def owner(funcs, addr):
    best = None
    for f in funcs:
        if f <= addr and (best is None or f > best):
            body = funcs[f]
            if body and body[0][0] <= addr <= body[-1][0]:
                best = f
    return best


def show(img, text):
    out = text
    for v in values(text):
        if img and v >= 0x10000:
            s = img.cstr(v)
            if s and len(s) >= 3:
                out += f'    ; "{s}"'
    return out


def cmd_func(funcs, insns, img, addr):
    f = addr if addr in funcs else owner(funcs, addr)
    if f is None:
        print("no function")
        return
    print(f"sub_{f:08X}")
    for a, t in funcs[f]:
        print(f"  {a:08X} {show(img, t)}")


def cmd_refs(funcs, insns, img, value):
    for a, f, t in insns:
        if value in values(t):
            print(f"sub_{f:08X}  {a:08X} {show(img, t)}")


def cmd_callers(funcs, insns, img, value):
    seen = {}
    for a, f, t in insns:
        if (t.startswith("call ") or t.startswith("jmp ")) and value in values(t):
            seen.setdefault(f, []).append(a)
    for f, sites in sorted(seen.items()):
        print(f"sub_{f:08X}  " + " ".join(f"{s:08X}" for s in sites))


def cmd_callees(funcs, insns, img, addr):
    f = addr if addr in funcs else owner(funcs, addr)
    for a, t in funcs.get(f, []):
        if t.startswith("call "):
            print(f"  {a:08X} {show(img, t)}")


def cmd_str(funcs, insns, img, needle):
    hits = []
    for name, flags, va, vsize, raw, rsize in img.data_sections():
        blob = img.data[raw:raw + rsize]
        start = 0
        while True:
            i = blob.find(needle.encode("latin-1"), start)
            if i < 0:
                break
            j = i
            while j > 0 and 0x20 <= blob[j - 1] <= 0x7E:
                j -= 1
            hits.append(va + j)
            start = i + 1
    for va in sorted(set(hits)):
        print(f'{va:08X} "{img.cstr(va)}"')
        for a, f, t in insns:
            if va in values(t):
                print(f"    sub_{f:08X}  {a:08X} {t}")


def cmd_ptr(funcs, insns, img, value):
    for name, flags, va, vsize, raw, rsize in img.sections:
        blob = img.data[raw:raw + rsize]
        key = struct.pack("<I", value)
        start = 0
        while True:
            i = blob.find(key, start)
            if i < 0:
                break
            if i % 4 == 0:
                print(f"{name} {va + i:08X}")
            start = i + 1


def cmd_vtable(funcs, insns, img, va, count=40):
    for i in range(count):
        p = img.u32(va + i * 4)
        if p is None or p not in funcs:
            break
        print(f"  +{i * 4:03X} sub_{p:08X}")


def cmd_vtables_of(funcs, insns, img, func):
    for name, flags, va, vsize, raw, rsize in img.data_sections():
        blob = img.data[raw:raw + rsize]
        key = struct.pack("<I", func)
        start = 0
        while True:
            i = blob.find(key, start)
            if i < 0:
                break
            if i % 4 == 0:
                slot = va + i
                head = slot
                while img.u32(head - 4) in funcs:
                    head -= 4
                print(f"{name} slot {slot:08X} vtable {head:08X} +{slot - head:03X}")
            start = i + 1


def main():
    if len(sys.argv) < 3 and not (len(sys.argv) == 2 and sys.argv[1] == "build"):
        print("xbe_xref.py build | func ADDR | refs VALUE | callers ADDR | callees ADDR | str TEXT | ptr VALUE | vtable VA [N] | vtables-of ADDR")
        return
    cmd = sys.argv[1]
    if cmd == "build":
        build()
        return
    funcs, insns = load()
    img = Image(XBE) if os.path.exists(XBE) else None
    arg = sys.argv[2]
    num = lambda s: int(s, 16)
    if cmd == "func":
        cmd_func(funcs, insns, img, num(arg))
    elif cmd == "refs":
        cmd_refs(funcs, insns, img, num(arg))
    elif cmd == "callers":
        cmd_callers(funcs, insns, img, num(arg))
    elif cmd == "callees":
        cmd_callees(funcs, insns, img, num(arg))
    elif cmd == "str":
        cmd_str(funcs, insns, img, arg)
    elif cmd == "ptr":
        cmd_ptr(funcs, insns, img, num(arg))
    elif cmd == "vtable":
        cmd_vtable(funcs, insns, img, num(arg), int(sys.argv[3]) if len(sys.argv) > 3 else 40)
    elif cmd == "vtables-of":
        cmd_vtables_of(funcs, insns, img, num(arg))


if __name__ == "__main__":
    main()
