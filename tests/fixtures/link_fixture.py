"""Synthetic internal links and notes shared by model and reader checks."""
import zipfile


def book(path):
    references = [
        '<p>Read a <a id="short-ref" ops:type="noteref" href="Notes/foot%20notes.xhtml#short">short note</a>.</p>',
        '<p>Read a <a role="doc-noteref" href="Notes/foot%20notes.xhtml#long">long note</a>.</p>',
        '<p>Visit a <a href="second.xhtml#destination">later passage</a>.</p>',
        '<p>Jump to a <a href="#same-page">nearby passage</a>.</p>',
        '<p>Try a <a href="second.xhtml#missing">missing passage</a>.</p>',
        '<p>See a <a href="https://example.org/book">website</a>.</p>',
        '<p>Reject an <a href="javascript:alert(1)">unsupported link</a>.</p>',
        '<p>Read a <a href="Notes/foot%20notes.xhtml#legacy">legacy note</a>.</p>',
        '<p>Read an <a epub:type="noteref" href="Notes/foot%20notes.xhtml#oversize">oversized note</a>.</p>',
        '<p>Read a <a epub:type="noteref" href="Notes/foot%20notes.xhtml#hidden">hidden note</a>.</p>',
        '<p id="same-page">The nearby passage stays in the same chapter.</p>',
    ]
    notes = ('<aside id="short" epub:type="footnote"><p>A <strong>short</strong> note with café &amp; tea.</p>'
             '<p>Second paragraph of the same note.</p></aside>'
             '<aside id="long" role="doc-footnote">'
             + ''.join(f'<p>Note paragraph {i}. Explicit paging keeps this text readable without leaving the original passage.</p>' for i in range(60))
             + '<p>End of the long note.</p></aside>'
             '<p class="footnote"><a id="legacy"></a>A legacy note inside one paragraph.</p>'
             '<aside id="oversize" epub:type="footnote" style="display:none"><p>' + 'Long note text. ' * 10000 + '</p></aside>'
             '<aside id="hidden" epub:type="footnote" style="display:none"><p>A hidden note remains available in its popup.</p></aside>')
    pages = {"first.xhtml": ''.join(references), "second.xhtml": '<p id="destination">The destination passage. <a style="color:#cc3300" href="first.xhtml#short-ref">Return through a link</a>.</p>' + '<p>More destination text.</p>' * 40,
             "Notes/foot notes.xhtml": notes}
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('mimetype', 'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        archive.writestr('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OEBPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        archive.writestr('OEBPS/book.opf', '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">kobalt-link-fixture</dc:identifier><dc:title>Links and footnotes</dc:title><dc:language>en</dc:language></metadata><manifest><item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/><item id="first" href="first.xhtml" media-type="application/xhtml+xml"/><item id="second" href="second.xhtml" media-type="application/xhtml+xml"/><item id="notes" href="Notes/foot%20notes.xhtml" media-type="application/xhtml+xml"/></manifest><spine><itemref idref="first"/><itemref idref="second"/><itemref idref="notes"/></spine></package>')
        archive.writestr('OEBPS/nav.xhtml', '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head><body><nav epub:type="toc"><ol><li><a href="first.xhtml">References</a></li><li><a href="second.xhtml">Passages</a></li><li><a href="Notes/foot%20notes.xhtml">Notes</a></li></ol></nav></body></html>')
        for name, body in pages.items():
            archive.writestr('OEBPS/' + name, '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xmlns:ops="http://www.idpf.org/2007/ops"><head><title>Links</title></head><body>' + body + '</body></html>')
