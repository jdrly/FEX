#!/usr/bin/env python3
# gw2 (jdrly/gw2-on-mac#109): compare my build of FEX's four files with the ones in Mach9's
# engine, and a later commit's objects with my build of the engine's commit. It reads files and
# runs nothing.
#
#   compare.py baseline <mine> <engine> [strict | structural]
#   compare.py record <record dir> <source dir> <build dir> [<since>]
#   compare.py patched <mine> <engine> <base record> <head record> [<changed path>...]
#   compare.py info <dll>
#   compare.py selftest <engine>
#
# <engine> is the engine's lib/wine. <mine> has the same layout (aarch64-windows/,
# aarch64-unix/) and, from fex.zsh, unstripped/ with the two DLLs as linked. Prints a Markdown
# report that ends in "failed: <files>" or "all checks passed", and exits 1 when a check fails.
#
# baseline: my build of the engine's commit (fex.zsh build). For each of the four files: the
#   exports and imports are the engine's, and the bytes are:
#   - a .so, with the strict gate (the default, for a build with the engine's Xcode): the byte
#     gate of jdrly/wine's compare.py (byte_gate: the header, every section of every loaded
#     segment, the load commands and the link edit data);
#   - a .so, with the structural gate (for another Xcode, whose clang can't give the engine's
#     bytes): the engine's install name, rpaths, libraries, minos, segments (names, order, kinds),
#     each segment's sections (names and kinds; the order is the linker's, so only reported), and
#     the same functions by name. Which functions differ in their bytes, with branch and address
#     immediates masked, is a report and fails nothing;
#   - a DLL, with either gate, since llvm-mingw builds it: every byte, with the COFF header's
#     TimeDateStamp and each debug directory entry's
#     masked, since LLD writes the link time there. The build id (the CodeView record's GUID and
#     age, LLD's hash of the image without those times) is a line of its own.
#   Where a DLL's bytes differ, the report lists the sections that differ and the functions,
#   through .pdata, with branch and address immediates masked, named from my unstripped build's
#   COFF symbols. That listing is not the gate: it never passes or fails anything.
#   The report's last lines before the verdict are "baseline: <file> <which gate held>, …", for
#   the build's VERSION.
# record: after a build, what the patched compare needs of it, from the files fex.zsh record
#   wrote (<tree>.deps, <tree>.inputs and <tree>.compdb.json, from ninja's -t deps, -t inputs and
#   -t compdb): each object the four files link, a copy of it, its compile command and its inputs
#   (ninja's dependency record: its source and every header the compiler read), and the sha256 of
#   every input that is a file of the build tree (generated headers). With <since> (seconds since
#   the epoch), every object has to be newer, so none is one an earlier build left behind.
# patched: my build of a later commit, made in the same tree after the base build with every
#   object compiled again. Each file's exports and imports are the engine's. Each object none of
#   whose inputs the commit changes (a <changed path> from git diff, or a submodule's directory,
#   or a generated input whose hash differs) and whose compile command is the base's has to be
#   the base build's byte for byte. The others are listed with what touched them. A header that
#   most files include lists most objects: none is hidden.
#   With the baseline that makes the chain: the engine's file = my base build, and my base
#   build's objects = this build's, except the touched ones.
import difflib
import hashlib
import json
import os
import shutil
import struct
import sys
import tempfile

FILES = ["aarch64-windows/libarm64ecfex.dll", "aarch64-windows/libwow64fex.dll",
         "aarch64-unix/libarm64ecfex.so", "aarch64-unix/libwow64fex.so"]
# the names Wine loads each DLL as, copies of it in the engine
ALIASES = {"aarch64-windows/libarm64ecfex.dll": "aarch64-windows/xtajit64.dll",
           "aarch64-windows/libwow64fex.dll": "aarch64-windows/xtajit.dll"}
TREES = ["wow64", "arm64ec", "unixlib"]
OBJECT_SUFFIXES = (".o", ".obj")


class Report:
    def __init__(self):
        self.failed = 0

    def ok(self, text):
        print(f"- ok: {text}")

    def fail(self, text):
        print(f"- **FAIL**: {text}")
        self.failed += 1

    def check(self, cond, good, bad):
        self.ok(good) if cond else self.fail(bad)


def listing(names, limit=40):
    names = sorted(names)
    shown = ", ".join(f"`{n}`" for n in names[:limit])
    return shown + (f" and {len(names) - limit} more" if len(names) > limit else "")


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def cstr(data, off):
    end = data.index(b"\0", off)
    return data[off:end].decode("utf-8", "replace")


# Mach-O, as jdrly/wine's compare.py reads it

LC_REQ_DYLD = 0x80000000
LC_SEGMENT_64, LC_SYMTAB, LC_DYSYMTAB = 0x19, 0x2, 0xb
LC_LOAD_DYLIB, LC_ID_DYLIB, LC_LOAD_WEAK_DYLIB = 0xc, 0xd, 0x18 | LC_REQ_DYLD
LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB, LC_LOAD_UPWARD_DYLIB = 0x1f | LC_REQ_DYLD, 0x20, 0x23 | LC_REQ_DYLD
LC_RPATH, LC_BUILD_VERSION, LC_UUID, LC_CODE_SIGNATURE = 0x1c | LC_REQ_DYLD, 0x32, 0x1b, 0x1d
DYLIB_CMDS = {LC_LOAD_DYLIB: "", LC_LOAD_WEAK_DYLIB: " (weak)", LC_REEXPORT_DYLIB: " (reexport)",
              LC_LAZY_LOAD_DYLIB: " (lazy)", LC_LOAD_UPWARD_DYLIB: " (upward)"}
N_STAB, N_TYPE, N_EXT, N_UNDF, N_SECT = 0xe0, 0x0e, 0x01, 0x0, 0xe
N_SO, N_OSO = 0x64, 0x66
S_ZEROFILL_TYPES = (0x1, 0xc, 0x12)  # S_ZEROFILL, S_GB_ZEROFILL, S_THREAD_LOCAL_ZEROFILL
LINKEDIT_BLOBS = {0x80000034: "chained fixups", 0x80000033: "exports trie", 0x26: "function starts",
                  0x29: "data in code"}


def version(v):
    return f"{v >> 16}.{(v >> 8) & 0xff}" + (f".{v & 0xff}" if v & 0xff else "")


class MachO:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = d = f.read()
        magic, cputype, _, filetype, ncmds, _, _, _ = struct.unpack_from("<IiiIIIII", d, 0)
        if magic != 0xfeedfacf or cputype != 0x0100000c:
            raise SystemExit(f"{path}: not an arm64 Mach-O file")
        self.filetype = filetype
        self.sections, self.segments, self.commands, self.dylibs, self.rpaths = [], [], [], [], []
        self.install_name = self.minos = self.sdk = None
        symtab = dysymtab = None
        off = 32
        for _ in range(ncmds):
            cmd, size = struct.unpack_from("<II", d, off)
            self.commands.append((cmd, off, size))
            if cmd == LC_SEGMENT_64:
                segname = d[off + 8:off + 24].rstrip(b"\0").decode()
                vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects = struct.unpack_from("<QQQQiiI", d, off + 24)
                self.segments.append(dict(name=segname, vmaddr=vmaddr, vmsize=vmsize, fileoff=fileoff,
                                          filesize=filesize, maxprot=maxprot, initprot=initprot))
                for i in range(nsects):
                    s = off + 72 + i * 80
                    sect, seg = (d[s:s + 16].rstrip(b"\0").decode(), d[s + 16:s + 32].rstrip(b"\0").decode())
                    addr, sz, foff, align, reloff, nreloc, flags, r1, r2 = struct.unpack_from("<QQIIIIIII", d, s + 32)
                    self.sections.append(dict(name=f"{seg},{sect}", addr=addr, size=sz, offset=foff, align=align,
                                              reloff=reloff, nreloc=nreloc, flags=flags, type=flags & 0xff,
                                              reserved1=r1, reserved2=r2))
            elif cmd in DYLIB_CMDS or cmd == LC_ID_DYLIB:
                name = cstr(d, off + struct.unpack_from("<I", d, off + 8)[0])
                if cmd == LC_ID_DYLIB:
                    self.install_name = name
                else:
                    self.dylibs.append(name + DYLIB_CMDS[cmd])
            elif cmd == LC_RPATH:
                self.rpaths.append(cstr(d, off + struct.unpack_from("<I", d, off + 8)[0]))
            elif cmd == LC_BUILD_VERSION:
                _, minos, sdk = struct.unpack_from("<III", d, off + 8)
                self.minos, self.sdk = version(minos), version(sdk)
            elif cmd == LC_SYMTAB:
                symtab = struct.unpack_from("<IIII", d, off + 8)
            elif cmd == LC_DYSYMTAB:
                dysymtab = struct.unpack_from("<18I", d, off + 8)
            off += size
        if not symtab:
            raise SystemExit(f"{path}: no symbol table")

        self.dysymtab = dysymtab
        symoff, nsyms, stroff, _ = symtab
        self.syms = []
        for i in range(nsyms):
            strx, ntype, nsect, ndesc, value = struct.unpack_from("<IBBHQ", d, symoff + i * 16)
            self.syms.append((cstr(d, stroff + strx), ntype, nsect, ndesc, value))
        self.indirect = []
        if dysymtab:
            indirectoff, nindirect = dysymtab[12], dysymtab[13]
            self.indirect = list(struct.unpack_from(f"<{nindirect}I", d, indirectoff))

        self.exports = sorted(n[1:] for n, t, _, _, _ in self.syms
                              if not t & N_STAB and t & N_EXT and t & N_TYPE == N_SECT)
        self.imports = {}
        for n, t, _, desc, _ in self.syms:
            if not t & N_STAB and t & N_EXT and t & N_TYPE == N_UNDF:
                ordinal = (desc >> 8) & 0xff
                lib = (self.dylibs[ordinal - 1].split(" (")[0] if 0 < ordinal <= len(self.dylibs)
                       else {0: "self", 0xfe: "executable", 0xff: "flat"}.get(ordinal, f"ordinal {ordinal}"))
                self.imports[n[1:]] = lib

    def functions(self):
        """{name: (address, size)} of the symbols in __TEXT,__text, each up to the next one"""
        index = next((i + 1 for i, s in enumerate(self.sections) if s["name"] == "__TEXT,__text"), None)
        if index is None:
            return {}
        text = self.sections[index - 1]
        starts = sorted({(v, n) for n, t, sect, _, v in self.syms
                         if not t & N_STAB and t & N_TYPE == N_SECT and sect == index})
        ends = [v for v, _ in starts[1:]] + [text["addr"] + text["size"]]
        return {n: (v, e - v) for (v, n), e in zip(starts, ends)}

    def bytes_of(self, addr, size):
        for s in self.sections:
            if s["addr"] <= addr and addr + size <= s["addr"] + s["size"] and s["offset"]:
                o = s["offset"] + addr - s["addr"]
                return self.data[o:o + size]
        raise SystemExit(f"{self.path}: no bytes at {addr:#x}")


def debug_map_free(m):
    """the symbol table without what only says where and when it was built: an N_SO entry's
    directory and an N_OSO entry's object modification time"""
    out = []
    for n, t, sect, desc, value in m.syms:
        if t == N_SO and n.endswith("/"):
            n = "<source directory>"
        if t == N_OSO:
            value = 0
        out.append((n, t, sect, desc, value))
    return out


def command_bytes(m, cmd, off, size):
    """a load command's bytes, with the fields that follow the string table's length or hash the
    whole file zeroed"""
    raw = bytearray(m.data[off:off + size])
    if cmd in (LC_UUID, LC_CODE_SIGNATURE):
        raw[8:] = bytes(len(raw) - 8)
    elif cmd == LC_SYMTAB:
        raw[20:24] = bytes(4)
    elif cmd == LC_SEGMENT_64 and raw[8:24].rstrip(b"\0") == b"__LINKEDIT":
        raw[32:40] = bytes(8)
        raw[48:56] = bytes(8)
    return bytes(raw)


def linkedit_blob(m, cmd):
    for c, o, _ in m.commands:
        if c == cmd:
            dataoff, size = struct.unpack_from("<II", m.data, o + 8)
            return m.data[dataoff:dataoff + size]
    return None


def byte_gate(mine, theirs, r):
    """jdrly/wine's byte gate: the linked file against the engine's, section by section and
    through the link edit data"""
    differ = []
    if mine.data[:32] != theirs.data[:32]:
        differ.append("the Mach-O header")
    if [s["name"] for s in mine.segments] != [s["name"] for s in theirs.segments]:
        differ.append("the segments")
    for sm, st in zip(mine.segments, theirs.segments):
        if sm["name"] != "__LINKEDIT" and sm != st:
            differ.append(f"segment {sm['name']}'s layout")
    if [s["name"] for s in mine.sections] != [s["name"] for s in theirs.sections]:
        differ.append("the sections")
    zerofill = []
    for sm, st in zip(mine.sections, theirs.sections):
        layout = lambda s: (s["name"], s["addr"], s["size"], s["align"], s["flags"], s["reserved1"], s["reserved2"])
        if layout(sm) != layout(st):
            differ.append(f"{sm['name']}'s place or size")
        elif sm["type"] in S_ZEROFILL_TYPES:
            zerofill.append(sm["name"])
        elif mine.bytes_of(sm["addr"], sm["size"]) != theirs.bytes_of(st["addr"], st["size"]):
            differ.append(sm["name"])
    cm = [(c, command_bytes(mine, c, o, n)) for c, o, n in mine.commands]
    ct = [(c, command_bytes(theirs, c, o, n)) for c, o, n in theirs.commands]
    if cm != ct:
        differ.append("the load commands")
    for cmd, what in LINKEDIT_BLOBS.items():
        if linkedit_blob(mine, cmd) != linkedit_blob(theirs, cmd):
            differ.append(f"the {what}")
    if mine.indirect != theirs.indirect or mine.dysymtab != theirs.dysymtab:
        differ.append("the indirect symbol table")
    if debug_map_free(mine) != debug_map_free(theirs):
        differ.append("the symbol table")
    r.check(not differ,
            f"the header, every section of every loaded segment, the load commands and the link edit data are "
            f"the engine's byte for byte. Zero-filled, so compared by place and size: {', '.join(zerofill) or 'none'}. "
            f"Left out: LC_UUID and the code signature (hashes of the whole file), the string table's "
            f"length and __LINKEDIT's size, the symbol table's N_SO source directories and N_OSO times",
            f"not byte for byte the engine's: {', '.join(differ)}")
    return not differ


def structural_gate(mine, theirs, r):
    """the engine's file's shape, for a build with another compiler: the segments by name, order and
    kind, each one's sections by name and kind, and the functions by name; which functions differ
    in their bytes, and sections in another order, are a report. Returns the line for VERSION."""
    segments = lambda m: [(s["name"], s["maxprot"], s["initprot"]) for s in m.segments]
    sections = lambda m: sorted((s["name"], s["type"], s["flags"]) for s in m.sections)
    r.check(segments(mine) == segments(theirs), f"the engine's {len(theirs.segments)} segments, by name, order and kind: "
                                                f"{listing([s['name'] for s in theirs.segments])}",
            f"segments differ: mine {segments(mine)}, the engine's {segments(theirs)}")
    r.check(sections(mine) == sections(theirs), f"the engine's {len(theirs.sections)} sections, by name and kind: "
                                                f"{listing([s['name'] for s in theirs.sections], 20)}",
            f"sections differ: mine {sections(mine)}, the engine's {sections(theirs)}")
    order = lambda m: [s["name"] for s in m.sections]
    if order(mine) != order(theirs) and sections(mine) == sections(theirs):
        print(f"- report, not the gate: the linker put the sections in another order: mine {', '.join(order(mine))}; "
              f"the engine's {', '.join(order(theirs))}")
    fm, ft = mine.functions(), theirs.functions()
    r.check(set(fm) == set(ft), f"the engine's {len(ft)} functions by name",
            f"functions differ by name: missing {listing(set(ft) - set(fm))}; extra {listing(set(fm) - set(ft))}")
    code = lambda m, f: masked_words(m.bytes_of(*f))
    differ = sorted(n for n in set(fm) & set(ft) if code(mine, fm[n]) != code(theirs, ft[n]))
    print(f"- report, not the gate: with branch and address immediates masked, {len(differ)} of {len(ft)} functions "
          f"differ in their bytes" + (f": {listing(differ, 100)}" if differ else ""))
    return f"structural, {len(differ)} of {len(ft)} functions differ from the engine's file"


def macho_checks(mine, theirs, r, baseline, gate="strict"):
    r.check(mine.filetype == theirs.filetype == 6, "a dylib, as the engine's", "not a dylib like the engine's")
    r.check(mine.install_name == theirs.install_name, f"install name `{mine.install_name}`",
            f"install name `{mine.install_name}`, the engine's is `{theirs.install_name}`")
    r.check(mine.rpaths == theirs.rpaths, f"rpaths {mine.rpaths}", f"rpaths {mine.rpaths}, the engine's are {theirs.rpaths}")
    r.check(mine.dylibs == theirs.dylibs, f"loads the engine's {len(theirs.dylibs)} libraries: {listing(theirs.dylibs)}",
            f"loads {mine.dylibs}, the engine's loads {theirs.dylibs}")
    r.check(mine.minos is not None and mine.minos == theirs.minos, f"minos {mine.minos} (SDK {mine.sdk}, the engine's {theirs.sdk})",
            f"minos {mine.minos}, the engine's is {theirs.minos}")
    r.check(mine.exports == theirs.exports, f"exports the engine's {len(theirs.exports)} symbols: {listing(theirs.exports, 5)}",
            f"exports differ: missing {listing(set(theirs.exports) - set(mine.exports))}; "
            f"extra {listing(set(mine.exports) - set(theirs.exports))}")
    if mine.imports == theirs.imports:
        r.ok(f"imports the engine's {len(theirs.imports)} symbols from the same libraries")
    else:
        diff = {f"{n} ({mine.imports.get(n)} / {theirs.imports.get(n)})"
                for n in set(mine.imports) | set(theirs.imports) if mine.imports.get(n) != theirs.imports.get(n)}
        r.fail(f"imports differ (mine / the engine's): {listing(diff)}")
    if not baseline:
        return None
    if gate == "structural":
        return structural_gate(mine, theirs, r)
    held = byte_gate(mine, theirs, r)
    same = mine.data == theirs.data
    print(f"- the whole file is {'' if same else 'not '}the engine's byte for byte"
          + ("" if same else " (only the byte gate above decides)"))
    return "byte for byte" if held else "not byte for byte"


# PE

IMAGE_SCN_CNT_CODE = 0x20


class PE:
    def __init__(self, path):
        self.path = path
        with open(path, "rb") as f:
            self.data = d = f.read()
        if d[:2] != b"MZ":
            raise SystemExit(f"{path}: not a PE file")
        self.pe = pe = struct.unpack_from("<I", d, 0x3c)[0]
        if d[pe:pe + 4] != b"PE\0\0":
            raise SystemExit(f"{path}: not a PE file")
        (self.machine, nsec, self.timestamp, self.symptr, self.nsyms, optsize,
         self.characteristics) = struct.unpack_from("<HHIIIHH", d, pe + 4)
        opt = pe + 24
        if struct.unpack_from("<H", d, opt)[0] != 0x20b:
            raise SystemExit(f"{path}: not a PE32+ file")
        self.image_base = struct.unpack_from("<Q", d, opt + 24)[0]
        self.size_of_image = struct.unpack_from("<I", d, opt + 56)[0]
        ndirs = struct.unpack_from("<I", d, opt + 108)[0]
        self.dirs = [struct.unpack_from("<II", d, opt + 112 + 8 * i) for i in range(ndirs)]
        self.headers_end = opt + optsize + 40 * nsec
        self.sections = []
        for i in range(nsec):
            raw_name, vsize, va, rsize, rptr, _, _, _, _, chars = struct.unpack_from("<8sIIIIIIHHI", d, opt + optsize + 40 * i)
            name = raw_name.rstrip(b"\0").decode("utf-8", "replace")
            if name.startswith("/") and self.symptr:
                name = cstr(d, self.symptr + 18 * self.nsyms + int(name[1:]))
            self.sections.append(dict(name=name, va=va, vsize=vsize, rptr=rptr, rsize=rsize, chars=chars))

    def offset(self, rva):
        for s in self.sections:
            if s["va"] <= rva < s["va"] + max(s["vsize"], s["rsize"]) and rva - s["va"] < s["rsize"]:
                return s["rptr"] + rva - s["va"]
        raise SystemExit(f"{self.path}: no bytes at RVA {rva:#x}")

    def directory(self, i):
        return self.dirs[i] if i < len(self.dirs) and self.dirs[i][0] else (0, 0)

    def name_at(self, rva):
        return cstr(self.data, self.offset(rva))

    def exports(self):
        rva, _ = self.directory(0)
        if not rva:
            return []
        o = self.offset(rva)
        _, _, _, _, _, _, nfuncs, nnames, _, names_rva, _ = struct.unpack_from("<IIHHIIIIIII", self.data, o)
        names = [self.name_at(struct.unpack_from("<I", self.data, self.offset(names_rva) + 4 * i)[0])
                 for i in range(nnames)]
        return sorted(names) + [f"({nfuncs} entries)"]

    def thunk_names(self, rva):
        names, o = [], self.offset(rva)
        while True:
            t = struct.unpack_from("<Q", self.data, o)[0]
            if not t:
                return names
            names.append(f"#{t & 0xffff}" if t >> 63 else self.name_at((t & 0x7fffffff) + 2))
            o += 8

    def imports(self):
        out = {}
        rva, _ = self.directory(1)
        o = self.offset(rva) if rva else None
        while o is not None:
            ilt, _, _, name, iat = struct.unpack_from("<IIIII", self.data, o)
            if not name:
                break
            out[self.name_at(name)] = self.thunk_names(ilt or iat)
            o += 20
        rva, _ = self.directory(13)
        o = self.offset(rva) if rva else None
        while o is not None:
            _, name, _, _, names, _, _, _ = struct.unpack_from("<8I", self.data, o)
            if not name:
                break
            out[f"{self.name_at(name)} (delayed)"] = self.thunk_names(names)
            o += 32
        return out

    def debug_entries(self):
        """(file offset of the entry, type, its TimeDateStamp, its data's file offset and size)"""
        rva, size = self.directory(6)
        if not rva:
            return []
        o = self.offset(rva)
        out = []
        for i in range(size // 28):
            _, ts, _, _, typ, dsize, _, ptr = struct.unpack_from("<IIHHIIII", self.data, o + 28 * i)
            out.append((o + 28 * i, typ, ts, ptr, dsize))
        return out

    def build_id(self):
        """the CodeView record's GUID and age: LLD's build id"""
        for _, typ, _, ptr, size in self.debug_entries():
            if typ == 2 and self.data[ptr:ptr + 4] == b"RSDS":
                return self.data[ptr + 4:ptr + 20], struct.unpack_from("<I", self.data, ptr + 20)[0]
        return None, None

    def link_time_offsets(self):
        return [self.pe + 8] + [o + 4 for o, *_ in self.debug_entries()]

    def masked(self):
        d = bytearray(self.data)
        for o in self.link_time_offsets():
            d[o:o + 4] = bytes(4)
        return bytes(d)

    def runtime_functions(self):
        """(start RVA, length) of every function .pdata describes: the exception directory, or in
        an ARM64EC file, whose directory holds x64 code's, the whole .pdata section. Entries
        that start at 0 (a function the linker dropped) are skipped."""
        rva, size = self.directory(3)
        if not rva:
            s = next((s for s in self.sections if s["name"] == ".pdata"), None)
            if not s:
                return []
            rva, size = s["va"], s["vsize"]
        o, out = self.offset(rva), set()
        for i in range(size // 8):
            begin, unwind = struct.unpack_from("<II", self.data, o + 8 * i)
            if not begin:
                continue
            if unwind & 3:
                length = ((unwind >> 2) & 0x7ff) * 4
            else:
                length = (struct.unpack_from("<I", self.data, self.offset(unwind))[0] & 0x3ffff) * 4
            out.add((begin, length))
        return sorted(out)

    def function_names(self):
        """RVA -> name, from the COFF symbol table of an unstripped file"""
        names = {}
        strtab = self.symptr + 18 * self.nsyms
        i = 0
        while i < self.nsyms:
            o = self.symptr + 18 * i
            raw, value, secnum, _, sclass, naux = struct.unpack_from("<8sIhHBB", self.data, o)
            i += 1 + naux
            if secnum <= 0 or sclass not in (2, 3) or secnum > len(self.sections):
                continue
            s = self.sections[secnum - 1]
            if not s["chars"] & IMAGE_SCN_CNT_CODE:
                continue
            name = (cstr(self.data, strtab + struct.unpack_from("<I", raw, 4)[0]) if raw[:4] == bytes(4)
                    else raw.rstrip(b"\0").decode("utf-8", "replace"))
            if name and not name.startswith((".", "$")):
                names.setdefault(s["va"] + value, name)
        return names


def masked_words(code):
    """a function's instructions with what moves when code moves masked: branch offsets, ADR and
    ADRP pages, literal loads, and the low 12 bits added to or loaded through an ADRP's register.
    Only for the listing, which names what differs."""
    out, adrp = [], set()
    for i in range(0, len(code) - len(code) % 4, 4):
        w = struct.unpack_from("<I", code, i)[0]
        rd, rn = w & 31, (w >> 5) & 31
        if w & 0x7c000000 == 0x14000000:          # B, BL
            w &= 0xfc000000
        elif w & 0xff000000 == 0x54000000:        # B.cond
            w &= 0xff00001f
        elif w & 0x7e000000 in (0x34000000,):     # CBZ, CBNZ
            w &= 0xff00001f
        elif w & 0x7e000000 == 0x36000000:        # TBZ, TBNZ
            w &= 0xfff8001f
        elif w & 0x1f000000 == 0x10000000:        # ADR, ADRP
            w &= 0x9f00001f
            adrp.add(rd)
        elif w & 0x3b000000 == 0x18000000:        # LDR (literal)
            w &= 0xff00001f
        elif w & 0xff800000 == 0x91000000 and rn in adrp:  # ADD (immediate), 64-bit
            w &= 0xffc003ff
        elif w & 0x3b000000 == 0x39000000 and rn in adrp:  # LDR, STR (unsigned offset)
            w &= 0xffc003ff
        out.append(w)
    return tuple(out)


def diagnose_pe(mine, theirs, names):
    """where my DLL isn't the engine's: the sections, then the functions. Only a listing."""
    a, b = mine.masked(), theirs.masked()
    differ = []
    if a[:mine.headers_end] != b[:theirs.headers_end]:
        differ.append("the headers")
    for sm in mine.sections:
        st = next((s for s in theirs.sections if s["name"] == sm["name"]), None)
        if not st:
            differ.append(f"{sm['name']} (only in mine)")
        elif (sm["va"], sm["vsize"], sm["rsize"]) != (st["va"], st["vsize"], st["rsize"]):
            differ.append(f"{sm['name']}'s place or size")
        elif a[sm["rptr"]:sm["rptr"] + sm["rsize"]] != b[st["rptr"]:st["rptr"] + st["rsize"]]:
            differ.append(sm["name"])
    differ += [f"{s['name']} (only in the engine's)" for s in theirs.sections
               if s["name"] not in {m["name"] for m in mine.sections}]
    print(f"- the sections that differ: {', '.join(differ) or 'none'}")
    fm, ft = mine.runtime_functions(), theirs.runtime_functions()
    if not fm or not ft:
        print("- no .pdata to list functions by")
        return
    km = [masked_words(mine.data[mine.offset(s):mine.offset(s) + n]) for s, n in fm]
    kt = [masked_words(theirs.data[theirs.offset(s):theirs.offset(s) + n]) for s, n in ft]
    name = lambda rva: names.get(rva, f"RVA {rva:#x}")
    out = []
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(None, km, kt, autojunk=False).get_opcodes():
        if tag == "equal":
            continue
        out += [name(fm[i][0]) + (" (only in mine)" if tag == "insert" else "") for i in range(i1, i2)]
        if tag == "delete":
            out += [f"the engine's RVA {ft[j][0]:#x} (only in the engine's)" for j in range(j1, j2)]
    print(f"- with branch and address immediates masked, {len(out)} of {len(ft)} functions differ"
          + ("" if names else " (no unstripped build to name them by)") + ":")
    for o in out[:200]:
        print(f"  - `{o}`")
    if len(out) > 200:
        print(f"  - and {len(out) - 200} more")


def pe_checks(mine, theirs, r, baseline, unstripped=None):
    r.check(mine.machine == theirs.machine, f"machine {mine.machine:#x}, as the engine's",
            f"machine {mine.machine:#x}, the engine's is {theirs.machine:#x}")
    em, et = mine.exports(), theirs.exports()
    r.check(em == et, f"exports the engine's {len(et) - 1} names {et[-1]}: {listing(et[:-1], 5)}",
            f"exports differ: missing {listing(set(et) - set(em))}; extra {listing(set(em) - set(et))}")
    im, it = mine.imports(), theirs.imports()
    r.check(im == it, f"imports the engine's {sum(map(len, it.values()))} names from {listing(it)}",
            f"imports differ: mine {im}, the engine's {it}")
    gm, am = mine.build_id()
    gt, at = theirs.build_id()
    show = lambda g, a: f"{g.hex() if g else 'none'} age {a}"
    if baseline:
        r.check(gm is not None and (gm, am) == (gt, at), f"build id {show(gm, am)}, the engine's",
                f"build id {show(gm, am)}, the engine's is {show(gt, at)}")
    else:
        print(f"- build id {show(gm, am)} (the engine's {show(gt, at)}), SizeOfImage {mine.size_of_image:#x}")
    if not baseline:
        return None
    times = lambda p: ", ".join(f"{struct.unpack_from('<I', p.data, o)[0]:#x}" for o in p.link_time_offsets())
    same = len(mine.data) == len(theirs.data) and mine.masked() == theirs.masked()
    r.check(same, f"every byte the engine's except the {len(theirs.link_time_offsets())} link times "
                  f"(the COFF header's TimeDateStamp and the debug directory's): mine {times(mine)}, the engine's {times(theirs)}",
            f"not byte for byte the engine's, link times masked ({len(mine.data)} bytes, the engine's {len(theirs.data)})")
    if not same:
        diagnose_pe(mine, theirs, unstripped.function_names() if unstripped else {})
    return "byte for byte but the link times" if same else "not byte for byte"


# baseline and patched

def file_report(rel, mine_dir, engine_dir, baseline, gate="strict"):
    """one file's checks, printed; returns whether all passed, and which gate held"""
    r = Report()
    mine_path, theirs_path = os.path.join(mine_dir, rel), os.path.join(engine_dir, rel)
    name = os.path.basename(rel)
    print(f"\n#### {name}\n")
    print(f"- mine: {os.path.getsize(mine_path)} bytes, sha256 `{sha256(mine_path)}`")
    print(f"- the engine's: {os.path.getsize(theirs_path)} bytes, sha256 `{sha256(theirs_path)}`")
    if rel in ALIASES:
        alias = os.path.join(engine_dir, ALIASES[rel])
        r.check(os.path.exists(alias) and sha256(alias) == sha256(theirs_path),
                f"the engine's {os.path.basename(alias)} is the same file",
                f"the engine's {os.path.basename(alias)} isn't the same file as its {name}")
    if rel.endswith(".dll"):
        unstripped = os.path.join(mine_dir, "unstripped", name)
        held = pe_checks(PE(mine_path), PE(theirs_path), r, baseline, PE(unstripped) if os.path.exists(unstripped) else None)
    else:
        held = macho_checks(MachO(mine_path), MachO(theirs_path), r, baseline, gate)
    return r.failed == 0, held


def norm(path, tree_dir, src, build):
    a = os.path.normpath(os.path.join(tree_dir, path))
    if a.startswith(src + "/"):
        return "src:" + a[len(src) + 1:]
    if a.startswith(build + "/"):
        return "build:" + a[len(build) + 1:]
    return "sys:" + a


def parse_deps(text):
    """ninja -t deps: '<output>: #deps N, ...' and then one indented input a line"""
    deps, cur = {}, None
    for line in text.splitlines():
        if line.startswith((" ", "\t")):
            if cur is not None:
                deps[cur].append(line.strip())
        elif line.strip():
            cur = line.split(": #deps", 1)[0]
            deps[cur] = []
    return deps


def record(out, src, build, since=None):
    src, build = os.path.realpath(src), os.path.realpath(build)
    rec, bad = {"trees": {}, "generated": {}}, []
    for tree in TREES:
        tdir = os.path.join(build, tree)
        with open(os.path.join(out, f"{tree}.deps")) as f:
            deps = parse_deps(f.read())
        with open(os.path.join(out, f"{tree}.inputs")) as f:
            objects = sorted({l.strip() for l in f if l.strip().endswith(OBJECT_SUFFIXES)})
        with open(os.path.join(out, f"{tree}.compdb.json")) as f:
            compdb = {e.get("output"): e for e in json.load(f)}
        if not objects:
            bad.append(f"{tree}: no objects among the link targets' inputs")
        objs = {}
        for obj in objects:
            path = os.path.join(tdir, obj)
            if not os.path.exists(path):
                bad.append(f"{tree}/{obj}: not built")
                continue
            if since is not None and os.path.getmtime(path) < since:
                bad.append(f"{tree}/{obj}: older than this build")
            e = compdb.get(obj)
            inputs = sorted({norm(p, tdir, src, build) for p in deps.get(obj, [])}
                            | ({norm(e["file"], e.get("directory", tdir), src, build)} if e else set()))
            dst = os.path.join(out, "objects", tree, obj)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(path, dst)
            objs[obj] = {"sha256": sha256(path), "command": e["command"] if e else None, "inputs": inputs}
            for i in inputs:
                if i.startswith("build:") and i not in rec["generated"]:
                    p = os.path.join(build, i[len("build:"):])
                    rec["generated"][i] = sha256(p) if os.path.isfile(p) else None
        rec["trees"][tree] = objs
    with open(os.path.join(out, "record.json"), "w") as f:
        json.dump(rec, f, indent=1, sort_keys=True)
    n = sum(len(t) for t in rec["trees"].values())
    per_tree = ", ".join(f"{t} {len(rec['trees'][t])}" for t in TREES)
    print(f"recorded {n} objects ({per_tree}), {len(rec['generated'])} generated inputs")
    for b in bad:
        print(f"::error::{b}")
    return 1 if bad else 0


def compare_objects(base, head, changed):
    """(failures, touched {object: (reasons, same bytes)}, new, removed, untouched count)"""
    def changed_src(i):
        p = i[len("src:"):]
        return any(p == c or p.startswith(c.rstrip("/") + "/") for c in changed)
    bad, touched, new, removed, untouched = [], {}, [], [], 0
    for tree in TREES:
        b, h = base["trees"].get(tree, {}), head["trees"].get(tree, {})
        for obj in sorted(set(b) | set(h)):
            key = f"{tree}/{obj}"
            if obj not in h:
                removed.append(key)
                continue
            if obj not in b:
                new.append(key)
                continue
            inputs = set(b[obj]["inputs"]) | set(h[obj]["inputs"])
            if not inputs:
                bad.append(f"`{key}`: no record of its inputs")
                continue
            reasons = sorted(i for i in inputs
                             if (i.startswith("src:") and changed_src(i))
                             or (i.startswith("build:") and base["generated"].get(i) != head["generated"].get(i)))
            if b[obj]["command"] != h[obj]["command"]:
                reasons.append("its compile command")
            same = b[obj]["sha256"] == h[obj]["sha256"]
            if reasons:
                touched[key] = (reasons, same)
            elif same:
                untouched += 1
            else:
                bad.append(f"`{key}`: none of its inputs changed, and it isn't the base build's")
    return bad, touched, new, removed, untouched


def print_objects(bad, touched, new, removed, untouched):
    total = untouched + len(touched) + len(bad)
    print(f"\n#### Objects against my build of the engine's commit\n")
    print(f"- {untouched} of {total} objects in both builds: none of their inputs changed, and each is the base "
          f"build's byte for byte")
    for b in bad:
        print(f"- **FAIL**: {b}")
    by_reason = {}
    for key, (reasons, same) in touched.items():
        by_reason.setdefault(tuple(reasons), []).append((key, same))
    print(f"- touched by this commit: {len(touched)}" + (", by what touched them:" if touched else ""))
    for reasons, objs in sorted(by_reason.items()):
        print(f"  - through {', '.join(f'`{r}`' for r in reasons)}:")
        for key, same in sorted(objs):
            print(f"    - `{key}`" + (" (still the base's bytes)" if same else ""))
    for title, items in (("new", new), ("gone", removed)):
        print(f"- {title} objects: {len(items)}" + (f": {listing(items, 1000)}" if items else ""))


def run_files(mine, engine, baseline, gate="strict"):
    failed, held = [], []
    for rel in FILES:
        ok, h = file_report(rel, mine, engine, baseline, gate)
        if not ok:
            failed.append(os.path.basename(rel))
        if h:
            held.append(f"{os.path.basename(rel)} {h}" if ok else f"{os.path.basename(rel)} failed its gate")
    if held:
        print(f"\nbaseline: {', '.join(held)}")
    return failed


def finish(failed):
    print("\n" + (f"failed: {' '.join(failed)}" if failed else "all checks passed"))
    return 1 if failed else 0


def info(path):
    p = PE(path)
    g, a = p.build_id()
    print(f"build id {g.hex() if g else 'none'} age {a}, SizeOfImage {p.size_of_image:#x}")
    return 0


# selftest: the gates against the engine's own files, changed by hand

def selftest(engine):
    import contextlib
    import io
    status = []

    def run(label, fn, want_exit, want_failed):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = fn()
        out = buf.getvalue()
        last = out.strip().splitlines()[-1] if out.strip() else ""
        good = code == want_exit and last == (f"failed: {want_failed}" if want_failed else "all checks passed")
        status.append(good)
        print(f"{'ok' if good else 'FAIL'}: {label}: exit {code}, \"{last}\"")
        return out

    def patch(path, off, data):
        with open(path, "r+b") as f:
            f.seek(off)
            f.write(data)

    with tempfile.TemporaryDirectory() as tmp:
        def copy():
            d = tempfile.mkdtemp(dir=tmp)
            for rel in FILES + list(ALIASES.values()):
                os.makedirs(os.path.dirname(os.path.join(d, rel)), exist_ok=True)
                shutil.copyfile(os.path.join(engine, rel), os.path.join(d, rel))
            return d

        run("the engine's files against themselves", lambda: finish(run_files(engine, engine, True)), 0, "")

        d = copy()
        for rel in ALIASES:
            p = PE(os.path.join(d, rel))
            for o in p.link_time_offsets():
                patch(p.path, o, struct.pack("<I", 0x12345678))
        out = run("other link times only", lambda: finish(run_files(d, engine, True)), 0, "")
        status.append("mine 0x12345678, 0x12345678" in out)
        print(f"{'ok' if status[-1] else 'FAIL'}: the report names my link times")

        d = copy()
        p = PE(os.path.join(d, FILES[0]))
        f0 = p.runtime_functions()[100]
        # the second instruction's top bit, which no mask hides
        o = p.offset(f0[0]) + 7
        patch(p.path, o, bytes([p.data[o] ^ 0x80]))
        out = run("one bit in a function of libarm64ecfex.dll", lambda: finish(run_files(d, engine, True)), 1, "libarm64ecfex.dll")
        status.append(f"`RVA {f0[0]:#x}`" in out and "the sections that differ: .text" in out)
        print(f"{'ok' if status[-1] else 'FAIL'}: the listing names .text and RVA {f0[0]:#x}")

        d = copy()
        p = PE(os.path.join(d, FILES[1]))
        _, _, _, ptr, _ = next(e for e in p.debug_entries() if e[1] == 2)
        patch(p.path, ptr + 4, bytes(8))
        out = run("another build id in libwow64fex.dll", lambda: finish(run_files(d, engine, True)), 1, "libwow64fex.dll")
        status.append("- **FAIL**: build id 00000000" in out)
        print(f"{'ok' if status[-1] else 'FAIL'}: the build id fails on a line of its own")

        d = copy()
        m = MachO(os.path.join(d, FILES[3]))
        t = next(s for s in m.sections if s["name"] == "__TEXT,__text")
        patch(m.path, t["offset"] + 8, bytes([m.data[t["offset"] + 8] ^ 0x01]))
        out = run("one bit in libwow64fex.so's code", lambda: finish(run_files(d, engine, True)), 1, "libwow64fex.so")
        status.append("not byte for byte the engine's: __TEXT,__text" in out)
        print(f"{'ok' if status[-1] else 'FAIL'}: the byte gate names __TEXT,__text")

        # the structural gate: a function's other bytes alone pass, with the report naming it;
        # a missing export, another import, another minos or a missing function each fail
        so = FILES[2]
        run_s = lambda d: finish(run_files(d, engine, True, "structural"))
        def symbol_name_offset(m, name):
            symoff, _, stroff, _ = next(struct.unpack_from("<IIII", m.data, o + 8) for c, o, _ in m.commands if c == LC_SYMTAB)
            i = next(i for i, sym in enumerate(m.syms) if sym[0] == name)
            return stroff + struct.unpack_from("<I", m.data, symoff + 16 * i)[0]

        out = run("the engine's files, structural", lambda: run_s(engine), 0, "")
        status.append("libarm64ecfex.so structural, 0 of 12 functions differ" in out)
        print(f"{'ok' if status[-1] else 'FAIL'}: the baseline line says 0 of 12 functions differ")

        d = copy()
        m = MachO(os.path.join(d, so))
        f = m.functions()["__ZL24FEXUnixLib_GetPIDHandlerPv"]
        o = m.sections[0]["offset"] + f[0] - m.sections[0]["addr"] + 3
        patch(m.path, o, bytes([m.data[o] ^ 0x80]))
        out = run("structural: other bytes in one function only", lambda: run_s(d), 0, "")
        status.append("1 of 12 functions differ in their bytes: `__ZL24FEXUnixLib_GetPIDHandlerPv`" in out)
        print(f"{'ok' if status[-1] else 'FAIL'}: the report names that function")

        # two sections of one segment in each other's place, as Xcode 27's linker puts
        # __DATA_CONST's __const and __got: reported, not failed
        d = copy()
        m = MachO(os.path.join(d, so))
        seg = next(o for c, o, _ in m.commands if c == LC_SEGMENT_64 and m.data[o + 8:o + 24].rstrip(b"\0") == b"__DATA_CONST")
        a, b = seg + 72, seg + 72 + 80
        patch(m.path, a, m.data[b:b + 80] + m.data[a:a + 80])
        out = run("structural: two sections of a segment swapped", lambda: run_s(d), 0, "")
        status.append("the linker put the sections in another order" in out)
        print(f"{'ok' if status[-1] else 'FAIL'}: the report says the order differs")

        for label, change in (
                ("a missing export", lambda m: (symbol_name_offset(m, "___wine_unix_call_funcs") + 3, b"X")),
                ("another import", lambda m: (symbol_name_offset(m, "_getenv") + 1, b"X")),
                ("another minos", lambda m: (next(o for c, o, _ in m.commands if c == LC_BUILD_VERSION) + 12,
                                             struct.pack("<I", 0x1a0500))),
                ("a missing function", lambda m: (symbol_name_offset(m, "__ZL25FEXUnixLib_MadviseHandlerPv") + 10, b"X"))):
            d = copy()
            m = MachO(os.path.join(d, so))
            patch(m.path, *change(m))
            run(f"structural: {label}", lambda: run_s(d), 1, "libarm64ecfex.so")

    # the object compare, on hand-made records
    obj = lambda sha, cmd, *inputs: {"sha256": sha, "command": cmd, "inputs": list(inputs)}
    base = {"generated": {"build:arm64ec/include/git_version.h": "a", "build:arm64ec/gen.inl": "g"},
            "trees": {"arm64ec": {"same.obj": obj("1", "cc", "src:Same.cpp", "sys:/usr/x.h"),
                                  "edited.obj": obj("2", "cc", "src:Edited.cpp"),
                                  "version.obj": obj("3", "cc", "src:V.cpp", "build:arm64ec/include/git_version.h"),
                                  "flags.obj": obj("4", "cc", "src:F.cpp"),
                                  "sub.obj": obj("5", "cc", "src:External/fmt/src/format.cc"),
                                  "drift.obj": obj("6", "cc", "src:Drift.cpp", "build:arm64ec/gen.inl"),
                                  "gone.obj": obj("7", "cc", "src:Gone.cpp")}}}
    head = {"generated": {"build:arm64ec/include/git_version.h": "b", "build:arm64ec/gen.inl": "g"},
            "trees": {"arm64ec": {"same.obj": obj("1", "cc", "src:Same.cpp", "sys:/usr/x.h"),
                                  "edited.obj": obj("2x", "cc", "src:Edited.cpp"),
                                  "version.obj": obj("3x", "cc", "src:V.cpp", "build:arm64ec/include/git_version.h"),
                                  "flags.obj": obj("4", "cc -DX", "src:F.cpp"),
                                  "sub.obj": obj("5x", "cc", "src:External/fmt/src/format.cc"),
                                  "drift.obj": obj("6x", "cc", "src:Drift.cpp", "build:arm64ec/gen.inl"),
                                  "new.obj": obj("8", "cc", "src:New.cpp")}}}
    bad, touched, new, removed, untouched = compare_objects(base, head, ["Edited.cpp", "External/fmt"])
    want = ({"arm64ec/edited.obj": (["src:Edited.cpp"], False),
             "arm64ec/version.obj": (["build:arm64ec/include/git_version.h"], False),
             "arm64ec/flags.obj": (["its compile command"], True),
             "arm64ec/sub.obj": (["src:External/fmt/src/format.cc"], False)},
            ["arm64ec/new.obj"], ["arm64ec/gone.obj"], 1)
    good = (touched, new, removed, untouched) == want and len(bad) == 1 and "drift.obj" in bad[0]
    status.append(good)
    print(f"{'ok' if good else 'FAIL'}: the object compare: {len(touched)} touched, {untouched} untouched, "
          f"{len(new)} new, {len(removed)} gone, failures {bad}")

    print(f"\nselftest: {sum(status)} of {len(status)} passed")
    return 0 if all(status) else 1


def main():
    a = sys.argv[1:]
    if a[:1] == ["selftest"] and len(a) == 2:
        return selftest(a[1])
    if a[:1] == ["info"] and len(a) == 2:
        return info(a[1])
    if a[:1] == ["record"] and len(a) in (4, 5):
        return record(a[1], a[2], a[3], float(a[4]) if len(a) == 5 else None)
    if a[:1] == ["baseline"] and (len(a) == 3 or (len(a) == 4 and a[3] in ("strict", "structural"))):
        gate = a[3] if len(a) == 4 else "strict"
        print(f"### FEX: my build of the engine's commit against the engine's files ({gate} gate for the .so files)")
        return finish(run_files(a[1], a[2], True, gate))
    if a[:1] == ["patched"] and len(a) >= 5:
        mine, engine, base_rec, head_rec = a[1:5]
        print("### FEX: this commit against the engine's files and my base build")
        failed = run_files(mine, engine, False)
        with open(os.path.join(base_rec, "record.json")) as f:
            base = json.load(f)
        with open(os.path.join(head_rec, "record.json")) as f:
            head = json.load(f)
        result = compare_objects(base, head, a[5:])
        print_objects(*result)
        if result[0]:
            failed.append("objects")
        return finish(failed)
    print("usage: compare.py baseline <mine> <engine> [strict | structural]\n"
          "       compare.py record <record dir> <source dir> <build dir> [<since>]\n"
          "       compare.py patched <mine> <engine> <base record> <head record> [<changed path>...]\n"
          "       compare.py info <dll>\n"
          "       compare.py selftest <engine>", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
