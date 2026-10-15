"""Create a curated Astra review snapshot and verify its archive contents."""
from pathlib import Path
import hashlib
import json
import posixpath
import re
import shutil
import zipfile

repo = Path(__file__).resolve().parent.parent
handoff = repo / 'handoff'
out = repo / 'deliverables'
name = 'Lumina-Review-2026-10-12-v1'
stage = out / name
archive = out / f'{name}.zip'
assert repo.name == 'Lumina'
assert not stage.exists() and not archive.exists(), 'Existing snapshot: use a new version; do not overwrite.'
qa_root = handoff / 'qa-results'
qa_report = json.loads((qa_root / 'qa/report.json').read_text(encoding='utf-8'))
assert qa_report['result'] == 'passed' and len(qa_report['layouts']) == 12
stage.mkdir(parents=True)

def copy(src, relative):
    dest = stage / relative
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest)

copy(handoff / 'PACK-README.md', 'README.md')
for filename in ['ASTRA-REVIEW.md', 'DECISIONS.md', 'REVIEW-PROMPT.md', 'verify-integrity.py']:
    copy(handoff / filename, filename)
for src in sorted((repo / 'docs').glob('*.md')):
    copy(src, Path('docs') / src.name)
# Pack context places the review decision file at the package root.
stack = stage / 'docs/02-stack.md'
stack.write_text(stack.read_text(encoding='utf-8').replace('../handoff/DECISIONS.md', '../DECISIONS.md'), encoding='utf-8')
for filename in ['lumina-studio-fragment.html', 'lumina-studio-preview.html']:
    copy(repo / 'prototype' / filename, Path('prototype') / filename)
for filename in ['portrait-v2.png', 'portrait-v2-preview.jpg', 'portrait-v2-source.md']:
    copy(repo / 'prototype/assets' / filename, Path('prototype/assets') / filename)
for src in sorted((qa_root / 'prototype/screenshots').glob('*.png')):
    copy(src, Path('prototype/screenshots') / src.name)
assert len(list((stage / 'prototype/screenshots').glob('*.png'))) == 5
copy(qa_root / 'qa/report.json', 'qa/report.json')
for filename in ['verify-prototype.cjs', 'README.md']:
    copy(handoff / 'qa' / filename, Path('qa') / filename)

sections = ['ASTRA-REVIEW.md', 'DECISIONS.md', 'REVIEW-PROMPT.md'] + [p.relative_to(stage).as_posix() for p in sorted((stage / 'docs').glob('*.md'))]
link_re = re.compile(r'(\[[^\]]*\]\()([^\s)]+)(\))')
def rebase(content, source):
    def replace(match):
        href = match.group(2)
        if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', href) or href.startswith(('/', '#')):
            return match.group(0)
        result = posixpath.normpath(posixpath.join(posixpath.dirname(source), href))
        return match.group(1) + result + match.group(3)
    return link_re.sub(replace, content)
parts = ['# Lumina · 完整审查汇总稿\n\nReview v1 · 2026-10-12。各章保留候选状态；原型与性能预算不代表已实现软件。\n\n']
for filename in sections:
    parts.append(f'\n---\n\n来源：[ {filename} ]({filename})\n\n' + rebase((stage / filename).read_text(encoding='utf-8'), filename))
(stage / 'ALL-IN-ONE.md').write_text(''.join(parts), encoding='utf-8')

# Verify relative Markdown targets, and ensure the curated set carries no machine cache paths.
for src in stage.rglob('*'):
    if not src.is_file() or src.suffix not in {'.md', '.cjs', '.py', '.html', '.json'}:
        continue
    content = src.read_text(encoding='utf-8')
    assert 'C:/Users/CascadeAI' not in content and 'C:\\Users\\CascadeAI' not in content, src
    if src.suffix == '.md':
        for _, href, _ in link_re.findall(content):
            if re.match(r'^[a-zA-Z][a-zA-Z0-9+.-]*:', href) or href.startswith(('/', '#')):
                continue
            target = (src.parent / href.split('#')[0]).resolve()
            assert stage.resolve() in target.parents and target.exists(), f'Broken or escaping link: {src.name} -> {href}'

files = []
for src in sorted(stage.rglob('*')):
    if src.is_file():
        data = src.read_bytes()
        files.append({'path': src.relative_to(stage).as_posix(), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
manifest = {'schema': 1, 'package': name, 'date': '2026-10-12', 'status': 'Design review; not implemented desktop software', 'qa': {'result': qa_report['result'], 'runAtUtc': qa_report['runAtUtc'], 'layoutCases': 12}, 'files': files}
(stage / 'MANIFEST.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for src in sorted(stage.rglob('*')):
        if src.is_file():
            z.write(src, Path(name) / src.relative_to(stage))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
    assert len(z.namelist()) == len(files) + 1
    for item in files:
        data = z.read(f"{name}/{item['path']}")
        assert len(data) == item['bytes'] and hashlib.sha256(data).hexdigest() == item['sha256']
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(out / f'{name}.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='ascii')
print(json.dumps({'archive': str(archive), 'unpacked': str(stage), 'files': len(files) + 1, 'bytes': archive.stat().st_size, 'sha256': digest, 'crcAndHashes': 'passed', 'markdownLinks': 'passed', 'qa': qa_report['result']}, ensure_ascii=False, indent=2))
