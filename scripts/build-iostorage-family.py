#!/usr/bin/env python3
"""Experimental source IOStorageFamily builder retained for ABI investigation.

The prelinked IOStorageFamily creates every IOMedia with an empty content hint.
This recipe builds the public IOStorageFamily sources with the kernel flags already
used for ShengUfs, propagates the provider's ``Content Hint`` into IOMedia, and
records a marker the kernel and prelink audits can verify.

The default NX741J build does not use this archive. Replacing the full bundle
requires resolving 86 private function/data bindings, so the board preserves
Apple's bundle and applies one pinned text-literal patch instead. Do not add this
archive to the link without completing that ABI mapping.
"""
import hashlib
import json
import shutil
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "refer/IOStorageFamily"
COMPILE_RECORD = ROOT / "build/sheng-ufs/compile-command.json"
OUT = ROOT / "build/iostorage"
WORK = OUT / "src"
ARCHIVE = OUT / "libIOStorageFamily.a"
SOURCE_FILES = ("IOMedia.cpp", "IOBlockStorageDriver.cpp", "IOBlockStorageDevice.cpp")
MARKER = b"NXUFS storage"


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def prepare_source():
    if not (SOURCE / "IOBlockStorageDriver.cpp").is_file():
        raise SystemExit(f"Missing public IOStorageFamily source at {SOURCE}")
    if WORK.exists():
        shutil.rmtree(WORK)
    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copytree(SOURCE, WORK, ignore=shutil.ignore_patterns(".git", "IconResources"))

    cpp_path = WORK / "IOBlockStorageDriver.cpp"
    header_path = WORK / "IOBlockStorageDriver.h"
    cpp = cpp_path.read_text()
    header = header_path.read_text()

    cpp = replace_once(cpp, '#include "IOUserBlockStorageDevice_kext.h"\n', "",
                       "remove DriverKit-only header")
    cpp = replace_once(
        cpp,
        "_userBlockStorageDevice = OSDynamicCast(IOUserBlockStorageDevice, provider);",
        "_userBlockStorageDevice = nullptr;",
        "disable DriverKit user-block adapter",
    )
    cpp = replace_once(
        cpp,
        "_perfControlClient = _userBlockStorageDevice->getPerfControlClient();",
        "_perfControlClient = nullptr;",
        "disable DriverKit perf-control lookup",
    )
    header = replace_once(
        header,
        "        IOUserBlockStorageDevice * userBlockStorageDevice;",
        "        IOService * userBlockStorageDevice;",
        "use kernel IOService storage",
    )
    # The compile command's fakeroot has an older public storage header. Put
    # the patched header in an earlier include root so the source copy wins.
    include_header = OUT / "include/IOKit/storage/IOBlockStorageDriver.h"
    include_header.parent.mkdir(parents=True, exist_ok=True)
    include_header.write_text(header)
    header_path.write_text(header)

    lines = cpp.splitlines(keepends=True)
    init_index = None
    hint_index = None
    for index, line in enumerate(lines):
        if "result = m->init(" in line and "base byte offset" in line:
            init_index = index
        if '"");' in line and "content hint" in line:
            hint_index = index
    if init_index is None or hint_index is None or init_index >= hint_index:
        raise SystemExit("Could not locate instantiateMediaObject's IOMedia init call")

    lines[hint_index] = lines[hint_index].replace('"");', "contentHint);", 1)
    lines[init_index:init_index] = [
        '    OSString *contentHintProperty = OSDynamicCast(OSString, getProvider()->getProperty(kIOMediaContentHintKey));\n',
        '    const char *contentHint = contentHintProperty ? contentHintProperty->getCStringNoCopy() : "";\n',
        '    IOLog("NXUFS storage: own IOStorageFamily media hint=%s\\n", contentHint);\n',
    ]
    cpp_path.write_text("".join(lines))


def compile_objects():
    if not COMPILE_RECORD.is_file():
        raise SystemExit(f"Missing {COMPILE_RECORD}; build the ShengUfs component first")
    template = json.loads(COMPILE_RECORD.read_text())
    if not isinstance(template, list):
        raise SystemExit(f"{COMPILE_RECORD}: expected the compiler argument list")

    include = [
        "-I", str(OUT / "include"),
        "-I", str(WORK),
        "-I", str(ROOT / "build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT"),
        "-I", str(ROOT / ".cache/xnu-work/osfmk"),
        "-I", str(ROOT / ".cache/xnu-work/iokit"),
        "-I", str(ROOT / "build/storage-abi/include"),
        "-Wno-error",
    ]
    objects = []
    for name in SOURCE_FILES:
        source = WORK / name
        obj = OUT / f"{name}.o"
        command = []
        source_replaced = target_replaced = False
        for argument in template:
            if argument.endswith("NativeUfs.cpp"):
                command.append(str(source))
                source_replaced = True
            elif argument.endswith("NativeUfs.o"):
                command.append(str(obj))
                target_replaced = True
            else:
                command.append(argument)
        if not source_replaced or not target_replaced:
            raise SystemExit(f"{COMPILE_RECORD}: no NativeUfs source/object placeholders")
        # The compile record already carries the generated storage ABI include
        # path. Put our shim first, then keep the remaining post-compile flags.
        command = command[:1] + include[:2] + command[1:] + include[2:]
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if result.returncode:
            print(result.stderr[-8000:])
            raise SystemExit(f"Failed to compile {name}")
        objects.append(obj)
    return objects


def main():
    prepare_source()
    objects = compile_objects()
    ARCHIVE.unlink(missing_ok=True)
    archive_tool = shutil.which("llvm-ar") or shutil.which("ar")
    if not archive_tool:
        raise SystemExit("Neither llvm-ar nor ar is available")
    subprocess.run([archive_tool, "rcs", str(ARCHIVE), *map(str, objects)], check=True)
    if MARKER not in ARCHIVE.read_bytes():
        raise SystemExit(f"{ARCHIVE} does not contain the {MARKER.decode()!r} marker")

    revision_command = subprocess.run(
        ["git", "-C", str(SOURCE), "rev-parse", "--short=8", "HEAD"],
        text=True, capture_output=True,
    )
    revision = revision_command.stdout.strip() if revision_command.returncode == 0 else "unknown"
    report = {
        "archive": str(ARCHIVE.relative_to(ROOT)),
        "archive_sha256": sha256(ARCHIVE),
        "source_revision": revision,
        "source_sha256": {name: sha256(WORK / name) for name in SOURCE_FILES},
        "objects": {path.name: sha256(path) for path in objects},
        "marker": MARKER.decode(),
    }
    (OUT / "build-report.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    print(f"== iostorage: {ARCHIVE} sha256 {report['archive_sha256']}")


if __name__ == "__main__":
    main()
