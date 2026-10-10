#!/usr/bin/env python3
"""Check or format Kobalt's C++ source with the pinned clang-format version."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VERSION = '18.1.8'
SUFFIXES = {'.cpp', '.h', '.hpp', '.cc', '.cxx'}


def source_files(root=ROOT):
    names = subprocess.check_output(
        ['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard', '--', 'src', 'tests'])
    return sorted({root / name.decode('utf-8') for name in names.split(b'\0')
                   if name and Path(name.decode('utf-8')).suffix in SUFFIXES
                   and (root / name.decode('utf-8')).is_file()})


def format_files(files, binary, write=False):
    version = subprocess.check_output([binary, '--version'], text=True)
    if not re.search(r'\bversion ' + re.escape(VERSION) + r'(?:\s|$)', version):
        raise ValueError('Expected clang-format ' + VERSION + ', got ' + version.strip())
    options = ['-i'] if write else ['--dry-run', '--Werror']
    # Keep arguments below platform limits, including checkouts with long paths.
    for offset in range(0, len(files), 50):
        result = subprocess.run([binary, '--style=file', '--fallback-style=none', *options,
                                 *map(str, files[offset:offset + 50])])
        if result.returncode:
            return result.returncode
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--check', action='store_true', help='check formatting without changes (default)')
    action.add_argument('--write', action='store_true', help='format source and tests in place')
    parser.add_argument('--binary', default='clang-format', help='clang-format executable')
    args = parser.parse_args()
    try:
        files = source_files()
        result = format_files(files, args.binary, args.write)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')
    if result == 0:
        print(('Formatted ' if args.write else 'Checked ') + str(len(files)) + ' C++ files with clang-format ' + VERSION)
    return result


if __name__ == '__main__':
    sys.exit(main())
