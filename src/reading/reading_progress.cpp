#include "support/qt_compat.h"
#include "reading_progress.h"

namespace kobalt {
bool ReadingProgress::samePosition(const ReadingProgress &other) const {
    return valid && other.valid && book == other.book && location == other.location && status == other.status;
}
bool ReadingProgress::operator==(const ReadingProgress &other) const {
    return samePosition(other) && modified == other.modified;
}
QJsonObject ReadingProgress::json() const {
    return kobalt::qt::object({{"book", book},
                               {"location", location},
                               {"modified", QString::number(modified)},
                               {"status", status},
                               {"valid", valid}});
}
ReadingProgress ReadingProgress::fromJson(const QJsonObject &value) {
    return {value.value("book").toString(), value.value("location").toString(),
            value.value("modified").toString().toLongLong(), value.value("status").toInt(),
            value.value("valid").toBool()};
}
void ProgressReconciler::observe(const ReadingProgress &incoming) {
    if (!incoming.valid || incoming.book != latest.book || incoming == latest ||
        (latest.valid && incoming.modified <= latest.modified))
        return;
    // A duplicate timestamp update, or a return to the acknowledged position,
    // does not need another decision. A pending different position still does.
    const bool duplicate = incoming.samePosition(acknowledged);
    latest = incoming;
    if (duplicate)
        acknowledged = incoming;
}
void ProgressReconciler::acknowledge(const ReadingProgress &shown) {
    if (!shown.valid || shown.book != latest.book || shown.modified > latest.modified)
        return;
    acknowledged = shown;
    if (latest.samePosition(shown))
        acknowledged = latest;
}
bool preferNativeProgress(const ReadingProgress &current, const ReadingProgress &saved) {
    return current.valid && !current.location.isEmpty() && !current.samePosition(saved) &&
           (!saved.valid || current.book != saved.book || current.modified > saved.modified);
}
}
