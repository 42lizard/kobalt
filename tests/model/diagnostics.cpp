#include "support/qt_compat.h"
#include "support/diagnostics.h"
#include "support/strings.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <thread>
#include <cerrno>
#include <unistd.h>

static QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Cannot read log");
    return file.readAll();
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto code = QStringLiteral("error.native_publication_transfer_timed_out");
    if (kobalt::diagnosticErrorCode(kobalt::strings::get(code.toLatin1().constData())) != code ||
        kobalt::diagnosticErrorCode(code) != code || !kobalt::diagnosticErrorCode({}).isEmpty() ||
        kobalt::diagnosticErrorCode("Cannot read /private/book.epub") != "unknown")
        qFatal("Error codes lost or arbitrary error text disclosed");
    QTemporaryDir directory;
    if (!directory.isValid())
        qFatal("No temporary directory");
    kobalt::initializeDiagnostics(directory.path());
    const auto path = (directory.path() + "/" + "events.jsonl");
    auto writer = [] {
        for (int i = 0; i < 600; ++i) {
            errno = EDOM;
            // Linux adds memory fields to each record, other hosts do not. The
            // padding makes 1,200 records pass the 128 KiB rotation limit on both.
            kobalt::diagnostic("render", {{"sequence", i}, {"padding", QString(64, 'p')}}, false);
            if (errno != EDOM)
                qFatal("Logging changed errno");
        }
    };
    std::thread first(writer), second(writer);
    first.join();
    second.join();
    for (const auto &file : {path, path + ".previous"}) {
        const auto bytes = read(file);
        if (bytes.isEmpty() || bytes.size() > 128 * 1024 || !bytes.endsWith('\n'))
            qFatal("Unbounded or incomplete log");
        for (const auto &line : bytes.split('\n')) {
            if (line.isEmpty())
                continue;
            const auto object = QJsonDocument::fromJson(line).object();
            if (!object.contains("event") || !object.contains("monotonic_ms") || !object.contains("pid"))
                qFatal("Invalid record");
        }
    }
    const auto before = read(path);
    kobalt::pauseDiagnostics(true);
    kobalt::diagnostic("exported-storage");
    if (read(path) != before)
        qFatal("Wrote diagnostics during USB export");
    kobalt::pauseDiagnostics(false);
    kobalt::diagnostic("remounted-storage");
    if (read(path) == before)
        qFatal("Did not resume diagnostics after USB");
    const auto resumed = read(path);
    kobalt::diagnostic("oversized", {{"value", QString(5000, 'x')}});
    if (read(path) != resumed)
        qFatal("Oversized entry accepted");
    QFile::remove(path);
    const auto target = (directory.path() + "/" + "untouched");
    QFile sentinel(target);
    sentinel.open(QIODevice::WriteOnly);
    sentinel.write("sentinel");
    sentinel.close();
    if (::symlink(QFile::encodeName(target), QFile::encodeName(path)) != 0)
        qFatal("Cannot create test symlink");
    kobalt::diagnostic("must-not-follow");
    if (read(target) != "sentinel")
        qFatal("Followed log symlink");
    kobalt::initializeDiagnostics(target + "/not-a-directory");
    kobalt::diagnostic("unavailable");
    if (read(target) != "sentinel")
        qFatal("Logging failure damaged file");
    qInfo("PASS: bounded concurrent logs, complete records, errno, oversized entry, symlink and unavailable storage");
}
