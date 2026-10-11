"""Generate the CJK fallback for mini Kobo packages without font hinting."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from session_lock import acquire

CJK = 'NotoSerifCJK-Regular.ttc'


def prepare(root, cache=None, offline=False):
    source = root / 'resources/fonts' / CJK
    pin = json.loads((root / 'resources/fonts/noto-cjk.json').read_text())['files'][0]
    source_hash = hashlib.sha256(source.read_bytes()).hexdigest()
    if pin['file'] != CJK or source_hash != pin['sha256']:
        raise ValueError('Mini build requires the pinned CJK fallback font')
    spec = json.loads((root / 'dependencies/manifest.json').read_text())['fonttools']
    key = hashlib.sha256((source_hash + spec['sha256']).encode() + Path(__file__).read_bytes()).hexdigest()
    directory = root / '.cache/kobalt/mini-fonts' / key
    destination = directory / CJK
    record = directory / 'sha256'
    (root / 'build').mkdir(exist_ok=True)
    with acquire(root / 'build/mini-fonts.lock', wait=True):
        if destination.is_file() and record.is_file():
            if hashlib.sha256(destination.read_bytes()).hexdigest() == record.read_text().strip():
                return destination
        from build import fetch
        cache = Path(cache) if cache is not None else root / '.cache/kobalt'
        cache.mkdir(parents=True, exist_ok=True)
        fetch('fonttools', spec, cache, offline=offline)
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as temporary:
            generated = Path(temporary) / CJK
            subprocess.run([sys.executable, str(Path(__file__).resolve()), '--generate',
                            str(cache / spec['directory'] / 'Lib'), str(source), str(generated)], check=True)
            checksum = hashlib.sha256(generated.read_bytes()).hexdigest()
            generated.replace(destination)
            record.write_text(checksum + '\n')
    return destination


def generate(library, source, destination):
    sys.path.insert(0, str(library))
    from fontTools.ttLib import TTCollection
    with TTCollection(source, recalcTimestamp=False) as collection:
        # The pinned collection shares one CFF table across all five faces.
        # FontTools loads separate objects; retain that sharing when rewriting.
        records = [font.reader.tables['CFF '] for font in collection.fonts]
        if len({(table.offset, table.length) for table in records}) != 1:
            raise ValueError('Expected shared CJK outlines')
        cff = collection.fonts[0]['CFF ']
        for font in collection.fonts:
            font['CFF '] = cff
        cff.cff.remove_hints()
        collection.save(destination)


if __name__ == '__main__':
    if len(sys.argv) != 5 or sys.argv[1] != '--generate':
        raise SystemExit('Use tools/build.py --mini to build a mini package')
    generate(Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4]))
