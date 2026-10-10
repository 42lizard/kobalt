#pragma once

#include <QFile>
#include <QFontDatabase>
#include <QSet>
#include <QVector>

namespace kobalt::reader {

inline void registerPickerFont(const QString &path, bool copyForQt, QSet<QString> &paths, QVector<int> &ids) {
    if (paths.contains(path))
        return;
    int id = -1;
    if (copyForQt) {
        // Qt can retain a face after removal. Read it into memory so USB
        // storage can unmount while the picker is open.
        QFile file(path);
        if (file.open(QIODevice::ReadOnly) && file.size() <= 25 * 1024 * 1024)
            id = QFontDatabase::addApplicationFontFromData(file.readAll());
    } else {
        id = QFontDatabase::addApplicationFont(path);
    }
    if (id < 0)
        return;
    paths.insert(path);
    ids.append(id);
}

}
