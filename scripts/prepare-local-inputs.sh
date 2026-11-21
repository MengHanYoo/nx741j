#!/bin/bash
# Rebuild deterministic payload inputs from the local IPSW extraction.
set -eu

REPO=$(cd "$(dirname "$0")/.." && pwd)
RAW=${UNIXNU_TRUSTCACHE_SOURCE:-$(cd "$REPO/../.." && pwd)/ipsw/087-86593-047.dmg.trustcache}
OUT=$REPO/build/trustcache.bundle

[ -f "$RAW" ] || {
  echo "trust-cache source not found: $RAW" >&2
  exit 1
}

python3 "$REPO/scripts/mk-trustcache-bundle.py" "$RAW" "$OUT"
python3 - "$REPO/board.toml" "$OUT" <<'PY'
import hashlib
from pathlib import Path
import sys
import tomllib

board = tomllib.loads(Path(sys.argv[1]).read_text())
allowed = board.get("payload", {}).get("trust_cache_sha256") or []
actual = hashlib.sha256(Path(sys.argv[2]).read_bytes()).hexdigest()
if allowed and actual not in allowed:
    raise SystemExit(
        f"{sys.argv[2]} sha256 {actual} is not in [payload] trust_cache_sha256"
    )
print(f"trustcache.bundle sha256 {actual}")
PY
