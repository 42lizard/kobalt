"""Resolve one version for the application, assets and release metadata."""
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def validate(value):
    if not isinstance(value, str) or not re.fullmatch(r'v[0-9]+(?:\.[0-9]+){1,2}(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?', value):
        raise ValueError('Expected a version such as v0.4, v0.4.0 or v0.4.0-beta.1')
    return value


def release_version(root=ROOT):
    override = os.environ.get('KOBALT_RELEASE_VERSION')
    if override:
        return validate(override)
    bundled = root / 'SOURCE-BUNDLE.json'
    if bundled.is_file():
        return validate(json.loads(bundled.read_text())['version'])
    try:
        top = subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--show-toplevel'],
                                      stderr=subprocess.DEVNULL, text=True).strip()
        if Path(top).resolve() == root.resolve():
            names = subprocess.check_output(['git', '-C', str(root), 'tag', '--points-at', 'HEAD'], text=True).splitlines()
            versions = []
            for name in names:
                if re.match(r'^v[0-9]', name):
                    versions.append(validate(name))
            if len(versions) > 1:
                raise ValueError('Multiple version tags point at HEAD; set KOBALT_RELEASE_VERSION explicitly')
            if versions:
                return versions[0]
    except (OSError, subprocess.CalledProcessError):
        pass
    match = re.search(r'^set\(KOBALT_VERSION "([^"\n]+)"(?: CACHE STRING "[^"]*")?\)$',
                      (root / 'CMakeLists.txt').read_text(), re.MULTILINE)
    if not match:
        raise ValueError('Missing KOBALT_VERSION development fallback in CMakeLists.txt')
    return validate(match[1])
