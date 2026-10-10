"""Package pinned public-domain chapters for screenshots and reader checks."""
import hashlib
import html
import json
import re
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile

HERE = Path(__file__).resolve().parent / 'public-domain'


def inventory():
    books = []
    for entry in json.loads((HERE / 'catalog.json').read_text()):
        data = (HERE / entry['file']).read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('Public-domain source checksum differs: ' + entry['file'])
        book = json.loads(data)
        for chapter in book['chapters']:
            text = '\n'.join(chapter['paragraphs']) + '\n'
            if hashlib.sha256(text.encode()).hexdigest() != chapter['sha256']:
                raise ValueError('Public-domain chapter checksum differs: ' + chapter['source'])
            ET.fromstring('<body>' + ''.join(chapter['paragraphs']) + '</body>')
        books.append(book)
    return books


def write_book(path, book, modes=None):
    language = book['language']
    direction = 'rtl' if language in ('ar', 'he') else 'ltr'
    progression = 'rtl' if modes or direction == 'rtl' else 'ltr'
    escape = html.escape
    chapters = book['chapters']
    items = ''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(len(chapters)))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(len(chapters)))
    contents = ''.join(f'<li><a href="c{i}.xhtml">{escape(c["title"])}</a></li>' for i, c in enumerate(chapters))
    files = {
        'mimetype': 'application/epub+zip',
        'META-INF/container.xml': '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="EPUB/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        'EPUB/book.opf': '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
            '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
            f'<dc:identifier id="id">kobalt-public-domain-{escape(language)}</dc:identifier>'
            f'<dc:title>{escape(book["title"])}</dc:title><dc:creator>{escape(book["author"])}</dc:creator>'
            f'<dc:language>{language}</dc:language><dc:contributor>Wikisource contributors</dc:contributor>'
            f'<dc:rights>{escape(book["rights"])}; {book["license"]}</dc:rights>'
            + ''.join(f'<dc:source>{escape(c["source"])}</dc:source>' for c in chapters)
            + f'</metadata><manifest>{items}<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>'
            '<item id="credits" href="sources.json" media-type="application/json"/></manifest>'
            f'<spine page-progression-direction="{progression}">{spine}</spine></package>',
        'EPUB/nav.xhtml': f'<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="{language}"><head><title>{escape(book["title"])}</title></head><body dir="{direction}"><nav epub:type="toc"><ol>{contents}</ol></nav></body></html>',
        'EPUB/sources.json': json.dumps(book, ensure_ascii=False, indent=2),
    }
    for i, chapter in enumerate(chapters):
        mode = modes[i] if modes else 'horizontal-tb'
        style = f'body{{writing-mode:{mode};direction:{direction}}}h1{{font-size:1.2em;text-align:center;margin:1em 0}}p{{margin:0 0 .5em;text-indent:1em}}'
        first = html.unescape(re.sub(r'<[^>]+>', '', chapter['paragraphs'][0])).strip()
        heading = '' if first == chapter['title'] else f'<h1>{escape(chapter["title"])}</h1>'
        files[f'EPUB/c{i}.xhtml'] = f'<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="{language}" lang="{language}"><head><title>{escape(chapter["title"])}</title><style>{style}</style></head><body dir="{direction}">{heading}{"".join(chapter["paragraphs"])}</body></html>'
    with zipfile.ZipFile(path, 'w') as archive:
        for name, text in files.items():
            member = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            member.compress_type = zipfile.ZIP_STORED if name == 'mimetype' else zipfile.ZIP_DEFLATED
            archive.writestr(member, text.encode())


def write_all(directory):
    entries = []
    for book in inventory():
        filename = 'public-domain-' + book['language'] + '.epub'
        write_book(directory / filename, book)
        chapters = []
        for chapter in book['chapters']:
            # Search a contiguous prose run; ruby annotations split the engine's logical text.
            opening = next(p for p in chapter['paragraphs'] if html.unescape(re.sub(r'<[^>]+>', '', p)).strip() != chapter['title'])
            runs = re.split(r'<ruby>.*?</ruby>', opening)
            text = max((html.unescape(re.sub(r'<[^>]+>', '', run)).strip() for run in runs), key=len)
            chapters.append({'title': chapter['title'], 'probe': text[:24]})
        entries.append({'file': filename, 'language': book['language'], 'chapters': chapters})
        if book['language'] in ('ja', 'ko', 'zh-Hans', 'zh-Hant'):
            write_book(directory / ('public-domain-' + book['language'] + '-writing-modes.epub'), book,
                       ['vertical-rl' if i % 2 == 0 else 'horizontal-tb' for i in range(len(book['chapters']))])
    (directory / 'public-domain-inventory.json').write_text(json.dumps(entries, ensure_ascii=False, indent=2) + '\n')
