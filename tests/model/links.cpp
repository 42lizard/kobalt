#include "reader_inputs.h"
#include "support/qt_compat.h"
#include "engine/crengine/reader_model.h"
#include <QFile>
#include <QGuiApplication>
#include <QMap>
#include <cstdio>
#include "check.h"

namespace {
QImage decoratedLink(const QByteArray &markup, const QByteArray &css, bool dark) {
    const QMap<QString, QByteArray> files{
        {"mimetype", "application/epub+zip"},
        {"META-INF/container.xml",
         "<container xmlns='urn:oasis:names:tc:opendocument:xmlns:container' version='1.0'><rootfiles><rootfile full-path='book.opf' media-type='application/oebps-package+xml'/></rootfiles></container>"},
        {"book.opf",
         "<package xmlns='http://www.idpf.org/2007/opf' version='2.0'><metadata><title>Link decoration</title></metadata><manifest><item id='c' href='c.xhtml' media-type='application/xhtml+xml'/></manifest><spine><itemref idref='c'/></spine></package>"},
        {"c.xhtml", "<html xmlns='http://www.w3.org/1999/xhtml'><head><style>p{margin:0;text-indent:0}" + css +
                        "</style></head><body><p>" + markup + "</p></body></html>"}};
    QMap<QString, qint64> sizes;
    for (auto it = files.cbegin(); it != files.cend(); ++it)
        sizes[it.key()] = it.value().size();
    auto resources = std::make_shared<kobalt::ResourceCache>(
        files.keys(),
        [files](const QString &name, const kobalt::ResourceCache::Sink &sink, QString *) {
            const auto bytes = files.value(name);
            return sink(bytes.constData(), bytes.size());
        },
        std::make_shared<std::atomic_bool>(false));
    kobalt::ReaderModel model(kobalt::test::readerFonts());
    QString error;
    require(model.openResources(resources, sizes, QSize(800, 600),
                                kobalt::qt::object({{"font_pixels", 40}, {"dark_mode", dark}}), &error),
            qPrintable(error));
    require(model.pageLinks().size() == 1, "Decorated link lost its hit region");
    const auto image = model.image(0);
    kobalt::ReaderModel preview(kobalt::test::readerFonts());
    require(preview.openPreview(model, QSize(800, 600), &error), qPrintable(error));
    require(preview.image(0) == image, "Link decoration changed in the appearance preview");
    return image;
}
void linkDecoration() {
    for (const bool dark : {false, true}) {
        const auto render = [dark](const QByteArray &markup, const QByteArray &css) {
            return decoratedLink(markup, css, dark);
        };
        const QByteArray wrapped =
            "<a href='https://example.org'><se-span class='kobo-link'><span class='koboSpan'>Sample link</span></se-span></a>";
        const QByteArray border = "se-span.kobo-link{border-bottom:1px dotted currentColor!important}";
        const auto expected = render(wrapped, border + "a{ text-decoration:none }");
        const auto actual = render(wrapped, border);
        require(actual == expected, "Default underline duplicates the publisher's Kobo link border");
        const QByteArray ordinary = "<a href='https://example.org'>Sample link</a>";
        require(render(ordinary, {}) != render(ordinary, "a{text-decoration:none}"),
                "Ordinary links lost their default underline");
        require(render(wrapped, {}) != render(wrapped, "a{text-decoration:none}se-span.kobo-link{border:none}"),
                "Unstyled Kobo link wrapper lacks a visible decoration");
        const QByteArray custom =
            "se-span.kobo-link{border-bottom:3px dashed black!important;text-decoration:overline!important}";
        require(render(wrapped, custom) == render(wrapped, custom + "a{text-decoration:none}"),
                "Kobo compatibility styling changed publisher decoration");
        require(render(wrapped, border + "a{text-decoration:overline}") != expected,
                "Kobo compatibility styling overrode a publisher anchor decoration");
        const QByteArray mixed =
            "<a href='https://example.org'>Prefix <se-span class='kobo-link'>Sample link</se-span></a>";
        require(render(mixed, border) != render(mixed, border + "a{text-decoration:none}"),
                "Partially wrapped link lost the underline on its ordinary text");
    }
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    require(argc == 2, "Pass the synthetic links EPUB");
    QFile file(QString::fromLocal8Bit(argv[1]));
    require(file.open(QIODevice::ReadOnly), "Cannot open link fixture");
    kobalt::ReaderModel model(kobalt::test::readerFonts());
    QString error;
    require(model.open(file.readAll(), QSize(1072, 1300), kobalt::qt::object({{"font_pixels", 32}}), &error),
            qPrintable(error));
    linkDecoration();
    const auto origin = model.position();
    for (const bool dark : {false, true}) {
        model.setDarkMode(dark);
        require(model.image(0).text("kobalt.animation_text_only") == "true",
                "Default link colors do not follow the monochrome page palette");
    }
    model.setDarkMode(false);
    QMap<QString, kobalt::LinkDetails> details;
    QMap<QString, QString> sources;
    const auto links = model.pageLinks();
    for (const auto &link : links) {
        require(!link.source.isEmpty() && !link.rectangles.isEmpty(), "Visible link lacks a source or hit region");
        for (const auto &rect : link.rectangles)
            require(QRect(0, 0, 1072, 1300).contains(rect), "Link hit region escaped the viewport");
        const auto value = model.linkDetails(link.source);
        details[value.label] = value;
        sources[value.label] = link.source;
    }
    require(details.size() == 10,
            qPrintable(QString("Expected 10 visible links, found: %1").arg(QStringList(details.keys()).join(", "))));
    using kobalt::LinkKind;
    require(details["short note"].kind == LinkKind::Footnote && !details["short note"].target.isEmpty(),
            "EPUB note link did not resolve");
    const auto html = details["short note"].html;
    require(html.contains("<strong>short</strong>") && html.contains("café &amp; tea") &&
                html.contains("Second paragraph"),
            "Note lost structure, Unicode or escaped text");
    require(!html.contains("Note paragraph") && !html.contains("href="),
            "Note preview included unrelated content or active links");
    require(details["long note"].kind == LinkKind::Footnote &&
                details["long note"].html.contains("End of the long note."),
            "Long note was silently truncated");
    require(details["later passage"].kind == LinkKind::Passage && details["nearby passage"].kind == LinkKind::Passage,
            "Internal passage links were misclassified");
    require(details["short note"].chapter == "Notes" && details["later passage"].chapter == "Passages" &&
                details["nearby passage"].chapter == "References",
            "Link targets do not name their chapter");
    require(details["missing passage"].kind == LinkKind::Unavailable && details["missing passage"].target.isEmpty(),
            "Missing target appeared navigable");
    require(details["website"].kind == LinkKind::Website && details["website"].target == "https://example.org/book",
            "Website URL was changed or followed");
    require(details["unsupported link"].kind == LinkKind::Unavailable, "Unsupported link scheme became navigable");
    require(details["legacy note"].html.contains("A legacy note") &&
                !details["legacy note"].html.contains("hidden note"),
            "Legacy note escaped its paragraph");
    require(!details["oversized note"].error.isEmpty() && details["oversized note"].html.isEmpty(),
            "Oversized note bypassed the preview bound");
    require(details["hidden note"].kind == LinkKind::Footnote && details["hidden note"].html.contains("A hidden note"),
            "Hidden note could not be previewed");
    require(model.position() == origin && model.page() == 0, "Inspecting links moved the reader");
    require(model.style(48, 140), "Cannot reflow the links fixture");
    require(model.linkDetails(sources["short note"]).target == details["short note"].target,
            "Reflow changed the note's source target");
    require(model.goTo(details["later passage"].target), "Cannot navigate to a resolved passage");
    require(model.image(model.page()).text("kobalt.animation_text_only") == "false",
            "Default link styling discarded the publisher link color");
    require(model.goTo(origin) && model.position() == origin, "Cannot return to the exact original source anchor");
    require(model.linkDetails("/missing/node").kind == LinkKind::None, "Invalid source produced a link");
    puts(
        "PASS: page link regions, internal targets, note boundaries, preview limits, hidden notes and exact return after reflow");
}
