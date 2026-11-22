#!/usr/bin/env python3
"""Extract and hash every changed range named by diff-pinned-root.py."""

import argparse
import hashlib
import json
import pathlib


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", required=True, type=pathlib.Path)
    parser.add_argument("--base", required=True, type=pathlib.Path)
    parser.add_argument("--target", required=True, type=pathlib.Path)
    parser.add_argument("--out-dir", required=True, type=pathlib.Path)
    args = parser.parse_args()

    report = json.loads(args.report.read_text())
    if report.get("schema") != 1:
        raise SystemExit("unsupported root delta report")
    if args.target.stat().st_size != report["target_size"]:
        raise SystemExit("target size does not match delta report")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    ranges = []
    if args.base.stat().st_size != report["target_size"]:
        raise SystemExit("base size does not match delta report")
    with args.base.open("rb", buffering=0) as base, args.target.open(
        "rb", buffering=0
    ) as target:
        for index, source in enumerate(report["ranges"]):
            item = dict(source)
            expected_name = f"range-{index:04d}.bin"
            if item["file"] != expected_name:
                raise SystemExit(f"unexpected range filename {item['file']}")
            if item["bytes"] % 4096 or item["blocks"] * 4096 != item["bytes"]:
                raise SystemExit(f"{item['file']}: invalid block geometry")
            target.seek(item["logical_start"])
            data = target.read(item["bytes"])
            if len(data) != item["bytes"]:
                raise SystemExit(f"{item['file']}: short read")
            base.seek(item["logical_start"])
            before = base.read(item["bytes"])
            if len(before) != item["bytes"]:
                raise SystemExit(f"{item['file']}: short base read")
            path = args.out_dir / item["file"]
            path.write_bytes(data)
            item["base_sha256"] = sha256(before)
            item["sha256"] = sha256(data)
            ranges.append(item)

    manifest = {
        "schema": 1,
        "target_sha256": report["target_sha256"],
        "userdata_first_lba": report["userdata_first_lba"],
        "userdata_last_lba": report["userdata_last_lba"],
        "changed_blocks": report["changed_blocks"],
        "changed_bytes": report["changed_bytes"],
        "range_count": len(ranges),
        "ranges": ranges,
    }
    out = args.out_dir / "manifest.json"
    out.write_text(json.dumps(manifest, indent=2) + "\n")
    print(
        f"ranges={len(ranges)} changed_blocks={report['changed_blocks']} "
        f"changed_bytes={report['changed_bytes']} manifest={out}"
    )
    for item in ranges:
        print(
            f"{item['file']} lba={item['userdata_start_lba']} "
            f"blocks={item['blocks']} sha256={item['sha256']}"
        )


if __name__ == "__main__":
    main()
