#include "reader_inputs.h"
#include "assets/bundled_fonts.h"
#include "support/qt_compat.h"
#include "engine/crengine/reader_model.h"
#include <QFile>
#include <QGuiApplication>
#include <QSet>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRawFont>
#include <cstdio>
#include "check.h"

namespace {
QByteArray read(const char *path) {
    QFile file(QString::fromLocal8Bit(path));
    require(file.open(QIODevice::ReadOnly), "Cannot open multilingual EPUB");
    return file.readAll();
}

QVector<kobalt::SearchHit> search(kobalt::ReaderModel &model, const QString &query) {
    QVector<kobalt::SearchHit> found;
    QSet<QString> starts;
    for (int page = 0; page < model.pages(); ++page) {
        const auto batch = model.searchPage(query, page);
        for (const auto &hit : batch.hits) {
            require(!starts.contains(hit.start), "Duplicate multilingual search result");
            starts.insert(hit.start);
            require(!hit.start.isEmpty() && !hit.end.isEmpty() && hit.page == page,
                    "Multilingual search returned an invalid source range");
            found.push_back(hit);
        }
    }
    return found;
}

QRect bounds(const QVector<QRect> &rectangles) {
    QRect result;
    for (const auto &rect : rectangles)
        result = result.isNull() ? rect : result.united(rect);
    return result;
}

int inkPixels(const QImage &image, const QVector<QRect> &rectangles) {
    int count = 0;
    for (const auto &rect : rectangles) {
        const auto area = rect.intersected(image.rect());
        for (int y = area.top(); y <= area.bottom(); ++y)
            for (int x = area.left(); x <= area.right(); ++x)
                if (qGray(image.pixel(x, y)) < 230)
                    ++count;
    }
    return count;
}

void checkBook(const QByteArray &bytes, const QString &language, const QString &marker, const QString &first,
               const QString &second, const QString &ltrFirst, const QString &ltrSecond, const QString &ltrNumber,
               const QString &arabicDigits, const QString &westernDigits, const QString &rtlAlignment,
               const QString &ltrAlignment, const QString &scriptWord) {
    kobalt::ReaderModel model(kobalt::test::readerFonts());
    QString error;
    require(model.open(bytes, {1264, 1680}, kobalt::qt::object({{"font_pixels", 36}, {"text_alignment", "publisher"}}),
                       &error),
            qPrintable(error));
    require(model.pages() >= 2, qPrintable(language + " fixture did not paginate"));
    require(model.chapters().size() >= 2, qPrintable(language + " fixture lost its chapter structure"));

    const auto markerHits = search(model, marker);
    require(markerHits.size() == 1, qPrintable(language + " logical marker search was not unique"));
    const auto markerStart = markerHits.first().start;
    require(model.goTo(markerStart), qPrintable(language + " marker anchor did not resolve"));
    require(!model.rangeRects(markerHits.first().start, markerHits.first().end).isEmpty(),
            qPrintable(language + " marker has no rendered selection geometry"));

    const auto firstHits = search(model, first);
    const auto secondHits = search(model, second);
    require(firstHits.size() == 1 && secondHits.size() == 1,
            qPrintable(language + " mixed-direction words are not searchable"));
    const auto firstBounds = bounds(model.rangeRects(firstHits.first().start, firstHits.first().end));
    const auto secondBounds = bounds(model.rangeRects(secondHits.first().start, secondHits.first().end));
    require(!firstBounds.isEmpty() && !secondBounds.isEmpty(), qPrintable(language + " mixed run lost geometry"));
    require(qAbs(firstBounds.center().y() - secondBounds.center().y()) <=
                qMax(firstBounds.height(), secondBounds.height()),
            qPrintable(language + " mixed-order probes unexpectedly wrapped to separate lines"));
    require(firstBounds.center().x() > secondBounds.center().x(),
            qPrintable(language + " RTL logical order did not place its first LTR run on the right"));
    const auto arabicDigitHits = search(model, arabicDigits);
    const auto westernDigitHits = search(model, westernDigits);
    require(arabicDigitHits.size() == 1 && westernDigitHits.size() == 1,
            qPrintable(language + " directional digit runs are not searchable"));
    const auto arabicDigitBounds = bounds(model.rangeRects(arabicDigitHits.first().start, arabicDigitHits.first().end));
    const auto westernDigitBounds =
        bounds(model.rangeRects(westernDigitHits.first().start, westernDigitHits.first().end));
    require(!arabicDigitBounds.isEmpty() && !westernDigitBounds.isEmpty() &&
                qAbs(arabicDigitBounds.center().y() - westernDigitBounds.center().y()) <=
                    qMax(arabicDigitBounds.height(), westernDigitBounds.height()),
            qPrintable(language + " RTL-base digit runs lost same-line geometry"));
    require(arabicDigitBounds.center().x() > westernDigitBounds.center().x(),
            qPrintable(language + " RTL logical order did not place its first digit run on the right"));

    const auto ltrFirstHits = search(model, ltrFirst);
    const auto ltrSecondHits = search(model, ltrSecond);
    require(ltrFirstHits.size() == 1 && ltrSecondHits.size() == 1,
            qPrintable(language + " LTR-base mixed-direction words are not searchable"));
    const auto ltrFirstBounds = bounds(model.rangeRects(ltrFirstHits.first().start, ltrFirstHits.first().end));
    const auto ltrSecondBounds = bounds(model.rangeRects(ltrSecondHits.first().start, ltrSecondHits.first().end));
    require(!ltrFirstBounds.isEmpty() && !ltrSecondBounds.isEmpty() &&
                qAbs(ltrFirstBounds.center().y() - ltrSecondBounds.center().y()) <=
                    qMax(ltrFirstBounds.height(), ltrSecondBounds.height()),
            qPrintable(language + " LTR-base mixed-run probes lost same-line geometry"));
    require(ltrFirstBounds.center().x() < ltrSecondBounds.center().x(),
            qPrintable(language + " LTR logical order did not place its first run on the left"));
    const auto ltrNumberHits = search(model, ltrNumber);
    require(ltrNumberHits.size() == 1, qPrintable(language + " LTR-base number is not searchable"));
    const auto ltrNumberBounds = bounds(model.rangeRects(ltrNumberHits.first().start, ltrNumberHits.first().end));
    require(!ltrNumberBounds.isEmpty() && ltrNumberBounds.center().x() > ltrSecondBounds.center().x(),
            qPrintable(language + " LTR logical order misplaced the following number"));

    const auto rtlAlignmentHits = search(model, rtlAlignment);
    const auto ltrAlignmentHits = search(model, ltrAlignment);
    require(rtlAlignmentHits.size() == 1 && ltrAlignmentHits.size() == 1,
            qPrintable(language + " alignment probes are not unique"));
    const auto rtlAlignmentBounds =
        bounds(model.rangeRects(rtlAlignmentHits.first().start, rtlAlignmentHits.first().end));
    const auto ltrAlignmentBounds =
        bounds(model.rangeRects(ltrAlignmentHits.first().start, ltrAlignmentHits.first().end));
    require(!rtlAlignmentBounds.isEmpty() && !ltrAlignmentBounds.isEmpty() &&
                rtlAlignmentBounds.center().x() > model.viewport().width() * 0.6 &&
                ltrAlignmentBounds.center().x() < model.viewport().width() * 0.4,
            qPrintable(language + " short paragraphs did not align to their direction start"));

    const auto scriptHits = search(model, scriptWord);
    require(scriptHits.size() == 1, qPrintable(language + " diacritic-bearing script text is not searchable"));
    require(model.goTo(scriptHits.first().start), qPrintable(language + " script-word anchor did not resolve"));
    const auto scriptRects = model.rangeRects(scriptHits.first().start, scriptHits.first().end);
    require(!scriptRects.isEmpty(), qPrintable(language + " script word has no geometry"));
    require(inkPixels(model.image(model.page()), scriptRects) > 2,
            qPrintable(language + " script word produced no visible ink in its selection geometry"));
    const auto selection = model.wordAt(scriptRects.first().center());
    require(selection.text.normalized(QString::NormalizationForm_C) ==
                    scriptWord.normalized(QString::NormalizationForm_C) &&
                !selection.start.isEmpty() && !selection.end.isEmpty(),
            qPrintable(language + " word selection mismatch: expected " + scriptWord + ", got " + selection.text));

    const auto savedAnchor = model.position();
    require(model.style(48, 120), qPrintable(language + " fixture could not reflow"));
    require(model.pages() >= 2, qPrintable(language + " fixture stopped paginating after reflow"));
    const auto afterReflow = search(model, marker);
    require(afterReflow.size() == 1 && afterReflow.first().start == markerStart,
            qPrintable(language + " search anchor changed after reflow"));
    require(model.goTo(savedAnchor) && !model.rangeRects(scriptHits.first().start, scriptHits.first().end).isEmpty(),
            qPrintable(language + " logical anchor or selection geometry failed after reflow"));

    int visiblePages = 0;
    for (int page = 0; page < model.pages(); ++page) {
        require(model.go(page), qPrintable(language + " page navigation failed after reflow"));
        if (!model.pageStart(page).isEmpty() && !model.image(page).isNull())
            ++visiblePages;
    }
    require(visiblePages >= 2, qPrintable(language + " reflow produced fewer than two renderable pages"));
}
void checkPublicDomain(const QString &directory) {
    QFile file(QDir(directory).filePath("public-domain-inventory.json"));
    require(file.open(QIODevice::ReadOnly), "Cannot read literary fixture inventory");
    const auto entries = QJsonDocument::fromJson(file.readAll()).array();
    require(entries.size() == 16, "Literary script inventory is incomplete");
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        const auto language = entry["language"].toString();
        const auto path = QDir(directory).filePath(entry["file"].toString()).toLocal8Bit();
        kobalt::ReaderModel model(kobalt::test::readerFonts());
        QString error;
        require(model.open(read(path.constData()), {1264, 1680}, {}, &error), qPrintable(error));
        require(model.pages() > 1, qPrintable(language + " literary chapter did not paginate"));
        require(model.state()["page_progression_rtl"].toBool() == (language == "ar" || language == "he"),
                qPrintable(language + " literary book has the wrong page direction"));
        const auto chapters = model.chapters();
        for (const auto &item : entry["chapters"].toArray()) {
            const auto chapter = item.toObject();
            bool visited = false;
            for (const auto &choice : chapters)
                if (choice.title == chapter["title"].toString()) {
                    require(model.goTo(choice.position) && !model.image(model.page()).isNull(),
                            qPrintable(language + " literary chapter navigation failed"));
                    visited = true;
                    break;
                }
            require(visited,
                    qPrintable(language + " literary TOC lost a native title: " + chapter["title"].toString()));
            require(
                !search(model, chapter["probe"].toString()).isEmpty(),
                qPrintable(language + " original literary opening is not searchable: " + chapter["probe"].toString()));
        }
        std::printf("PASS: %s public-domain chapters and native TOC\n", qPrintable(language));
    }
}
void checkInternational(const QString &directory) {
    QFile metadata(QDir(directory).filePath("international.json"));
    require(metadata.open(QIODevice::ReadOnly), "Cannot open international fixture inventory");
    const auto entries = QJsonDocument::fromJson(metadata.readAll()).array();
    require(entries.size() >= 13, "International fixture inventory is incomplete");
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        const auto language = entry["language"].toString(), probe = entry["probe"].toString();
        QRawFont font(kobalt::test::readerAssets().file("fonts/" + entry["font"].toString()), 36);
        QRawFont latin(kobalt::test::readerAssets().file("fonts/NotoSerif-Regular.ttf"), 36);
        require(font.isValid() && latin.isValid(), qPrintable(language + " fallback font did not load"));
        const auto text = entry["title"].toString() + probe + entry["paragraph"].toString();
        for (const auto codepoint : text.toUcs4()) {
            if (QChar::category(codepoint) == QChar::Other_Format || QChar::isSpace(codepoint))
                continue;
            require(font.supportsCharacter(codepoint) || latin.supportsCharacter(codepoint),
                    qPrintable(language + " has a missing glyph: U+" + QString::number(codepoint, 16)));
        }
        const auto path = QDir(directory).filePath(entry["file"].toString()).toLocal8Bit();
        kobalt::ReaderModel model(kobalt::test::readerFonts());
        QString error;
        require(model.open(read(path.constData()), {1264, 1680},
                           kobalt::qt::object({{"font_pixels", kobalt::defaultFontPixels()}}), &error),
                qPrintable(error));
        require(model.state()["language"].toString().compare(language, Qt::CaseInsensitive) == 0,
                qPrintable(language + " document language was not exposed to the picker"));
        const auto supplemental = kobalt::supplementalFontFamily(language);
        bool offered = supplemental.isEmpty();
        for (const auto &face : model.fontCatalog())
            if (face.family == supplemental)
                offered = true;
        require(offered, qPrintable(language + " supplemental reading font is unavailable: " + supplemental));
        require(model.pages() >= 3 && model.chapters().size() >= 2,
                qPrintable(language + " book lost pagination or chapters"));
        require(!model.state()["page_progression_rtl"].toBool(),
                qPrintable(language + " LTR book has RTL progression"));
        const auto hits = search(model, probe);
        require(hits.size() == 1, qPrintable(language + " native-script probe is not uniquely searchable"));
        require(model.goTo(hits.first().start), qPrintable(language + " native-script anchor did not resolve"));
        auto rectangles = model.rangeRects(hits.first().start, hits.first().end);
        require(!rectangles.isEmpty() && inkPixels(model.image(model.page()), rectangles) > 2,
                qPrintable(language + " native-script text has no visible ink"));
        const auto selected = model.wordAt(rectangles.first().center());
        require(!selected.text.isEmpty() && !selected.start.isEmpty() && !selected.end.isEmpty(),
                qPrintable(language + " word selection has no source range"));
        require(probe.contains(selected.text) || selected.text.contains(probe),
                qPrintable(language + " native-script selection differs: " + selected.text));
        require(model.style(kobalt::defaultFontPixels(), 130), qPrintable(language + " default-size reflow failed"));
        const auto latinHits = search(model, QStringLiteral("SCRIPT-PROBE-1"));
        require(latinHits.size() == 1 && model.goTo(latinHits.first().start),
                qPrintable(language + " mixed Latin text is not searchable"));
        const auto latinRects = model.rangeRects(latinHits.first().start, latinHits.first().end);
        require(!latinRects.isEmpty() && inkPixels(model.image(model.page()), latinRects) > 2,
                qPrintable(language + " mixed Latin text has no visible ink"));
        const auto anchor = hits.first().start;
        require(model.style(48, 130), qPrintable(language + " book could not reflow"));
        const auto reflowed = search(model, probe);
        require(reflowed.size() == 1 && reflowed.first().start == anchor && model.goTo(anchor),
                qPrintable(language + " source anchor changed after reflow"));
        require(model.goTo(model.chapters().last().position), qPrintable(language + " second chapter did not open"));
        require(!model.image(model.page()).isNull(), qPrintable(language + " second chapter did not render"));
        const auto checkpoint = model.state();
        kobalt::ReaderModel reopened(kobalt::test::readerFonts());
        require(reopened.open(read(path.constData()), {1264, 1680}, checkpoint, &error), qPrintable(error));
        require(reopened.position() == model.position(),
                qPrintable(language + " reopened book lost its source position"));
        printf("PASS: %s glyph coverage, native-script search, selection, chapters and reflow\n", qPrintable(language));
    }
}

}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    require(argc == 3, "Pass Arabic and Hebrew synthetic EPUB paths");
    checkBook(read(argv[1]), QStringLiteral("Arabic"), QStringLiteral("ARABIC-ANCHOR-7429"),
              QStringLiteral("FIRST6814"), QStringLiteral("SECOND9273"), QStringLiteral("LTRFIRST8120"),
              QStringLiteral("LTRSECOND5926"), QStringLiteral("37"), QString::fromUtf8("١٢٣٤٥"),
              QStringLiteral("67890"), QString::fromUtf8("خِتَامٌ"), QStringLiteral("Direction starts here"),
              QString::fromUtf8("بِدَايَةٌ"));
    checkBook(read(argv[2]), QStringLiteral("Hebrew"), QStringLiteral("HEBREW-ANCHOR-8362"),
              QStringLiteral("FIRST3141"), QStringLiteral("SECOND5926"), QStringLiteral("LTRFIRST4162"),
              QStringLiteral("LTRSECOND2673"), QStringLiteral("84"), QString::fromUtf8("١٢٣٤٥"),
              QStringLiteral("67890"), QString::fromUtf8("סִיּוּם"), QStringLiteral("Direction starts here"),
              QString::fromUtf8("רֵאשִׁית"));
    checkInternational(QFileInfo(QString::fromLocal8Bit(argv[1])).absolutePath());
    checkPublicDomain(QFileInfo(QString::fromLocal8Bit(argv[1])).absolutePath());
    puts("PASS: Arabic and Hebrew shaping inputs, mixed LTR/RTL placement, selection and reflow anchors");
}
