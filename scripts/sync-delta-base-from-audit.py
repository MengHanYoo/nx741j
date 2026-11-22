#!/usr/bin/env python3
"""Apply live audit blocks into a partial base image and refresh delta hashes."""

import argparse
import json
import pathlib
import re


def local_path(text):
    match = re.match(r"^([A-Za-z]):[\\/](.*)$", text)
    if not match:
        return pathlib.Path(text)
    drive = match.group(1).lower()
    tail = match.group(2).replace("\\", "/")
    return pathlib.Path(f"/mnt/{drive}/{tail}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audit", required=True, type=pathlib.Path)
    parser.add_argument("--base-image", required=True, type=pathlib.Path)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    args = parser.parse_args()

    audit = json.loads(args.audit.read_text(encoding="utf-8-sig"))
    report = json.loads(args.report.read_text(encoding="utf-8-sig"))
    manifest = json.loads(args.manifest.read_text(encoding="utf-8-sig"))
    audit_by_file = {item["file"]: item for item in audit["mismatches"]}
    report_by_file = {item["file"]: item for item in report["ranges"]}
    manifest_by_file = {item["file"]: item for item in manifest["ranges"]}

    with args.base_image.open("r+b", buffering=0) as base:
        for name, mismatch in sorted(audit_by_file.items()):
            if name not in report_by_file or name not in manifest_by_file:
                raise SystemExit(f"{name}: missing from delta report")
            source = local_path(mismatch["live_path"])
            data = source.read_bytes()
            if len(data) != mismatch["blocks"] * 4096:
                raise SystemExit(f"{name}: live audit block size mismatch")
            base.seek(mismatch["logical_start"])
            base.write(data)
            manifest_by_file[name]["base_sha256"] = mismatch["live_sha256"]

    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    print(
        f"synced={len(audit_by_file)} base_image={args.base_image} "
        f"manifest={args.manifest}"
    )


if __name__ == "__main__":
    main()
