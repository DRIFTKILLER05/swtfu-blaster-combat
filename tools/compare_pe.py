"""Compares two builds of a 32-bit Windows DLL, ignoring only the fields MSVC stamps into every build: the link time
(COFF header, export and debug directories), the PE checksum, the PDB id (CodeView GUID + age) and the
POGO / repro debug records. Everything else (code, data, imports, exports, resources, layout) must match.

  python tools\\compare_pe.py bin\\DK_Blaster.dll <released>\\DK_Blaster.dll
Exit code 0 = same build, 1 = different."""
import struct, sys


def masked(path):
    b = bytearray(open(path, "rb").read())
    pe = struct.unpack_from("<I", b, 0x3C)[0]
    nsec, = struct.unpack_from("<H", b, pe + 6)
    opt = pe + 24
    secs = [struct.unpack_from("<IIII", b, opt + 224 + 40 * i + 8) for i in range(nsec)]   # vsize, va, rawsize, raw

    def off(rva):
        for vs, va, rs, raw in secs:
            if va <= rva < va + max(vs, rs):
                return raw + rva - va
        return None
    b[pe + 8:pe + 12] = bytes(4)             # COFF TimeDateStamp
    b[opt + 64:opt + 68] = bytes(4)          # CheckSum
    exp_rva, = struct.unpack_from("<I", b, opt + 96)
    if exp_rva and off(exp_rva) is not None:
        b[off(exp_rva) + 4:off(exp_rva) + 8] = bytes(4)
    dbg_rva, dbg_size = struct.unpack_from("<II", b, opt + 96 + 6 * 8)
    if dbg_rva and off(dbg_rva) is not None:
        d = off(dbg_rva)
        for i in range(dbg_size // 28):
            e = d + 28 * i
            b[e + 4:e + 8] = bytes(4)
            typ, size, _, raw = struct.unpack_from("<IIII", b, e + 12)
            if typ == 2 and b[raw:raw + 4] == b"RSDS":
                b[raw + 4:raw + 24] = bytes(20)
            elif typ in (13, 16):            # POGO, repro hash
                b[raw:raw + size] = bytes(size)
    return bytes(b)


def main(a_path, b_path):
    a, b = masked(a_path), masked(b_path)
    diff = [i for i in range(min(len(a), len(b))) if a[i] != b[i]]
    if len(a) == len(b) and not diff:
        print("SAME BUILD: %s = %s (%d bytes; only build stamps differ)" % (a_path, b_path, len(a)))
        return 0
    print("DIFFERENT: %s (%d bytes) vs %s (%d bytes), %d bytes differ%s" % (
        a_path, len(a), b_path, len(b), len(diff), ", first at file offset 0x%x" % diff[0] if diff else ""))
    return 1


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2]))
