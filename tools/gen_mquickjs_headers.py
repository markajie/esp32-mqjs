#!/usr/bin/env python3
"""Generate MQuickJS ROM tables without modifying the upstream mquickjs tree.

This is a small, faithful port of mquickjs_build.c for the standard library in
mquickjs/mqjs_stdlib.c.  It only needs Python (shipped with ESP-IDF) and a C
preprocessor; the generated headers are checked in so normal firmware builds do
not need any host C compiler.

Usage:
  python tools/gen_mquickjs_headers.py \
      --preprocessed build/mqjs_stdlib.i \
      --out components/mjs_engine/generated
"""
import argparse
import json
import math
import os
import re
import struct
import subprocess
import sys

JSW = 4
ATOM_ALIGN = 64
JS_MTAG_BITS = 4
JS_MTAG_STRING = 3
JS_MTAG_VALUE_ARRAY = 5
JS_MTAG_FLOAT64 = 2
JS_MTAG_OBJECT = 1

DEF_END, DEF_CFUNC, DEF_CGETSET, DEF_DOUBLE, DEF_UNDEFINED, DEF_STRING, DEF_NULL, DEF_CLASS = range(8)


def cvt_name(s):
    if not s:
        return "empty"
    return s.replace("<", "_").replace(">", "_").replace("-", "_")


def parse_c_string(tok):
    tok = tok.strip()
    if tok == "NULL" or tok == "((void *)0)":
        return None
    if not (tok.startswith('"') and tok.endswith('"')):
        raise ValueError("not a C string: %r" % tok)
    return json.loads(tok)


def split_top(s):
    out = []
    depth = 0
    in_str = False
    esc = False
    start = 0
    for i, ch in enumerate(s):
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_str = False
        else:
            if ch == '"':
                in_str = True
            elif ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif ch == "," and depth == 0:
                out.append(s[start:i].strip())
                start = i + 1
    tail = s[start:].strip()
    if tail:
        out.append(tail)
    return out


def strip_outer_braces(s):
    s = s.strip()
    if not (s.startswith("{") and s.endswith("}")):
        raise ValueError("expected braces: %r" % s[:80])
    return s[1:-1]


def parse_atoms_block(src):
    m = re.search(r"static const char \*atoms\[\]\s*=\s*\{(.*?)\n\};", src, re.S)
    if not m:
        raise RuntimeError("atoms[] not found in mquickjs_build.c")
    body = m.group(1)
    atoms = []
    for line in body.splitlines():
        line = re.sub(r"/\*.*?\*/", "", line).strip()
        if not line or line.startswith("#"):
            continue
        dm = re.match(r"DEF\(\s*([^,]+?)\s*,\s*\"((?:[^\"\\]|\\.)*)\"\s*\)", line)
        if dm:
            atoms.append(json.loads('"' + dm.group(2) + '"'))
            continue
        sm = re.search(r"\"((?:[^\"\\]|\\.)*)\"", line)
        if sm:
            atoms.append(json.loads('"' + sm.group(1) + '"'))
    return atoms


def parse_stdlib(preprocessed):
    props = {}
    classes = {}
    for m in re.finditer(r"static const JSPropDef\s+(\w+)\s*\[\]\s*=\s*\{(.*?)\n?\};", preprocessed, re.S):
        name = m.group(1)
        body = m.group(2)
        entries = []
        for raw in split_top(body):
            raw = raw.strip()
            if not raw:
                continue
            parts = split_top(strip_outer_braces(raw))
            if not parts:
                continue
            dtype = parts[0].strip()
            if dtype == "JS_DEF_END":
                entries.append({"type": "END"})
                continue
            if len(parts) < 2:
                raise ValueError("bad prop %s: %s" % (name, raw[:120]))
            pname = parse_c_string(parts[1])
            rest = parts[2] if len(parts) > 2 else ""
            if dtype == "JS_DEF_CFUNC":
                fm = re.search(r"\.func\s*=\s*\{(.*?)\}\s*\}", rest, re.S)
                f = split_top(fm.group(1))
                entries.append({"type": "CFUNC", "name": pname, "length": int(f[0]),
                                "magic": parse_c_string(f[1]), "cproto": parse_c_string(f[2]),
                                "func": parse_c_string(f[3])})
            elif dtype == "JS_DEF_CGETSET":
                gm = re.search(r"\.getset\s*=\s*\{(.*?)\}\s*\}", rest, re.S)
                g = split_top(gm.group(1))
                get_name = parse_c_string(g[2])
                set_name = parse_c_string(g[3])
                entries.append({"type": "CGETSET", "name": pname,
                                "magic": parse_c_string(g[0]), "cproto": parse_c_string(g[1]),
                                "get": None if get_name == "NULL" else get_name,
                                "set": None if set_name == "NULL" else set_name})
            elif dtype == "JS_DEF_PROP_DOUBLE":
                fm = re.search(r"\.f64\s*=\s*(.*?)\s*\}", rest, re.S)
                entries.append({"type": "DOUBLE", "name": pname, "value": parse_float_expr(fm.group(1))})
            elif dtype == "JS_DEF_PROP_STRING":
                sm = re.search(r"\.str\s*=\s*(\"(?:[^\"\\]|\\.)*\")", rest, re.S)
                entries.append({"type": "STRING", "name": pname, "value": parse_c_string(sm.group(1))})
            elif dtype == "JS_DEF_PROP_UNDEFINED":
                entries.append({"type": "UNDEFINED", "name": pname})
            elif dtype == "JS_DEF_PROP_NULL":
                entries.append({"type": "NULL", "name": pname})
            elif dtype == "JS_DEF_CLASS":
                cm = re.search(r"\.class1\s*=\s*&?\s*(\w+)", rest)
                entries.append({"type": "CLASS", "name": pname, "ref": cm.group(1)})
            else:
                raise ValueError("unknown def type %s" % dtype)
        if entries and entries[-1]["type"] == "END":
            entries = entries[:-1]
        props[name] = entries

    for m in re.finditer(r"static const JSClassDef\s+(\w+)\s*=\s*\{(.*?)\};", preprocessed, re.S):
        name = m.group(1)
        parts = split_top(m.group(2))
        if len(parts) != 9:
            raise ValueError("bad class %s: %d fields" % (name, len(parts)))
        def ref(tok):
            tok = tok.strip()
            if tok in ("NULL", "((void *)0)"):
                return None
            return tok.lstrip("&").strip()
        classes[name] = {
            "name": parse_c_string(parts[0]),
            "length": int(parts[1]),
            "cproto": parse_c_string(parts[2]),
            "func": parse_c_string(parts[3]),
            "class_id": parse_c_string(parts[4]),
            "class_props": ref(parts[5]),
            "proto_props": ref(parts[6]),
            "parent": ref(parts[7]),
            "finalizer": parse_c_string(parts[8]),
        }
    if "js_global_object" not in props:
        raise RuntimeError("js_global_object not found")
    if "js_c_function_decl" not in props:
        raise RuntimeError("js_c_function_decl not found")
    return props, classes


def parse_float_expr(expr):
    expr = expr.strip()
    expr = expr.replace("(__builtin_nanf(\"\"))", "float('nan')")
    expr = expr.replace("(__builtin_inff())", "float('inf')")
    expr = expr.replace("(__builtin_huge_val())", "float('inf')")
    expr = expr.replace("1.0 / 0.0", "float('inf')")
    expr = expr.replace("NAN", "float('nan')")
    expr = expr.replace("INFINITY", "float('inf')")
    return float(eval(expr, {"__builtins__": {}, "float": float}, {}))


class Gen:
    def __init__(self, atoms, props, classes):
        self.props = props
        self.classes = classes
        self.atoms = []
        self.atom_map = {}
        self.atom_offset = 0
        self.cfuncs = []
        self.cur = 0
        self.out = []
        self.class_cache = {}
        self.class_atom_seen = set()
        self.sorted_atom_offset = 0
        self.global_offset = 0
        self.finalizers = []
        for a in atoms:
            self.add_atom(a)

    def add_atom(self, s):
        if s in self.atom_map:
            return self.atom_map[s]
        self.atom_map[s] = self.atom_offset
        self.atoms.append(s)
        self.atom_offset += 1 + (len(s.encode("utf-8")) + JSW) // JSW
        return self.atom_map[s]

    def add_cfunc(self, name, length, magic, cproto, func):
        for i, e in enumerate(self.cfuncs):
            if (e["name"], e["length"], e["magic"], e["cproto"], e["func"]) == (name, length, magic, cproto, func):
                return i
        self.cfuncs.append({"name": name, "length": length, "magic": magic,
                            "cproto": cproto, "func": func})
        return len(self.cfuncs) - 1

    def emit(self, s=""):
        self.out.append(s)

    # ---------- atom value helpers ----------
    def atom_expr(self, s):
        if len(s) == 1:
            return "JS_VALUE_MAKE_SPECIAL(JS_TAG_STRING_CHAR, %d)" % ord(s)
        idx = self.atom_map.get(s)
        if idx is None:
            raise ValueError("atom %r undefined" % s)
        return "JS_ROM_VALUE(%d)" % idx

    def atom_value_only(self, s):
        if len(s) == 1:
            return (ord(s) << 5) | 0x1b
        idx = self.atom_map.get(s)
        if idx is None:
            raise ValueError("atom %r undefined" % s)
        return idx * JSW + 1

    def hash_prop(self, s):
        p = self.atom_value_only(s)
        return (p // JSW) ^ (p % JSW)

    def add_cfunc_atom(self, d):
        self.add_atom(d["name"])

    # ---------- atom traversal (matches mquickjs_build.c) ----------
    def atoms_props(self, items, kind):
        for d in items:
            self.add_atom(d["name"])
            if d["type"] == "STRING":
                self.add_atom(d["value"])
            elif d["type"] == "CLASS":
                self.atoms_class(d["ref"])
            elif d["type"] == "CGETSET":
                if d.get("get"):
                    self.add_atom("get " + d["name"])
                if d.get("set"):
                    self.add_atom("set " + d["name"])

    def atoms_class(self, cname):
        if cname in self.class_atom_seen:
            return
        c = self.classes[cname]
        if c["parent"]:
            self.atoms_class(c["parent"])
        if c["func"]:
            self.add_atom(c["name"])
        if c["class_props"]:
            self.atoms_props(self.props[c["class_props"]], "CLASS" if c["func"] else "OBJECT")
        if c["proto_props"]:
            self.atoms_props(self.props[c["proto_props"]], "PROTO")
        self.class_atom_seen.add(cname)

    # ---------- value/class/props emission ----------
    def define_value(self, d):
        if d["type"] == "DOUBLE":
            if is_short_int(d["value"]):
                return -1
            ident = self.cur
            self.emit("  /* float64 (offset=%d) */" % ident)
            self.emit("  JS_MB_HEADER_DEF(JS_MTAG_FLOAT64),")
            bits = struct.unpack("<Q", struct.pack("<d", d["value"]))[0]
            self.emit("  0x%08x," % (bits & 0xffffffff))
            self.emit("  0x%08x," % (bits >> 32))
            self.emit("")
            self.cur += 3
            return ident
        if d["type"] == "CLASS":
            return self.define_class(d["ref"])
        if d["type"] == "CGETSET":
            get_idx = -1
            set_idx = -1
            if d.get("get"):
                get_idx = self.add_cfunc("get " + d["name"], 0, d["magic"], d["cproto"], d["get"])
            if d.get("set"):
                set_idx = self.add_cfunc("set " + d["name"], 1, d["magic"], d["cproto"], d["set"])
            ident = self.cur
            self.emit("  /* getset (offset=%d) */" % ident)
            self.emit("  JS_VALUE_ARRAY_HEADER(2),")
            self.emit("  JS_VALUE_MAKE_SPECIAL(JS_TAG_SHORT_FUNC, %d)," % get_idx if get_idx >= 0 else "  JS_UNDEFINED,")
            self.emit("  JS_VALUE_MAKE_SPECIAL(JS_TAG_SHORT_FUNC, %d)," % set_idx if set_idx >= 0 else "  JS_UNDEFINED,")
            self.emit("")
            self.cur += 3
            return ident
        return -1

    def define_class(self, cname):
        if cname in self.class_cache:
            return self.class_cache[cname]
        c = self.classes[cname]
        parent_idx = self.define_class(c["parent"]) if c["parent"] else -1
        ctor_idx = -1
        if c["func"]:
            ctor_idx = self.add_cfunc(c["name"], c["length"], c["class_id"], c["cproto"], c["func"])
        class_props_idx = -1
        proto_props_idx = -1
        if ctor_idx >= 0:
            class_props_idx = self.define_props(c["class_props"], "CLASS", c)
            proto_props_idx = self.define_props(c["proto_props"], "PROTO", c)
        elif c["class_props"]:
            class_props_idx = self.define_props(c["class_props"], "OBJECT", c)
        ident = self.cur
        self.emit("  /* class (offset=%d) */" % ident)
        self.emit("  JS_MB_HEADER_DEF(JS_MTAG_OBJECT),")
        self.emit("  JS_ROM_VALUE(%d)," % class_props_idx if class_props_idx >= 0 else "  JS_NULL,")
        self.emit("  %d," % ctor_idx)
        self.emit("  JS_ROM_VALUE(%d)," % proto_props_idx if proto_props_idx >= 0 else "  JS_NULL,")
        self.emit("  JS_ROM_VALUE(%d)," % parent_idx if parent_idx >= 0 else "  JS_NULL,")
        self.emit("")
        self.cur += 5
        self.class_cache[cname] = ident
        if ctor_idx >= 0 and c["finalizer"] and c["finalizer"] != "NULL":
            self.finalizers.append((c["class_id"], c["finalizer"]))
        return ident

    def define_props(self, pname, kind, cls=None):
        items = [] if pname is None else self.props[pname]
        actual = len(items)
        n_props = actual + (1 if kind in ("PROTO", "CLASS") else 0)
        idents = [self.define_value(d) for d in items]
        prop_hash = [0] * n_props
        if kind == "GLOBAL":
            props_ident = self.cur
            self.emit("  /* global object properties (offset=%d) */" % props_ident)
            self.emit("  JS_VALUE_ARRAY_HEADER(%d)," % (2 * actual))
            self.cur += 2 * actual + 1
        else:
            if n_props <= 1:
                log2 = 0
            else:
                log2 = (n_props - 1).bit_length() - 1
            hash_size = 1 << log2
            if hash_size > ATOM_ALIGN // JSW:
                hash_size = ATOM_ALIGN // JSW
            mask = hash_size - 1
            table = [0] * hash_size
            for i in range(n_props):
                if i < actual:
                    name = items[i]["name"]
                else:
                    name = "constructor" if kind == "PROTO" else "prototype"
                h = self.hash_prop(name) & mask
                prop_hash[i] = table[h]
                table[h] = 2 + hash_size + 3 * i
            props_ident = self.cur
            self.emit("  /* properties (offset=%d) */" % props_ident)
            self.emit("  JS_VALUE_ARRAY_HEADER(%d)," % (2 + hash_size + n_props * 3))
            self.emit("  %d << 1, /* n_props */" % n_props)
            self.emit("  %d << 1, /* hash_mask */" % mask)
            for v in table:
                self.emit("  %d << 1," % v)
            self.cur += hash_size + 3 + 3 * n_props

        for i in range(n_props):
            if i < actual:
                d = items[i]
                name = d["name"]
            else:
                d = {"type": "END"}
                name = "constructor" if kind == "PROTO" else "prototype"
            self.emit("  " + self.atom_expr(name) + ",")
            t = d["type"]
            if t == "DOUBLE":
                if idents[i] >= 0:
                    self.emit("  JS_ROM_VALUE(%d)," % idents[i])
                else:
                    self.emit("  %d << 1," % (int(d["value"]) << 1))
            elif t in ("CGETSET", "CLASS"):
                self.emit("  JS_ROM_VALUE(%d)," % idents[i])
            elif t == "UNDEFINED":
                self.emit("  JS_UNDEFINED,")
            elif t == "NULL":
                self.emit("  JS_NULL,")
            elif t == "STRING":
                self.emit("  " + self.atom_expr(d["value"]) + ",")
            elif t == "CFUNC":
                idx = self.add_cfunc(d["name"], d["length"], d["magic"], d["cproto"], d["func"])
                self.emit("  JS_VALUE_MAKE_SPECIAL(JS_TAG_SHORT_FUNC, %d)," % idx)
            elif t == "END":
                if kind == "PROTO":
                    self.emit("  (uint32_t)(-%s - 1) << 1," % cls["class_id"])
                else:
                    self.emit("  %s << 1," % cls["class_id"])
            else:
                raise ValueError("bad type")
            if kind != "GLOBAL":
                prop_type = "GETSET" if t == "CGETSET" else ("SPECIAL" if t == "END" else "NORMAL")
                self.emit("  (%d << 1) | (JS_PROP_%s << 30)," % (prop_hash[i], prop_type))
            self.emit("")
        return props_ident

    # ---------- output ----------
    def generate(self, c_function_decl):
        # predeclared C functions
        for d in c_function_decl:
            if d["type"] == "END":
                continue
            if d["type"] != "CFUNC":
                raise ValueError("only CFUNC allowed in c_function_decl")
            self.add_atom(d["name"])
            self.add_cfunc(d["name"], d["length"], d["magic"], d["cproto"], d["func"])

        global_items = self.props["js_global_object"]
        self.atoms_props(global_items, "GLOBAL")

        self.emit("/* automatically generated by tools/gen_mquickjs_headers.py - do not edit */")
        self.emit("")
        self.emit('#include "mquickjs_priv.h"')
        self.emit("")
        self.emit("static const uint32_t __attribute((aligned(%d))) js_stdlib_table[] = {" % ATOM_ALIGN)
        self.emit("  /* atom_table */")
        for s in self.atoms:
            b = s.encode("utf-8")
            n = len(b)
            is_ascii = int(all(c < 128 for c in b))
            is_numeric = int(s in ("NaN", "Infinity", "-Infinity"))
            self.emit("  (JS_MTAG_STRING << 1) | (1 << JS_MTAG_BITS) | (%d << (JS_MTAG_BITS + 1)) | (%d << (JS_MTAG_BITS + 2)) | (%d << (JS_MTAG_BITS + 3)), /* %s (offset=%d) */" %
                      (is_ascii, is_numeric, n, s.replace("*/", "* /"), self.atom_map[s]))
            nwords = (n + JSW) // JSW
            for j in range(nwords):
                v = 0
                for k, ch in enumerate(b[j * JSW:(j + 1) * JSW]):
                    v |= ch << (k * 8)
                self.emit("  0x%08x," % v)
            self.cur += nwords + 1
        self.emit("")
        self.sorted_atom_offset = self.cur
        self.emit("  /* sorted atom table (offset=%d) */" % self.cur)
        self.emit("  JS_VALUE_ARRAY_HEADER(%d)," % len(self.atoms))
        for s in sorted(self.atoms):
            self.emit("  JS_ROM_VALUE(%d), /* %s */" % (self.atom_map[s], cvt_name(s)))
        self.cur += len(self.atoms) + 1
        self.emit("")

        self.global_offset = self.define_props("js_global_object", "GLOBAL")
        self.emit("};")
        self.emit("")

        self.emit("static const JSCFunctionDef js_c_function_table[] = {")
        for e in self.cfuncs:
            self.emit("  { { .%s = %s }," % (e["cproto"], e["func"]))
            self.emit("    " + self.atom_expr(e["name"]) + ",")
            self.emit("    JS_CFUNC_%s, %d, %s }," % (e["cproto"], e["length"], e["magic"]))
        self.emit("};")
        self.emit("")
        self.emit("#ifndef JS_CLASS_COUNT")
        self.emit("#define JS_CLASS_COUNT JS_CLASS_USER /* total number of classes */")
        self.emit("#endif")
        self.emit("")
        self.emit("static const JSCFinalizer js_c_finalizer_table[JS_CLASS_COUNT - JS_CLASS_USER] = {")
        for cid, fn in self.finalizers:
            self.emit("  [%s - JS_CLASS_USER] = %s," % (cid, fn))
        self.emit("};")
        self.emit("")
        self.emit("const JSSTDLibraryDef js_stdlib = {")
        self.emit("  js_stdlib_table,")
        self.emit("  js_c_function_table,")
        self.emit("  js_c_finalizer_table,")
        self.emit("  %d," % self.cur)
        self.emit("  %d," % ATOM_ALIGN)
        self.emit("  %d," % self.sorted_atom_offset)
        self.emit("  %d," % self.global_offset)
        self.emit("  JS_CLASS_COUNT,")
        self.emit("};")
        self.emit("")
        return "\n".join(self.out)


def is_short_int(d):
    return d == d and not math.isinf(d) and -2**30 <= d <= 2**30 - 1 and int(d) == d


def gen_atom_header(atoms):
    off = 0
    lines = ["/* automatically generated by tools/gen_mquickjs_headers.py - do not edit */",
             "#ifndef MQUICKJS_ATOM_H", "#define MQUICKJS_ATOM_H", ""]
    seen = set()
    for s in atoms:
        if s in seen:
            continue
        seen.add(s)
        lines.append("#define JS_ATOM_%s %d" % (cvt_name(s), off))
        off += 1 + (len(s.encode("utf-8")) + JSW) // JSW
    lines += ["", "#define JS_ATOM_END %d" % off, "", "#endif", ""]
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mquickjs", default="mquickjs")
    ap.add_argument("--preprocessed", default=None)
    ap.add_argument("--out", default="components/mjs_headers")
    args = ap.parse_args()

    src_build = open(os.path.join(args.mquickjs, "mquickjs_build.c"), encoding="utf-8").read()
    predefined_atoms = parse_atoms_block(src_build)

    if args.preprocessed:
        pre = open(args.preprocessed, encoding="utf-8", errors="replace").read()
    else:
        cc = os.environ.get("CC", "riscv32-esp-elf-gcc")
        cmd = [cc, "-E", "-P", "-I", args.mquickjs, os.path.join(args.mquickjs, "mqjs_stdlib.c")]
        pre = subprocess.check_output(cmd, text=True)

    props, classes = parse_stdlib(pre)
    g = Gen(predefined_atoms, props, classes)
    stdlib_h = g.generate(props["js_c_function_decl"])
    atom_h = gen_atom_header(predefined_atoms)

    os.makedirs(args.out, exist_ok=True)
    open(os.path.join(args.out, "mqjs_stdlib.h"), "w", encoding="utf-8", newline="\n").write(stdlib_h)
    open(os.path.join(args.out, "mquickjs_atom.h"), "w", encoding="utf-8", newline="\n").write(atom_h)
    print("generated %s and %s" % (os.path.join(args.out, "mqjs_stdlib.h"),
                                   os.path.join(args.out, "mquickjs_atom.h")))
    print("stdlib words=%d global_offset=%d sorted_atoms=%d atoms=%d" %
          (g.cur, g.global_offset, g.sorted_atom_offset, len(g.atoms)))


if __name__ == "__main__":
    main()
