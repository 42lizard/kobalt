"""Build an original Arabic, Hebrew and mixed-direction EPUB test book."""
import argparse
from pathlib import Path
import zipfile


ARABIC = "مَرْحَبًا بِكُمْ فِي هَذَا النَّصِّ التَّجْرِيبِيِّ. نَكْتُبُ جُمْلَةً قَصِيرَةً لِاخْتِبَارِ اتِّجَاهِ الْكِتَابَةِ وَتَشْكِيلِ الْحُرُوفِ."
HEBREW = "שָׁלוֹם עוֹלָם, זֶהוּ טֶקְסְט מְקוֹרִי לְבְדִיקַת כִּוּוּן הַכְּתִיבָה וְנִקּוּד הָאוֹתִיּוֹת."


def book(path, language="ar"):
    """Write an Arabic (``ar``) or Hebrew (``he``) two-chapter test EPUB."""
    if language not in ("ar", "he"):
        raise ValueError("language must be 'ar' or 'he'")
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as epub:
        def write(name, data, compression=zipfile.ZIP_DEFLATED):
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.compress_type = compression
            epub.writestr(info, data)

        write("mimetype", "application/epub+zip", zipfile.ZIP_STORED)
        write("META-INF/container.xml", '''<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="EPUB/book.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>''')
        label = "النص العربي" if language == "ar" else "הטקסט העברי"
        write("EPUB/book.opf", f'''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">kobalt-multilingual-fixture</dc:identifier>
    <dc:title>{label}</dc:title><dc:language>{language}</dc:language>
    <dc:creator>Kobalt Test Fixtures</dc:creator>
  </metadata>
  <manifest>
    <item id="chapter-one" href="chapter-one.xhtml" media-type="application/xhtml+xml"/>
    <item id="chapter-two" href="chapter-two.xhtml" media-type="application/xhtml+xml"/>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
  </manifest>
  <spine toc="ncx" page-progression-direction="rtl"><itemref idref="chapter-one"/><itemref idref="chapter-two"/></spine>
</package>''')
        write("EPUB/nav.xhtml", f'''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" lang="en" xml:lang="en"><head><title>Contents</title></head>
<body><nav epub:type="toc" xmlns:epub="http://www.idpf.org/2007/ops"><ol>
  <li><a href="chapter-one.xhtml">{label}, 1</a></li><li><a href="chapter-two.xhtml">{label}, 2</a></li>
</ol></nav></body></html>''')
        write("EPUB/toc.ncx", f'''<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="kobalt-multilingual-fixture"/></head>
  <docTitle><text>{label}</text></docTitle><navMap>
    <navPoint id="one" playOrder="1"><navLabel><text>{label}, 1</text></navLabel><content src="chapter-one.xhtml"/></navPoint>
    <navPoint id="two" playOrder="2"><navLabel><text>{label}, 2</text></navLabel><content src="chapter-two.xhtml"/></navPoint>
  </navMap>
</ncx>''')
        write("EPUB/chapter-one.xhtml", _chapter(language, 1))
        write("EPUB/chapter-two.xhtml", _chapter(language, 2))


def _chapter(language, number):
    if language == "ar":
        language_label, title = "النص العربي", "النص العربي المُشَكَّل"
        paragraph = ARABIC
        mixed = ('<p>بِدَايَةٌ <span class="latin" dir="ltr">FIRST6814</span> وَسَطٌ '
                 '<span class="latin" dir="ltr">SECOND9273</span> نِهَايَةٌ.</p>')
        mixed_numbers = ('<p dir="rtl">رَقْمَانِ: <span dir="ltr">١٢٣٤٥</span> وَ '
                         '<span dir="ltr">67890</span>.</p>')
        mixed_ltr = ('<p dir="ltr"><span dir="ltr">LTRFIRST8120</span> '
                     '<span dir="rtl">مِثَالٌ</span> <span dir="ltr">LTRSECOND5926</span> 37.</p>')
        rtl_alignment, ltr_alignment = "خِتَامٌ", "Direction starts here."
        anchor = 'علامة البحث العربية: <span dir="ltr">ARABIC-ANCHOR-7429</span>.'
        caption = "الشكل ٤ — Arabic caption, 2026"
        link = '<a href="chapter-two.xhtml">اِنْتَقِلْ إِلَى الْفَصْلِ الثَّانِي</a>'
    else:
        language_label, title = "הטקסט העברי", "הטקסט העברי המְנֻקָּד"
        paragraph = HEBREW
        mixed = ('<p>רֵאשִׁית <span class="latin" dir="ltr">FIRST3141</span> בֵּינוֹת '
                 '<span class="latin" dir="ltr">SECOND5926</span> סוֹף.</p>')
        mixed_numbers = ('<p dir="rtl">שְׁנֵי מִסְפָּרִים: <span dir="ltr">١٢٣٤٥</span> ו־'
                         '<span dir="ltr">67890</span>.</p>')
        mixed_ltr = ('<p dir="ltr"><span dir="ltr">LTRFIRST4162</span> '
                     '<span dir="rtl">טֶקְסְט</span> <span dir="ltr">LTRSECOND2673</span> 84.</p>')
        rtl_alignment, ltr_alignment = "סִיּוּם", "Direction starts here."
        anchor = 'סִימַן חִפּוּשׂ עִבְרִי: <span dir="ltr">HEBREW-ANCHOR-8362</span>.'
        caption = "איור 5 — Hebrew caption, 2026"
        link = '<a href="chapter-two.xhtml">מַעֲבָר לַפֶּרֶק הַשֵּׁנִי</a>'
    if number != 1:
        mixed = f"<p>{paragraph}</p>"
        mixed_numbers = ""
        mixed_ltr = ""
        anchor = f"{paragraph}"
        link = '<a href="chapter-one.xhtml">↶</a>'
        alignment = ""
    else:
        alignment = f'<p dir="rtl">{rtl_alignment}</p><p dir="ltr">{ltr_alignment}</p>'
    paras = "".join(f"<p>{paragraph}</p>" for _ in range(18))
    return f'''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" lang="{language}" xml:lang="{language}" dir="rtl">
<head><title>{language_label}</title><style>body {{ direction: rtl; }} .latin {{ direction: ltr; unicode-bidi: isolate; }}</style></head>
<body dir="rtl"><h1>{title} — {number}</h1>
<figure><figcaption>{caption}</figcaption></figure>
{mixed}{mixed_numbers}{mixed_ltr}<p>{link}</p>
{alignment}
<p id="logical-anchor">{anchor}</p>{paras}</body></html>'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="output EPUB path")
    parser.add_argument("language", choices=("ar", "he"), help="EPUB language")
    args = parser.parse_args()
    book(args.output, args.language)


if __name__ == "__main__":
    main()
