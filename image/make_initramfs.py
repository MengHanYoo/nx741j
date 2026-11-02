#!/usr/bin/env python3
"""Build an initramfs that applies image/manifest.json to a mounted HFS+ iOS root.

Requires a template directory with bin/busybox and the HFS+ kernel modules
(default /root/hfswork/init, produced by the port tooling).
"""
import argparse, json, os, shutil, subprocess, sys

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--artifacts", required=True)
    ap.add_argument("--template", default="/root/hfswork/init")
    ap.add_argument("--work", default="/root/imgbuild")
    ap.add_argument("--out", default="/root/imgbuild.gz")
    a = ap.parse_args()

    man = json.load(open(a.manifest, encoding="utf-8-sig"))
    work = a.work
    if os.path.exists(work):
        shutil.rmtree(work)
    os.makedirs(os.path.join(work, "files"), exist_ok=True)

    for tool in ("bin", "hfsplus.ko", "nls_utf8.ko", "nls_iso8859-1.ko"):
        src = os.path.join(a.template, tool)
        dst = os.path.join(work, tool)
        if os.path.isdir(src):
            shutil.copytree(src, dst, symlinks=True)
        else:
            shutil.copy2(src, dst)

    L = []
    L.append("#!/bin/busybox sh")
    L.append("/bin/busybox --install -s /bin")
    L.append("mkdir -p /proc /sys /dev /mnt")
    L.append("mount -t proc proc /proc 2>/dev/null")
    L.append("mount -t devtmpfs dev /dev 2>/dev/null")
    L.append("insmod /nls_utf8.ko 2>/dev/null")
    L.append("insmod /hfsplus.ko && echo INSMOD_OK")
    L.append("mount -t hfsplus -o force,nls=utf8 /dev/vda /mnt && echo MOUNT_OK || { echo MOUNT_FAIL; exec /bin/sh; }")

    missing = 0
    for d in man.get("mkdir", []):
        L.append("mkdir -p /mnt" + d)
    for r in man.get("remove", []):
        L.append("rm -rf /mnt" + r)
    for item in man["install"]:
        rel = item["src"]
        dst = item["dest"]
        src = os.path.join(a.artifacts, rel.replace("/", os.sep))
        if not os.path.exists(src):
            print("MISSING " + src, file=sys.stderr)
            missing += 1
            continue
        flat = rel.replace("/", "__")
        shutil.copy2(src, os.path.join(work, "files", flat))
        L.append('mkdir -p "/mnt' + os.path.dirname(dst) + '"')
        L.append('cp -f "/files/' + flat + '" "/mnt' + dst + '"')
        L.append('chmod ' + item["mode"] + ' "/mnt' + dst + '"')
        L.append('chown ' + item["owner"] + ' "/mnt' + dst + '"')
    if missing:
        print("missing artifacts: " + str(missing), file=sys.stderr)
        sys.exit(2)
    seed = man.get("seed")
    if seed:
        sp = os.path.join(a.artifacts, seed["src"])
        if not os.path.exists(sp):
            print("MISSING SEED " + sp, file=sys.stderr)
            sys.exit(2)
        shutil.copy2(sp, os.path.join(work, "seed.tar"))
        L.append("echo SEED_EXTRACT")
        L.append('tar xpf /seed.tar -C "/mnt' + seed.get("dest", "") + '" && echo SEED_OK || echo SEED_FAIL')
    L.append("echo APPLY_DONE")
    L.append("sync")
    L.append("umount /mnt && echo UMOUNT_OK")
    L.append("poweroff -f")
    with open(os.path.join(work, "init"), "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    os.chmod(os.path.join(work, "init"), 0o755)

    cmd = "cd " + work + " && find . | cpio -o -H newc 2>/dev/null | gzip -9 > " + a.out
    subprocess.run(["bash", "-lc", cmd], check=True)
    print("initramfs %s %.1f MB" % (a.out, os.path.getsize(a.out) / 1e6))

if __name__ == "__main__":
    main()