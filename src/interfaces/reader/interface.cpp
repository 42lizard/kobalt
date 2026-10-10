#include "interfaces/reader_interface.h"
#include "interfaces/reader_window.h"
#include "interfaces/reader/views/opening.h"
#include "interfaces/reader/ui/style/fonts.h"
#include "interfaces/reader/ui/style/metrics.h"

namespace kobalt {
namespace {
QSize readingViewport(QSize screen, const QJsonObject &preferences) {
    namespace m = ui::metrics::reading;
    screen.rheight() -= (preferences.value("show_header").toBool(true)
                             ? m::stripHeight(preferences.value("header_text_size").toString())
                         : preferences.value("show_clock").toBool(true) ? m::clockHeight
                                                                        : 0) +
                        (preferences.value("show_footer").toBool(true)
                             ? m::stripHeight(preferences.value("footer_text_size").toString())
                             : 0) +
                        (preferences.value("show_progress_bar").toBool(true) ? m::progressHeight : 0);
    return screen;
}
}
const ReaderInterface &readerInterface() {
    static const ReaderInterface implementation{
        createReaderWindow,
        [](QWidget *background, const QJsonObject &preferences,
           PresentationFactory presentation) -> std::unique_ptr<ReaderOpeningView> {
            // The opening screen uses the first fonts, so choose their weight here.
            ui::setInterfaceWeight(ui::interfaceWeightSteps(preferences));
            return createOpening(background, preferences, std::move(presentation));
        },
        readingViewport, ui::setDensity};
    return implementation;
}
}
