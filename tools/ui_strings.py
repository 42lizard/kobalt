"""Validate Apple-style .strings and compile the embedded Kobalt catalog."""
import argparse
from functools import lru_cache
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / 'resources/en.strings'
TOKEN = re.compile(r'\s+|/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|[=;]')
SLOTS = re.compile(r'%[1-9][0-9]?')
SOURCE_KEYS = re.compile(
    r'strings::(?:get|key)\(\s*(?:[^?;"\n]+\?\s*)?'
    r'"([a-z0-9_.]+)"(?:\s*:\s*"([a-z0-9_.]+)")?\s*\)')


def unquote(token):
    value, pos = [], 1
    escapes = {'n': '\n', 'r': '\r', 't': '\t', 'b': '\b', 'f': '\f', '"': '"', '\\': '\\'}
    while pos < len(token) - 1:
        char = token[pos]
        pos += 1
        if char != '\\':
            value.append(char)
            continue
        char = token[pos]
        pos += 1
        if char in escapes:
            value.append(escapes[char])
        elif char == 'U':
            digits = token[pos:pos + 4]
            if not re.fullmatch(r'[0-9a-fA-F]{4}', digits):
                raise ValueError('Expected four hex digits after \\U')
            value.append(chr(int(digits, 16)))
            pos += 4
        elif char in '01234567':
            digits = char
            while len(digits) < 3 and token[pos] in '01234567':
                digits += token[pos]
                pos += 1
            value.append(chr(int(digits, 8)))
        else:
            raise ValueError('Unsupported escape: \\' + char)
    # Apple Unicode escapes encode UTF-16 code units, including surrogate pairs.
    result = ''.join(value).encode('utf-16', 'surrogatepass').decode('utf-16')
    if any(ord(c) < 32 and c not in '\n\r\t\b\f' for c in result):
        raise ValueError('Unsupported control character in display string')
    return result


def parse(source):
    tokens, cursor, arguments = [], 0, None
    for match in TOKEN.finditer(source):
        if match.start() != cursor:
            raise ValueError(f'Invalid syntax at line {source.count(chr(10), 0, cursor) + 1}')
        cursor = match.end()
        token = match.group()
        if token.isspace():
            continue
        if token.startswith(('/*', '//')):
            declaration = re.search(r'Arguments:\s*([^*\n]+)', token)
            if declaration:
                arguments = set(SLOTS.findall(declaration.group(1)))
            continue
        tokens.append((token, arguments))
        arguments = None
    if cursor != len(source):
        raise ValueError(f'Invalid syntax at line {source.count(chr(10), 0, cursor) + 1}')
    if len(tokens) % 4:
        raise ValueError('Each entry must be "key" = "value";')
    result = {}
    for offset in range(0, len(tokens), 4):
        (key, expected), (equals, _), (value, _), (semicolon, _) = tokens[offset:offset + 4]
        if not key.startswith('"') or equals != '=' or not value.startswith('"') or semicolon != ';':
            raise ValueError('Each entry must be "key" = "value";')
        key, value = unquote(key), unquote(value)
        if not re.fullmatch(r'[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)*', key):
            raise ValueError(f'Invalid string key: {key}')
        if key in result:
            raise ValueError(f'Duplicate string key: {key}')
        if not value:
            raise ValueError(f'Empty display string: {key}')
        if set(SLOTS.findall(value)) != (expected or set()):
            raise ValueError(f'{key}: formatting arguments differ from its Arguments comment')
        result[key] = value
    if not result:
        raise ValueError('Empty string catalog')
    return result


def read(path=CATALOG):
    data = Path(path).read_bytes()
    return parse(data.decode('utf-16' if data.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig'))


@lru_cache(maxsize=None)
def text(key):
    return read()[key]


def validate_keys(catalog, root=ROOT / 'src'):
    used = set()
    for path in root.rglob('*'):
        if path.suffix not in ('.h', '.cpp'):
            continue
        for match in SOURCE_KEYS.finditer(path.read_text()):
            used.update(key for key in match.groups() if key)
    missing = used - catalog.keys()
    if missing:
        raise ValueError('Missing string keys: ' + ', '.join(sorted(missing)))
    return used


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    catalog = read()
    validate_keys(catalog)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        data = ''.join('    {' + json.dumps(k) + ', ' + json.dumps(v, ensure_ascii=False) + '},\n'
                       for k, v in sorted(catalog.items()))
        # JSON and C++ share these escapes; keep actual Unicode in the generated C++.
        if not args.output.exists() or args.output.read_text() != data:
            args.output.write_text(data)
    else:
        print(f'Validated {len(catalog)} strings and their source keys')


if __name__ == '__main__':
    main()
