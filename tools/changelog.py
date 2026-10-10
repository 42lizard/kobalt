#!/usr/bin/env python3
"""Read the release notes for an exact version heading in CHANGELOG.md."""
import argparse
from pathlib import Path

from version import validate

ROOT = Path(__file__).resolve().parents[1]


def section(root, tag):
    validate(tag)
    heading = '## ' + tag
    lines = (root / 'CHANGELOG.md').read_text(encoding='utf-8').splitlines()
    matches = [index for index, line in enumerate(lines) if line == heading]
    if not matches:
        raise ValueError('Missing changelog section: ' + tag)
    if len(matches) != 1:
        raise ValueError('Duplicate changelog section: ' + tag)
    start = matches[0] + 1
    end = next((index for index in range(start, len(lines)) if lines[index].startswith('## ')), len(lines))
    notes = '\n'.join(lines[start:end]).strip()
    if not notes:
        raise ValueError('Empty changelog section: ' + tag)
    return notes


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    args = parser.parse_args()
    try:
        print(section(ROOT, args.tag))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')
