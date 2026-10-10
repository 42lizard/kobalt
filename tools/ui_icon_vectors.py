#!/usr/bin/env python3
"""Compile the bundled Lucide SVG subset to Qt paths without rasterizing it."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

TOKEN = re.compile(r'[MmLlHhVvAa]|[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?')


def number(value):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError('non-finite coordinate')
    return format(value, '.12g')


def path_commands(data):
    tokens = TOKEN.findall(data)
    if TOKEN.sub('', data).strip(' ,\t\r\n'):
        raise ValueError('unsupported path command')
    if not tokens or tokens[0] not in ('M', 'm'):
        raise ValueError('path must begin with a move')
    result, position, command, index = [], [0., 0.], None, 0
    while index < len(tokens):
        if tokens[index].isalpha():
            command = tokens[index]
            index += 1
        if command is None:
            raise ValueError('missing path command')
        count = {'m': 2, 'l': 2, 'h': 1, 'v': 1, 'a': 7}[command.lower()]
        values = [float(value) for value in tokens[index:index + count]]
        if len(values) != count or not all(math.isfinite(value) for value in values):
            raise ValueError('invalid path coordinates')
        index += count
        relative = command.islower()
        x, y = position
        kind = command.lower()
        if kind in ('m', 'l'):
            end = [values[0] + (x if relative else 0), values[1] + (y if relative else 0)]
            result.append('path.%s(%s, %s);' % ('moveTo' if kind == 'm' else 'lineTo', *map(number, end)))
            if kind == 'm':
                command = 'l' if relative else 'L'
        elif kind in ('h', 'v'):
            end = [x, y]
            axis = 0 if kind == 'h' else 1
            end[axis] = values[0] + (position[axis] if relative else 0)
            result.append('path.lineTo(%s, %s);' % tuple(map(number, end)))
        else:
            rx, ry, rotation, large, sweep, dx, dy = values
            end = [dx + (x if relative else 0), dy + (y if relative else 0)]
            # The bundled paths contain only axis-aligned circular quarter arcs.
            if not (rx > 0 and rx == ry and rotation == 0 and large == 0 and sweep in (0, 1)
                    and math.isclose(abs(end[0] - x), rx) and math.isclose(abs(end[1] - y), ry)):
                raise ValueError('unsupported arc, expected an axis-aligned circular quarter arc')
            candidates = [(x, end[1]), (end[0], y)]
            center = next(c for c in candidates if ((x - c[0]) * (end[1] - c[1]) - (y - c[1]) * (end[0] - c[0]) > 0) == bool(sweep))
            start_angle = math.atan2(y - center[1], x - center[0])
            delta = math.pi / 2 * (1 if sweep else -1)
            k = 4 / 3 * math.tan(delta / 4)
            end_angle = start_angle + delta
            control = [x - k * rx * math.sin(start_angle), y + k * rx * math.cos(start_angle),
                       end[0] + k * rx * math.sin(end_angle), end[1] - k * rx * math.cos(end_angle), *end]
            result.append('path.cubicTo(%s);' % ', '.join(map(number, control)))
        position = end
    return result


def geometry(source, entry):
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise ValueError('SVG checksum does not match manifest: ' + source.name)
    root = ET.fromstring(data)
    expected = {'width': '24', 'height': '24', 'viewBox': '0 0 24 24', 'fill': 'none',
                'stroke': 'currentColor', 'stroke-width': '2', 'stroke-linecap': 'round', 'stroke-linejoin': 'round'}
    if root.tag != '{http://www.w3.org/2000/svg}svg' or root.attrib != expected:
        raise ValueError('unsupported SVG root: ' + source.name)
    commands = []
    for element in root:
        tag = element.tag.removeprefix('{http://www.w3.org/2000/svg}')
        attrs = element.attrib
        if tag == 'path' and set(attrs) == {'d'}:
            commands += path_commands(attrs['d'])
        elif tag == 'circle' and set(attrs) == {'cx', 'cy', 'r'}:
            cx, cy, radius = (float(attrs[key]) for key in ('cx', 'cy', 'r'))
            if radius <= 0:
                raise ValueError('invalid circle')
            commands.append('path.addEllipse(QRectF(%s));' % ', '.join(map(number, (cx - radius, cy - radius, 2 * radius, 2 * radius))))
        elif tag == 'rect' and set(attrs) == {'x', 'y', 'width', 'height', 'rx'}:
            values = [float(attrs[key]) for key in ('x', 'y', 'width', 'height', 'rx')]
            if min(values[2:]) <= 0:
                raise ValueError('invalid rounded rectangle')
            commands.append('path.addRoundedRect(QRectF(%s), %s, %s);' % (', '.join(map(number, values[:4])), number(values[4]), number(values[4])))
        else:
            raise ValueError('unsupported SVG element: ' + tag)
        if len(element) or (element.text or '').strip():
            raise ValueError('unexpected SVG element contents')
    if not commands:
        raise ValueError('empty SVG geometry')
    width = float(entry.get('stroke_width', root.attrib['stroke-width']))
    if not math.isfinite(width) or width <= 0:
        raise ValueError('invalid stroke width')
    return commands, width


def generate(directory):
    manifest = json.loads((directory / 'manifest.json').read_text())
    lines = ['// Generated from the checked Lucide SVGs by tools/ui_icon_vectors.py.',
             'static bool lucideGeometry(const QString &name, QPainterPath &path, qreal &stroke) {']
    seen = set()
    for entry in manifest['files']:
        filename = entry['file']
        if Path(filename).name != filename or not filename.endswith('.svg') or filename in seen:
            raise ValueError('invalid or duplicate SVG filename')
        seen.add(filename)
        commands, width = geometry(directory / filename, entry)
        lines.append('    if (name == QStringLiteral(%s)) {' % json.dumps(filename[:-4]))
        lines += ['        ' + command for command in commands]
        lines += ['        stroke = %s;' % number(width), '        return true;', '    }']
    return '\n'.join(lines + ['    return false;', '}', ''])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    data = generate(args.directory)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(data)


if __name__ == '__main__':
    main()
