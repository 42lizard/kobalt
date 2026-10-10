#!/usr/bin/env python3
"""Stage a verified variable font for native tests, outside reader assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile

MANIFEST = Path(__file__).with_name('variable-font.json')


def axes(data):
    if len(data) < 12 or data[:4] != b'\x00\x01\x00\x00':
        raise ValueError('Invalid variable font header')
    count = struct.unpack_from('>H', data, 4)[0]
    if 12 + count * 16 > len(data):
        raise ValueError('Truncated variable font table directory')
    tables = {}
    for index in range(count):
        tag, _, offset, length = struct.unpack_from('>4sIII', data, 12 + index * 16)
        if tag in tables or offset + length > len(data):
            raise ValueError('Invalid variable font table bounds')
        tables[tag] = data[offset:offset + length]
    if not all(tables.get(tag) for tag in (b'fvar', b'gvar', b'HVAR')):
        raise ValueError('Missing variable font variation tables')
    table = tables[b'fvar']
    if len(table) < 16:
        raise ValueError('Truncated variable font axes')
    major, minor, offset, _, count, size = struct.unpack_from('>HHHHHH', table)
    if (major, minor) != (1, 0) or offset < 16 or size < 20 or offset + count * size > len(table):
        raise ValueError('Invalid variable font axes')
    result = {}
    for index in range(count):
        tag, minimum, default, maximum = struct.unpack_from('>4siii', table, offset + index * size)
        if tag in result or not minimum <= default <= maximum:
            raise ValueError('Invalid variable font axis range')
        result[tag] = [value / 65536 for value in (minimum, default, maximum)]
    return result


def stage(source, destination, manifest=MANIFEST):
    source, destination, manifest = Path(source), Path(destination), Path(manifest)
    spec = json.loads(manifest.read_text())
    if source.is_symlink() or not source.is_file():
        raise ValueError('Missing or linked variable font fixture: ' + str(source))
    if source.stat().st_size != spec['size']:
        raise ValueError('Variable font fixture size mismatch')
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != spec['sha256']:
        raise ValueError('Variable font fixture checksum mismatch')
    actual = axes(data)
    for tag, expected in spec['axes'].items():
        if actual.get(tag.encode('ascii')) != expected:
            raise ValueError('Variable font fixture axis mismatch: ' + tag)
    notice = manifest.with_name(spec['notice']).read_bytes()
    destination.mkdir(parents=True, exist_ok=True)
    # Validate everything before replacing an existing, usable fixture.
    for name, contents in ((spec['file'], data), ('OFL.txt', notice)):
        target = destination / name
        with tempfile.NamedTemporaryFile(dir=destination, delete=False) as output:
            temporary = Path(output.name)
            output.write(contents)
        try:
            os.replace(temporary, target)
        finally:
            temporary.unlink(missing_ok=True)
    return destination / spec['file']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    stage(args.source, args.destination)


if __name__ == '__main__':
    main()
