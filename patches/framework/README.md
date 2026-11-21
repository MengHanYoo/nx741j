# UniXNU framework patch

`UniXNU-ba9e14fb-nx741j.patch` contains the complete framework-side changes used
by the NX741J iOS 16 port. It is generated against:

```text
repository: https://github.com/UniXNU/UniXNU.git
branch:     main
commit:     ba9e14fb46cff0f2d3e8b1a006bd9f849cbd5ce6
```

The patch includes tracked edits and the required previously untracked files
(`macosx/`, `package/storage-bringup/`, `scripts/appsync/`, XNU patches, runtime
helpers and the storage archive builder). It intentionally excludes the local
backup `unixnu.local.toml.bak`.

```sh
git clone https://github.com/UniXNU/UniXNU.git
git -C UniXNU checkout ba9e14fb46cff0f2d3e8b1a006bd9f849cbd5ce6
./scripts/apply-framework-patch.sh ../UniXNU
```

The apply script refuses any other base and is idempotent only when the exact
patch is already present. To regenerate the patch from a re-qualified working
tree:

```sh
./scripts/export-framework-patch.sh ../UniXNU
```

The patch is the source of truth for framework deltas; do not commit generated
build trees, feeds, SDKs, IPSW files or WSL VHDX data.
