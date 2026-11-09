#!/usr/bin/env python3
"""Generate a verified-root policy envelope for an image.

The VerifiedRoot component is split in two halves:

* tools/manifest.py signs an image: it hashes every 4096-byte block, writes a
  160-byte header (magic, sizes, volume UUID, image and hash-table digests, key
  digest, generation), signs the header with an RSA-3072 key and appends the
  ordered block hashes.
* src/verified_root.c verifies the envelope at block granularity, using a
  395-byte trusted key anchor (magic "VROOTKEY" + modulus + 0x010001).

This wrapper keeps the device workflow in one place: one owner key per device
repository, one envelope per authenticated image, and a size report so the
envelope is not generated into a partition that cannot hold it.

Examples:
    # the numbers for the current dump, without hashing 17 GiB
    mk-verified-root-policy.py --image iosroot-GOLDEN.img --estimate

    # a real envelope (owner key generated on first use)
    mk-verified-root-policy.py --kit <tree>/kits/VerifiedRoot \
        --image root.img --volume-uuid <uuid> --generation 1 --output out/vr \
        --private-key ~/.config/unixnu/nx741j-owner.pem

The private key never goes into the repository: by default it is written next
to the output, and the script refuses a key inside a work tree that git does
not ignore.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import subprocess
import sys
import uuid
from pathlib import Path

BLOCK_BYTES = 4096
HASH_BYTES = 32
HEADER_BYTES = 160
SIGNATURE_BYTES = 384
PAYLOAD_BUDGET = 64 * 1024 * 1024


def estimate(image: Path):
    size = image.stat().st_size
    blocks = size // BLOCK_BYTES + (size % BLOCK_BYTES != 0)
    return {
        'image': str(image),
        'image_bytes': size,
        'block_bytes': BLOCK_BYTES,
        'block_count': blocks,
        'hash_table_bytes': blocks * HASH_BYTES,
        'envelope_bytes': HEADER_BYTES + SIGNATURE_BYTES + blocks * HASH_BYTES,
    }


def load_kit(kit: Path):
    path = kit / 'tools' / 'manifest.py'
    if not path.is_file():
        sys.exit(f'{kit} is not a VerifiedRoot checkout: {path} is missing')
    spec = importlib.util.spec_from_file_location('verified_root_manifest', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def git_ignored(path: Path) -> bool:
    result = subprocess.run(['git', '-C', str(path.parent), 'check-ignore', '-q', str(path)],
                            capture_output=True)
    return result.returncode == 0


def owner_key(path: Path):
    private = path
    public = path.with_name(path.stem + '-public.pem')
    if not private.exists():
        private.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'RSA',
                        '-pkeyopt', 'rsa_keygen_bits:3072', '-out', str(private)], check=True)
        os.chmod(private, 0o600)
        print(f'generated owner key {private}')
    subprocess.run(['openssl', 'pkey', '-in', str(private), '-pubout', '-out', str(public)], check=True)
    return private, public


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--image', type=Path, required=True, help='image to authenticate')
    parser.add_argument('--kit', type=Path, help='VerifiedRoot checkout (needs tools/manifest.py)')
    parser.add_argument('--private-key', type=Path, help='owner key (default: <output>/owner.pem)')
    parser.add_argument('--volume-uuid', type=uuid.UUID)
    parser.add_argument('--generation', type=int, default=1)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--estimate', action='store_true', help='only report the envelope size')
    args = parser.parse_args()

    if not args.image.is_file():
        parser.error(f'{args.image} is not a regular file')
    numbers = estimate(args.image)
    if args.estimate:
        print(json.dumps(numbers, indent=2))
        table = numbers['hash_table_bytes']
        if table > PAYLOAD_BUDGET:
            print(f'note: {table / 2**20:.1f} MiB of hashes does not fit the 64 MiB payload '
                  'budget; the envelope has to be delivered from disk, not from the boot payload.',
                  file=sys.stderr)
        return 0

    for required in ('kit', 'volume_uuid', 'output'):
        if not getattr(args, required):
            parser.error('--' + required.replace('_', '-') + ' is required unless --estimate is given')

    key = args.private_key or (args.output / 'owner.pem')
    if args.private_key is None and git_ignored(key) is False:
        parser.error(f'{key} would land in a tracked path; pass --private-key outside the '
                     'work tree or ignore the path')

    private, public = owner_key(key)
    kit = load_kit(args.kit)
    with args.image.open('rb') as stream:
        record = kit.make(stream, numbers['image_bytes'], private, public,
                          args.volume_uuid, args.generation, args.output)
    record.update({'image': str(args.image), 'envelope_bytes': numbers['envelope_bytes'],
                   'owner_key': str(private)})
    (args.output / 'policy.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))
    print()
    print(f'trusted key : {args.output / "trusted-key.bin"} (395 B -> verified_root_key)')
    print(f'envelope    : {args.output / "manifest.bin"} '
          f'({numbers["envelope_bytes"] / 2**20:.1f} MiB = 160 + 384 + '
          f'{numbers["block_count"]} * 32)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
