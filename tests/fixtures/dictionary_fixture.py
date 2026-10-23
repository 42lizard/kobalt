"""Generate public-domain synthetic text and dictionaries for lookup checks.

The optional dictionary output needs marisa-trie. The EPUB needs only Python.
Dictionary format: https://pgaskin.net/dictutil/dicthtml/format.html
"""
import argparse
from collections import defaultdict
import html
import gzip
from pathlib import Path
import zipfile


def book(path, language="en"):
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("mimetype", "application/epub+zip")
        archive.writestr("META-INF/container.xml", '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr("book.opf", f'''<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">kobalt-dictionary-fixture</dc:identifier><dc:title>Dictionary selection fixture</dc:title><dc:language>{language}</dc:language></metadata><manifest><item id="one" href="one.xhtml" media-type="application/xhtml+xml"/><item id="two" href="two.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="one"/><itemref idref="two"/></spine></package>''')
        paragraphs = [
            '<p id="plain">Hello, world! “Reader’s” don\'t café café naïve.</p>',
            '<p id="inline">A rain<strong>bow</strong> and co<em>oper</em>ation.</p>',
            '<p id="soft">An extra\u00adordinary word can wrap across lines.</p>',
            '<p id="multilingual">Astral A𐐀word. Bonjour. 日本語の言葉。中文词语。</p>',
            '<p id="annotation-punctuation"><span>“</span>quoted<span>…”</span> [bracket], adjacent. Next <em>𐐀token</em>。</p>',
            '<p id="annotation-sentences">Al<span>pha</span> finishes clearly. Beta completes fully! “Quotation ending.” (Nested closing?) Dr. Smith waits. Value 3.14 remains. 東京。 Ends with ellipsis… “Earlier completed.” Later rests.” We\'re arriving.\' <span>(“</span>Layered pairing<span>.</span><span>”)</span> “Greeting,” she murmured softly. “First one. Second two.”</p>',
        ]
        for name, body in (("one", "".join(paragraphs) + "<p>More fixture text.</p>" * 80), ("two", '<p id="boundary">Chapter boundary. Hello again.</p>')):
            archive.writestr(name + ".xhtml", '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Lookup fixture</title></head><body>' + body + '</body></html>')


def dictionaries(directory):
    import marisa_trie
    directory.mkdir(parents=True, exist_ok=True)
    for name, words in (
        ("dicthtml", ("hello", "world", "reader’s", "don't", "café", "naïve", "rainbow", "cooperation", "extraordinary")),
        ("dicthtml-fr", ("bonjour", "café")),
        ("dicthtml-fr-en", ("bonjour", "café")),
    ):
        groups = defaultdict(list)
        for word in words:
            escaped = html.escape(word, quote=False)
            groups[word[:2]].append(f'<w><p><a name="{escaped}" /><b>{escaped}</b> <i>fixture</i></p><var></var><p>A synthetic definition for lookup tests.</p><ol><li>First sense.</li><li>Second sense.</li></ol></w>')
        if 'hello' in words:
            groups['he'][0] = groups['he'][0].replace('</w>', '<p>Additional synthetic definition text for explicit paging.</p>' * 45 + '</w>')
        # Firmware 5 reads extracted dictionaries and ungzips each HTML entry.
        target = directory / name
        target.mkdir(parents=True, exist_ok=True)
        (target / "words").write_bytes(marisa_trie.Trie(words).tobytes())
        for prefix, entries in groups.items():
            text = "<html>" + "".join(entries) + "</html>"
            (target / (prefix + ".html")).write_bytes(gzip.compress(text.encode(), mtime=0))
        if name == 'dicthtml':
            # Stock dictionaries carry their attribution in the archive comment.
            with zipfile.ZipFile(directory / (name + '.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
                archive.comment = b'Synthetic dictionary attribution for integration tests.'
                for entry in sorted(target.iterdir()):
                    archive.write(entry, entry.name)
            for entry in target.iterdir(): entry.unlink()
            target.rmdir()



if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--dictionaries", type=Path)
    parser.add_argument("--language", choices=("en", "fr"), default="en")
    args = parser.parse_args()
    book(args.output, args.language)
    if args.dictionaries:
        dictionaries(args.dictionaries)
