#include "book_fixtures.h"
#include "dictionary_fixtures.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>
#include <QtEndian>
#include <zlib.h>
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        qFatal("%s", message);
}
void be32(QByteArray &b, quint32 v) {
    v = qToBigEndian(v);
    b.append(reinterpret_cast<char *>(&v), 4);
}

}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2)
        return 1;
    const QString directory = argv[1];
    auto files = kobalt::test::lookupBook();
    require(kobalt::test::writeBook(directory, "lookup.epub", files), "write lookup EPUB");
    QSet<QString> words;
    const QRegularExpression word("[\\p{L}]+(?:['’][\\p{L}]+)*");
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        if (!it.key().endsWith("xhtml"))
            continue;
        auto text = QString::fromUtf8(it.value()).remove(QRegularExpression("<[^>]*>"));
        auto matches = word.globalMatch(text);
        while (matches.hasNext())
            words.insert(matches.next().captured().toCaseFolded());
    }
    auto sorted = words.values();
    std::sort(sorted.begin(), sorted.end());
    QMap<QString, QByteArray> dictionary;
    dictionary["mimetype"] = "application/zip";
    for (const auto &id : {QString("en-en"), QString("en-fr")}) {
        QByteArray body, index;
        for (const auto &word : sorted) {
            const auto definition =
                QByteArray("<p>A synthetic PocketBook definition.</p>").repeated(id == "en-en" ? 100 : 1);
            index += word.toUtf8();
            index += '\0';
            be32(index, body.size());
            be32(index, definition.size());
            body += definition;
        }
        dictionary[id + "/" + id + ".ifo"] =
            "StarDict's dict ifo file\nversion=2.4.2\nwordcount=" + QByteArray::number(sorted.size()) +
            "\nidxfilesize=" + QByteArray::number(index.size()) + "\nsametypesequence=h\n";
        dictionary[id + "/" + id + ".idx"] = index;
        dictionary[id + "/" + id + ".dict.dz"] = kobalt::test::dictzip(body, 32768);
        dictionary[id + "/LICENSE"] = "Synthetic test data, CC0.\n";
        dictionary[id + "/ATTRIBUTION"] = "Kobalt synthetic dictionary fixture.\n";
    }
    require(kobalt::test::writeBook(directory, "dictionary-fixture.zip", dictionary), "write dictionary archive");
    return 0;
}
