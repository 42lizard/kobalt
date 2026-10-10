#pragma once
#include <QString>
#include <functional>
class QMenu;

namespace kobalt {
bool previewBook(const void *volume);
enum class ReaderBookRoute { Unsupported, Direct, Protected, Failed, Remembered };
ReaderBookRoute readerBookRoute(const void *volume, QString *identity = nullptr, QString *path = nullptr);
void rememberNativeBook(const QString &identity, const QString &path, bool protectedBook);
// Use the same eligibility check for library menus and accepted openings.
bool eligibleReaderBook(const void *volume, QString *identity = nullptr, QString *path = nullptr);
void configureBookMenu(std::function<bool()> available, std::function<bool()> preferred);
bool readerChoice(const void *volume, bool preferred);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
extern void (*nativeBookMenu)(void *, void *, const void *, QMenu *, const QString &, const QString &, const void *);
#else
extern void (*nativeBookMenu)(void *, void *, const void *, QMenu *, const QString &, const QString &, bool,
                              const void *);
#endif
}
