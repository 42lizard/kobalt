#include "engine/crengine/reader_model.h"
#include "check.h"
#include "reader_inputs.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <array>

namespace {
void open(kobalt::ReaderModel &model, const QByteArray &body, const QByteArray &css = {},
          QJsonObject settings = {}, QSize size = {1072, 1448}) {
    const QMap<QString, QByteArray> data{
        {"mimetype", "application/epub+zip"},
        {"META-INF/container.xml", "<container xmlns='urn:oasis:names:tc:opendocument:xmlns:container' version='1.0'><rootfiles><rootfile full-path='book.opf' media-type='application/oebps-package+xml'/></rootfiles></container>"},
        {"book.opf", "<package xmlns='http://www.idpf.org/2007/opf' version='2.0'><metadata><title>Advanced text</title></metadata><manifest><item id='c' href='c.xhtml' media-type='application/xhtml+xml'/></manifest><spine><itemref idref='c'/></spine></package>"},
        {"c.xhtml", "<html xmlns='http://www.w3.org/1999/xhtml'><head><style>p{margin:0;text-indent:0}" + css +
                          "</style></head><body>" + body + "</body></html>"}};
    QMap<QString, qint64> sizes;
    for (auto it = data.cbegin(); it != data.cend(); ++it)
        sizes[it.key()] = it.value().size();
    auto source = std::make_shared<kobalt::ResourceCache>(data.keys(),
        [data](const QString &name, const kobalt::ResourceCache::Sink &sink, QString *) {
            const auto bytes = data.value(name);
            return sink(bytes.constData(), bytes.size());
        }, std::make_shared<std::atomic_bool>(false));
    settings["font_pixels"] = 40;
    settings["line_spacing"] = 100;
    settings["margin_pixels"] = 0;
    settings["top_margin_pixels"] = 0;
    settings["bottom_margin_pixels"] = 0;
    QString error;
    require(model.openResources(source, sizes, size, settings, &error), qPrintable(error));
}
std::array<int, 4> paragraphPages(kobalt::ReaderModel &model) {
    std::array<int, 4> pages;
    int index = 0;
    for (const auto *marker : {"Firstmarker", "Secondmarker", "Thirdmarker", "Fourthmarker"}) {
        bool found = false;
        for (int page = 0; page < model.pages(); ++page) {
            const auto hits = model.searchPage(marker, page).hits;
            if (hits.isEmpty())
                continue;
            require(model.goTo(hits.first().start), "Cannot navigate to paragraph line");
            pages[index++] = model.page();
            found = true;
            break;
        }
        require(found, "Pagination lost a paragraph line");
    }
    return pages;
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    using kobalt::ReaderModel;
    const QByteArray prose = "<p>" + QByteArray("Office affinity café amber birch cedar dogwood hazel oak. ").repeated(20) + "</p>";
    ReaderModel model(kobalt::test::readerFonts());
    open(model, prose, {}, {{"text_alignment", "justify"}});
    const auto original = model.image(0);
    const auto anchor = model.position();
    const int pages = model.pages();
    require(model.state().value("justification_tracking").toInt(-1) == 0 &&
                model.state().value("widows_orphans").toString() == "publisher", "Advanced text defaults changed");
    for (int percent : {5, 15}) {
        require(model.applyAppearance({{"justification_tracking", percent}}), "Cannot apply tracking");
        require(model.image(0) != original, "Tracking did not change loose justified lines");
        require(model.pages() == pages && model.position() == anchor, "Tracking changed line fitting or position");
        ReaderModel preview(kobalt::test::readerFonts());
        QString error;
        require(preview.openPreview(model, {1072, 1448}, &error), qPrintable(error));
        require(preview.state().value("justification_tracking").toInt() == percent &&
                    preview.image(0) == model.image(0), "Preview lost tracking");
        ReaderModel reopened(kobalt::test::readerFonts());
        open(reopened, prose, {}, model.state());
        require(reopened.image(0) == model.image(0), "Reopening lost tracking");
    }
    require(model.applyAppearance({{"justification_tracking", 0}}) && model.image(0) == original,
            "Turning tracking off did not restore pixels");
    require(!model.applyAppearance({{"justification_tracking", -1}}) &&
                !model.applyAppearance({{"justification_tracking", 21}}) &&
                !model.applyAppearance({{"widows_orphans", "invalid"}}), "Invalid advanced text was accepted");
    for (const auto *alignment : {"left", "right", "center"}) {
        ReaderModel aligned(kobalt::test::readerFonts());
        open(aligned, prose, {}, {{"text_alignment", alignment}});
        const auto before = aligned.image(0);
        require(aligned.applyAppearance({{"justification_tracking", 15}}) && aligned.image(0) == before,
                "Tracking changed non-justified text");
    }
    for (const QByteArray &text : {QByteArray("日本語の文章を読む楽しみ。漢字と仮名。"),
                                 QByteArray("القراءة تمنح الإنسان معرفة وحكمة ")}) {
        ReaderModel shaped(kobalt::test::readerFonts());
        open(shaped, "<p>" + text.repeated(15) + "</p>", {}, {{"text_alignment", "justify"}});
        const auto before = shaped.image(0);
        require(shaped.applyAppearance({{"justification_tracking", 15}}) && shaped.image(0) == before,
                "Tracking changed CJK glyphs or cursive joining");
    }
    const QByteArray spacingProse = "<p>" + QByteArray("A small boat lay by the shore while we walked slowly over the sand and watched birds rise above the water. " ).repeated(25) + "</p>";
    ReaderModel spaced(kobalt::test::readerFonts());
    open(spaced, spacingProse, {}, {{"text_alignment", "left"}}, {530, 320});
    const auto normalSpaces = spaced.image(0);
    require(spaced.state().value("word_spacing").toInt() == 100, "Word spacing default changed");
    QStringList narrowStarts;
    for (int percent : {80, 120}) {
        require(spaced.applyAppearance({{"word_spacing", percent}}), "Cannot apply word spacing");
        require(spaced.image(0) != normalSpaces, "Word spacing did not change rendered text");
        QStringList starts;
        for (int page = 0; page < spaced.pages(); ++page)
            starts.append(spaced.pageStart(page));
        if (percent == 80)
            narrowStarts = starts;
        else
            require(starts != narrowStarts, "Word spacing did not reflow the book");
        ReaderModel preview(kobalt::test::readerFonts());
        QString error;
        require(preview.openPreview(spaced, {530, 320}, &error), qPrintable(error));
        require(preview.state().value("word_spacing").toInt() == percent && preview.image(0) == spaced.image(0),
                "Preview lost word spacing");
        ReaderModel reopened(kobalt::test::readerFonts());
        open(reopened, spacingProse, {}, spaced.state(), {530, 320});
        require(reopened.image(0) == spaced.image(0), "Reopening lost word spacing");
    }
    require(spaced.applyAppearance({{"word_spacing", 100}}) && spaced.image(0) == normalSpaces,
            "Normal word spacing did not restore pixels");
    require(!spaced.applyAppearance({{"word_spacing", 0}}) && !spaced.applyAppearance({{"word_spacing", 101}}),
            "Invalid word spacing was accepted");
    ReaderModel invalidSpacing(kobalt::test::readerFonts());
    open(invalidSpacing, spacingProse, {}, {{"word_spacing", 500}, {"text_alignment", "left"}}, {530, 320});
    require(invalidSpacing.state().value("word_spacing").toInt() == 100 && invalidSpacing.image(0) == normalSpaces,
            "Invalid saved word spacing did not fall back to Normal");
    bool orphan = false, widow = false;
    const QByteArray paragraph = "<p id='target' style='orphans:1!important;widows:1!important'>"
                                 "Firstmarker<br/>Secondmarker<br/>Thirdmarker<br/>Fourthmarker</p>";
    for (int count = 3; count < 10 && (!orphan || !widow); ++count) {
        const auto body = "<p>" + QByteArray("Filler<br/>").repeated(count - 1) + "Filler</p>" + paragraph;
        ReaderModel pagination(kobalt::test::readerFonts());
        open(pagination, body, {}, {}, {1072, 320});
        const auto allowed = paragraphPages(pagination);
        const bool hasOrphan = allowed[0] != allowed[1];
        const bool hasWidow = allowed[2] != allowed[3] && allowed[1] == allowed[2];
        if (!hasOrphan && !hasWidow)
            continue;
        orphan |= hasOrphan;
        widow |= hasWidow;
        require(pagination.applyAppearance({{"widows_orphans", "avoid"}}), "Cannot avoid widows and orphans");
        const auto avoided = paragraphPages(pagination);
        require(avoided[0] == avoided[1] && avoided[2] == avoided[3],
                "Avoid both failed to override inline publisher important rules");
        ReaderModel preview(kobalt::test::readerFonts());
        QString error;
        require(preview.openPreview(pagination, {1072, 320}, &error), qPrintable(error));
        require(paragraphPages(preview) == avoided, "Preview lost widow and orphan avoidance");
        ReaderModel reopened(kobalt::test::readerFonts());
        open(reopened, body, {}, pagination.state(), {1072, 320});
        require(paragraphPages(reopened) == avoided, "Reopening lost widow and orphan avoidance");
        require(pagination.applyAppearance({{"widows_orphans", "publisher"}}) &&
                    paragraphPages(pagination) == allowed, "Publisher did not restore authored pagination");
        ReaderModel publisher(kobalt::test::readerFonts());
        auto protectedBody = body;
        protectedBody.replace("orphans:1!important;widows:1!important", "orphans:2!important;widows:2!important");
        open(publisher, protectedBody, {}, {}, {1072, 320});
        require(paragraphPages(publisher) == avoided, "Publisher mode ignored authored avoidance");
        require(publisher.applyAppearance({{"widows_orphans", "allow"}}) && paragraphPages(publisher) == allowed,
                "Allow both failed to override publisher avoidance");
        ReaderModel inherited(kobalt::test::readerFonts());
        auto inheritedBody = body;
        inheritedBody.replace(" style='orphans:1!important;widows:1!important'", "");
        open(inherited, inheritedBody, "body{orphans:2;widows:2}", {}, {1072, 320});
        require(paragraphPages(inherited) == avoided, "Publisher mode lost inherited avoidance");
    }
    require(orphan && widow, "Fixture did not exercise both page-boundary cases");
    QTemporaryDir cache;
    require(cache.isValid(), "Cannot create advanced text cache");
    {
        ReaderModel cached(kobalt::test::readerFonts());
        cached.setDocumentCache(cache.path(), "advanced-text", "advanced-text-test", 4096);
        open(cached, prose, {}, {{"text_alignment", "justify"}});
        require(cached.saveDocumentCache(), "Cannot save advanced text cache");
    }
    ReaderModel restored(kobalt::test::readerFonts());
    restored.setDocumentCache(cache.path(), "advanced-text", "advanced-text-test", 4096);
    open(restored, prose, {}, {{"text_alignment", "justify"}, {"justification_tracking", 15}});
    require(restored.openedFromCache() && restored.image(0) != original,
            "Document cache retained disabled tracking");
    ReaderModel cachedSpaces(kobalt::test::readerFonts());
    cachedSpaces.setDocumentCache(cache.path(), "advanced-text", "advanced-text-test", 4096);
    open(cachedSpaces, prose, {}, {{"text_alignment", "justify"}, {"word_spacing", 120}});
    ReaderModel freshSpaces(kobalt::test::readerFonts());
    open(freshSpaces, prose, {}, {{"text_alignment", "justify"}, {"word_spacing", 120}});
    require(cachedSpaces.openedFromCache() && cachedSpaces.image(0) == freshSpaces.image(0) &&
                cachedSpaces.image(0) != original, "Document cache retained Normal word spacing");
    std::puts("PASS: adaptive tracking, publisher overrides, pagination, previews and cached reopening");
}
