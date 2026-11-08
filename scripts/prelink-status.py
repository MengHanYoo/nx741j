#!/usr/bin/env python3
"""Report prelink qualification status and the evidence still required."""
import json
import sys
from pathlib import Path

repo = Path(__file__).resolve().parent.parent
uni = Path(__import__('os').environ.get('UNIXNU', repo.parent / 'UniXNU'))
report = uni / 'out/nx741j/ios-16.7.16-20H392/nx741j/kernel/kernel.json'
if not report.is_file():
    sys.exit(f'prelink: no report at {report}; run unixnu kernel first')
d = json.loads(report.read_text())
rt, abi = d.get('runtime_qualified', False), d.get('abi_qualified', False)
print(f'prelink: runtime_qualified={rt} abi_qualified={abi}')
print(f'prelink: components={len(d.get("components") or [])} bindings={len(d.get("bindings") or [])}')
print(f'prelink: early_security_initializers={d.get("early_security_initializers")}')
if not (rt and abi):
    print('prelink: qualification requires a booted device log (the tool marks it false until')
    print('         hardware evidence exists; do not fake it in the report).')
    sys.exit(1)
