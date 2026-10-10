"""A synthetic EPUB with writing-direction changes across chapters and blocks."""
import zipfile


def book(path):
    paragraphs = ''.join('<p>これは縦書きの文章です。文字は上から下へ、行は右から左へ進みます。'
                         '「日本語の本」を読みながら、ページをめくります。'
                         '<ruby>東京<rt>とうきょう</rt></ruby>の空は明るく、風は静かです。'
                         '数字の<span style="text-combine-upright:all">12</span>も表示します。</p>' for _ in range(16))
    chapters = [
        ('vertical.xhtml', 'vertical-rl', '縦書きの章', paragraphs),
        ('horizontal.xhtml', 'horizontal-tb', 'Horizontal chapter',
         '<p>This chapter belongs to the same book. Its text runs horizontally, from left to right.</p>'
         '<p>横書きの文章です。前の章と次の章は縦書きですが、この章は横書きです。</p>'
         '<p>The reading position, search and page navigation continue across the change in writing mode.</p>' * 6),
        ('vertical-again.xhtml', 'vertical-rl', '再び縦書き', paragraphs),
        ('mixed.xhtml', 'vertical-rl', '同じ章の縦書き',
         '<p>この章には縦書きと横書きの文章があります。</p>'
         '<div style="writing-mode:horizontal-tb"><h2>Horizontal block</h2>'
         '<p>This horizontal block is inside the same XHTML chapter as the vertical passages.</p></div>'
         '<div><h2>同じ章の続き</h2><p>横書きの後は、再び縦書きで読み進めます。</p></div>'),
    ]
    manifest = ''.join(f'<item id="c{i}" href="{name}" media-type="application/xhtml+xml"/>'
                       for i, (name, _, _, _) in enumerate(chapters))
    spine = ''.join(f'<itemref idref="c{i}"/>' for i in range(len(chapters)))
    files = {
        'mimetype': 'application/epub+zip',
        'META-INF/container.xml': '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>',
        'book.opf': '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">'
                    '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">kobalt-writing-modes</dc:identifier>'
                    '<dc:title>Mixed chapter writing modes</dc:title><dc:language>ja</dc:language></metadata>'
                    f'<manifest>{manifest}<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest>'
                    f'<spine page-progression-direction="rtl">{spine}</spine></package>',
        'nav.xhtml': '<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><head><title>Contents</title></head>'
                     '<body><nav epub:type="toc"><ol>' + ''.join(f'<li><a href="{name}">{title}</a></li>' for name, _, title, _ in chapters)
                     + '</ol></nav></body></html>',
    }
    for name, mode, title, content in chapters:
        files[name] = '<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="ja"><head><title>' + title + '</title><style>' \
            + f'body{{writing-mode:{mode};font-family:"Noto Serif CJK JP"}}h1{{font-size:1.25em}}p{{line-height:1.6}}' \
            + '</style></head><body><h1>' + title + '</h1>' + content + '</body></html>'
    with zipfile.ZipFile(path, 'w') as archive:
        for name, content in files.items():
            member = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            member.compress_type = zipfile.ZIP_STORED if name == 'mimetype' else zipfile.ZIP_DEFLATED
            archive.writestr(member, content.encode('utf-8'))
