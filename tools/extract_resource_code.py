#!/usr/bin/env python3
"""Extract the PowerPC code Rebel Strike keeps in its resource packs, for r2recomp.

Rebel Strike (GLRE64) stores some code inside its resources: zlib streams in Rogue3/bData.pool
and Rogue3/data.dat, each a header (its size first) and a table of chunks (5 words each: name
offset, chunk offset from the header's end, size, 0, another offset), the chunks 32-byte
aligned. A chunk can hold a function the game copies to main memory and calls (the
cinematics' per-model code: through the trampoline at 0x8003A6AC). The recompiler never sees
that code, so the runtime would interpret it.

This tool finds those functions by following control flow from their prologues, checks that
each chunk's code is position-independent (no branch leaves the chunk, no absolute addresses,
no computed jumps, nothing reading the program counter), and writes:

  <out>/rescode.bin   every unique chunk's code span, each at its own 256-byte-aligned spot,
                      its layout kept (relative branches stay valid); bytes outside the
                      functions are zeroed
  <out>/rescode.txt   "<address> <name>" for each function entry, the addresses as they are
                      in rescode.bin loaded at --base (default 90000000)

r2recomp takes them with --extra <out>/rescode.bin@90000000 --extra-entries <out>/rescode.txt
(CMake: -DR2_EXTRA_CODE=<out>). The runtime recognises the code by its bytes wherever the game
copies it (src/runtime/interp.cpp). Like all generated code, the output is your game's: keep
it out of the repository.

  python tools/extract_resource_code.py <disc image (.iso/.gcm/.rvz/.wia)> <out dir>
"""
import argparse
import hashlib
import os
import re
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

DEFAULT_FILES = ["Rogue3/bData.pool", "Rogue3/data.dat"]
PROLOGUE = re.compile(rb"\x94\x21..\x7c\x08\x02\xa6", re.S)  # stwu r1,-N(r1); mflr r0


class IsoDisc:
    def __init__(self, path):
        self.f = open(path, "rb")

    def read(self, offset, size):
        self.f.seek(offset)
        return self.f.read(size)


def open_disc(path):
    if path.lower().endswith((".rvz", ".wia")):
        import rvz
        return rvz.Disc(path)
    return IsoDisc(path)


def disc_files(disc):
    hdr = disc.read(0, 0x440)
    fst_off, fst_size = struct.unpack(">II", hdr[0x424:0x42C])
    fst = disc.read(fst_off, fst_size)
    n = struct.unpack(">I", fst[8:12])[0]
    names = fst[n * 12:]
    out, stack, i = {}, [("", n)], 1
    while i < n:
        while stack and i >= stack[-1][1]:
            stack.pop()
        w0, w1, w2 = struct.unpack(">III", fst[i * 12:i * 12 + 12])
        name = names[w0 & 0xFFFFFF:names.index(b"\0", w0 & 0xFFFFFF)].decode("latin1")
        path = (stack[-1][0] if stack else "") + name
        if w0 >> 24:
            stack.append((path + "/", w2))
        else:
            out[path] = (w1, w2)
        i += 1
    return out


def resources(data):
    """(offset, unpacked bytes) of each zlib stream in a pack."""
    pos = 0
    while True:
        i = data.find(b"\x78\xda", pos)
        if i < 0:
            return
        d = zlib.decompressobj()
        try:
            out = d.decompress(data[i:i + 0x800000])
        except zlib.error:
            pos = i + 1
            continue
        if not d.eof or len(out) < 64:
            pos = i + 1
            continue
        yield i, out
        pos = i + min(len(data) - i, 0x800000) - len(d.unused_data)


def chunk_table(out):
    """[(name, start, size)] from a resource's header (start: offset in the resource)."""
    hsize = struct.unpack(">I", out[:4])[0]
    chunks, k = [], 4
    while k + 20 <= hsize:
        nm, off, size, _zero, _other = struct.unpack(">5I", out[k:k + 20])
        if nm == 0 or nm >= hsize or hsize + off + size > len(out) + 32:
            break
        end = out.find(b"\0", nm)
        chunks.append((out[nm:end].decode("latin1", "replace"), hsize + off, size))
        k += 20
    return chunks


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def follow(code, roots):
    """Control flow from the roots: ({entry: set(instruction offsets)}, [problems])."""
    word = lambda o: struct.unpack(">I", code[o:o + 4])[0]
    funcs, problems, todo = {}, [], list(roots)
    while todo:
        entry = todo.pop()
        if entry in funcs or not 0 <= entry <= len(code) - 4:
            continue
        seen, work = set(), [entry]
        while work:
            o = work.pop()
            while 0 <= o <= len(code) - 4 and o not in seen:
                seen.add(o)
                w = word(o)
                op = w >> 26
                if op == 18:  # b, bl
                    off = w & 0x03FFFFFC
                    if off & 0x02000000:
                        off -= 0x04000000
                    t = o + off
                    if w & 2 or not 0 <= t < len(code):
                        problems.append(("branch leaves the chunk", o))
                    elif w & 1:
                        todo.append(t)
                    elif t in funcs or t < entry:
                        todo.append(t)  # tail call
                    else:
                        work.append(t)
                    if not w & 1:
                        break
                elif op == 16:  # bc
                    if w == 0x429F0005:
                        problems.append(("reads the program counter", o))
                    t = o + s16(w & 0xFFFC)
                    if w & 2 or not 0 <= t < len(code):
                        problems.append(("branch leaves the chunk", o))
                    else:
                        work.append(t)
                    if (w >> 21) & 0x14 == 0x14 and not w & 1:
                        break
                elif op == 19:
                    xo = (w >> 1) & 0x3FF
                    if xo == 528 and not w & 1:
                        problems.append(("computed jump (bctr)", o))
                    if xo in (16, 528) and (w >> 21) & 0x14 == 0x14 and not w & 1:
                        break
                    if xo == 50:
                        problems.append(("rfi", o))
                        break
                elif op == 15 and (w >> 16) & 31 and ((w & 0xFFFF) >> 8) in (0x7E, 0x7F, 0x80, 0x81, 0xCC):
                    problems.append(("absolute address", o))
                elif op in (0, 1, 5, 6):
                    problems.append(("not code", o))
                    break
                o += 4
        funcs[entry] = seen
    return funcs, problems


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("disc")
    ap.add_argument("out")
    ap.add_argument("--base", default="90000000", help="address rescode.bin is loaded at (hex)")
    ap.add_argument("--files", nargs="*", default=DEFAULT_FILES, help="packs to search")
    args = ap.parse_args(argv)
    base = int(args.base, 16)

    disc = open_disc(args.disc)
    files = disc_files(disc)
    blob = bytearray()
    entries = []  # (address, name)
    seen = set()
    n_res = n_chunks = n_dup = 0
    bad = []
    for path in args.files:
        if path not in files:
            print(f"{path}: not on this disc", file=sys.stderr)
            continue
        off, size = files[path]
        data = disc.read(off, size)
        for at, out in resources(data):
            n_res += 1
            starts = [m.start() for m in PROLOGUE.finditer(out) if m.start() % 4 == 0]
            if not starts:
                continue
            for name, cstart, csize in chunk_table(out):
                roots = [s - cstart for s in starts if cstart <= s < cstart + csize]
                if not roots:
                    continue
                n_chunks += 1
                code = out[cstart:cstart + csize]
                funcs, problems = follow(code, roots)
                if problems:
                    bad.append((path, at, name, problems[:3]))
                    continue
                insns = set().union(*funcs.values())
                lo, hi = min(insns), max(insns) + 4
                span = bytearray(hi - lo)
                for e, body in funcs.items():  # each function's extent, bytes as they are
                    e_hi = max(body) + 4
                    span[e - lo:e_hi - lo] = code[e:e_hi]
                key = hashlib.sha1(bytes(span) + struct.pack(">I", lo)).digest()
                if key in seen:
                    n_dup += 1
                    continue
                seen.add(key)
                blob += b"\0" * ((-len(blob)) % 256)
                at_blob = base + len(blob) - lo
                blob += span
                for i, e in enumerate(sorted(e for e in funcs if e in roots)):
                    tag = re.sub(r"[^A-Za-z0-9_]", "_", name)[:40] or "chunk"
                    entries.append((at_blob + e, f"res_{tag}" + (f"_{i}" if i else "")))
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, "rescode.bin"), "wb") as f:
        f.write(blob)
    with open(os.path.join(args.out, "rescode.txt"), "w") as f:
        f.write(f"# from {os.path.basename(args.disc)}: {len(entries)} functions, loaded at {base:08x}\n")
        for a, n in entries:
            f.write(f"{a:08x} {n}\n")
    print(f"{n_res} resources, {n_chunks} code chunks ({n_dup} duplicates): {len(entries)} entries, "
          f"{len(blob)} bytes at {base:08x}-{base + len(blob):08x}")
    for path, at, name, problems in bad:
        print(f"  skipped {path}+{at:#x} {name}: {problems}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
