"""Build the user-storage assets for a Kobalt release."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import tempfile
import fetch_fonts
from version import release_version, validate as validate_version

ROOT = Path(__file__).resolve().parents[1]
UI_FONTS = (
    'IBMPlexSans-Regular.ttf', 'IBMPlexSans-Italic.ttf', 'IBMPlexSans-Bold.ttf',
    'IBMPlexMono-Regular.ttf', 'IBMPlexMono-Bold.ttf',
    'InstrumentSerif-Regular.ttf', 'InstrumentSerif-Italic.ttf',
)
# Fallback faces for characters that a reading font lacks.
FALLBACK_FONTS = ('NotoSerif-Regular.ttf', 'NotoSansMath-Regular.ttf', 'NotoSerifCJK-Regular.ttc',
                  'NotoNaskhArabic-Regular.ttf', 'NotoSerifHebrew-Regular.ttf',
                  'NotoSerifArmenian-Regular.ttf', 'NotoSerifGeorgian-Regular.ttf', 'NotoSerifDevanagari-Regular.ttf', 'NotoSerifBengali-Regular.ttf', 'NotoSerifTamil-Regular.ttf', 'NotoSerifThai-Regular.ttf')
# Hyphenation dictionaries from the pinned engine. Other languages keep the
# engine's algorithmic hyphenation.
HYPHENATION = (
    'Dutch.pattern', 'English_GB.pattern', 'English_US.pattern', 'French.pattern', 'German.pattern',
    'Italian.pattern', 'Portuguese.pattern', 'Portuguese_BR.pattern', 'Spanish.pattern',
)
# Installed asset folders that the reader accepts.
FOLDERS = ('fonts/', 'hyph/')


def source_path(name, root=ROOT):
    if name.startswith('fonts/ebook-core/'):
        return fetch_fonts.font_path(name.removeprefix('fonts/ebook-core/'), root)
    resources = root / 'resources'
    path = resources / name
    path.resolve().relative_to(resources.resolve())
    return path


def manifest(root=ROOT, version=None):
    fetch_fonts.verify(root)
    fonts = root / 'resources/fonts'
    core = json.loads((fonts / 'ebook-core.json').read_text())
    names = ['fonts/' + name for name in ('ebook-core.json', *UI_FONTS, *FALLBACK_FONTS, *('ebook-core/' + item['file'] for item in core['files']))]
    names += ['hyph/' + name for name in HYPHENATION]
    files = []
    for name in sorted(names):
        path = source_path(name, root)
        data = path.read_bytes()
        files.append(dict(path=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
    return (json.dumps(dict(schema=1, version=validate_version(version) if version is not None else release_version(root), files=files), sort_keys=True, separators=(',', ':')) + '\n').encode()


def stage(output, root=ROOT, version=None):
    data = manifest(root, version)
    destination = output / 'install/.adds/kobalt/assets'
    # Recreate build assets so removed fonts cannot survive an incremental build.
    if destination.exists():
        shutil.rmtree(destination)
    destination.mkdir(parents=True, exist_ok=True)
    for item in json.loads(data)['files']:
        target = destination / item['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_path(item['path'], root), target)
    (destination / 'manifest.json').write_bytes(data)
    return destination


def verify_assets(directory, data):
    """Reject incomplete bundles before publishing a firmware installer."""
    if directory.is_symlink() or (directory / 'manifest.json').read_bytes() != data:
        raise ValueError('Asset manifest does not match the library build')
    document = json.loads(data)
    if document.get('schema') != 1 or not document.get('version') or not document.get('files'):
        raise ValueError('Invalid asset manifest')
    names = set()
    for item in document['files']:
        name = item['path']
        path = PurePosixPath(name)
        if (not name.startswith(FOLDERS) or path.is_absolute() or '..' in path.parts
                or '\\' in name or str(path) != name or name in names):
            raise ValueError('Invalid asset path: ' + name)
        names.add(name)
        source = directory / name
        source.resolve().relative_to(directory.resolve())
        if source.is_symlink() or not source.is_file():
            raise ValueError('Missing or linked asset: ' + name)
        if source.stat().st_size != item['size'] or hashlib.sha256(source.read_bytes()).hexdigest() != item['sha256']:
            raise ValueError('Asset checksum mismatch: ' + name)
    return document['files']


def install_assets(output, onboard):
    """Replace managed assets, then publish their manifest. Preserve other files."""
    output, onboard = Path(output), Path(onboard)
    data = (output / 'asset-manifest.json').read_bytes()
    source = output / 'install/.adds/kobalt/assets'
    entries = verify_assets(source, data)
    target = onboard / '.adds/kobalt/assets'
    if target.is_symlink():
        raise ValueError('Linked asset directory')
    previous = []
    installed = target / 'manifest.json'
    if installed.exists():
        old = installed.read_bytes()
        previous = verify_assets(target, old)
        if old == data:
            return target
    managed = {item['path'] for item in previous}
    for item in entries:
        destination = target / item['path']
        destination.resolve().relative_to(target.resolve())
        if (destination.exists() or destination.is_symlink()) and item['path'] not in managed:
            raise ValueError('Unmanaged asset already exists: ' + item['path'])
    missing = []
    parent = target.parent
    while not parent.exists():
        missing.append(parent)
        parent = parent.parent
    for parent in reversed(missing):
        parent.mkdir(mode=0o755)
        parent.chmod(0o755)
    temporary = Path(tempfile.mkdtemp(prefix='.install-', dir=target.parent))
    try:
        temporary.chmod(0o755)
        for item in entries:
            destination = temporary / item['path']
            destination.parent.mkdir(parents=True, exist_ok=True)
            for parent in destination.parents:
                if parent == temporary: break
                parent.chmod(0o755)
            with (source / item['path']).open('rb') as reader, destination.open('xb') as writer:
                shutil.copyfileobj(reader, writer)
                writer.flush()
                os.fsync(writer.fileno())
            destination.chmod(0o644)
        with (temporary / 'manifest.json').open('xb') as writer:
            writer.write(data)
            writer.flush()
            os.fsync(writer.fileno())
        (temporary / 'manifest.json').chmod(0o644)
        verify_assets(temporary, data)
        target.mkdir(exist_ok=True)
        target.chmod(0o755)
        for item in entries:
            destination = target / item['path']
            destination.parent.mkdir(parents=True, exist_ok=True)
            for parent in destination.parents:
                if parent == target: break
                parent.chmod(0o755)
            os.replace(temporary / item['path'], destination)
        # A partial copy keeps the old manifest and fails validation on restart.
        os.replace(temporary / 'manifest.json', installed)
        for name in managed - {item['path'] for item in entries}:
            (target / name).unlink()
        return target
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version', help='version shared with the application build')
    parser.add_argument('--manifest', type=Path, required=True)
    args = parser.parse_args()
    data = manifest(version=args.version)
    if not args.manifest.exists() or args.manifest.read_bytes() != data:
        args.manifest.write_bytes(data)


if __name__ == '__main__':
    main()
