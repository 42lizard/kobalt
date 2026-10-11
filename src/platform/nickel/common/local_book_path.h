#pragma once

#include <QString>

namespace kobalt {
// Nickel's local ContentIDs contain literal absolute paths. Do not URL-decode
// them: #, ? and percent sequences can be part of the filename. The caller
// checks the volume's format and protection metadata, then inspects the file.
inline QString localReaderBookPath(const QString &identity) {
    return identity.startsWith("file:///") ? identity.mid(7) : QString();
}
}
