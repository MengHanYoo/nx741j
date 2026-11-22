#!/usr/bin/env python3
"""Apply the data-a-live contract to UniXNU's live-FDT-derived ADT.

The hardware description remains generated from NX741J's stock live FDT.
This postprocessor adds the parts that are OS policy rather than NX741J
hardware:

  * the D22AP / iPhone10,3 root and /product identity from the original
    iPhone X DeviceTree (``UNIXNU_D22AP_ADT``);
  * the security/AMFI manifest properties used by data-a-live;
  * /chosen fuxi-root-extents and fuxi-root-blocks from pinfile's JSON
    (``UNIXNU_ROOT_EXTENTS``);
  * removal of the old APFS IOMedia root-matching dictionary.

Both external inputs are Apple/device artifacts and are never committed.
"""
import argparse
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path


def cstr(value):
    return value.encode() + b"\0"


def u32(value):
    return struct.pack("<I", value)


def u64(value):
    return struct.pack("<Q", value)


class Node:
    def __init__(self):
        self.props = []
        self.children = []

    @property
    def name(self):
        for name, flags, value in self.props:
            if name == "name" and not flags:
                return value.split(b"\0", 1)[0].decode()
        return "?"

    def prop(self, key):
        values = [value for name, _flags, value in self.props if name == key]
        if len(values) > 1:
            raise ValueError(f"{self.name}: duplicate property {key}")
        return values[0] if values else None

    def set_prop(self, key, value):
        self.props = [(name, flags, value if name == key else existing)
                      for name, flags, existing in self.props]
        if not any(name == key for name, _flags, _value in self.props):
            self.props.append((key, 0, value))

    def remove_prop(self, key):
        self.props = [entry for entry in self.props if entry[0] != key]

    def child(self, name):
        values = [child for child in self.children if child.name == name]
        if len(values) > 1:
            raise ValueError(f"{self.name}: duplicate child {name}")
        return values[0] if values else None

    def remove_child(self, name):
        self.children = [child for child in self.children if child.name != name]


def parse(blob, offset=0):
    node = Node()
    if offset + 8 > len(blob):
        raise ValueError("truncated ADT node")
    node_count, child_count = struct.unpack_from("<II", blob, offset)
    offset += 8
    for _ in range(node_count):
        if offset + 36 > len(blob):
            raise ValueError("truncated ADT property")
        name = blob[offset:offset + 32].split(b"\0", 1)[0].decode()
        size = struct.unpack_from("<I", blob, offset + 32)[0]
        flags, length = size >> 31, size & 0x7FFFFFFF
        start = offset + 36
        end = start + length
        if end > len(blob):
            raise ValueError("truncated ADT property value")
        node.props.append((name, flags, blob[start:end]))
        offset = end + (-length & 3)
    for _ in range(child_count):
        child, offset = parse(blob, offset)
        node.children.append(child)
    return node, offset


def serialize(node):
    output = bytearray(struct.pack("<II", len(node.props), len(node.children)))
    for name, flags, value in node.props:
        encoded = name.encode()
        if len(encoded) > 31:
            raise ValueError(f"ADT property name too long: {name}")
        output += encoded.ljust(32, b"\0")
        output += struct.pack("<I", len(value) | (flags << 31))
        output += value + b"\0" * (-len(value) & 3)
    for child in node.children:
        output += serialize(child)
    return bytes(output)


def max_phandle(node):
    values = [node.prop("AAPL,phandle"), node.prop("phandle")]
    own = max((struct.unpack("<I", value)[0] for value in values
               if value and len(value) == 4), default=0)
    return max([own] + [max_phandle(child) for child in node.children])


def check_phandles(node, seen=None):
    seen = {} if seen is None else seen
    for key in ("phandle", "AAPL,phandle"):
        value = node.prop(key)
        if not value:
            continue
        if len(value) != 4:
            raise ValueError(f"{node.name}: {key} is not one cell")
        handle = struct.unpack("<I", value)[0]
        if not handle:
            raise ValueError(f"{node.name}: zero {key}")
        if handle in seen:
            raise ValueError(f"duplicate phandle {handle:#x}: {seen[handle]} and {node.name}")
        seen[handle] = node.name
    for child in node.children:
        check_phandles(child, seen)
    return seen


def copied_properties(source):
    return [(name, flags, value) for name, flags, value in source.props
            if not flags and name not in ("name", "AAPL,phandle")]


def load_adt(path):
    blob = Path(path).read_bytes()
    if blob.startswith(b"bvx2"):
        candidates = [os.environ.get("LZFSE", ""), "/opt/unixnu-linux/bin/lzfse", "lzfse"]
        for candidate in candidates:
            if not candidate:
                continue
            result = subprocess.run([candidate, "-decode"], input=blob,
                                    capture_output=True)
            if result.returncode == 0:
                blob = result.stdout
                break
        else:
            raise ValueError(f"{path}: LZFSE-compressed ADT; set LZFSE to the decoder")
    if blob[:1] == b"\x30" and b"IM4P" in blob[:32]:
        raise ValueError(f"{path}: IM4P-wrapped ADT is not accepted; extract its payload")
    return blob


def product_from_d22ap(path, next_phandle):
    blob = load_adt(path)
    root, used = parse(blob)
    if used != len(blob):
        raise ValueError(f"{path}: trailing bytes after ADT root")
    product = root.child("product")
    if product is None:
        raise ValueError(f"{path}: no /product node")

    def build(name, source, children=()):
        props = [("name", 0, cstr(name)), ("AAPL,phandle", 0, u32(next(next_phandle)))]
        props += copied_properties(source)
        return NodeFrom(props, children)

    audio = product.child("audio")
    children = [build("audio", audio)] if audio is not None else []
    return build("product", product, children)


def NodeFrom(props, children=()):  # noqa: N802 - compact local constructor
    node = Node()
    node.props = list(props)
    node.children = list(children)
    return node


def extent_properties(path):
    table = json.loads(Path(path).read_text())
    root_bytes = int(table["size"])
    if not root_bytes or root_bytes % 4096:
        raise ValueError("pinned root size is not a nonzero multiple of 4 KiB")
    words = []
    logical = 0
    ranges = []
    for entry in table["extents"]:
        if len(entry) != 4:
            raise ValueError("pinfile extent entry does not have four fields")
        file_off, part_off, length, flags = (int(value) for value in entry)
        if file_off != logical or not length or any(value % 4096 for value in
                                                    (file_off, part_off, length)):
            raise ValueError("extent table is not contiguous and 4 KiB aligned")
        # 0x1088/0x1089 are F2FS's ENCODED | DATA_ENCRYPTED | MERGED
        # plus optional LAST. Encryption affects file reads, not raw blocks.
        if flags & ~(0x8 | 0x80 | 0x1000 | 0x1):
            raise ValueError(f"unexpected FIEMAP flags 0x{flags:x}")
        words += [file_off // 4096, part_off // 4096, length // 4096]
        ranges.append((part_off, part_off + length))
        logical += length
    if logical < root_bytes:
        raise ValueError("extent table does not cover the complete pinned root")
    ranges.sort()
    for previous, current in zip(ranges, ranges[1:]):
        if previous[1] > current[0]:
            raise ValueError("pinned root has overlapping physical extents")
    return [
        ("fuxi-root-extents", struct.pack(f"<{len(words)}Q", *words)),
        ("fuxi-root-blocks", u64(root_bytes // 4096)),
    ]


def apply_policy(root, product_adt, extents):
    first_phandle = max_phandle(root) + 1
    next_phandle = iter(range(first_phandle, first_phandle + 1000))
    root.set_prop("compatible", b"".join(cstr(value) for value in (
        "D22AP", "iPhone10,3", "xiaomi,pandora", "nubia,nx741j",
        "qcom,sm8850", "AppleARM")))
    root.set_prop("model", cstr("iPhone10,3"))
    root.set_prop("target-type", cstr("D22"))
    root.set_prop("target-sub-type", cstr("D22AP"))
    root.set_prop("manufacturer", cstr("Apple Inc."))

    root.remove_child("product")
    root.children.append(product_from_d22ap(product_adt, next_phandle))

    chosen = root.child("chosen")
    if chosen is None:
        raise ValueError("generated ADT has no /chosen")
    chosen.remove_prop("root-matching")
    chosen.remove_prop("fuxi-root-extents")
    chosen.remove_prop("fuxi-root-blocks")
    chosen.set_prop("debug-enabled", u32(1))
    chosen.set_prop("display-scale", u32(3))
    chosen.set_prop("firmware-version", cstr("nx741j-shim"))
    chosen.set_prop("system-firmware-version", cstr("nx741j-shim"))
    chosen.set_prop("boot-manifest-hash", hashlib.sha384(b"sweet boot manifest").digest())
    chosen.set_prop("crypto-hash-method", cstr("sha2-384"))
    chosen.set_prop("certificate-production-status", u32(0))
    chosen.set_prop("effective-production-status-ap", u32(0))
    chosen.set_prop("certificate-security-mode", u32(1))
    chosen.set_prop("effective-security-mode-sep", u32(1))
    chosen.set_prop("mix-n-match-prevention-status", u32(0))
    chosen.set_prop("effective-security-mode-ap", u32(1))
    chosen.set_prop("amfi-only-platform-code", u32(0))
    chosen.set_prop("protected-data-access", u32(1))
    chosen.set_prop("amfi-allows-trust-cache-load", u32(0))
    for key, value in extents:
        chosen.set_prop(key, value)

    manifest = chosen.child("manifest-properties")
    if manifest is None:
        manifest = NodeFrom([("name", 0, cstr("manifest-properties")),
                             ("AAPL,phandle", 0, u32(next(next_phandle)))])
        chosen.children.append(manifest)
    manifest.set_prop("BNCH", bytes(48))

    asmb = chosen.child("asmb")
    if asmb is None:
        asmb = NodeFrom([("name", 0, cstr("asmb")),
                         ("AAPL,phandle", 0, u32(next(next_phandle)))])
        chosen.children.append(asmb)
    asmb.set_prop("lp-sip0", u64(0x6F))
    asmb.set_prop("lp-sip1", u32(1))

    defaults = root.child("defaults")
    if defaults is None:
        defaults = NodeFrom([("name", 0, cstr("defaults")),
                             ("AAPL,phandle", 0, u32(next(next_phandle)))])
        root.children.append(defaults)
    defaults.set_prop("no-effaceable-storage", u32(1))
    if root.child("options") is None:
        root.children.append(NodeFrom([("name", 0, cstr("options")),
                                       ("AAPL,phandle", 0, u32(next(next_phandle)))]))
    if root.child("buttons") is None:
        buttons = NodeFrom([("name", 0, cstr("buttons")),
                            ("AAPL,phandle", 0, u32(next(next_phandle)))])
        buttons.props += [
            ("button-names", 0, cstr("volup") + cstr("voldown") + cstr("hold")),
            ("home-button-type", 0, u32(2)),
            ("opposed-power-vol-buttons", 0, u32(1)),
        ]
        root.children.append(buttons)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--product-adt",
                        default=os.environ.get("UNIXNU_D22AP_ADT"))
    parser.add_argument("--root-extents",
                        default=os.environ.get("UNIXNU_ROOT_EXTENTS"))
    args = parser.parse_args()
    if not args.product_adt:
        parser.error("set UNIXNU_D22AP_ADT or pass --product-adt")
    if not args.root_extents:
        parser.error("set UNIXNU_ROOT_EXTENTS or pass --root-extents")

    source = Path(args.input).read_bytes()
    root, used = parse(source)
    if used != len(source):
        raise SystemExit("input ADT has trailing bytes")
    extents = extent_properties(args.root_extents)
    apply_policy(root, args.product_adt, extents)
    if root.prop("compatible", ).split(b"\0", 1)[0] != b"D22AP":
        raise SystemExit("root compatible was not rewritten to D22AP")
    chosen = root.child("chosen")
    if chosen.prop("root-matching") is not None:
        raise SystemExit("old APFS root-matching property is still present")
    if len(chosen.prop("fuxi-root-extents") or b"") % 24:
        raise SystemExit("fuxi-root-extents is not a sequence of u64 triples")
    check_phandles(root)
    output = serialize(root)
    Path(args.output).write_bytes(output)
    print(f"ADT postprocess: {len(output)} bytes, "
          f"{len(extents[0][1]) // 24} root extents, D22AP product")


if __name__ == "__main__":
    main()
