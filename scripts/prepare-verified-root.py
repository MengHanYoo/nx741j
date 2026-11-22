#!/usr/bin/env python3
"""Sign SHNGROOT windows and add header-only VerifiedRoot policies to userdata.

Each APFS window keeps its original bytes.  The ordered 4 KiB hashes are stored
as a version-2 SHNGROOT object named ``<window>.vroot``.  Only the signed
544-byte header/signature pair is compiled into the kernel.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tomllib
import uuid

BLOCK_BYTES = 4096
HEADER_SIGNATURE_BYTES = 160 + 384
TRUSTED_KEY_BYTES = 395


class WindowReader:
    def __init__(self, stream, offset: int, size: int):
        stream.seek(offset)
        self.stream = stream
        self.remaining = size

    def read(self, size: int = -1) -> bytes:
        if self.remaining == 0:
            return b""
        if size < 0 or size > self.remaining:
            size = self.remaining
        data = self.stream.read(size)
        if not data:
            raise ValueError("short read from userdata window")
        self.remaining -= len(data)
        return data


def parse_manifest(block: bytes) -> dict:
    if len(block) != BLOCK_BYTES or block[:8] != b"SHNGROOT":
        raise ValueError("userdata image has no SHNGROOT manifest")
    if hashlib.sha256(block[:4064]).digest() != block[4064:]:
        raise ValueError("SHNGROOT manifest checksum mismatch")
    version, count = struct.unpack_from("<II", block, 8)
    image_bytes = struct.unpack_from("<Q", block, 16)[0]
    if version not in (1, 2) or not count or count > 8:
        raise ValueError(f"unsupported SHNGROOT version/count {version}/{count}")
    entries = []
    for index in range(count):
        raw = block[24 + index * 128:24 + (index + 1) * 128]
        name = raw[:32].split(b"\0", 1)[0].decode("ascii")
        offset, size, block_size, flags = struct.unpack_from("<QQII", raw, 32)
        entries.append({
            "name": name,
            "offset": offset,
            "size": size,
            "block_size": block_size,
            "flags": flags,
            "sha256": raw[56:88].hex(),
        })
    return {"version": version, "count": count, "image_bytes": image_bytes, "entries": entries}


def entry_bytes(name: str, offset: int, size: int, block_size: int,
                flags: int, digest: bytes) -> bytes:
    encoded = name.encode("ascii")
    if not encoded or len(encoded) >= 32:
        raise ValueError(f"invalid SHNGROOT object name {name!r}")
    return struct.pack("<32sQQII32s40s", encoded, offset, size, block_size,
                       flags, digest, bytes(40))


def manifest_block(image_bytes: int, entries: list[dict]) -> bytes:
    block = bytearray(BLOCK_BYTES)
    block[:8] = b"SHNGROOT"
    struct.pack_into("<IIQ", block, 8, 2, len(entries), image_bytes)
    for index, entry in enumerate(entries):
        block[24 + index * 128:24 + (index + 1) * 128] = entry_bytes(
            entry["name"], entry["offset"], entry["size"], entry["block_size"],
            entry["flags"], bytes.fromhex(entry["sha256"]))
    block[4064:] = hashlib.sha256(block[:4064]).digest()
    return bytes(block)


def load_manifest_kit(kit: Path):
    source = kit / "tools" / "manifest.py"
    if not source.is_file():
        raise ValueError(f"{source} is missing")
    spec = importlib.util.spec_from_file_location("verified_root_manifest", source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def owner_keys(private: Path) -> tuple[Path, Path]:
    public = private.with_name(private.stem + "-public.pem")
    private.parent.mkdir(parents=True, exist_ok=True)
    if not private.exists():
        subprocess.run([
            "openssl", "genpkey", "-algorithm", "RSA",
            "-pkeyopt", "rsa_keygen_bits:3072", "-out", str(private),
        ], check=True)
        os.chmod(private, 0o600)
        print(f"generated owner key {private}")
    subprocess.run([
        "openssl", "pkey", "-in", str(private), "-pubout", "-out", str(public),
    ], check=True)
    return private, public


def sign_window(name: str, image: Path, offset: int, size: int, volume_uuid: uuid.UUID,
                generation: int, kit, private: Path, public: Path, output: Path) -> dict:
    work = output / (name + ".work")
    work.mkdir(parents=True, exist_ok=True)
    with image.open("rb") as stream:
        bounded = WindowReader(stream, offset, size)
        record = kit.make(bounded, size, private, public, volume_uuid, generation, work)
    header = (work / "header.bin").read_bytes()
    signature = (work / "signature.bin").read_bytes()
    if len(header) != 160 or len(signature) != 384:
        raise ValueError(f"{name}: unexpected signed header sizes")
    header_pair = output / (name + ".header.bin")
    hashes = output / (name + ".hashes.bin")
    header_pair.write_bytes(header + signature)
    os.replace(work / "hashes.bin", hashes)
    policy = work / "manifest.json"
    os.replace(policy, output / (name + ".json"))
    (work / "manifest.bin").unlink(missing_ok=True)
    for temporary in ("header.bin", "signature.bin", "trusted-key.bin"):
        (work / temporary).unlink(missing_ok=True)
    work.rmdir()
    record.update({
        "window": name,
        "header_bytes": HEADER_SIGNATURE_BYTES,
        "header_sha256": hashlib.sha256(header_pair.read_bytes()).hexdigest(),
        "hashes_file": hashes.name,
        "hashes_bytes": hashes.stat().st_size,
        "hashes_sha256": hashlib.sha256(hashes.read_bytes()).hexdigest(),
    })
    return record


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(16 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--board", type=Path, required=True)
    parser.add_argument("--kit", type=Path, required=True)
    parser.add_argument("--private-key", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--in-place", action="store_true",
                        help="append the .vroot objects and rewrite the SHNGROOT header")
    args = parser.parse_args()

    try:
        board = tomllib.loads(args.board.read_text())
        spec = board["userdata"]["verified_root"]
        generation = int(spec.get("generation", 1))
        uuids = {name: uuid.UUID(value) for name, value in spec["volume_uuid"].items()}
        report = json.loads(args.report.read_text())
        windows = report["windows"]
        if not windows:
            raise ValueError("userdata report has no windows")
        missing = [window["name"] for window in windows if window["name"] not in uuids]
        if missing:
            raise ValueError("missing APFS UUIDs for: " + ", ".join(missing))
        if args.image.stat().st_size < report["image_bytes"]:
            raise ValueError("userdata image is shorter than its report")
        with args.image.open("rb") as stream:
            manifest = parse_manifest(stream.read(BLOCK_BYTES))
        if manifest["image_bytes"] != report["image_bytes"]:
            raise ValueError("SHNGROOT and JSON image sizes differ")
        original_by_name = {entry["name"]: entry for entry in manifest["entries"]}
        for window in windows:
            original = original_by_name.get(window["name"])
            if not original or original["offset"] != window["offset"] or original["size"] != window["size"]:
                raise ValueError(f"{window['name']}: report and manifest disagree")

        args.output_dir.mkdir(parents=True, exist_ok=True)
        private, public = owner_keys(args.private_key)
        kit = load_manifest_kit(args.kit)
        policies = []
        for window in windows:
            policies.append(sign_window(
                window["name"], args.image, window["offset"], window["size"],
                uuids[window["name"]], generation, kit, private, public,
                args.output_dir))
        trusted = args.output_dir / "trusted-key.bin"
        # sign_window removes the per-window copy; derive the common anchor.
        trusted.write_bytes(kit.public_anchor(public))
        if trusted.stat().st_size != TRUSTED_KEY_BYTES:
            raise ValueError("unexpected trusted key size")
        if not args.in_place:
            print(json.dumps({"policies": policies, "image_modified": False}, indent=2))
            return 0

        base_bytes = report["image_bytes"]
        cursor = base_bytes
        objects = []
        for policy in policies:
            hashes = args.output_dir / policy["hashes_file"]
            size = hashes.stat().st_size
            cursor = (cursor + BLOCK_BYTES - 1) & -BLOCK_BYTES
            objects.append({
                "name": policy["window"] + ".vroot",
                "offset": cursor,
                "size": size,
                "block_size": BLOCK_BYTES,
                "flags": 3,
                "sha256": policy["hashes_sha256"],
                "source": str(hashes),
            })
            cursor += size
        image_bytes = (cursor + BLOCK_BYTES - 1) & -BLOCK_BYTES
        entries = [
            {"name": window["name"], "offset": window["offset"], "size": window["size"],
             "block_size": 512, "flags": 1, "sha256": window["sha256"]}
            for window in windows
        ] + objects
        header = manifest_block(image_bytes, entries)
        with args.image.open("r+b", buffering=0) as stream:
            stream.truncate(base_bytes)
            for item in objects:
                stream.seek(item["offset"])
                stream.write((args.output_dir / (Path(item["source"]).name)).read_bytes())
            stream.truncate(image_bytes)
            stream.seek(0)
            stream.write(header)
            stream.flush()
            os.fsync(stream.fileno())

        updated = dict(report)
        updated.update({
            "image_bytes": image_bytes,
            "image_sha256": sha256_file(args.image),
            "manifest_sha256": hashlib.sha256(header).hexdigest(),
            "base_image_bytes": base_bytes,
            "objects": [
                {key: value for key, value in item.items() if key != "source"}
                for item in objects
            ],
            "verified_root": {
                "generation": generation,
                "trusted_key_sha256": hashlib.sha256(trusted.read_bytes()).hexdigest(),
                "policies": policies,
                "header_only": True,
            },
            "hardware_tested": False,
        })
        args.report.write_text(json.dumps(updated, indent=2) + "\n")
        (args.output_dir / "policy.json").write_text(json.dumps(updated["verified_root"], indent=2) + "\n")
        print(json.dumps({
            "image": str(args.image),
            "image_bytes": image_bytes,
            "manifest_sha256": updated["manifest_sha256"],
            "objects": updated["objects"],
            "policy_dir": str(args.output_dir),
        }, indent=2))
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"prepare-verified-root: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
