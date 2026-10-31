"""Extract an Xbox / Xbox 360 XDVDFS disc image (XGD1/2/3) into a folder.
usage: xdvdfs_extract.py IMAGE OUTDIR [--list]"""
import os, struct, sys
SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
OFFSETS = [0, 0xFD90000, 0x2080000, 0x18300000]   # plain, XGD2, XGD3, XGD1 video partition

def find_base(f):
    for base in OFFSETS:
        f.seek(base + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return base
    sys.exit("no XDVDFS volume found")

def walk(f, base, sector, size, prefix, out):
    if size == 0:
        return
    f.seek(base + sector * SECTOR); data = f.read(size)
    stack = [0]; seen = set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        left, right, start, length, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if left == 0xFFFF and right == 0xFFFF:
            continue
        name = data[off + 14: off + 14 + nlen].decode("ascii", "replace")
        if left: stack.append(left * 4)
        if right: stack.append(right * 4)
        path = prefix + "/" + name if prefix else name
        if attr & 0x10:
            out.append((path, None, None))
            walk(f, base, start, length, path, out)
        else:
            out.append((path, start, length))

def main():
    args = [a for a in sys.argv[1:] if a != "--list"]
    if len(args) != 2:
        sys.exit(__doc__)
    img, dst = args
    listing = "--list" in sys.argv
    with open(img, "rb") as f:
        base = find_base(f)
        f.seek(base + 32 * SECTOR + 20)
        root, rsize = struct.unpack("<II", f.read(8))
        entries = []
        walk(f, base, root, rsize, "", entries)
        files = [e for e in entries if e[1] is not None]
        total = sum(e[2] for e in files)
        print(f"volume at 0x{base:X}: {len(files)} files, {len([e for e in entries if e[1] is None])} folders, {total/2**30:.2f} GB")
        if listing:
            for p, s, n in entries[:40]:
                print(("  dir  " if s is None else f"  {n:>11} ") + p)
            return
        done = 0
        for p, s, n in entries:
            out = os.path.join(dst, *p.split("/"))
            if s is None:
                os.makedirs(out, exist_ok=True); continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            f.seek(base + s * SECTOR)
            with open(out, "wb") as o:
                left = n
                while left:
                    chunk = f.read(min(left, 8 << 20))
                    if not chunk: sys.exit(f"short read in {p}")
                    o.write(chunk); left -= len(chunk)
            done += n
        print(f"extracted {done/2**30:.2f} GB to {dst}")

main()
