#!/usr/bin/env python3
"""Analyse `[MS]` survivor dumps produced by MemTrace::dumpSurvivors (esp32dev_memtrace env).

Usage:
    rtk python3 scripts/analyze_survivors.py logBootN.txt [--dump K] [--top N] [--elf PATH]

Reads the K-th dump (default: last), sorts the live blocks by address, computes the gaps between
consecutive blocks and prints the largest gaps with the blocks that border them (decoded with
addr2line). The blocks bordering the biggest gaps are the permanent objects that split the heap.
Only blocks >= 48 B are tracked, so gaps are an upper bound of the real free runs.
"""
import argparse
import collections
import os
import re
import subprocess
import sys

A2L = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-addr2line")
HEAP_OVERHEAD = 8  # approx. per-block header in the ESP-IDF heap


def parse_dumps(path):
    dumps, cur = [], None
    with open(path, errors="replace") as fh:
        for line in fh:
            if "[MS-BEGIN]" in line:
                cur = {"tag": line.split("[MS-BEGIN]")[1].strip(), "blocks": [], "sum": ""}
                dumps.append(cur)
            elif cur is not None and "[MS-SUM]" in line:
                cur["sum"] = line.split("[MS-SUM]")[1].strip()
                cur = None
            elif cur is not None:
                m = re.search(r"\[MS\] (0x[0-9a-f]+) (\d+) (0x[0-9a-f]+) (0x[0-9a-f]+) (0x[0-9a-f]+)", line)
                if m:
                    cur["blocks"].append((int(m.group(1), 16), int(m.group(2)),
                                          [int(m.group(i), 16) for i in (3, 4, 5)]))
    return dumps


def decode(elf, pcs):
    cache = {}
    uniq = sorted({p for p in pcs if p > 0x40000000})
    if not uniq or not os.path.exists(elf):
        return cache
    out = subprocess.run([A2L, "-pfiaC", "-e", elf] + [hex(p) for p in uniq],
                         capture_output=True, text=True).stdout.strip().splitlines()
    for p, line in zip(uniq, out):
        cache[p] = re.sub(r"^0x[0-9a-f]+: ", "", line).split(" at ")[0][:70]
    return cache


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log")
    ap.add_argument("--dump", type=int, default=-1)
    ap.add_argument("--top", type=int, default=8)
    ap.add_argument("--elf", default=".pio/build/esp32dev_memtrace/firmware.elf")
    a = ap.parse_args()

    dumps = parse_dumps(a.log)
    if not dumps:
        sys.exit("no [MS-BEGIN] dump found in log")
    d = dumps[a.dump]
    blocks = sorted(d["blocks"])
    print(f"dump '{d['tag']}' ({len(dumps)} in log): {len(blocks)} live blocks >= 48 B | {d['sum']}")

    pcs = [pc for _, _, ps in blocks for pc in ps]
    sym = decode(a.elf, pcs)

    def name(ps):
        return " <- ".join(sym.get(p, hex(p)) for p in ps[:3] if p)

    gaps = []
    for (p0, s0, _), (p1, s1, ps1) in zip(blocks, blocks[1:]):
        end0 = p0 + s0 + HEAP_OVERHEAD
        if p1 > end0:
            gaps.append((p1 - end0, end0, p0, s0, p1, s1))
    gaps.sort(reverse=True)

    print(f"\nTop {a.top} gaps (upper bound of free runs):")
    by_addr = {b[0]: b for b in blocks}
    for g in gaps[:a.top]:
        size, start, p0, s0, p1, s1 = g
        print(f"\n  gap {size:6d} B @0x{start:08x}")
        print(f"    below : 0x{p0:08x} {s0:5d} B  {name(by_addr[p0][2])}")
        print(f"    above : 0x{p1:08x} {s1:5d} B  {name(by_addr[p1][2])}")

    print("\nLive bytes by allocation site (frame #3 chain), top 15:")
    agg = collections.Counter()
    cnt = collections.Counter()
    for _, s, ps in blocks:
        k = name(ps)
        agg[k] += s
        cnt[k] += 1
    for k, v in agg.most_common(15):
        print(f"  {v:7d} B  x{cnt[k]:<3d} {k}")


if __name__ == "__main__":
    main()
