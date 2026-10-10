#include "qt_compat.h"
#include "diagnostics.h"
#include "strings.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace kobalt {
namespace {
QMutex mutex;
QString logPath;
bool paused = false;
constexpr qint64 limit = 128 * 1024;
}
QString diagnosticErrorCode(const QString &reason) {
    if (reason.isEmpty())
        return {};
    int count = 0;
    const auto *entries = strings::entries(&count);
    // Never persist arbitrary error text: it may contain a private file path.
    for (int i = 0; i < count; ++i) {
        const auto &entry = entries[i];
        if (std::strncmp(entry.key, "error.", 6) == 0 &&
            (reason == QString::fromUtf8(entry.value) || reason == QLatin1String(entry.key)))
            return QString::fromLatin1(entry.key);
    }
    return QStringLiteral("unknown");
}
void pauseDiagnostics(bool value) {
    QMutexLocker lock(&mutex);
    paused = value;
}
void initializeDiagnostics(const QString &directory) {
    {
        QMutexLocker lock(&mutex);
        if (!QDir().mkpath(directory))
            return;
        logPath = directory + "/events.jsonl";
    }
    diagnostic("startup", kobalt::qt::object({{"build", __DATE__ " " __TIME__}, {"qt", qVersion()}}));
}
void diagnostic(const char *event, QJsonObject fields, bool durable) {
    const int savedErrno = errno;
    QMutexLocker lock(&mutex);
    if (paused || logPath.isEmpty())
        return;
    struct timespec now {};
    clock_gettime(CLOCK_MONOTONIC, &now);
    fields["event"] = QString::fromLatin1(event);
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    fields["utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
#else
    fields["utc"] = QDateTime::currentDateTimeUtc().toString("yyyy-MM-ddTHH:mm:ss.zzz") + 'Z';
#endif
    fields["monotonic_ms"] = double(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
    fields["pid"] = int(getpid());
    QFile memory("/proc/self/status");
    if (memory.open(QIODevice::ReadOnly)) {
        for (const auto &line : memory.readAll().split('\n')) {
            if (line.startsWith("VmRSS:") || line.startsWith("VmHWM:")) {
                const auto parts = line.simplified().split(' ');
                if (parts.size() >= 2)
                    fields[line.startsWith("VmRSS:") ? "rss_kib" : "peak_rss_kib"] = parts[1].toDouble();
            }
        }
    }
    const auto bytes = QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n';
    // Keep Nickel's crash handlers. Durable boundaries leave the last operation
    // on storage without adding a signal handler that could itself fail.
    if (bytes.size() <= 4096) {
        const auto path = QFile::encodeName(logPath);
        int fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
        struct stat info {};
        if (fd >= 0 && fstat(fd, &info) == 0 && S_ISREG(info.st_mode)) {
            if (info.st_size + bytes.size() > limit) {
                ::close(fd);
                fd = -1;
                const auto previous = path + ".previous";
                if (::rename(path.constData(), previous.constData()) == 0)
                    fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
            }
            if (fd >= 0) {
                qint64 written = 0;
                while (written < bytes.size()) {
                    const auto count = ::write(fd, bytes.constData() + written, bytes.size() - written);
                    if (count < 0 && errno == EINTR)
                        continue;
                    if (count <= 0)
                        break;
                    written += count;
                }
                if (durable)
                    ::fdatasync(fd);
            }
        }
        if (fd >= 0)
            ::close(fd);
    }
    errno = savedErrno;
}
}
