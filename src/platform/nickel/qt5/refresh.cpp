#include "platform/api/kobo_refresh.h"
#include "refresh.h"
#include "platform/nickel/display/page_animation.h"
#include "platform/nickel/display/page_animation_hook.h"
#include "platform/nickel/display/page_transition.h"
#include "platform/nickel/display/refresh_policy.h"
#include <QApplication>
#include <QBackingStore>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QVariant>
#include <QWidget>
#include <QWindow>
#include <QSettings>
#include <dlfcn.h>
#include <limits>

// Firmware 4's platform plugin turns Kobo widget attributes into a policy for
// each flushed rectangle: bit (attribute - 128). The Qt 5.2 SDK headers name
// the attributes up to 139. On 4.46.23836, 134 selects GC16 (2), 137 GL16 (3),
// 138 REAGL (4), and 140/141 the dark waveforms (8/9). Attributes 130/131
// select partial/full updates. Do not use firmware 5's separate dark-mode flag.
namespace kobalt::platform {
Flush nativeFlush = nullptr;
Flush32 nativeFlush32 = nullptr;
namespace {
constexpr quint64 bit(int attribute) {
    return quint64(1) << (attribute - 128);
}
constexpr quint64 heuristic = bit(129), partialMode = bit(130), full = bit(131), automatic = bit(132), gc16 = bit(134),
                  gl16 = bit(137), glr16 = bit(138), gck16 = bit(140), glkw16 = bit(141), darkFlag = bit(164),
                  autoImage = bit(165), autoText = bit(166);
QPointer<QWidget> readerWindow, pageWidget;
struct ImxCapabilities {
    bool regal = false, eclipse = false;
};
ImxCapabilities imxCapabilities() {
    if (readerWindow && (readerWindow->property("reader_regal_supported").isValid() ||
                         readerWindow->property("reader_eclipse_supported").isValid())) {
        return {readerWindow->property("reader_regal_supported").toBool() &&
                    !readerWindow->property("reader_regal_disabled").toBool(),
                readerWindow->property("reader_eclipse_supported").toBool()};
    }
    static const ImxCapabilities capabilities = [] {
        ImxCapabilities result;
        using Instance = QObject *(*)();
        using Supports = bool (*)(const QObject *);
        const auto instance = reinterpret_cast<Instance>(dlsym(RTLD_DEFAULT, "_ZN15HardwareFactory14sharedInstanceEv"));
        const auto hardware = instance ? instance() : nullptr;
        if (!hardware)
            return result;
        // Call exported implementations only on their verified QObject class.
        // Their bodies access native members; base-class symbols are not dispatchers.
        if (hardware->inherits("iMX508Netronix")) {
            const auto supports =
                reinterpret_cast<Supports>(dlsym(RTLD_DEFAULT, "_ZNK14iMX508Netronix21supportsRegalWaveformEv"));
            QSettings settings("/mnt/onboard/.kobo/Kobo/Kobo eReader.conf", QSettings::IniFormat);
            result.regal = supports && supports(hardware) && !settings.value("Reading/DisableRegal", false).toBool();
        }
        if (hardware->inherits("iMX6Netronix")) {
            const auto supports =
                reinterpret_cast<Supports>(dlsym(RTLD_DEFAULT, "_ZNK12iMX6Netronix23supportsEclipseWaveformEv"));
            result.eclipse = supports && supports(hardware);
        }
        return result;
    }();
    return capabilities;
}
quint64 ordinaryPolicy(bool dark, bool wide = true) {
    const auto backend = refreshBackend(readerWindow);
    if (backend == RefreshBackend::Mtk && wide)
        return autoText | full | (dark ? darkFlag : 0);
    if (backend == RefreshBackend::Imx) {
        const auto supported = imxCapabilities();
        return (dark ? (supported.eclipse ? glkw16 : automatic) : (supported.regal ? glr16 : automatic)) | partialMode;
    }
    if (backend == RefreshBackend::Mtk)
        return (dark ? glkw16 : glr16) | full;
    return automatic | heuristic;
}
quint64 repairPolicy(bool dark, bool wide = true) {
    const auto backend = refreshBackend(readerWindow);
    if (wide && backend == RefreshBackend::Mtk)
        return autoImage | full | (dark ? darkFlag : 0);
    if (backend == RefreshBackend::Imx)
        return (dark && imxCapabilities().eclipse ? gck16 : gc16) | full;
    return (dark ? gck16 : gc16) | full;
}
quint64 pagePolicy = 0;
bool previousDark = false;
bool readerOwnsScreen() {
    if (!qApp || QThread::currentThread() != qApp->thread() || !readerWindow || !readerWindow->isVisible())
        return false;
    return QApplication::activeModalWidget() == readerWindow;
}
}
void setReaderRefreshWindow(QWidget *widget) {
#ifdef KOBALT_PAGE_ANIMATIONS
    if (widget)
        installPageAnimationHook();
#endif
    cancelPageAnimation();
    readerWindow = widget;
    pageWidget.clear();
    pagePolicy = 0;
    previousDark = false;
}
template <class Submit>
static void flushReaderUsing(QBackingStore *store, const QRegion &region, QWindow *target, const QPoint &offset,
                             const FlushFlags &flags, Submit submit, bool wide = true) {
    cancelPageAnimation();
    if (qApp && QThread::currentThread() == qApp->thread() && readerWindow &&
        ((readerWindow->property("reader_preparing_handoff").toBool() &&
          store->window() != readerWindow->windowHandle()) ||
         (readerWindow->property("reader_preparing_wake").toBool() &&
          store->window() == readerWindow->windowHandle())) &&
        (!target || target == store->window()) && offset.isNull())
        return;
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        submit(region, flags);
        return;
    }
    if (readerWindow && readerWindow->property("reader_sleep_handoff").toBool() &&
        store->window() != readerWindow->windowHandle() && (!target || target == store->window()) && offset.isNull() &&
        !region.isEmpty()) {
        // Nickel's sleep view replaces the page. One full GC16 refresh shows
        // it and clears the page, so the panel flashes once.
        const QRect screen(QPoint(), store->window()->size());
        submit(QRegion(screen), {{screen, gc16 | full}});
        return;
    }
    if (!readerOwnsScreen() || store->window() != readerWindow->windowHandle() ||
        (target && target != store->window()) || !offset.isNull() || region.isEmpty() ||
        !readerWindow->rect().contains(region.boundingRect())) {
        if (readerWindow)
            readerWindow->setProperty("reader_animation_direction", 0);
        submit(region, flags);
        return;
    }
    const int direction = readerWindow->property("reader_animation_direction").toInt();
    readerWindow->setProperty("reader_animation_direction", 0);
    const bool dark = readerWindow->property("reader_dark").toBool();
    const bool pageTurn = readerWindow->property("reader_page_turn").toBool();
    readerWindow->setProperty("reader_page_turn", false);
    // The reader asks for one full flash to clear ghosting: after the menu
    // closes, and at some chapter starts. It replaces that turn's animation.
    const bool flash = readerWindow->property("reader_full_flash").toBool();
    readerWindow->setProperty("reader_full_flash", false);
    if (flash || (dark && !previousDark)) {
        // Clear the entire panel, including UI remnants. Entering dark mode
        // always does this.
        previousDark = dark;
        const QRect screen = readerWindow->rect();
        submit(QRegion(screen), {{screen, repairPolicy(dark, wide)}});
        return;
    }
    previousDark = dark;
    // Firmware 4 does not take the page widget's attributes into the rectangle
    // policy: a page turn arrives with the controls' automatic policy. Give
    // the page's own rectangles the policy pageRefresh chose.
    FlushFlags policies = flags;
    if (pageWidget && pageWidget->isVisible() && pagePolicy) {
        for (auto &entry : policies) {
            // Controls can lie over the page. Only a rectangle whose centre
            // shows the page widget itself is a page update.
            const auto *shown = readerWindow->childAt(entry.first.center());
            if (shown && (shown == pageWidget || pageWidget->isAncestorOf(shown)))
                entry.second = pagePolicy;
        }
    }
    if (!wide)
        for (auto &entry : policies) {
            if (entry.second & autoImage)
                entry.second = repairPolicy(dark, false);
            else if (entry.second & autoText)
                entry.second = ordinaryPolicy(dark, false);
        }
    // Firmware 5's controls ask for Auto Text, which its platform resolves to
    // the REAGL text waveforms. Firmware 4's automatic waveform leaves the
    // choice to the driver instead, so name the same text waveform here.
    for (auto &entry : policies)
        if (entry.second & automatic) {
            const auto mode = entry.second & (heuristic | full | partialMode);
            entry.second = ordinaryPolicy(dark, wide);
            if (mode == heuristic && refreshBackend(readerWindow) != RefreshBackend::Imx)
                entry.second = (entry.second & ~(heuristic | full | partialMode)) | heuristic;
        }
    // Firmware 4's Qt lists a rectangle more than once. Keep one entry each,
    // after the changes above have made equal entries equal.
    {
        FlushFlags unique;
        for (const auto &entry : policies)
            if (!unique.contains(entry))
                unique.append(entry);
        policies = unique;
    }
    bool compatible = !policies.isEmpty();
    for (const auto &entry : policies) {
        // Kobalt's controls use the automatic waveform, as firmware 5's use Auto Text.
        const auto waveform = entry.second & (automatic | autoText | glr16 | glkw16 | gl16 | gck16);
        const auto mode = entry.second & (heuristic | full | partialMode);
        // A dark cleanup must keep GCK16/full when Qt splits its damage.
        if (dark && waveform == gck16)
            compatible = false;
        if ((entry.second &
             ~(automatic | glr16 | glkw16 | gl16 | gck16 | autoText | darkFlag | heuristic | full | partialMode)) ||
            (waveform != automatic && waveform != autoText && waveform != glr16 && waveform != glkw16 &&
             waveform != gl16 && waveform != gck16) ||
            (mode != heuristic && mode != full && mode != partialMode))
            compatible = false;
    }
    auto completedFlush = [&](const QRegion &damage, const FlushFlags &chosen) {
        cancelPageAnimation();
        if (wide && direction && compatible && chosen.size() == 1 && chosen[0].second == ordinaryPolicy(dark, wide)) {
            if (submitPageTransition(store, readerWindow, direction, dark, ordinaryPolicy(dark, wide),
                                     repairPolicy(dark, wide), [&](const QRegion &parts, quint64 policy) {
                                         FlushFlags selected;
                                         // Submit now so the animation hook runs before the sweep is cancelled.
                                         for (const auto &part : parts.rects())
                                             selected.append({part, policy | bit(128)});
                                         submit(parts, selected);
                                     }))
                return;
        }
        if (wide && pageTurn && compatible && refreshBackend(readerWindow) == RefreshBackend::Mtk) {
            const bool colour = pageAnimationPanel() == AnimationPanel::Colour;
            const QRegion images =
                readerWindow->property(colour ? "reader_image_repairs" : "reader_mono_repairs").value<QRegion>() &
                readerWindow->rect();
            if (!images.isEmpty() || (colour && readerWindow->property("reader_colour_regions").toBool())) {
                const QRect screen = readerWindow->rect();
                submit(QRegion(screen), {{screen, repairPolicy(dark, wide) | bit(128)}});
                return;
            }
        }
        const bool plainControls = !dark && readerWindow->property("reader_plain_controls").toBool();
        if ((pageTurn || plainControls) && !direction && compatible &&
            refreshBackend(readerWindow) == RefreshBackend::Mtk &&
            chosen.size() == 1 && chosen[0].second == ordinaryPolicy(dark, wide)) {
            // Keep one submission while the driver compares pixels within the rectangle.
            const quint64 policy = (chosen[0].second & ~(heuristic | full)) | partialMode;
            submit(damage, {{chosen[0].first, policy}});
            return;
        }
        if (pageTurn && !direction && compatible && refreshBackend(readerWindow) != RefreshBackend::Mtk &&
            chosen.size() == 1 && chosen[0].second == ordinaryPolicy(dark, wide)) {
            // Older and unknown backends keep their reading policy over the whole screen.
            const QRect screen = readerWindow->rect();
            submit(QRegion(screen), {{screen, chosen[0].second}});
            return;
        }
        submit(damage, chosen);
    };
    if (region.rectCount() > 1 && compatible) {
        // Qt has finished painting every rectangle into this backing store.
        // Merge here, before MTK submits each rectangle separately.
        const QRect bounds = region.boundingRect();
        const auto policy = ordinaryPolicy(dark, wide);
        completedFlush(QRegion(bounds), {{bounds, policy}});
        return;
    }
    completedFlush(region, policies);
}
void flushReader(QBackingStore *store, const QRegion &region, QWindow *target, const QPoint &offset,
                 const FlushFlags &flags) {
    flushReaderUsing(store, region, target, offset, flags, [&](const QRegion &damage, const FlushFlags &chosen) {
        nativeFlush(store, damage, target, offset, chosen);
    });
}
void flushReader32(QBackingStore *store, const QRegion &region, QWindow *target, const QPoint &offset,
                   const FlushFlags32 &flags) {
    FlushFlags widened;
    for (const auto &entry : flags)
        widened.append({entry.first, quint64(entry.second)});
    flushReaderUsing(
        store, region, target, offset, widened,
        [&](const QRegion &damage, const FlushFlags &chosen) {
            FlushFlags32 narrowed;
            for (const auto &entry : chosen) {
                // A future policy may use bits that this firmware cannot carry.
                // Keep its original request rather than truncate that policy.
                if (entry.second > std::numeric_limits<quint32>::max()) {
                    nativeFlush32(store, region, target, offset, flags);
                    return;
                }
                narrowed.append({entry.first, quint32(entry.second)});
            }
            nativeFlush32(store, damage, target, offset, narrowed);
        },
        false);
}
void textRefresh(QWidget *widget) {
    if (qobject_cast<QPushButton *>(widget))
        widget->setFocusPolicy(Qt::NoFocus);
    for (int attribute = 129; attribute <= 141; ++attribute)
        widget->setAttribute(static_cast<Qt::WidgetAttribute>(attribute), false);
    widget->setAttribute(Qt::WA_KoboEpdWfModeAutomatic);
    widget->setAttribute(Qt::WA_KoboEpdUpdateModeHeuristic);
}
void pageRefresh(QWidget *widget, bool clean, bool) {
    const bool dark = widget->window()->property("reader_dark").toBool();
    // Keep the full native policy even when the widget exposes only low flags.
    pageWidget = widget;
    pagePolicy = clean ? repairPolicy(dark) : ordinaryPolicy(dark);
    for (int attribute = 129; attribute <= 141; ++attribute)
        widget->setAttribute(static_cast<Qt::WidgetAttribute>(attribute),
                             bool((clean ? (dark ? gck16 : gc16) | full : pagePolicy) & bit(attribute)));
}
}

extern "C" __attribute__((visibility("default"))) void kobalt_reader_flush(QBackingStore *store, const QRegion &region,
                                                                           QWindow *target, const QPoint &offset,
                                                                           const kobalt::platform::FlushFlags &flags) {
    kobalt::platform::flushReader(store, region, target, offset, flags);
}
extern "C" __attribute__((visibility("default"))) void
kobalt_reader_flush32(QBackingStore *store, const QRegion &region, QWindow *target, const QPoint &offset,
                      const kobalt::platform::FlushFlags32 &flags) {
    kobalt::platform::flushReader32(store, region, target, offset, flags);
}
