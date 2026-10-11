#include "platform/api/kobo_refresh.h"
#include "platform/nickel/display/page_animation.h"
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include "platform/nickel/qt5/refresh.h"
#else
#include "platform/nickel/qt6/refresh.h"
#endif
#include <QApplication>
#include <QBackingStore>
#include <QDialog>
#include <QVariant>
#include <QWindow>
#include <cstdio>
#include <cstdlib>

namespace {
using namespace kobalt::platform;
int calls = 0, animationArms = 0;
bool animationAvailable = false;
QRegion submittedRegion;
FlushFlags submittedFlags;
void require(bool condition, const char *message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
void capture(QBackingStore *, const QRegion &region, QWindow *, const QPoint &, const FlushFlags &flags) {
    ++calls;
    submittedRegion = region;
    submittedFlags = flags;
}
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
constexpr int start = 128, partialAttribute = 130, fullAttribute = 131, gc16Attribute = 134, gl16Attribute = 137,
              gck16Attribute = 140, ordinaryAttribute = 166, lightOrdinaryAttribute = 166, imageAttribute = 165;
constexpr quint64 darkFlag = quint64(1) << (164 - start);
#else
constexpr int start = Qt::WA_KoboEpdFlagsStart, partialAttribute = Qt::WA_KoboEpdUpdateModePartial,
              fullAttribute = Qt::WA_KoboEpdUpdateModeFull, gc16Attribute = Qt::WA_KoboEpdWfModeGC16,
              gl16Attribute = Qt::WA_KoboEpdWfModeGL16, gck16Attribute = Qt::WA_KoboEpdWfModeGCK16,
              imageAttribute = Qt::WA_KoboEpdWfModeAutoImage, ordinaryAttribute = Qt::WA_KoboEpdWfModeAutoText,
              lightOrdinaryAttribute = Qt::WA_KoboEpdWfModeAutoText;
constexpr quint64 darkFlag = quint64(1) << (Qt::WA_KoboEpdFlagDarkMode - start);
#endif
constexpr quint64 bit(int attribute) {
    return quint64(1) << (attribute - start);
}
bool attribute(QWidget &widget, int value) {
    return widget.testAttribute(static_cast<Qt::WidgetAttribute>(value));
}
void expectPolicy(quint64 policy, const char *message) {
    require(submittedFlags.size() == 1 && submittedFlags[0].second == policy, message);
}
}

// Count the platform's request without requiring a framebuffer or an ioctl hook.
namespace kobalt {
void armPageAnimation(int, const QRect &, bool, bool) {
    ++animationArms;
}
void cancelPageAnimation() {}
bool pageAnimationsAvailable() {
    return animationAvailable;
}
AnimationPanel pageAnimationPanel() {
    return animationAvailable ? AnimationPanel::Mono : AnimationPanel::None;
}
bool pageAnimationSucceeded() {
    return animationAvailable;
}
PageAnimationResult pageAnimationResult() {
    return PageAnimationResult::None;
}
void beginPageRepair() {}
bool endPageRepair() {
    return false;
}
void installPageAnimationHook() {}
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    using namespace kobalt::platform;
    nativeFlush = capture;
    QDialog reader;
    reader.resize(400, 600);
    reader.setWindowModality(Qt::ApplicationModal);
    QWidget page(&reader);
    page.setGeometry(reader.rect());
    page.show();
    reader.show();
    app.processEvents();
    require(QApplication::activeModalWidget() == &reader, "Reader did not become the active modal window");
    reader.setProperty("reader_refresh_backend", "hwtcon");
    setReaderRefreshWindow(&reader);
    QBackingStore store(reader.windowHandle());
    const QRect first(20, 20, 80, 80), second(160, 20, 80, 80);
    const QRegion split = QRegion(first) | QRegion(second);
    auto flush = [&](const QRegion &damage, const FlushFlags &flags) {
        const int before = calls;
        flushReader(&store, damage, nullptr, {}, flags);
        require(calls == before + 1, "Flush did not submit exactly once");
    };
    const quint64 darkOrdinary = bit(ordinaryAttribute) | bit(fullAttribute) | darkFlag;
    const quint64 darkClean = bit(imageAttribute) | bit(fullAttribute) | darkFlag;
    reader.setProperty("reader_dark", true);
    reader.setProperty("reader_plain_controls", true);
    pageRefresh(&page, false, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    require(attribute(page, ordinaryAttribute) && attribute(page, fullAttribute) &&
                !attribute(page, partialAttribute) && !attribute(page, gck16Attribute) &&
                !attribute(page, gc16Attribute),
            "Ordinary dark page has the wrong attributes");
    require(attribute(page, Qt::WA_KoboEpdFlagDarkMode), "Dark page is missing its dark flag");
#else
    require(!attribute(page, partialAttribute) && attribute(page, fullAttribute) && !attribute(page, gck16Attribute),
            "Qt5 ordinary page lost its full update mode");
#endif
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkClean, "Entering dark mode did not use image/full");
    require(submittedRegion == QRegion(reader.rect()), "Entering dark mode did not clean the entire screen");
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkOrdinary, "Ordinary dark turn changed its full policy");
    flush(split, {{first, darkOrdinary}, {second, darkOrdinary}});
    expectPolicy(darkOrdinary, "Split ordinary dark turn lost its full policy");
    require(submittedRegion == QRegion(split.boundingRect()), "Ordinary dark damage was not merged");
    reader.setProperty("reader_page_turn", true);
    flush(split, {{first, darkOrdinary}, {second, darkOrdinary}});
    expectPolicy(bit(ordinaryAttribute) | bit(partialAttribute) | darkFlag,
                 "Ordinary dark page turn did not use partial mode");
    pageRefresh(&page, true, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    require(attribute(page, imageAttribute) && attribute(page, fullAttribute) && !attribute(page, ordinaryAttribute) &&
                !attribute(page, gc16Attribute) && !attribute(page, partialAttribute),
            "Dark cleanup has the wrong attributes");
#else
    require(attribute(page, gck16Attribute) && attribute(page, fullAttribute) && !attribute(page, partialAttribute),
            "Qt5 cleanup lost its widget flags");
#endif
    flush(split, {{first, darkClean}, {second, darkClean}});
    require(submittedRegion == split && submittedFlags.size() == 2,
            "Split dark cleanup was replaced with an ordinary refresh");
    for (const auto &entry : submittedFlags)
        require(entry.second == darkClean, "Split dark cleanup lost image/full");
    reader.setProperty("reader_full_flash", true);
    flush(first, {{first, darkOrdinary}});
    expectPolicy(darkClean, "Requested dark cleanup did not use image/full");
    require(!reader.property("reader_full_flash").toBool(), "Cleanup request was not consumed");
    pageRefresh(&page, false, true);
    reader.setProperty("reader_animation_direction", 1);
    flush(first, {{first, bit(gck16Attribute) | bit(partialAttribute) | darkFlag}});
    require(animationArms == 0, "A stray dark direction armed an animation");
    require(reader.property("reader_animation_direction").toInt() == 0, "Animation direction was not consumed");
    reader.setProperty("reader_dark", false);
    pageRefresh(&page, false, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    require(attribute(page, lightOrdinaryAttribute) && attribute(page, fullAttribute) &&
                !attribute(page, partialAttribute) && !attribute(page, gck16Attribute) &&
                !attribute(page, gl16Attribute),
            "Light page did not select native text/full");
    require(!attribute(page, Qt::WA_KoboEpdFlagDarkMode), "Light page retained the dark flag");
#else
    require(!attribute(page, partialAttribute) && attribute(page, fullAttribute),
            "Qt5 ordinary light page lost its full update mode");
#endif
    const quint64 lightText = bit(lightOrdinaryAttribute) | bit(fullAttribute);
    flush(first, {{first, lightText}});
    expectPolicy(bit(lightOrdinaryAttribute) | bit(partialAttribute), "Light reading bars did not use partial mode");
    flush(split, {{first, lightText}, {second, lightText}});
    expectPolicy(bit(lightOrdinaryAttribute) | bit(partialAttribute),
                 "Split reading bars did not use one partial policy");
    reader.setProperty("reader_page_turn", true);
    reader.setProperty("reader_mono_repairs", QVariant::fromValue(QRegion(first)));
    flush(split, {{first, lightText}, {second, lightText}});
    require(submittedRegion == QRegion(reader.rect()), "Image-quality turn did not clean the entire screen");
    expectPolicy(bit(imageAttribute) | bit(fullAttribute) | bit(start),
                 "Image-quality turn lost native cleanup policy");
    reader.setProperty("reader_page_turn", true);
    reader.setProperty("reader_animation_direction", 1);
    animationAvailable = true;
    flush(second, {{second, lightText}});
    require(submittedRegion == QRegion(reader.rect()),
            "Outgoing image outside damage did not trigger whole-screen cleanup");
    expectPolicy(bit(imageAttribute) | bit(fullAttribute) | bit(start),
                 "Animated image turn did not select one cleanup");
    require(animationArms == 0, "Image-quality turn armed a sweep");
    animationAvailable = false;
    reader.setProperty("reader_mono_repairs", QVariant::fromValue(QRegion()));
    reader.setProperty("reader_animation_direction", 1);
    flush(first, {{first, lightText}});
    require(animationArms == 0, "Unpainted backing store armed an animation");
    const FlushFlags nativeFlags{{first, bit(gc16Attribute) | bit(fullAttribute)}};
    QWindow otherWindow;
    QBackingStore otherStore(&otherWindow);
    flushReader(&otherStore, first, nullptr, {}, nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "Another backing store was changed");
    flushReader(&store, first, &otherWindow, {}, nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "Another target window was changed");
    flushReader(&store, first, nullptr, QPoint(1, 0), nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "An offset flush was changed");
    reader.setProperty("reader_preparing_wake", true);
    const int beforeWake = calls;
    flushReader(&store, first, nullptr, {}, nativeFlags);
    require(calls == beforeWake, "Preparing wake submitted an intermediate reader frame");
    reader.setProperty("reader_preparing_wake", false);
    reader.setProperty("reader_preparing_handoff", true);
    const int beforeHandoff = calls;
    flushReader(&otherStore, first, nullptr, {}, nativeFlags);
    require(calls == beforeHandoff, "Preparing handoff submitted the background too early");
    flushReader(&otherStore, first, nullptr, QPoint(1, 0), nativeFlags);
    require(calls == beforeHandoff + 1, "Handoff preparation swallowed an unrelated offset flush");
    reader.setProperty("reader_preparing_handoff", false);
    QDialog modal(&reader);
    modal.setWindowModality(Qt::ApplicationModal);
    modal.show();
    app.processEvents();
    require(QApplication::activeModalWidget() == &modal, "Other modal did not become active");
    flush(first, nativeFlags);
    require(submittedRegion == QRegion(first) && submittedFlags == nativeFlags, "Another modal's flush was changed");
    modal.hide();
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    nativeRefreshFlags = +[](void *, quint64 &flags, const QRect &) {
        flags = 0x1234;
    };
    reader.setProperty("reader_exit_clean_pending", true);
    quint64 lookup = 0;
    lookupReaderRefreshFlags(nullptr, lookup, reader.rect());
    require(lookup == 0x1234 && reader.property("reader_exit_clean_pending").toBool(),
            "Visible reader consumed native exit cleanup");
    reader.hide();
    lookupReaderRefreshFlags(nullptr, lookup, first);
    require(lookup == 0x1234 && reader.property("reader_exit_clean_pending").toBool(),
            "Partial native damage consumed exit cleanup");
    lookupReaderRefreshFlags(nullptr, lookup, reader.rect());
    require(lookup == (bit(gc16Attribute) | bit(fullAttribute)) &&
                !reader.property("reader_exit_clean_pending").toBool(),
            "Hidden exit did not consume one native full cleanup");
    lookupReaderRefreshFlags(nullptr, lookup, reader.rect());
    require(lookup == 0x1234, "Exit cleanup changed a later native request");
#else
    reader.hide();
#endif
    reader.show();
    app.processEvents();
    setReaderRefreshWindow(&reader);
    reader.setProperty("reader_dark", false);
    reader.setProperty("reader_full_flash", false);
    reader.setProperty("reader_animation_direction", 1);
    store.resize(reader.size());
    store.beginPaint(QRegion(reader.rect()));
    store.endPaint();
    animationAvailable = true;
    const quint64 ordinary = bit(lightOrdinaryAttribute) | bit(fullAttribute);
    flush(QRegion(reader.rect()), {{reader.rect(), ordinary}});
    require(submittedRegion == QRegion(reader.rect()), "Sweep did not publish the full backing store");
    expectPolicy(ordinary | bit(start), "Animation phase omitted the synchronous screen flush flag");
    require(animationArms == 1, "Animation phase was not armed once");
    animationAvailable = false;
    reader.setProperty("reader_plain_controls", false);
    for (const char *backend : {"mxc_epdc_fb", "unknown"}) {
        reader.setProperty("reader_refresh_backend", backend);
        setReaderRefreshWindow(&reader);
        pageRefresh(&page, false, true);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        const quint64 input = bit(132) | bit(129);
#else
        const quint64 input = bit(Qt::WA_KoboEpdWfModeAutoText) | bit(Qt::WA_KoboEpdUpdateModeHeuristic);
#endif
        flush(split, {{first, input}, {second, input}});
        require(submittedFlags.size() == 1, "Fallback control update did not merge");
        const quint64 policy = submittedFlags[0].second;
        reader.setProperty("reader_page_turn", true);
        flush(split, {{first, input}, {second, input}});
        require(submittedRegion == QRegion(reader.rect()), "Fallback turn did not cover the whole screen");
        expectPolicy(policy, "Fallback turn changed the existing mode or waveform");
        require(!reader.property("reader_page_turn").toBool(), "Fallback turn request was not consumed");
    }
    setReaderRefreshWindow(nullptr);
    std::puts("PASS: dark refresh policies, animation suppression and native flush scope");
    return 0;
}
