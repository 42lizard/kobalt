#include "platform/api/kobo_refresh.h"
#include "refresh.h"
#include "platform/nickel/display/page_animation.h"
#include "platform/nickel/display/page_animation_hook.h"
#include "platform/nickel/display/page_transition.h"
#include "platform/nickel/display/refresh_policy.h"
#include <QPushButton>
#include <QVariant>
#include <QPointer>
#include <QApplication>
#include <QThread>
#include <QBackingStore>
#include <QWindow>

namespace kobalt::platform {
LookupRefreshFlags nativeRefreshFlags = nullptr;
Flush nativeFlush = nullptr;
namespace {
QPointer<QWidget> readerWindow;
bool previousDark = false;
constexpr quint64 bit(Qt::WidgetAttribute attribute) {
    return quint64(1) << (attribute - Qt::WA_KoboEpdFlagsStart);
}
quint64 ordinaryPolicy(bool dark) {
    const auto mode = refreshBackend(readerWindow) == RefreshBackend::Imx   ? Qt::WA_KoboEpdUpdateModePartial
                      : refreshBackend(readerWindow) == RefreshBackend::Mtk ? Qt::WA_KoboEpdUpdateModeFull
                                                                            : Qt::WA_KoboEpdUpdateModeHeuristic;
    return bit(Qt::WA_KoboEpdWfModeAutoText) | bit(mode) | (dark ? bit(Qt::WA_KoboEpdFlagDarkMode) : 0);
}
quint64 repairPolicy(bool dark) {
    return bit(Qt::WA_KoboEpdWfModeAutoImage) | bit(Qt::WA_KoboEpdUpdateModeFull) |
           (dark ? bit(Qt::WA_KoboEpdFlagDarkMode) : 0);
}
bool readerOwnsScreen() {
    if (!qApp || QThread::currentThread() != qApp->thread() || !readerWindow || !readerWindow->isVisible())
        return false;
    auto *active = QApplication::activeModalWidget();
    return active == readerWindow;
}
}
void setReaderRefreshWindow(QWidget *widget) {
#ifdef KOBALT_PAGE_ANIMATIONS
    if (widget)
        installPageAnimationHook();
#endif
    cancelPageAnimation();
    readerWindow = widget;
    previousDark = false;
}
void flushReader(QBackingStore *store, const QRegion &region, QWindow *target, const QPoint &offset,
                 const FlushFlags &flags) {
    cancelPageAnimation();
    if (qApp && QThread::currentThread() == qApp->thread() && readerWindow &&
        ((readerWindow->property("reader_preparing_handoff").toBool() &&
          store->window() != readerWindow->windowHandle()) ||
         (readerWindow->property("reader_preparing_wake").toBool() &&
          store->window() == readerWindow->windowHandle())) &&
        (!target || target == store->window()) && offset.isNull())
        return;
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        nativeFlush(store, region, target, offset, flags);
        return;
    }
    if (!readerOwnsScreen() || store->window() != readerWindow->windowHandle() ||
        (target && target != store->window()) || !offset.isNull() || region.isEmpty() ||
        !readerWindow->rect().contains(region.boundingRect())) {
        if (readerWindow)
            readerWindow->setProperty("reader_animation_direction", 0);
        nativeFlush(store, region, target, offset, flags);
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
        const auto clean = repairPolicy(dark);
        nativeFlush(store, QRegion(screen), target, offset, {{screen, clean}});
        return;
    }
    previousDark = dark;
    constexpr auto autoText = bit(Qt::WA_KoboEpdWfModeAutoText), gl16 = bit(Qt::WA_KoboEpdWfModeGL16),
                   gck16 = bit(Qt::WA_KoboEpdWfModeGCK16), darkFlag = bit(Qt::WA_KoboEpdFlagDarkMode);
    constexpr auto heuristic = bit(Qt::WA_KoboEpdUpdateModeHeuristic), full = bit(Qt::WA_KoboEpdUpdateModeFull),
                   partialMode = bit(Qt::WA_KoboEpdUpdateModePartial);
    bool compatible = !flags.isEmpty();
    for (const auto &entry : flags) {
        const auto waveform = entry.second & (autoText | gl16 | gck16);
        const auto mode = entry.second & (heuristic | full | partialMode);
        // A dark cleanup must keep GCK16/full when Qt splits its damage.
        if (dark && waveform == gck16)
            compatible = false;
        if ((entry.second & ~(autoText | gl16 | gck16 | heuristic | full | partialMode | darkFlag)) ||
            (waveform != autoText && waveform != gl16 && waveform != gck16) ||
            (mode != heuristic && mode != full && mode != partialMode) || bool(entry.second & darkFlag) != dark)
            compatible = false;
    }
    auto completedFlush = [&](const QRegion &damage, const FlushFlags &policies) {
        cancelPageAnimation();
        if (direction && compatible && policies.size() == 1 && policies[0].second == ordinaryPolicy(dark)) {
            if (submitPageTransition(store, readerWindow, direction, dark, ordinaryPolicy(dark), repairPolicy(dark),
                                     [&](const QRegion &parts, quint64 policy) {
                                         FlushFlags selected;
                                         // Submit now so the animation hook runs before the sweep is cancelled.
                                         for (const auto &part : parts)
                                             selected.append({part, policy | bit(Qt::WA_KoboEpdScreenFlush)});
                                         nativeFlush(store, parts, target, offset, selected);
                                     }))
                return;
        }
        if (pageTurn && compatible && refreshBackend(readerWindow) == RefreshBackend::Mtk) {
            const bool colour = pageAnimationPanel() == AnimationPanel::Colour;
            const QRegion images =
                readerWindow->property(colour ? "reader_image_repairs" : "reader_mono_repairs").value<QRegion>() &
                readerWindow->rect();
            if (!images.isEmpty() || (colour && readerWindow->property("reader_colour_regions").toBool())) {
                const QRect screen = readerWindow->rect();
                nativeFlush(store, QRegion(screen), target, offset,
                            {{screen, repairPolicy(dark) | bit(Qt::WA_KoboEpdScreenFlush)}});
                return;
            }
        }
        const bool plainControls = !dark && readerWindow->property("reader_plain_controls").toBool();
        if ((pageTurn || plainControls) && !direction && compatible &&
            refreshBackend(readerWindow) == RefreshBackend::Mtk &&
            policies.size() == 1 && policies[0].second == ordinaryPolicy(dark)) {
            // Keep one submission while the driver compares pixels within the rectangle.
            const quint64 policy = (policies[0].second & ~(heuristic | full)) | partialMode;
            nativeFlush(store, damage, target, offset, {{policies[0].first, policy}});
            return;
        }
        if (pageTurn && !direction && compatible && refreshBackend(readerWindow) != RefreshBackend::Mtk &&
            policies.size() == 1 && policies[0].second == ordinaryPolicy(dark)) {
            // Older and unknown backends keep their reading policy over the whole screen.
            const QRect screen = readerWindow->rect();
            nativeFlush(store, QRegion(screen), target, offset, {{screen, policies[0].second}});
            return;
        }
        nativeFlush(store, damage, target, offset, policies);
    };
    if (region.rectCount() > 1 && compatible) {
        // Qt has finished painting every rectangle into this backing store.
        // Merge here, before MTK submits each rectangle separately. No timer,
        // retained backing-store pointer or later frame is needed.
        const QRect bounds = region.boundingRect();
        const auto policy = ordinaryPolicy(dark);
        completedFlush(QRegion(bounds), {{bounds, policy}});
        return;
    }
    completedFlush(region, flags);
}
void lookupReaderRefreshFlags(void *screen, quint64 &flags, const QRect &rect) {
    nativeRefreshFlags(screen, flags, rect);
    // MTK composes sleep during QWindow::hide(), before QWidget's visibility
    // changes. It can reuse the removed reader's dirty flags. Replace those
    // flags only after the native window has left the compositor's stack.
    if (qApp && QThread::currentThread() == qApp->thread() && readerWindow && readerWindow->windowHandle() &&
        !readerWindow->windowHandle()->isVisible() &&
        (readerWindow->property("reader_sleep_clean_pending").toBool() ||
         readerWindow->property("reader_exit_clean_pending").toBool()) &&
        rect == readerWindow->rect()) {
        readerWindow->setProperty("reader_sleep_clean_pending", false);
        readerWindow->setProperty("reader_exit_clean_pending", false);
        flags = bit(Qt::WA_KoboEpdWfModeGC16) | bit(Qt::WA_KoboEpdUpdateModeFull);
        return;
    }
    constexpr quint64 collide = quint64(1) << (Qt::WA_KoboEpdFlagCanCollide - Qt::WA_KoboEpdFlagsStart);
    if (flags != 0 && flags != collide)
        return;
    if (!qApp || QThread::currentThread() != qApp->thread() || !readerWindow || !readerWindow->isVisible())
        return;
    auto *active = QApplication::activeModalWidget();
    while (active && active != readerWindow)
        active = active->parentWidget();
    if (!active)
        return;
    // Merged adjacent rectangles can miss Kobo's per-widget flag table. Only
    // repair that empty/default result while our modal reader owns the screen.
    // Explicit clean refreshes and every native window keep their own flags.
    flags |= (quint64(1) << (Qt::WA_KoboEpdWfModeAutoText - Qt::WA_KoboEpdFlagsStart)) |
             (quint64(1) << (Qt::WA_KoboEpdUpdateModeHeuristic - Qt::WA_KoboEpdFlagsStart));
    if (readerWindow->property("reader_dark").toBool())
        flags |= quint64(1) << (Qt::WA_KoboEpdFlagDarkMode - Qt::WA_KoboEpdFlagsStart);
}
void textRefresh(QWidget *widget) {
    if (qobject_cast<QPushButton *>(widget))
        widget->setFocusPolicy(Qt::NoFocus);
    for (int attribute = Qt::WA_KoboEpdUpdateModeHeuristic; attribute <= Qt::WA_KoboEpdWfModeGLK16; ++attribute)
        widget->setAttribute(static_cast<Qt::WidgetAttribute>(attribute), false);
    widget->setAttribute(Qt::WA_KoboEpdWfModeAutoImage, false);
    widget->setAttribute(Qt::WA_KoboEpdWfModeAutoText);
    widget->setAttribute(Qt::WA_KoboEpdUpdateModeHeuristic);
    widget->setAttribute(Qt::WA_KoboEpdFlagDarkMode, widget->window()->property("reader_dark").toBool());
}

void pageRefresh(QWidget *widget, bool clean, bool) {
    const bool dark = widget->window()->property("reader_dark").toBool();
    const auto chosen = clean ? repairPolicy(dark) : ordinaryPolicy(dark);
    for (int attribute = Qt::WA_KoboEpdUpdateModeHeuristic; attribute <= Qt::WA_KoboEpdWfModeGLK16; ++attribute)
        widget->setAttribute(static_cast<Qt::WidgetAttribute>(attribute), false);
    widget->setAttribute(Qt::WA_KoboEpdWfModeAutoImage, clean);
    widget->setAttribute(Qt::WA_KoboEpdWfModeAutoText, !clean);
    for (const auto mode :
         {Qt::WA_KoboEpdUpdateModeHeuristic, Qt::WA_KoboEpdUpdateModePartial, Qt::WA_KoboEpdUpdateModeFull})
        widget->setAttribute(mode, bool(chosen & bit(mode)));
    widget->setAttribute(Qt::WA_KoboEpdFlagDarkMode, dark);
}
}

extern "C" __attribute__((visibility("default"))) void kobalt_refresh_flags(void *screen, quint64 &flags,
                                                                            const QRect &rect) {
    kobalt::platform::lookupReaderRefreshFlags(screen, flags, rect);
}

extern "C" __attribute__((visibility("default"))) void kobalt_reader_flush(QBackingStore *store, const QRegion &region,
                                                                           QWindow *target, const QPoint &offset,
                                                                           const kobalt::platform::FlushFlags &flags) {
    kobalt::platform::flushReader(store, region, target, offset, flags);
}
