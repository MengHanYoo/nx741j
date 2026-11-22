#!/usr/bin/env python3
"""Install the data-a-live UFS-root path into the prepared XNU source tree.

The source file is kept outside the XNU patch so it can be audited against
data-a-live's 0028/0030/0034/0088/0118/0119/0120/0122 series directly.
This hook is idempotent and runs before the kernel link.
"""
from pathlib import Path

TREE = Path.cwd()
ROOT = TREE / ".cache/xnu-work" if (TREE / ".cache/xnu-work").is_dir() else TREE
DEVICE = Path(__file__).resolve().parents[1]
SOURCE_CANDIDATES = (
    *sorted(TREE.glob("kits/*/sweet-ufs/sweet_ufs.c")),
    *sorted(ROOT.glob("kits/*/sweet-ufs/sweet_ufs.c")),
    ROOT / "package/sweet-ufs/sweet_ufs.c",
    DEVICE / "package/sweet-ufs/sweet_ufs.c",
)


def read(relative):
    path = ROOT / relative
    if not path.is_file():
        raise SystemExit(f"install-sweet-ufs: missing {path}")
    return path, path.read_text()


def write(path, text):
    if path.read_text() != text:
        path.write_text(text)
        print(f"install-sweet-ufs: updated {path.relative_to(ROOT)}")


def replace_once(text, old, new, label):
    if old not in text:
        if new in text:
            return text
        raise SystemExit(f"install-sweet-ufs: anchor missing: {label}")
    return text.replace(old, new, 1)


def install_source():
    source = next((path for path in SOURCE_CANDIDATES if path.is_file()), None)
    if source is None:
        raise SystemExit(f"install-sweet-ufs: source missing; tried {SOURCE_CANDIDATES}")
    target = ROOT / "bsd/dev/sweet_ufs.c"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(source.read_bytes())
    print(f"install-sweet-ufs: copied {source} -> {target.relative_to(ROOT)}")


def install_files_table():
    path, text = read("bsd/conf/files")
    text = text.replace("bsd/dev/native_ufs_iova.c\t\t\tstandard\n", "")
    if "bsd/dev/sweet_ufs.c" not in text:
        text = replace_once(
            text,
            "bsd/dev/memdev.c\t\t\tstandard\n",
            "bsd/dev/memdev.c\t\t\tstandard\nbsd/dev/sweet_ufs.c\t\t\tstandard\n",
            "bsd/conf/files memdev",
        )
    write(path, text)


def install_root_device():
    path, text = read("iokit/bsddev/IOKitBSDInit.cpp")
    marker = "//\n//\tNow check if we are trying to root on a memory device\n//\n"
    block = """//
// NX741J: root on the pinned HFS+ file in Android userdata. The driver
// implements data-a-live's ufsroot0 BSD device and extent write policy.
//
if (strcmp(rdBootVar, "ufsroot0") == 0) {
\textern int sweet_ufs_rootdev(dev_t *dev);
\tOSSafeReleaseNULL(matching);
\tint ufs_error = sweet_ufs_rootdev(root);
\tif (ufs_error) {
\t\tpanic("IOFindBSDRoot: ufsroot0 unavailable: %d", ufs_error);
\t}
\tstrlcpy(rootName, rdBootVar, rootNameSize);
\tIOLog("BSD root: %s, major %d, minor %d\\n", rootName, major(*root), minor(*root));
\t*oflags = 0;
\tgoto iofrootx;
}

"""
    if "sweet_ufs_rootdev(root)" not in text:
        text = replace_once(text, marker, block + marker, "IOKitBSDInit memory-root marker")
    write(path, text)


def install_root_rw():
    path, text = read("bsd/vfs/vfs_subr.c")
    if "#include <pexpert/pexpert.h>" not in text:
        text = replace_once(
            text,
            "#include <sys/param.h>\n",
            "#include <sys/param.h>\n#include <pexpert/pexpert.h>\n",
            "vfs_subr sys/param include",
        )
    anchor = "\t\tmp->mnt_devvp = rootvp;\n"
    addition = """\t\tmp->mnt_devvp = rootvp;
\t\t/*
\t\t * data-a-live: /private/var is on this single HFS+ root. rootrw=1
\t\t * asks for a writable mount so its journal can be initialized.
\t\t */
\t\t{
\t\t\tuint32_t rootrw = 0;
\t\t\tif (PE_parse_boot_argn("rootrw", &rootrw, sizeof(rootrw)) && rootrw) {
\t\t\t\tmp->mnt_flag &= ~MNT_RDONLY;
\t\t\t}
\t\t}
"""
    if 'PE_parse_boot_argn("rootrw"' not in text:
        text = replace_once(text, anchor, addition, "vfs rootmountalloc mnt_devvp")
    write(path, text)


def install_unsealed_root():
    path, text = read("bsd/kern/bsd_init.c")
    owned = """\t\t\tif (autherr && !owner_authorized) {
\t\t\t\tpanic("rootvp not authenticated after mounting");
\t\t\t}
"""
    base = """\t\t\tif (autherr) {
\t\t\t\tpanic("rootvp not authenticated after mounting");
\t\t\t}
"""
    new = """\t\t\tif (autherr && !owner_authorized) {
\t\t\t\t/*
\t\t\t\t * data-a-live's HFS+ root is not an Apple sealed volume. There
\t\t\t\t * is no chain of trust to extend on this bring-up target.
\t\t\t\t */
\t\t\t\tprintf("NX741J: root volume is not authenticated (%d); continuing unsealed\\n",
\t\t\t\t    autherr);
\t\t\t}
"""
    if "continuing unsealed" not in text:
        if owned in text:
            text = text.replace(owned, new, 1)
        elif base in text:
            text = text.replace(base, """\t\t\tif (autherr) {
\t\t\t\t/*
\t\t\t\t * data-a-live's HFS+ root is not an Apple sealed volume. There
\t\t\t\t * is no chain of trust to extend on this bring-up target.
\t\t\t\t */
\t\t\t\tprintf("NX741J: root volume is not authenticated (%d); continuing unsealed\\n",
\t\t\t\t    autherr);
\t\t\t}
""", 1)
        else:
            raise SystemExit("install-sweet-ufs: anchor missing: bsd_init root authentication panic")
    write(path, text)


def install_rename_swap():
    path, text = read("bsd/vfs/kpi_vfs.c")
    if "SWEET: HFS has no RENAME_SWAP" in text:
        return
    anchor = """\t\t} else {
\t\t\t_err = VNOP_RENAME(fdvp, *fvpp, fcnp, tdvp, *tvpp, tcnp, ctx);
\t\t}
\t}

\t/*
\t * If moved to a new directory that is restricted,
"""
    addition = """\t\t} else {
\t\t\t_err = VNOP_RENAME(fdvp, *fvpp, fcnp, tdvp, *tvpp, tcnp, ctx);
\t\t}
\t}
\t/*
\t * SWEET: HFS has no RENAME_SWAP, which Foundation's atomic writes
\t * use. exchangedata is the same operation for two regular files.
\t */
\tif (_err == ENOTSUP && flags == VFS_RENAME_SWAP && *tvpp != NULLVP &&
\t    vnode_isreg(*fvpp) && vnode_isreg(*tvpp) &&
\t    vnode_mount(*fvpp) == vnode_mount(*tvpp)) {
\t\t_err = VNOP_EXCHANGE(*fvpp, *tvpp, 0, ctx);
\t}

\t/*
\t * If moved to a new directory that is restricted,
"""
    text = replace_once(text, anchor, addition, "kpi_vfs rename branch")
    write(path, text)


def main():
    install_source()
    install_files_table()
    install_root_device()
    install_root_rw()
    install_unsealed_root()
    install_rename_swap()
    print("install-sweet-ufs: ufsroot0, extent policy, rootrw and rename-swap installed")


if __name__ == "__main__":
    main()
