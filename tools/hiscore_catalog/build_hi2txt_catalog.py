#!/usr/bin/env python3
"""
Offline compiler & verifier for the embedded hi2txt high-score catalog
(`_HI2TXT_CATALOG_B85` inside `arcadematrix_companion_sync.py`).

Zero external dependencies (Python 3 standard library only).

Why an embedded compressed blob instead of a sidecar `.json`?
- `ArcadeMatrix_RPI/src/core/ssh_installer.rs` embeds `arcadematrix_companion_sync.py`
  at compile time via `include_str!("../../tools/arcadematrix_companion_sync.py")`.
- `tools/recalbox_daemon/install.sh` and `install.ps1` deploy a single standalone
  `arcadematrix_companion_sync.py` file over SSH onto Recalbox, Batocera, and RetroPie.

Usage:
  # Compile from GreatStoneEx/hi2txt-xml (cloned temporarily into /tmp and deleted)
  # and update tools/recalbox_daemon/arcadematrix_companion_sync.py + tools/arcadematrix_companion_sync.py:
  python3 tools/hiscore_catalog/build_hi2txt_catalog.py

  # Compile from an existing local checkout of hi2txt-xml:
  python3 tools/hiscore_catalog/build_hi2txt_catalog.py --xml-repo /tmp/hi2txt-xml

  # Determinism & integrity check (offline round-trip + SHA-256 parity across copies,
  # or full XML recompilation check when combined with --xml-repo / --online):
  python3 tools/hiscore_catalog/build_hi2txt_catalog.py --verify
"""

import argparse
import base64
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib

HI2TXT_GIT_URL = "https://github.com/GreatStoneEx/hi2txt-xml.git"
LINE_WIDTH = 100
FORBIDDEN_TOKENS = ("{{DMD_HOST}}", "{{BROKER}}", "{{TOPIC}}")

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
COMPANION_TARGETS = (
    os.path.join(REPO_ROOT, "tools", "recalbox_daemon", "arcadematrix_companion_sync.py"),
    os.path.join(REPO_ROOT, "tools", "arcadematrix_companion_sync.py"),
)


def load_dtd_entities(dtd_path):
    """Loads custom character entity definitions from hi2txt.dtd."""
    entities = {}
    if not os.path.isfile(dtd_path):
        return entities
    with open(dtd_path, "r", encoding="utf-8") as f:
        content = f.read()
    for m in re.finditer(r'<!ENTITY\s+([A-Za-z0-9_-]+)\s+"([^"]+)">', content):
        val = re.sub(r"&#(\d+);", lambda mm: chr(int(mm.group(1))), m.group(2))
        entities[m.group(1)] = val
    return entities


def parse_hi2txt_xml(path, entities):
    """Parses a single hi2txt XML definition using stdlib ElementTree."""
    with open(path, "r", encoding="utf-8") as f:
        raw = f.read()
    raw = re.sub(r"<!DOCTYPE[^>]*>", "", raw)

    def _repl_ent(m):
        ename = m.group(1)
        if ename in ("amp", "lt", "gt", "apos", "quot"):
            return m.group(0)
        ch = entities.get(ename, " ")
        if ch in ("&", "<", ">", '"', "'"):
            return f"&#{ord(ch)};"
        return ch

    raw = re.sub(r"&([A-Za-z0-9_-]+);", _repl_ent, raw)
    return ET.fromstring(raw)


def parse_int_auto(val, default=None):
    if val is None:
        return default
    s = str(val).strip()
    if not s:
        return default
    try:
        return int(s, 0)
    except Exception:
        return default


def extract_scale_div_add(fmt_str, formats_map):
    """Extracts integer multiplier, divisor, and additive offset from hi2txt <format> specs."""
    scale = 1
    div_val = 1
    add_val = 0
    if not fmt_str:
        return scale, div_val, add_val
    for tok in fmt_str.split(";"):
        tok = tok.strip()
        if not tok:
            continue
        m_mul = re.match(r"^\*(\d+)$", tok)
        if m_mul:
            scale *= int(m_mul.group(1))
            continue
        m_div = re.match(r"^/(\d+)$", tok)
        if m_div and int(m_div.group(1)) > 0:
            div_val *= int(m_div.group(1))
            continue
        m_add = re.match(r"^\+(-?\d+)$", tok)
        if m_add:
            add_val += int(m_add.group(1))
            continue
        m_sub = re.match(r"^-(\d+)$", tok)
        if m_sub:
            add_val -= int(m_sub.group(1))
            continue
        if tok in formats_map:
            fnode = formats_map[tok]
            for child in fnode:
                txt = (child.text or "").strip()
                if child.tag == "multiply" and txt.lstrip("-").isdigit():
                    scale *= int(txt)
                elif child.tag in ("divide", "divide_trunc") and txt.isdigit() and int(txt) > 0:
                    div_val *= int(txt)
                elif child.tag == "add" and txt.lstrip("-").isdigit():
                    add_val += int(txt)
                elif child.tag == "substract" and txt.lstrip("-").isdigit():
                    add_val -= int(txt)
                elif child.tag in ("suffix", "postfix") and txt.isdigit() and set(txt) == {"0"}:
                    scale *= 10 ** len(txt)
    return scale, div_val, add_val


def normalize_charset_char(dst):
    if dst is None:
        return ""
    if len(dst) == 1 and 32 <= ord(dst) < 127:
        return dst
    if dst.strip() == "":
        return " "
    ascii_only = "".join(ch for ch in dst if 32 <= ord(ch) < 127)
    return ascii_only[:1] if ascii_only else "."


def parse_charset(cs_node, ascii_offset=0, ascii_step=1):
    """
    Stores only non-standard / remapped bytes from <charset>, preserving the printable
    ASCII fallback `32 <= (b // ascii_step) + ascii_offset < 127` for unlisted bytes.
    """
    cmap = {}
    if cs_node is None:
        return cmap
    step = max(1, ascii_step)
    for ch_el in cs_node.findall("char"):
        src = parse_int_auto(ch_el.attrib.get("src"))
        if src is None or not (0 <= src <= 255):
            continue
        dst = normalize_charset_char(ch_el.attrib.get("dst", ""))
        default_code = (src // step) + ascii_offset
        default_ch = chr(default_code) if 32 <= default_code < 127 else ""
        if dst != default_ch:
            cmap[str(src)] = dst
    return cmap


def parse_bitmask_byte_perm(bm_node):
    """Extracts pure byte-permutation bitmasks (e.g. Irem M72 R-Type [1, 0, 2] score byte order)."""
    if bm_node is None:
        return None
    perm = []
    for ch_el in bm_node.findall("character"):
        mask_str = re.sub(r"\s+", "", ch_el.attrib.get("mask", ""))
        if not mask_str or len(mask_str) % 8 != 0 or (set(mask_str) - {"0", "1"}):
            return None
        nbytes = len(mask_str) // 8
        chunks = [mask_str[i * 8 : (i + 1) * 8] for i in range(nbytes)]
        ones = [idx for idx, ck in enumerate(chunks) if ck == "11111111"]
        zeros = [idx for idx, ck in enumerate(chunks) if ck == "00000000"]
        if len(ones) == 1 and len(zeros) == nbytes - 1:
            perm.append(ones[0])
        else:
            return None
    return perm if perm else None


def parse_byte_skip(bs):
    if not bs:
        return None
    bs = bs.strip()
    if bs == "odd":
        return "o"
    if bs == "even":
        return "e"
    if bs.startswith("0x") or bs.startswith("0X"):
        return parse_int_auto(bs)
    if set(bs) <= {"0", "1"} and len(bs) >= 2:
        return bs
    return parse_int_auto(bs)


def intern_charset(cs_id, a_off, a_step, charsets_xml, charset_pool, charset_index):
    if not cs_id or cs_id not in charsets_xml:
        return None
    cmap = parse_charset(charsets_xml[cs_id], ascii_offset=a_off, ascii_step=a_step or 1)
    if not cmap:
        return None
    ckey = json.dumps(cmap, sort_keys=True, separators=(",", ":"))
    if ckey not in charset_index:
        charset_index[ckey] = len(charset_pool)
        charset_pool.append(cmap)
    return charset_index[ckey]


def build_score_spec(
    elt,
    rel_off,
    fmt_str,
    formats_map,
    charsets_xml,
    bitmasks_xml,
    charset_pool,
    charset_index,
    extra_scale=1,
):
    sz = parse_int_auto(elt.attrib.get("size"), 0)
    if sz <= 0:
        return None
    tp = elt.attrib.get("type", "raw").strip()
    dp = elt.attrib.get("decoding-profile", "null").strip()
    base = elt.attrib.get("base", "").strip()
    end = elt.attrib.get("endianness", "default").strip()
    ns = elt.attrib.get("nibble-skip", "null").strip()
    bs = parse_byte_skip(elt.attrib.get("byte-skip", ""))
    btrim = parse_int_auto(elt.attrib.get("byte-trim"))
    bswap = parse_int_auto(elt.attrib.get("byte-swap"))
    bm_id = elt.attrib.get("bitmask", "").strip()
    cs_id = elt.attrib.get("charset", "").strip()
    a_off = parse_int_auto(elt.attrib.get("ascii-offset"), 0) or 0

    le = (end == "little_endian") or (dp == "bcd-le")
    if dp in ("bcd", "bcd-le"):
        enc = "b"
        ns_code = "o"
    elif tp == "text":
        enc = "t"
        ns_code = "o" if ns == "odd" else ("e" if ns == "even" else None)
    elif base == "16":
        enc = "b"
        ns_code = "o" if ns == "odd" else ("e" if ns == "even" else None)
    elif base == "256":
        enc = "u"
        ns_code = "o" if ns == "odd" else ("e" if ns == "even" else None)
    else:
        enc = "i"
        ns_code = "o" if ns == "odd" else ("e" if ns == "even" else None)

    comb_fmt = ";".join(x for x in (elt.attrib.get("format", ""), fmt_str or "") if x)
    scale, div_val, add_val = extract_scale_div_add(comb_fmt, formats_map)
    scale *= extra_scale

    spec = {"z": sz, "e": enc}
    if rel_off:
        spec["p"] = rel_off
    if le:
        spec["l"] = 1
    if bs is not None:
        spec["k"] = bs
    if ns_code:
        spec["n"] = ns_code
    if bswap and bswap > 1:
        spec["w"] = bswap
    if bm_id and bm_id in bitmasks_xml:
        perm = parse_bitmask_byte_perm(bitmasks_xml[bm_id])
        if perm:
            spec["b"] = perm
    if btrim is not None:
        spec["r"] = btrim
    if enc == "t":
        if a_off:
            spec["f"] = a_off
        cidx = intern_charset(cs_id, a_off, 1, charsets_xml, charset_pool, charset_index)
        if cidx is not None:
            spec["c"] = cidx
    if scale != 1:
        spec["x"] = scale
    if div_val != 1:
        spec["v"] = div_val
    if add_val != 0:
        spec["a"] = add_val
    return spec


def build_name_spec(elt, rel_off, charsets_xml, charset_pool, charset_index):
    sz = parse_int_auto(elt.attrib.get("size"), 0)
    if sz <= 0:
        return None
    dp = elt.attrib.get("decoding-profile", "null").strip()
    end = elt.attrib.get("endianness", "default").strip()
    ns = elt.attrib.get("nibble-skip", "null").strip()
    bs = parse_byte_skip(elt.attrib.get("byte-skip", ""))
    bswap = parse_int_auto(elt.attrib.get("byte-swap"))
    a_off = parse_int_auto(elt.attrib.get("ascii-offset"), 0) or 0
    a_step = parse_int_auto(elt.attrib.get("ascii-step"), 1) or 1
    cs_id = elt.attrib.get("charset", "").strip()

    spec = {"z": sz}
    if rel_off:
        spec["p"] = rel_off
    if dp == "base-40":
        spec["e"] = "r40"
    elif dp == "base-32":
        spec["e"] = "r32"
    if end == "little_endian":
        spec["l"] = 1
    if bs is not None:
        spec["k"] = bs
    if ns in ("odd", "even"):
        spec["n"] = "o" if ns == "odd" else "e"
    if bswap and bswap > 1:
        spec["w"] = bswap
    if a_off != 0:
        spec["f"] = a_off
    if a_step != 1:
        spec["s"] = a_step
    cidx = intern_charset(
        cs_id,
        a_off if dp not in ("base-40", "base-32") else 0,
        a_step,
        charsets_xml,
        charset_pool,
        charset_index,
    )
    if cidx is not None:
        spec["c"] = cidx
    return spec


def compile_structure(st_node, root, charset_pool, charset_index):
    """Compiles a single <structure> node into a deduplicable template dict + expected sizes."""
    formats_map = {f.attrib.get("id"): f for f in root.findall("format") if f.attrib.get("id")}
    charsets_xml = {c.attrib.get("id"): c for c in root.findall("charset") if c.attrib.get("id")}
    bitmasks_xml = {b.attrib.get("id"): b for b in root.findall("bitmask") if b.attrib.get("id")}

    out_ref = st_node.attrib.get("output", "").strip()
    out_node = None
    if out_ref:
        for o in root.findall("output"):
            if o.attrib.get("id", "").strip() == out_ref:
                out_node = o
                break
    if out_node is None:
        out_node = root.find("output")

    table_specs = []
    if out_node is not None:
        tables = [
            tb for tb in out_node.findall("table") if tb.attrib.get("display", "always") == "always"
        ]
        if not tables:
            tables = out_node.findall("table")
        for tb in tables:
            cols = [
                c
                for c in list(tb.findall("column")) + list(tb.findall("field"))
                if c.attrib.get("display", "always") != "debug"
            ]
            s_col = None
            n_col = None
            for c in cols:
                if (c.attrib.get("id") or "").strip().upper() == "SCORE":
                    s_col = c
                    break
            if s_col is None:
                for c in cols:
                    cid = (c.attrib.get("id") or "").strip().upper()
                    if "SCORE" in cid and "ACCURACY" not in cid and "ADDR" not in cid:
                        s_col = c
                        break
            for c in cols:
                if (c.attrib.get("id") or "").strip().upper() in ("NAME", "INITIALS", "PLAYER", "TAG"):
                    n_col = c
                    break
            if s_col is None:
                continue

            s_src = (s_col.attrib.get("src") or s_col.attrib.get("id") or "").strip()
            s_fmt = (s_col.attrib.get("format") or "").strip()
            n_src = (
                (n_col.attrib.get("src") or n_col.attrib.get("id") or "").strip()
                if n_col is not None
                else ""
            )
            lmax = parse_int_auto(tb.attrib.get("lines-max"))

            max_ign = None
            li = tb.attrib.get("line-ignore", "").strip()
            li_op = tb.attrib.get("line-ignore-operator", "").strip()
            if li.upper().startswith("SCORE:") and li_op in (">", ">="):
                iv = parse_int_auto(li.split(":", 1)[1])
                if iv is not None and iv > 0:
                    max_ign = iv if li_op == ">" else iv - 1

            sum_parts = []
            outer_scale = 1
            if s_fmt in formats_map:
                fnode = formats_map[s_fmt]
                sum_el = fnode.find("sum")
                if sum_el is not None:
                    outer_scale, _, _ = extract_scale_div_add(s_fmt, formats_map)
                    for subc in list(sum_el.findall("column")) + list(sum_el.findall("field")):
                        sub_id = (subc.attrib.get("src") or subc.attrib.get("id") or "").strip()
                        sub_fmt = (subc.attrib.get("format") or "").strip()
                        if sub_id:
                            sum_parts.append((sub_id, sub_fmt))
            table_specs.append((s_src, s_fmt, sum_parts, outer_scale, n_src, lmax, max_ign))

        if not table_specs:
            for fld in out_node.findall("field"):
                if (fld.attrib.get("id") or "").strip().upper() in (
                    "SCORE",
                    "TOP SCORE",
                    "HISCORE",
                    "HIGH SCORE",
                ):
                    src = (fld.attrib.get("src") or fld.attrib.get("id") or "").strip()
                    table_specs.append(
                        (src, (fld.attrib.get("format") or "").strip(), [], 1, "", None, None)
                    )
                    break

    if not table_specs:
        table_specs.append(("SCORE", "", [], 1, "NAME", None, None))

    cursor = 0
    occurrences = []
    fixed_ok = True
    for child in st_node:
        if child.tag == "check":
            continue
        if child.tag == "elt":
            sz = parse_int_auto(child.attrib.get("size"))
            if sz is None or sz < 0:
                fixed_ok = False
                break
            eid = child.attrib.get("id", "").strip()
            occurrences.append(
                {"count": 1, "vstart": cursor, "stride": sz, "elts": {eid: (child, 0)}}
            )
            cursor += sz
        elif child.tag == "loop":
            cnt = parse_int_auto(child.attrib.get("count"))
            if cnt is None or cnt <= 0:
                fixed_ok = False
                break
            sf = parse_int_auto(child.attrib.get("skip-first-bytes"), 0) or 0
            sl = parse_int_auto(child.attrib.get("skip-last-bytes"), 0) or 0
            rel = 0
            emap = {}
            for sub in child.findall("elt"):
                esz = parse_int_auto(sub.attrib.get("size"))
                if esz is None or esz < 0:
                    fixed_ok = False
                    break
                seid = sub.attrib.get("id", "").strip()
                if seid not in emap:
                    emap[seid] = (sub, rel)
                rel += esz
            if not fixed_ok:
                break
            occurrences.append(
                {"count": cnt, "vstart": cursor - sf, "stride": rel, "elts": emap}
            )
            cursor += (cnt * rel) - sf - sl

    sizes = []
    for sz_el in st_node.findall("./check/size"):
        v = parse_int_auto(sz_el.text)
        if v is not None and v > 0 and v not in sizes:
            sizes.append(v)
    if not sizes and fixed_ok and cursor > 0:
        sizes.append(cursor)

    groups = []
    for s_src, s_fmt, sum_parts, outer_scale, n_src, lmax, max_ign in table_specs:
        if sum_parts:
            for occ in occurrences:
                if all(sp_id in occ["elts"] for sp_id, _ in sum_parts):
                    sp_specs = []
                    for sp_id, sp_fmt in sum_parts:
                        e_node, r_off = occ["elts"][sp_id]
                        sp = build_score_spec(
                            e_node,
                            r_off,
                            sp_fmt,
                            formats_map,
                            charsets_xml,
                            bitmasks_xml,
                            charset_pool,
                            charset_index,
                            extra_scale=outer_scale,
                        )
                        if sp:
                            sp_specs.append(sp)
                    if not sp_specs:
                        continue
                    n_spec = None
                    if n_src and n_src in occ["elts"]:
                        ne_node, nr_off = occ["elts"][n_src]
                        n_spec = build_name_spec(
                            ne_node, nr_off, charsets_xml, charset_pool, charset_index
                        )
                    grp = {
                        "m": "c" if n_spec else "o",
                        "o": occ["vstart"],
                        "n": occ["count"],
                        "d": occ["stride"],
                        "s": sp_specs[0],
                    }
                    if len(sp_specs) > 1:
                        grp["s2"] = sp_specs[1:]
                    if n_spec:
                        grp["t"] = n_spec
                    if lmax and lmax < occ["count"]:
                        grp["l"] = lmax
                    if max_ign:
                        grp["u"] = max_ign
                    groups.append(grp)
            continue

        s_occs = [occ for occ in occurrences if s_src in occ["elts"]]
        n_occs = [occ for occ in occurrences if n_src and n_src in occ["elts"]]
        if not s_occs:
            continue

        matched_combined = False
        for occ in s_occs:
            if n_src and n_src in occ["elts"]:
                se_node, sr_off = occ["elts"][s_src]
                ne_node, nr_off = occ["elts"][n_src]
                s_spec = build_score_spec(
                    se_node,
                    sr_off,
                    s_fmt,
                    formats_map,
                    charsets_xml,
                    bitmasks_xml,
                    charset_pool,
                    charset_index,
                )
                n_spec = build_name_spec(
                    ne_node, nr_off, charsets_xml, charset_pool, charset_index
                )
                if s_spec and n_spec:
                    grp = {
                        "m": "c",
                        "o": occ["vstart"],
                        "n": occ["count"],
                        "d": occ["stride"],
                        "s": s_spec,
                        "t": n_spec,
                    }
                    if lmax and lmax < occ["count"]:
                        grp["l"] = lmax
                    if max_ign:
                        grp["u"] = max_ign
                    groups.append(grp)
                    matched_combined = True
        if matched_combined:
            continue

        if n_occs and len(s_occs) == len(n_occs):
            sep_ok = False
            for s_occ, n_occ in zip(s_occs, n_occs):
                if s_occ["count"] == n_occ["count"]:
                    se_node, sr_off = s_occ["elts"][s_src]
                    ne_node, nr_off = n_occ["elts"][n_src]
                    s_spec = build_score_spec(
                        se_node,
                        sr_off,
                        s_fmt,
                        formats_map,
                        charsets_xml,
                        bitmasks_xml,
                        charset_pool,
                        charset_index,
                    )
                    n_spec = build_name_spec(
                        ne_node, nr_off, charsets_xml, charset_pool, charset_index
                    )
                    if s_spec and n_spec:
                        grp = {
                            "m": "s",
                            "n": s_occ["count"],
                            "so": s_occ["vstart"],
                            "sd": s_occ["stride"],
                            "s": s_spec,
                            "to": n_occ["vstart"],
                            "td": n_occ["stride"],
                            "t": n_spec,
                        }
                        if lmax and lmax < s_occ["count"]:
                            grp["l"] = lmax
                        if max_ign:
                            grp["u"] = max_ign
                        groups.append(grp)
                        sep_ok = True
            if sep_ok:
                continue

        for s_occ in s_occs:
            se_node, sr_off = s_occ["elts"][s_src]
            s_spec = build_score_spec(
                se_node,
                sr_off,
                s_fmt,
                formats_map,
                charsets_xml,
                bitmasks_xml,
                charset_pool,
                charset_index,
            )
            if s_spec:
                grp = {
                    "m": "o",
                    "o": s_occ["vstart"],
                    "n": s_occ["count"],
                    "d": s_occ["stride"],
                    "s": s_spec,
                }
                if lmax and lmax < s_occ["count"]:
                    grp["l"] = lmax
                if max_ign:
                    grp["u"] = max_ign
                groups.append(grp)

    if not groups:
        return None, []
    tpl = {"g": groups}
    st_swap = parse_int_auto(st_node.attrib.get("byte-swap"))
    if st_swap and st_swap > 1:
        tpl["w"] = st_swap
    return tpl, sizes


def compile_catalog_from_db(db_dir):
    """Compiles all XML schemas in `src/main/db` (never `db_defaults`) into a canonical dict."""
    dtd_path = os.path.join(db_dir, "hi2txt.dtd")
    entities = load_dtd_entities(dtd_path)

    xml_files = sorted(glob.glob(os.path.join(db_dir, "*.xml")))
    if not xml_files:
        raise RuntimeError(f"No .xml files found in {db_dir}")

    xml_roots = {}
    sameas = {}
    for path in xml_files:
        rname = os.path.splitext(os.path.basename(path))[0].lower()
        rt = parse_hi2txt_xml(path, entities)
        sa = rt.find("sameas")
        if sa is not None and sa.attrib.get("id"):
            sameas[rname] = sa.attrib["id"].strip().lower()
        else:
            xml_roots[rname] = rt

    charset_pool = []
    charset_index = {}
    template_pool = []
    template_index = {}
    rom_variants = {}

    for rname, rt in sorted(xml_roots.items()):
        sts = rt.findall("structure")
        hi_sts = [s for s in sts if s.attrib.get("file", "") in (".hi", "hi")]
        other_sts = [s for s in sts if s.attrib.get("file", "") not in (".hi", "hi")]
        variants = []
        for st_node in hi_sts + other_sts:
            tpl, szs = compile_structure(st_node, rt, charset_pool, charset_index)
            if not tpl:
                continue
            tkey = json.dumps(tpl, sort_keys=True, separators=(",", ":"))
            if tkey not in template_index:
                template_index[tkey] = len(template_pool)
                template_pool.append(tpl)
            tidx = template_index[tkey]
            if szs:
                for sz in szs:
                    if [tidx, sz] not in variants:
                        variants.append([tidx, sz])
            else:
                if [tidx, None] not in variants:
                    variants.append([tidx, None])
        if variants:
            rom_variants[rname] = variants[0] if len(variants) == 1 else variants

    for rname in sorted(sameas.keys()):
        cur = rname
        for _ in range(8):
            if cur in rom_variants:
                rom_variants[rname] = rom_variants[cur]
                break
            if cur in sameas:
                cur = sameas[cur]
            else:
                break

    return {"c": charset_pool, "t": template_pool, "r": rom_variants}


def encode_catalog_b85(catalog_obj):
    """Deterministically encodes a catalog dict with zlib(level=9) + Base85."""
    raw_json = json.dumps(catalog_obj, sort_keys=True, separators=(",", ":")).encode("utf-8")
    compressed = zlib.compress(raw_json, level=9)
    b85 = base64.b85encode(compressed).decode("ascii")
    for forbidden in FORBIDDEN_TOKENS:
        if forbidden in b85:
            raise RuntimeError(f"Encoded Base85 unexpectedly contains template token {forbidden}")
    return b85


def format_b85_constant(b85_str):
    lines = [b85_str[i : i + LINE_WIDTH] for i in range(0, len(b85_str), LINE_WIDTH)]
    body = "\n".join(f'    "{line}",' for line in lines)
    return f"_HI2TXT_CATALOG_B85 = (\n{body}\n)"


def extract_embedded_b85(companion_path):
    with open(companion_path, "r", encoding="utf-8") as f:
        text = f.read()
    m = re.search(r"_HI2TXT_CATALOG_B85\s*=\s*\(\s*((?:\"[^\"]*\",?\s*)+)\)", text, re.DOTALL)
    if not m:
        raise RuntimeError(f"_HI2TXT_CATALOG_B85 constant not found in {companion_path}")
    chunks = re.findall(r'"([^"]*)"', m.group(1))
    return "".join(chunks)


def inject_b85_into_companion(companion_path, b85_str):
    with open(companion_path, "r", encoding="utf-8") as f:
        text = f.read()
    const_block = format_b85_constant(b85_str)
    pattern = re.compile(r"_HI2TXT_CATALOG_B85\s*=\s*\(\s*(?:\"[^\"]*\",?\s*)+\)", re.DOTALL)
    if not pattern.search(text):
        raise RuntimeError(f"Could not locate _HI2TXT_CATALOG_B85 block in {companion_path}")
    updated = pattern.sub(lambda _m: const_block, text, count=1)
    with open(companion_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(updated)


def verify_embedded_catalog(expected_b85=None):
    for p in COMPANION_TARGETS:
        if not os.path.isfile(p):
            raise RuntimeError(f"Missing companion script copy: {p}")

    hashes = []
    for p in COMPANION_TARGETS:
        with open(p, "rb") as f:
            hashes.append(hashlib.sha256(f.read()).hexdigest())
    if len(set(hashes)) != 1:
        raise RuntimeError(
            f"SHA-256 mismatch between companion script copies: {dict(zip(COMPANION_TARGETS, hashes))}"
        )

    embedded = extract_embedded_b85(COMPANION_TARGETS[0])
    for forbidden in FORBIDDEN_TOKENS:
        if forbidden in embedded:
            raise RuntimeError(f"Embedded catalog contains forbidden placeholder {forbidden}")

    raw = zlib.decompress(base64.b85decode(embedded))
    obj = json.loads(raw.decode("utf-8"))
    if not isinstance(obj, dict) or not all(k in obj for k in ("c", "t", "r")):
        raise RuntimeError("Decoded catalog missing required top-level keys ('c', 't', 'r')")

    rom_count = len(obj["r"])
    tpl_count = len(obj["t"])
    cs_count = len(obj["c"])
    if rom_count < 2500:
        raise RuntimeError(f"Expected >= 2500 resolved ROMs in catalog, got {rom_count}")

    reencoded = encode_catalog_b85(obj)
    if reencoded != embedded:
        raise RuntimeError("Embedded _HI2TXT_CATALOG_B85 is not canonically reproducible on round-trip!")

    if expected_b85 is not None and embedded != expected_b85:
        raise RuntimeError("Embedded _HI2TXT_CATALOG_B85 differs from freshly compiled hi2txt-xml output!")

    print(
        f"✅ Verified _HI2TXT_CATALOG_B85: {rom_count} ROMs, {tpl_count} templates, "
        f"{cs_count} charsets, {len(embedded)} B Base85 ({len(embedded) / 1024:.1f} KiB), "
        f"SHA-256={hashes[0][:16]}..."
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build or verify embedded hi2txt catalog")
    parser.add_argument(
        "--xml-repo",
        default="",
        help="Optional path to a local checkout of GreatStoneEx/hi2txt-xml",
    )
    parser.add_argument(
        "--verify",
        action="store_true",
        help="Verify deterministic reproducibility and parity without modifying files",
    )
    parser.add_argument(
        "--online",
        action="store_true",
        help="When combined with --verify, also clone GreatStoneEx/hi2txt-xml to /tmp and verify XML compilation",
    )
    args = parser.parse_args(argv)

    if args.verify and not args.xml_repo and not args.online:
        verify_embedded_catalog()
        return 0

    tmp_clone_dir = None
    try:
        repo_dir = args.xml_repo
        if not repo_dir:
            tmp_clone_dir = tempfile.mkdtemp(prefix="hi2txt-xml-")
            subprocess.run(
                ["git", "clone", "--depth", "1", HI2TXT_GIT_URL, tmp_clone_dir],
                check=True,
            )
            repo_dir = tmp_clone_dir

        db_dir = os.path.join(repo_dir, "src", "main", "db")
        if not os.path.isdir(db_dir):
            raise RuntimeError(f"hi2txt database directory not found: {db_dir}")

        catalog_obj = compile_catalog_from_db(db_dir)
        b85_str = encode_catalog_b85(catalog_obj)

        if args.verify:
            verify_embedded_catalog(expected_b85=b85_str)
            return 0

        for target in COMPANION_TARGETS:
            if os.path.isfile(target):
                inject_b85_into_companion(target, b85_str)

        verify_embedded_catalog(expected_b85=b85_str)
        return 0
    finally:
        if tmp_clone_dir and os.path.exists(tmp_clone_dir):
            shutil.rmtree(tmp_clone_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
