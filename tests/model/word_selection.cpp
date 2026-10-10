#include "reader_inputs.h"
#include "support/qt_compat.h"
#include "engine/crengine/reader_model.h"
#include <QFile>
#include <QGuiApplication>
#include <QSet>
#include <cstdio>
#include "check.h"

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    require(argc == 2, "Pass the synthetic dictionary EPUB");
    QFile file(QString::fromLocal8Bit(argv[1]));
    require(file.open(QIODevice::ReadOnly), "Cannot open fixture");
    kobalt::ReaderModel model(kobalt::test::readerFonts());
    QString error;
    require(model.open(file.readAll(), QSize(1072, 1300), {}, &error), qPrintable(error));
    QSet<QString> found;
    for (int y = 0; y < 600; y += 10) {
        for (int x = 0; x < 1072; x += 12) {
            const auto word = model.wordAt({x, y});
            if (word.text.isEmpty())
                continue;
            found.insert(word.text);
            require(!word.start.isEmpty() && !word.end.isEmpty() && !word.rectangles.isEmpty(),
                    "Selection lacks a source range or geometry");
        }
    }
    for (const auto *text : {"Hello", "world", "Reader’s", "don't", "café", "café", "naïve", "rainbow", "cooperation",
                             "extraordinary", "A𐐀word", "日", "本", "語", "中", "文"}) {
        if (!found.contains(QString::fromUtf8(text))) {
            fprintf(stderr, "Missing %s; found: %s\n", text, qPrintable(QStringList(found.values()).join(" | ")));
            return 1;
        }
    }
    require(model.wordAt({0, 0}).text.isEmpty(), "Margin selected a word");
    require(model.wordAt({-1, 100}).text.isEmpty(), "Outside viewport selected a word");
    require(model.go(model.pages() - 1), "Cannot navigate to the chapter boundary");
    found.clear();
    for (int y = 0; y < 180; y += 10)
        for (int x = 0; x < 1072; x += 12) {
            const auto text = model.wordAt({x, y}).text;
            if (!text.isEmpty())
                found.insert(text);
        }
    require(found.contains("Chapter") && found.contains("boundary"),
            "Selection used the old page or crossed the chapter boundary");
    require(
        model.applyAppearance(kobalt::qt::object({{"font_pixels", 64}, {"margin_pixels", 24}, {"hyphenation", "on"}}),
                              QSize(360, 700)),
        "Cannot reflow selection fixture");
    bool wrapped = false;
    for (int page = 0; page < qMin(6, model.pages()) && !wrapped; ++page) {
        model.go(page);
        for (int y = 0; y < 700 && !wrapped; y += 12)
            for (int x = 24; x < 336 && !wrapped; x += 12) {
                const auto word = model.wordAt({x, y});
                wrapped = word.text == "extraordinary" && word.rectangles.size() >= 2;
            }
    }
    require(wrapped, "Wrapped discretionary hyphen did not return the complete word with two line rectangles");
    puts(
        "PASS: layout hit testing, punctuation, apostrophes, combining text, inline spans, soft hyphens and chapter navigation");
}
