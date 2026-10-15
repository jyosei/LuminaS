"""Verify the review package files using only the Python standard library."""
from pathlib import Path
import hashlib
import json
import sys

root = Path(__file__).resolve().parent
manifest = json.loads((root / 'MANIFEST.json').read_text(encoding='utf-8'))
failures = []
for item in manifest['files']:
    target = (root / item['path']).resolve()
    if root not in target.parents:
        failures.append(f"Invalid path: {item['path']}")
        continue
    if not target.is_file():
        failures.append(f"Missing: {item['path']}")
        continue
    data = target.read_bytes()
    if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
        failures.append(f"Changed: {item['path']}")
if failures:
    print('\n'.join(failures))
    sys.exit(1)
print(f"Verified {len(manifest['files'])} files. Manifest is not self-authenticating.")
