#!/usr/bin/env python3
"""Fetch the pinned eBook Fonts collection into the ignored vendor directory."""
import argparse
import io
import zipfile
import hashlib
import json
import os
from pathlib import Path
import re
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MAX_FONT_BYTES = 16 * 1024 * 1024


def collection(root=ROOT):
    spec = json.loads((root / 'resources/fonts/ebook-core.json').read_text())
    if spec['repository'] != 'https://github.com/nicoverbruggen/ebook-fonts' or not re.fullmatch(r'[0-9a-f]{40}', spec['revision']):
        raise ValueError('Invalid eBook Fonts repository or revision')
    for release in spec.get('releases', {}).values():
        if not re.fullmatch(r'https://github\.com/nicoverbruggen/libron/releases/download/v[0-9.]+/Libron\.zip', release['url']) or not re.fullmatch(r'[0-9a-f]{64}', release['sha256']):
            raise ValueError('Invalid font release')
    names = set()
    for item in spec['files']:
        name = item['file']
        if not re.fullmatch(r'[A-Za-z0-9_-]+\.ttf', name) or name in names or not re.fullmatch(r'[0-9a-f]{64}', item['sha256']):
            raise ValueError('Invalid eBook Fonts file entry')
        if item.get('collection', spec['collection']) not in ('core', 'extra'):
            raise ValueError('Invalid eBook Fonts file collection')
        if type(item.get('bundled', False)) is not bool:
            raise ValueError('Invalid bundled font flag')
        if 'release' in item and (item['release'] not in spec.get('releases', {}) or item.get('bundled')):
            raise ValueError('Invalid font release reference')
        names.add(name)
    if not names or spec['collection'] != 'core':
        raise ValueError('Invalid eBook Fonts collection')
    return spec


def directory(root=ROOT):
    return root / 'vendor/ebook-fonts'


def font_path(name, root=ROOT):
    return checked_path(directory(root), name, root)


def bundled_path(name, root=ROOT):
    return checked_path(root / 'resources/fonts', name, root)


def checked_path(base, name, root):
    path = base / name
    # Do not overwrite files outside this checkout through a linked cache.
    for part in (path, *path.parents):
        if part == root:
            break
        if part.is_symlink():
            raise ValueError('Linked font cache path: ' + str(part))
    path.resolve().relative_to(base.resolve())
    return path


def matches(path, checksum):
    return path.is_file() and path.stat().st_size <= MAX_FONT_BYTES and hashlib.sha256(path.read_bytes()).hexdigest() == checksum


def verify(root=ROOT):
    for item in collection(root)['files']:
        if item.get('bundled') and not matches(bundled_path(item['file'], root), item['sha256']):
            raise ValueError('Missing or changed bundled font: ' + item['file'])
        if not matches(font_path(item['file'], root), item['sha256']):
            raise ValueError('Missing or changed font: ' + item['file'] + '. Run python3 tools/fetch_fonts.py.')


def fetch(root=ROOT, offline=False):
    spec = collection(root)
    archives = {}
    for item in spec['files']:
        path = font_path(item['file'], root)
        bundled = bundled_path(item['file'], root) if item.get('bundled') else None
        if bundled is not None and not matches(bundled, item['sha256']):
            raise ValueError('Missing or changed bundled font: ' + item['file'])
        if matches(path, item['sha256']):
            continue
        if offline and bundled is None:
            raise ValueError('Missing or changed font in offline mode: ' + item['file'])
        source = item.get('collection', spec['collection'])
        url = f"https://raw.githubusercontent.com/nicoverbruggen/ebook-fonts/{spec['revision']}/fonts/{source}/{item['file']}"
        if bundled is not None:
            data = bundled.read_bytes()
        elif 'release' in item:
            key = item['release']
            release = spec['releases'][key]
            if key not in archives:
                with urllib.request.urlopen(release['url'], timeout=60) as response:
                    archive = response.read(MAX_FONT_BYTES + 1)
                if len(archive) > MAX_FONT_BYTES or hashlib.sha256(archive).hexdigest() != release['sha256']:
                    raise ValueError('Font release checksum or size mismatch: ' + key)
                archives[key] = archive
            with zipfile.ZipFile(io.BytesIO(archives[key])) as package:
                entry = package.getinfo(item['file'])
                if entry.file_size > MAX_FONT_BYTES:
                    raise ValueError('Font size mismatch: ' + item['file'])
                data = package.read(entry)
        else:
            with urllib.request.urlopen(url, timeout=60) as response:
                data = response.read(MAX_FONT_BYTES + 1)
        if len(data) > MAX_FONT_BYTES or hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError('Font checksum or size mismatch: ' + item['file'])
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.font-', delete=False) as output:
                temporary = Path(output.name)
                output.write(data)
            temporary.chmod(0o644)
            os.replace(temporary, path)
        finally:
            if temporary is not None:
                temporary.unlink(missing_ok=True)
    verify(root)
    return directory(root)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--offline', action='store_true', help='verify cached fonts without downloading')
    args = parser.parse_args()
    print(fetch(offline=args.offline))


if __name__ == '__main__':
    main()
