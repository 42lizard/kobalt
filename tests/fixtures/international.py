"""Original two-chapter books for script coverage and navigation checks."""
import json
from pathlib import Path
import zipfile
from xml.sax.saxutils import escape

SAMPLES = {
    'el': ('greek', 'Ελληνική ανάγνωση', 'βιβλιοθήκη', 'Το βιβλίο είναι ανοιχτό. Διαβάζουμε μια σελίδα και συνεχίζουμε στο επόμενο κεφάλαιο.', 'NotoSerif-Regular.ttf'),
    'ru': ('cyrillic', 'Чтение книги', 'библиотека', 'Книга открыта. Мы читаем страницу, переходим к следующей главе и возвращаемся к началу.', 'NotoSerif-Regular.ttf'),
    'hy': ('armenian', 'Հայերեն գիրք', 'ընթերցում', 'Գիրքը բաց է։ Մենք կարդում ենք մի էջ և անցնում հաջորդ գլխին։', 'NotoSerifArmenian-Regular.ttf'),
    'ka': ('georgian', 'ქართული წიგნი', 'კითხვა', 'წიგნი გახსნილია. ჩვენ ვკითხულობთ ერთ გვერდს და გადავდივართ შემდეგ თავზე.', 'NotoSerifGeorgian-Regular.ttf'),
    'hi': ('hindi', 'हिन्दी पुस्तक', 'क्षेत्र', 'यह एक खुली किताब है। हम एक पृष्ठ पढ़ते हैं और अगले अध्याय पर जाते हैं। शब्दों में संयुक्त अक्षर और मात्रा हैं।', 'NotoSerifDevanagari-Regular.ttf'),
    'bn': ('bengali', 'বাংলা বই', 'পাঠক', 'বইটি খোলা আছে। আমরা একটি পৃষ্ঠা পড়ি এবং পরের অধ্যায়ে যাই। যুক্তাক্ষর এবং স্বরচিহ্ন দেখা যায়।', 'NotoSerifBengali-Regular.ttf'),
    'ta': ('tamil', 'தமிழ் நூல்', 'புத்தகம்', 'நூல் திறந்து இருக்கிறது. நாம் ஒரு பக்கத்தைப் படித்து அடுத்த பகுதிக்குச் செல்கிறோம்.', 'NotoSerifTamil-Regular.ttf'),
    'th': ('thai', 'การอ่านภาษาไทย', 'หนังสือ', 'เราอ่านหนึ่งหน้าแล้วไปยังบทถัดไป ตัวอักษรและเครื่องหมายวรรณยุกต์อยู่ในข้อความนี้', 'NotoSerifThai-Regular.ttf'),
    'vi': ('latin', 'Đọc sách tiếng Việt', 'nghiên cứu', 'Cuốn sách đang mở. Chúng ta xem một trang, sang chương tiếp theo và quay lại đoạn trước. Chữ Việt có dấu: ă â ê ô ơ ư đ.', 'NotoSerif-Regular.ttf'),
    'ja': ('japanese', '日本語の横書き', '読書', '本を開いて、一つのページを読みます。次の章に進んでから、前の文章に戻ります。ひらがな、カタカナ、漢字を表示します。', 'NotoSerifCJK-Regular.ttc'),
    'ko': ('korean', '한국어 책 읽기', '독서', '책을 열고 한 페이지를 읽습니다. 다음 장으로 이동한 뒤 앞의 문장으로 돌아갑니다.', 'NotoSerifCJK-Regular.ttc'),
    'zh-Hans': ('chinese-simplified', '简体中文阅读', '探索', '我们打开一本书，读完一页，再进入下一章。文字、标点和数字都应该清楚地显示。', 'NotoSerifCJK-Regular.ttc'),
    'zh-Hant': ('chinese-traditional', '繁體中文讀書', '探索', '我們打開一本書，讀完一頁，再進入下一章。文字、標點和數字都應該清楚地顯示。', 'NotoSerifCJK-Regular.ttc'),
}


def book(path, language):
    name, title, probe, paragraph, font = SAMPLES[language]
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(path, 'w') as epub:
        def write(name, data, compression=zipfile.ZIP_DEFLATED):
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0)); info.compress_type = compression
            epub.writestr(info, data)
        write('mimetype', 'application/epub+zip', zipfile.ZIP_STORED)
        write('META-INF/container.xml', '<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="EPUB/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        items = ''.join(f'<item id="c{i}" href="chapter-{i}.xhtml" media-type="application/xhtml+xml"/>' for i in (1, 2))
        write('EPUB/book.opf', f'<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="uid">kobalt-international-{language}</dc:identifier><dc:title>{escape(title)}</dc:title><dc:language>{language}</dc:language><dc:creator>Kobalt Test Fixtures</dc:creator></metadata><manifest>{items}<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest><spine page-progression-direction="ltr"><itemref idref="c1"/><itemref idref="c2"/></spine></package>')
        links = ''.join(f'<li><a href="chapter-{i}.xhtml">{escape(title)} {i}</a></li>' for i in (1, 2))
        write('EPUB/nav.xhtml', f'<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" lang="{language}" xml:lang="{language}"><head><title>{escape(title)}</title></head><body><nav epub:type="toc"><ol>{links}</ol></nav></body></html>')
        for i in (1, 2):
            lead = f'<p id="probe">{escape(probe)}</p><p dir="ltr">SCRIPT-PROBE-1: <span lang="en">Latin text 12345</span></p>' if i == 1 else '<p id="second">SCRIPT-PROBE-2</p>'
            text = ''.join(f'<p>{escape(paragraph)}</p>' for _ in range(36))
            # Keep decomposed accents as well as the precomposed Vietnamese text.
            if language == 'vi' and i == 1: text += '<p>a&#x306;&#x301; e&#x302;&#x300; o&#x31b;&#x309;</p>'
            write(f'EPUB/chapter-{i}.xhtml', f'<html xmlns="http://www.w3.org/1999/xhtml" lang="{language}" xml:lang="{language}" dir="ltr"><head><title>{escape(title)} {i}</title></head><body><h1>{escape(title)} {i}</h1>{lead}{text}</body></html>')


def write_all(directory):
    directory = Path(directory)
    entries = []
    for language, (name, title, probe, paragraph, font) in SAMPLES.items():
        filename = f'international-{language}.epub'
        book(directory / filename, language)
        entries.append(dict(language=language, name=name, title=title, probe=probe, paragraph=paragraph, font=font, file=filename))
    (directory / 'international.json').write_text(json.dumps(entries, ensure_ascii=False, indent=2) + '\n')
