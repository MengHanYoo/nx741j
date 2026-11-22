#!/usr/bin/env python3
"""Apply the NX741J iOS 16 bring-up state missing from a plain IPSW root.

Run as root against the mounted HFSX image. This is intentionally separate
from ios-fuxi's generic sweet-root-tweaks.py: these entries are required by
the NX741J data-a-live root so libMobileGestalt and launchd do not deadlock
before SpringBoard starts.
"""

import argparse
import os
import pathlib
import plistlib
import uuid


MG_IDENTIFIER = "systemgroup.com.apple.mobilegestaltcache"
MG_UUID_NAMESPACE = uuid.uuid5(
    uuid.NAMESPACE_OID, "NX741J/data-a-live/ios-16.7.16-20H392"
)

DISABLED = {
    "com.apple.CommCenter": True,
    "com.apple.magicswitchd.companion": True,
    "com.apple.security.otpaird": True,
    "com.apple.bluetoothd": True,
    "com.apple.CommCenter-noncellular": True,
    "com.apple.dhcp6d": True,
    "com.apple.driverkitd": True,
    "com.apple.bootpd": True,
    "com.apple.fairplayd.H2": True,
    "com.apple.ftp-proxy-embedded": False,
    "com.apple.relevanced": True,
    "com.apple.sirittsd": True,
}

SPRINGBOARD = {
    "BKNoWatchdogs": True,
    "SBDontDimOrLockOnAC": True,
    "SBDisableIdleSleep": True,
    "SBDisableAutoDim": True,
}


def load_or_empty(path):
    if path.exists():
        return plistlib.loads(path.read_bytes())
    return {}


def save(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(plistlib.dumps(data, fmt=plistlib.FMT_BINARY))


def chown(path, uid, gid, mode):
    os.chown(path, uid, gid)
    os.chmod(path, mode)


def mobilegestalt(root):
    container = (
        root
        / "private/var/containers/Shared/SystemGroup"
        / MG_IDENTIFIER
    )
    caches = container / "Library/Caches"
    caches.mkdir(parents=True, exist_ok=True)
    for path, mode in ((container, 0o755), (container / "Library", 0o755), (caches, 0o755)):
        chown(path, 0, 0, mode)

    metadata_path = container / ".com.apple.mobile_container_manager.metadata.plist"
    metadata = {
        "MCMMetadataActiveDPClass": -1,
        "MCMMetadataContentClass": 13,
        "MCMMetadataIdentifier": MG_IDENTIFIER,
        "MCMMetadataInfo": {
            "com.apple.MobileGestalt.ContentProtectionClass": -1,
        },
        "MCMMetadataSchemaVersion": 1,
        "MCMMetadataUUID": str(
            uuid.uuid5(MG_UUID_NAMESPACE, MG_IDENTIFIER)
        ).upper(),
        "MCMMetadataVersion": 6,
    }
    save(metadata_path, metadata)
    chown(metadata_path, 0, 0, 0o644)

    cache_path = caches / "com.apple.MobileGestalt.plist"
    save(
        cache_path,
        {
            "CacheData": plistlib.dumps({}, fmt=plistlib.FMT_BINARY),
            "CacheVersion": 1,
        },
    )
    chown(cache_path, 0, 0, 0o644)
    print(f"MobileGestalt container: {container}")
    print(f"MobileGestalt UUID: {metadata['MCMMetadataUUID']}")


def disabled_jobs(root):
    path = root / "private/var/db/com.apple.xpc.launchd/disabled.plist"
    current = load_or_empty(path)
    current.update(DISABLED)
    save(path, current)
    chown(path, 0, 0, 0o644)
    print(f"launchd disabled jobs: {len(current)} entries")


def springboard(root):
    path = (
        root
        / "private/var/mobile/Library/Preferences"
        / "com.apple.springboard.plist"
    )
    current = load_or_empty(path)
    current.update(SPRINGBOARD)
    save(path, current)
    chown(path, 501, 501, 0o600)
    print(f"SpringBoard power policy: {sorted(SPRINGBOARD)}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=pathlib.Path)
    args = parser.parse_args()
    root = args.root.resolve()
    if os.geteuid() != 0 or root == pathlib.Path("/"):
        parser.error("run as root against a mounted image, never /")
    for required in (
        "sbin/launchd",
        "System/Library/xpc/launchd.plist",
        "private/var/mobile/Library/Preferences",
    ):
        if not (root / required).exists():
            parser.error(f"not a staged iOS root: missing {required}")
    mobilegestalt(root)
    disabled_jobs(root)
    springboard(root)


if __name__ == "__main__":
    main()
