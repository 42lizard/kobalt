"""Qt build targets and their independent firmware support policies."""
import json
import re
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TARGET = 'qt6'
ALIASES = {'nickel4': 'qt5', 'nickel5': 'qt6'}
SUPPORTED_TARGETS = ('qt6', 'qt5', *ALIASES)


def target(name=DEFAULT_TARGET):
    if name not in SUPPORTED_TARGETS:
        raise ValueError('Unsupported Kobalt target: ' + name)
    name = ALIASES.get(name, name)
    values = json.loads((ROOT / 'targets' / (name + '.json')).read_text())
    selected = SimpleNamespace(name=name, **values)
    if not supports_firmware(selected, selected.firmware):
        raise ValueError('Default firmware is outside the support policy for ' + name)
    return selected


def lock_path(selected, kind):
    # Each target has its own build and suite lock, so qt5 and qt6 runs can overlap.
    return ROOT / 'build' / f'{selected.name}-{kind}.lock'


def output_path(selected, *, hardware=True, temporary=False, mini=False):
    if temporary and not hardware:
        raise ValueError('Temporary reader builds require hardware mode')
    name = 'reader-hardware-temporary' if temporary else ('reader-hardware' if hardware else 'reader')
    if mini:
        name += '-mini'
    return Path(selected.build_root) / name


def supports_firmware(selected, version):
    """Check a concrete firmware version against the build's release policy.

    Native symbol and layout checks remain necessary at runtime.
    """
    def parts(value):
        if not re.fullmatch(r"[0-9]+(?:\.[0-9]+){0,2}", value):
            raise ValueError('Invalid firmware version: ' + value)
        numbers = tuple(map(int, value.split('.')))
        return numbers + (0,) * (3 - len(numbers))
    actual = parts(version)
    return any(parts(span['minimum']) <= actual < parts(span['maximum_exclusive'])
               for span in selected.firmware_support)


def firmware_version(line, exact=None):
    """Keep assertion policy separate from the exact device image selection."""
    if line not in ('4', '5', '6'):
        raise ValueError('Firmware line must be 4, 5 or 6')
    if exact is None:
        return line
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', exact) or exact.split('.')[0] != line:
        raise ValueError('Exact firmware version must be in the selected firmware line')
    return exact
