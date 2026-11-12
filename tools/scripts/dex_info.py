#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""List classes and methods of a dex file (or the classes.dex inside an APK).

Usage: dex_info.py <file.apk|classes.dex> [class-prefix] [--natives] [--code|--full]

Prints "flags class.method(signature)" lines; --natives limits the output to
native methods (the game's Java -> C++ entry points). --code adds a summary
of each method body: invokes, field accesses, string constants and branches.
--full disassembles every instruction with its registers and constants.
"""
import struct
import sys
import zipfile

ACC = [(0x1, "public"), (0x2, "private"), (0x4, "protected"), (0x8, "static"), (0x10, "final"),
       (0x20, "synchronized"), (0x100, "native"), (0x400, "abstract")]


def uleb(data, off):
    result = shift = 0
    while True:
        b = data[off]
        off += 1
        result |= (b & 0x7F) << shift
        if b < 0x80:
            return result, off
        shift += 7


# Size in 16-bit code units of each Dalvik opcode.
SIZES = [1] * 256
for op, n in {0x02: 2, 0x03: 3, 0x05: 2, 0x06: 3, 0x08: 2, 0x09: 3, 0x13: 2, 0x14: 3, 0x15: 2, 0x16: 2,
              0x17: 3, 0x18: 5, 0x19: 2, 0x1a: 2, 0x1b: 3, 0x1c: 2, 0x1f: 2, 0x20: 2, 0x22: 2, 0x23: 2,
              0x24: 3, 0x25: 3, 0x26: 3, 0x29: 2, 0x2a: 3, 0x2b: 3, 0x2c: 3, 0xfa: 4, 0xfb: 4, 0xfc: 3,
              0xfd: 3, 0xfe: 2, 0xff: 2}.items():
    SIZES[op] = n
for lo, hi, n in ((0x2d, 0x3d, 2), (0x44, 0x6d, 2), (0x6e, 0x72, 3), (0x74, 0x78, 3), (0x90, 0xaf, 2),
                  (0xd0, 0xe2, 2)):
    for op in range(lo, hi + 1):
        SIZES[op] = n
INVOKE = {0x6e: "invoke-virtual", 0x6f: "invoke-super", 0x70: "invoke-direct", 0x71: "invoke-static",
          0x72: "invoke-interface", 0x74: "invoke-virtual", 0x75: "invoke-super", 0x76: "invoke-direct",
          0x77: "invoke-static", 0x78: "invoke-interface"}
IFS = ["if-eq", "if-ne", "if-lt", "if-ge", "if-gt", "if-le", "if-eqz", "if-nez", "if-ltz", "if-gez", "if-gtz", "if-lez"]


OPNAMES = {}
FORMATS = {}


def _ops(fmt, start, names):
    for i, nm in enumerate(names.split()):
        OPNAMES[start + i] = nm
        FORMATS[start + i] = fmt


_ops("10x", 0x00, "nop")
_ops("12x", 0x01, "move")
_ops("22x", 0x02, "move/from16")
_ops("12x", 0x04, "move-wide")
_ops("22x", 0x05, "move-wide/from16")
_ops("12x", 0x07, "move-object")
_ops("22x", 0x08, "move-object/from16")
_ops("11x", 0x0a, "move-result move-result-wide move-result-object move-exception")
_ops("10x", 0x0e, "return-void")
_ops("11x", 0x0f, "return return-wide return-object")
_ops("11n", 0x12, "const/4")
_ops("21s", 0x13, "const/16")
_ops("31i", 0x14, "const")
_ops("21h", 0x15, "const/high16")
_ops("21s", 0x16, "const-wide/16")
_ops("31i", 0x17, "const-wide/32")
_ops("51l", 0x18, "const-wide")
_ops("21h", 0x19, "const-wide/high16")
_ops("21c", 0x1a, "const-string")
_ops("31c", 0x1b, "const-string/jumbo")
_ops("21c", 0x1c, "const-class")
_ops("11x", 0x1d, "monitor-enter monitor-exit")
_ops("21c", 0x1f, "check-cast")
_ops("22c", 0x20, "instance-of")
_ops("12x", 0x21, "array-length")
_ops("21c", 0x22, "new-instance")
_ops("22c", 0x23, "new-array")
_ops("35c", 0x24, "filled-new-array")
_ops("3rc", 0x25, "filled-new-array/range")
_ops("31t", 0x26, "fill-array-data")
_ops("11x", 0x27, "throw")
_ops("10t", 0x28, "goto")
_ops("20t", 0x29, "goto/16")
_ops("30t", 0x2a, "goto/32")
_ops("31t", 0x2b, "packed-switch sparse-switch")
_ops("23x", 0x2d, "cmpl-float cmpg-float cmpl-double cmpg-double cmp-long")
_ops("22t", 0x32, "if-eq if-ne if-lt if-ge if-gt if-le")
_ops("21t", 0x38, "if-eqz if-nez if-ltz if-gez if-gtz if-lez")
_ops("23x", 0x44, "aget aget-wide aget-object aget-boolean aget-byte aget-char aget-short "
                  "aput aput-wide aput-object aput-boolean aput-byte aput-char aput-short")
_ops("22c", 0x52, "iget iget-wide iget-object iget-boolean iget-byte iget-char iget-short "
                  "iput iput-wide iput-object iput-boolean iput-byte iput-char iput-short")
_ops("21c", 0x60, "sget sget-wide sget-object sget-boolean sget-byte sget-char sget-short "
                  "sput sput-wide sput-object sput-boolean sput-byte sput-char sput-short")
_ops("35c", 0x6e, "invoke-virtual invoke-super invoke-direct invoke-static invoke-interface")
_ops("3rc", 0x74, "invoke-virtual/range invoke-super/range invoke-direct/range invoke-static/range "
                  "invoke-interface/range")
_ops("12x", 0x7b, "neg-int not-int neg-long not-long neg-float neg-double int-to-long int-to-float "
                  "int-to-double long-to-int long-to-float long-to-double float-to-int float-to-long "
                  "float-to-double double-to-int double-to-long double-to-float int-to-byte int-to-char "
                  "int-to-short")
_BIN = ("add sub mul div rem and or xor shl shr ushr", "add sub mul div rem and or xor shl shr ushr",
        "add sub mul div rem", "add sub mul div rem")
for _i, (_t, _start, _start2) in enumerate(zip(("int", "long", "float", "double"), (0x90, 0x9b, 0xa6, 0xab),
                                                (0xb0, 0xbb, 0xc6, 0xcb))):
    _ops("23x", _start, " ".join(f"{x}-{_t}" for x in _BIN[_i].split()))
    _ops("12x", _start2, " ".join(f"{x}-{_t}/2addr" for x in _BIN[_i].split()))
_ops("22s", 0xd0, "add-int/lit16 rsub-int mul-int/lit16 div-int/lit16 rem-int/lit16 and-int/lit16 "
                  "or-int/lit16 xor-int/lit16")
_ops("22b", 0xd8, "add-int/lit8 rsub-int/lit8 mul-int/lit8 div-int/lit8 rem-int/lit8 and-int/lit8 "
                  "or-int/lit8 xor-int/lit8 shl-int/lit8 shr-int/lit8 ushr-int/lit8")


class Dex:
    def __init__(self, data):
        self.d = data
        (self.string_ids_size, self.string_ids_off, self.type_ids_size, self.type_ids_off, self.proto_ids_size,
         self.proto_ids_off, self.field_ids_size, self.field_ids_off, self.method_ids_size, self.method_ids_off,
         self.class_defs_size, self.class_defs_off) = struct.unpack_from("<12I", data, 0x38)

    def string(self, i):
        off = struct.unpack_from("<I", self.d, self.string_ids_off + 4 * i)[0]
        _, off = uleb(self.d, off)
        end = self.d.index(b"\0", off)
        return self.d[off:end].decode("utf-8", "replace")

    def type(self, i):
        return self.string(struct.unpack_from("<I", self.d, self.type_ids_off + 4 * i)[0])

    def proto(self, i):
        _, ret, params = struct.unpack_from("<III", self.d, self.proto_ids_off + 12 * i)
        args = []
        if params:
            n = struct.unpack_from("<I", self.d, params)[0]
            args = [self.type(struct.unpack_from("<H", self.d, params + 4 + 2 * k)[0]) for k in range(n)]
        return "(" + "".join(args) + ")" + self.type(ret)

    def method(self, i):
        cls, proto, name = struct.unpack_from("<HHI", self.d, self.method_ids_off + 8 * i)
        return self.type(cls), self.string(name), self.proto(proto)

    def field(self, i):
        cls, typ, name = struct.unpack_from("<HHI", self.d, self.field_ids_off + 8 * i)
        return f"{self.type(cls)[1:-1]}.{self.string(name)}:{self.type(typ)}"

    def code(self, off):
        """Yields (pc, text) for the interesting instructions of a code item."""
        n = struct.unpack_from("<I", self.d, off + 12)[0]
        base = off + 16
        u = lambda k: struct.unpack_from("<H", self.d, base + 2 * k)[0]
        pc = 0
        while pc < n:
            w = u(pc)
            op = w & 0xFF
            if op == 0 and w in (0x100, 0x200, 0x300):  # switch / array payloads
                if w == 0x100:
                    size = 4 + u(pc + 1) * 2
                elif w == 0x200:
                    size = 2 + u(pc + 1) * 4
                else:
                    width = u(pc + 1)
                    count = u(pc + 2) | u(pc + 3) << 16
                    size = 4 + (width * count + 1) // 2
                pc += size
                continue
            if op in INVOKE:
                cls, name, sig = self.method(u(pc + 1))
                yield pc, f"{INVOKE[op]} {cls[1:-1]}.{name}{sig}"
            elif op in (0x1a, 0x1b):
                idx = u(pc + 1) if op == 0x1a else u(pc + 1) | u(pc + 2) << 16
                yield pc, f"const-string {self.string(idx)!r}"
            elif 0x52 <= op <= 0x6d:
                kind = ["iget", "iput", "sget", "sput"][(op - 0x52) // 7]
                yield pc, f"{kind} {self.field(u(pc + 1))}"
            elif op == 0x22:
                yield pc, f"new-instance {self.type(u(pc + 1))[1:-1]}"
            elif 0x32 <= op <= 0x3d:
                off16 = struct.unpack_from("<h", self.d, base + 2 * (pc + 1))[0]
                yield pc, f"{IFS[op - 0x32]} -> {pc + off16:#x}"
            elif op in (0x28, 0x29):
                off = (w >> 8) if op == 0x28 else u(pc + 1)
                off = off - 256 if op == 0x28 and off > 127 else (off - 65536 if op == 0x29 and off > 32767 else off)
                yield pc, f"goto -> {pc + off:#x}"
            elif op in (0x0e, 0x0f, 0x10, 0x11):
                yield pc, "return"
            elif op == 0x27:
                yield pc, "throw"
            pc += SIZES[op]

    def full(self, off):
        """Yields (pc, text) for every instruction of a code item."""
        n = struct.unpack_from("<I", self.d, off + 12)[0]
        base = off + 16
        u = lambda k: struct.unpack_from("<H", self.d, base + 2 * k)[0]
        s16 = lambda k: struct.unpack_from("<h", self.d, base + 2 * k)[0]
        s32 = lambda k: struct.unpack_from("<i", self.d, base + 2 * k)[0]
        pc = 0
        while pc < n:
            w = u(pc)
            op, aa = w & 0xFF, w >> 8
            a4, b4 = aa & 0xF, aa >> 4
            name = OPNAMES.get(op, f"op{op:02x}")
            if op == 0 and w in (0x100, 0x200, 0x300):
                if w == 0x100:
                    size, first = u(pc + 1), s32(pc + 2)
                    yield pc, f"packed-switch-payload first={first} targets(rel)={[s32(pc + 4 + 2 * i) for i in range(size)]}"
                    pc += 4 + size * 2
                elif w == 0x200:
                    size = u(pc + 1)
                    keys = [s32(pc + 2 + 2 * i) for i in range(size)]
                    tg = [s32(pc + 2 + 2 * size + 2 * i) for i in range(size)]
                    yield pc, f"sparse-switch-payload {dict(zip(keys, tg))}"
                    pc += 2 + size * 4
                else:
                    width, count = u(pc + 1), u(pc + 2) | u(pc + 3) << 16
                    raw = self.d[base + 2 * (pc + 4): base + 2 * (pc + 4) + width * count]
                    vals = [int.from_bytes(raw[i * width:(i + 1) * width], "little", signed=True) for i in range(count)]
                    yield pc, f"array-payload w={width} {vals}"
                    pc += 4 + (width * count + 1) // 2
                continue
            fmt = FORMATS.get(op, "10x")
            if fmt == "10x": t = name
            elif fmt == "12x": t = f"{name} v{a4}, v{b4}"
            elif fmt == "11n": t = f"{name} v{a4}, #{(b4 ^ 8) - 8}"
            elif fmt == "11x": t = f"{name} v{aa}"
            elif fmt == "10t": t = f"{name} -> {pc + ((aa ^ 0x80) - 0x80):#x}"
            elif fmt == "20t": t = f"{name} -> {pc + s16(pc + 1):#x}"
            elif fmt == "30t": t = f"{name} -> {pc + s32(pc + 1):#x}"
            elif fmt == "22x": t = f"{name} v{aa}, v{u(pc + 1)}"
            elif fmt == "21t": t = f"{name} v{aa} -> {pc + s16(pc + 1):#x}"
            elif fmt == "21s": t = f"{name} v{aa}, #{s16(pc + 1)}"
            elif fmt == "21h":
                sh = 16 if op == 0x15 else 48
                v = s16(pc + 1) << sh
                t = f"{name} v{aa}, #{v:#x}" + (f" ({struct.unpack('<f', struct.pack('<i', v))[0]}f)" if op == 0x15 else "")
            elif fmt == "21c":
                idx = u(pc + 1)
                if op == 0x1a: ref = repr(self.string(idx))
                elif op in (0x1c, 0x1f, 0x22): ref = self.type(idx)
                else: ref = self.field(idx)
                t = f"{name} v{aa}, {ref}"
            elif fmt == "23x": t = f"{name} v{aa}, v{u(pc + 1) & 0xFF}, v{u(pc + 1) >> 8}"
            elif fmt == "22b": t = f"{name} v{aa}, v{u(pc + 1) & 0xFF}, #{((u(pc + 1) >> 8) ^ 0x80) - 0x80}"
            elif fmt == "22t": t = f"{name} v{a4}, v{b4} -> {pc + s16(pc + 1):#x}"
            elif fmt == "22s": t = f"{name} v{a4}, v{b4}, #{s16(pc + 1)}"
            elif fmt == "22c":
                idx = u(pc + 1)
                ref = self.type(idx) if op in (0x20, 0x23) else self.field(idx)
                t = f"{name} v{a4}, v{b4}, {ref}"
            elif fmt == "31i":
                v = s32(pc + 1)
                t = f"{name} v{aa}, #{v} ({v & 0xFFFFFFFF:#x})"
            elif fmt == "31t": t = f"{name} v{aa}, payload@{pc + s32(pc + 1):#x}"
            elif fmt == "31c": t = f"{name} v{aa}, {self.string(u(pc + 1) | u(pc + 2) << 16)!r}"
            elif fmt == "51l": t = f"{name} v{aa}, #{struct.unpack_from('<q', self.d, base + 2 * (pc + 1))[0]}"
            elif fmt in ("35c", "3rc"):
                idx = u(pc + 1)
                if fmt == "35c":
                    cnt, g, dc = b4, a4, u(pc + 2)
                    regs = [dc & 0xF, dc >> 4 & 0xF, dc >> 8 & 0xF, dc >> 12, g][:cnt]
                else:
                    regs = list(range(u(pc + 2), u(pc + 2) + aa))
                if op == 0x24 or op == 0x25: ref = self.type(idx)
                else:
                    c, m, sig = self.method(idx)
                    ref = f"{c[1:-1]}.{m}{sig}"
                t = f"{name} {{{', '.join(f'v{r}' for r in regs)}}}, {ref}"
            else: t = name
            yield pc, t
            pc += SIZES[op]

    def classes(self):
        for c in range(self.class_defs_size):
            cls, flags, sup, _, _, _, data_off, _ = struct.unpack_from("<8I", self.d, self.class_defs_off + 32 * c)
            methods = []
            if data_off:
                off = data_off
                sf, off = uleb(self.d, off)
                inf, off = uleb(self.d, off)
                dm, off = uleb(self.d, off)
                vm, off = uleb(self.d, off)
                for _ in range(sf + inf):
                    _, off = uleb(self.d, off)
                    _, off = uleb(self.d, off)
                for count in (dm, vm):
                    idx = 0
                    for _ in range(count):
                        diff, off = uleb(self.d, off)
                        acc, off = uleb(self.d, off)
                        code_off, off = uleb(self.d, off)
                        idx += diff
                        methods.append((acc, self.method(idx), code_off))
            yield self.type(cls), (self.type(sup) if sup != 0xFFFFFFFF else None), methods


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__, file=sys.stderr)
        return 2
    natives = "--natives" in sys.argv
    show_code = "--code" in sys.argv
    show_full = "--full" in sys.argv
    prefix = args[1] if len(args) > 1 else ""
    path = args[0]
    if path.endswith(".apk"):
        with zipfile.ZipFile(path) as z:
            data = z.read("classes.dex")
    else:
        data = open(path, "rb").read()
    dex = Dex(data)
    for cls, sup, methods in dex.classes():
        name = cls[1:-1]
        if not name.startswith(prefix):
            continue
        if not natives:
            print(f"class {name} extends {sup[1:-1] if sup else '-'}")
        for acc, (_, mname, sig), code_off in methods:
            if natives and not acc & 0x100:
                continue
            flags = " ".join(n for bit, n in ACC if acc & bit)
            print(f"  {flags} {name}.{mname}{sig}")
            if (show_code or show_full) and code_off:
                for pc, text in (dex.full if show_full else dex.code)(code_off):
                    print(f"      {pc:04x}  {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
