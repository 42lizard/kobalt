#include "reader_inputs.h"
#include "support/qt_compat.h"
#include "engine/crengine/reader_model.h"
#include <QGuiApplication>
#include <QMap>
#include <atomic>
#include "check.h"

namespace {
struct Probe {
    int page;
    QRect rect;
};

Probe marker(kobalt::ReaderModel &model, const QString &text) {
    QVector<kobalt::SearchHit> hits;
    for (int page = 0; page < model.pages(); ++page) {
        const auto batch = model.searchPage(text, page);
        hits += batch.hits;
    }
    require(hits.size() == 1, qPrintable("MathML marker is not unique: " + text));
    const auto rectangles = model.rangeRects(hits.first().start, hits.first().end);
    require(!rectangles.isEmpty(), qPrintable("MathML marker has no geometry: " + text));
    QRect rect;
    for (const auto &part : rectangles)
        rect = rect.isNull() ? part : rect.united(part);
    return {hits.first().page, rect};
}

QImage openFixture() {
    const QMap<QString, QByteArray> data{
        {"mimetype", "application/epub+zip"},
        {"META-INF/container.xml",
         "<container xmlns='urn:oasis:names:tc:opendocument:xmlns:container' version='1.0'><rootfiles><rootfile full-path='book.opf' media-type='application/oebps-package+xml'/></rootfiles></container>"},
        {"book.opf",
         "<package xmlns='http://www.idpf.org/2007/opf' version='2.0'><metadata><title>MathML fraction test</title></metadata><manifest><item id='chapter' href='chapter.xhtml' media-type='application/xhtml+xml'/></manifest><spine><itemref idref='chapter'/></spine></package>"},
        {"chapter.xhtml",
         "<html xmlns='http://www.w3.org/1999/xhtml'><head><style>body{margin:0}p{margin:0;text-indent:0}</style></head><body>"
         "<p>MATH-FRACTION-BEGIN</p>"
         "<math xmlns='http://www.w3.org/1998/Math/MathML' display='block' dir='ltr'><mfrac><mn>19</mn><mn>4</mn></mfrac></math>"
         "<p>MATH-FRACTION-END</p>"
         "<p>MATH-CONTROL-BEGIN</p><p dir='ltr'>19 / 4</p><p>MATH-CONTROL-END</p>"
         "</body></html>"}};
    QMap<QString, qint64> sizes;
    for (auto it = data.cbegin(); it != data.cend(); ++it)
        sizes[it.key()] = it.value().size();
    auto resources = std::make_shared<kobalt::ResourceCache>(
        data.keys(),
        [data](const QString &name, const kobalt::ResourceCache::Sink &sink, QString *) {
            const auto bytes = data.value(name);
            return sink(bytes.constData(), bytes.size());
        },
        std::make_shared<std::atomic_bool>(false));
    kobalt::ReaderModel model(kobalt::test::readerFonts());
    QString error;
    require(model.openResources(resources, sizes, {640, 900}, {}, &error), qPrintable(error));
    const auto fractionBegin = marker(model, QStringLiteral("MATH-FRACTION-BEGIN"));
    const auto fractionEnd = marker(model, QStringLiteral("MATH-FRACTION-END"));
    const auto controlBegin = marker(model, QStringLiteral("MATH-CONTROL-BEGIN"));
    const auto controlEnd = marker(model, QStringLiteral("MATH-CONTROL-END"));
    require(fractionBegin.page == fractionEnd.page && fractionBegin.page == controlBegin.page &&
                fractionBegin.page == controlEnd.page,
            "MathML fraction and control did not fit on one page");
    require(fractionEnd.rect.center().y() - fractionBegin.rect.center().y() >
                controlEnd.rect.center().y() - controlBegin.rect.center().y() + 4,
            "MathML fraction did not add vertical layout beyond the plain-text control");

    require(model.go(fractionBegin.page), "Cannot open the MathML fraction page");
    const auto image = model.image(fractionBegin.page);
    int ink = 0;
    for (int y = qMax(0, fractionBegin.rect.bottom() + 1); y < qMin(image.height(), fractionEnd.rect.top()); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 230)
                ++ink;
    require(ink > 4, "MathML fraction rendered no ink between its framing markers");
    return image;
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    const auto image = openFixture();
    require(!image.isNull(), "MathML fraction page did not render");
    puts("PASS: MathML fraction occupies vertical layout and renders between its markers");
}
