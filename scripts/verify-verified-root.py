#!/usr/bin/env python3
"""Verify header-only policies against the actual windows in userdata-root.img."""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import uuid


class View(ctypes.Structure):
    _fields_ = [
        ("hashes", ctypes.c_void_p),
        ("image_bytes", ctypes.c_uint64),
        ("block_count", ctypes.c_uint64),
        ("generation", ctypes.c_uint64),
        ("volume_uuid", ctypes.c_uint8 * 16),
        ("image_sha256", ctypes.c_uint8 * 32),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--policy-dir", type=Path, required=True)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    report = json.loads(args.report.read_text())
    root = report["verified_root"]
    generation = int(root["generation"])
    key_bytes = (args.policy_dir / "trusted-key.bin").read_bytes()
    if len(key_bytes) != 395:
        raise SystemExit("trusted-key.bin must be exactly 395 bytes")
    library = ctypes.CDLL(str(args.library.resolve()))
    library.vr_host_open.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_uint64,
        ctypes.POINTER(View),
    ]
    library.vr_host_block.argtypes = [
        ctypes.POINTER(View), ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t,
    ]
    key = ctypes.create_string_buffer(key_bytes)
    verified = []
    for policy in root["policies"]:
        name = policy["window"]
        header = (args.policy_dir / (name + ".header.bin")).read_bytes()
        hashes = (args.policy_dir / policy["hashes_file"]).read_bytes()
        if len(header) != 544:
            raise SystemExit(f"{name}: signed header is not 544 bytes")
        envelope = ctypes.create_string_buffer(header + hashes)
        view = View()
        status = library.vr_host_open(envelope, len(header) + len(hashes), key,
                                      generation, ctypes.byref(view))
        if status:
            raise SystemExit(f"{name}: signature/header verification failed: {status}")
        if bytes(view.volume_uuid) != uuid.UUID(policy["volume_uuid"]).bytes:
            raise SystemExit(f"{name}: signed volume UUID differs")
        window = next(item for item in report["windows"] if item["name"] == name)
        expected_blocks = (window["size"] + 4095) // 4096
        if view.image_bytes != window["size"] or view.block_count != expected_blocks:
            raise SystemExit(f"{name}: signed capacity differs from the SHNGROOT window")
        digest = hashlib.sha256()
        with args.image.open("rb") as stream:
            stream.seek(window["offset"])
            for index in range(view.block_count):
                remaining = view.image_bytes - index * 4096
                want = min(4096, remaining)
                block = stream.read(want)
                if len(block) != want:
                    raise SystemExit(f"{name}: short read before block {index}")
                if library.vr_host_block(ctypes.byref(view), index, block, len(block)):
                    raise SystemExit(f"{name}: block {index} failed authentication")
                digest.update(block)
        if digest.digest() != bytes(view.image_sha256):
            raise SystemExit(f"{name}: complete image digest differs")
        verified.append({
            "window": name,
            "volume_uuid": str(uuid.UUID(bytes=bytes(view.volume_uuid))).upper(),
            "image_bytes": view.image_bytes,
            "blocks_verified": view.block_count,
            "image_sha256": digest.hexdigest(),
            "generation": view.generation,
        })
        print(f"PASS  {name}: {view.block_count} blocks authenticated")
    output = {
        "real_c_signature_and_block_verification": True,
        "trusted_key_sha256": hashlib.sha256(key_bytes).hexdigest(),
        "windows": verified,
        "host_only": True,
        "boot_root_authenticated": False,
        "hardware_tested": False,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
