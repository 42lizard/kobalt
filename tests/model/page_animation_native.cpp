#include "platform/nickel/display/page_animation.h"
#include "check.h"
#include <QCoreApplication>
#include <array>
#include <cerrno>
#include <cstring>
#include <vector>

namespace {
using Update = std::array<uint32_t, 9>;
struct Call {
    unsigned long request;
    Update update{};
    uint32_t marker = 0;
};
std::vector<Call> calls;
int failSend = 0, failWait = 0, failComplete = 0;
int sends = 0, waits = 0, completes = 0;
int ioctl(int, unsigned long request, void *data) {
    Call call{request};
    if (request == kobalt::PageAnimation::send) {
        std::memcpy(call.update.data(), data, sizeof(Update));
        ++sends;
    } else if (request == kobalt::PageAnimation::wait) {
        call.marker = *static_cast<uint32_t *>(data);
        ++waits;
    } else if (request == kobalt::PageAnimation::waitComplete) {
        auto *words = static_cast<uint32_t *>(data);
        call.marker = words[0];
        require(words[1] == 0, "Completion wait has the wrong payload");
        ++completes;
    }
    calls.push_back(call);
    if ((request == kobalt::PageAnimation::send && sends == failSend) ||
        (request == kobalt::PageAnimation::wait && waits == failWait) ||
        (request == kobalt::PageAnimation::waitComplete && completes == failComplete)) {
        errno = EIO;
        return -1;
    }
    return 0;
}
void reset() {
    calls.clear();
    sends = waits = completes = 0;
    failSend = failWait = failComplete = 0;
}
std::vector<Update> updates() {
    std::vector<Update> result;
    for (const auto &call : calls)
        if (call.request == kobalt::PageAnimation::send)
            result.push_back(call.update);
    return result;
}
void sweep(bool mono, bool dark, bool vertical, int direction, const QRect &page) {
    reset();
    kobalt::PageAnimation animation;
    animation.arm(direction, page, dark, vertical, mono);
    const Update original{uint32_t(page.top()),
                          uint32_t(page.left()),
                          uint32_t(page.width()),
                          uint32_t(page.height()),
                          dark ? 9u : 4u,
                          1,
                          83,
                          0,
                          0};
    auto argument = original;
    require(animation.submit(ioctl, 3, kobalt::PageAnimation::send, argument.data()) == 0, "Native animation failed");
    require(animation.result() == kobalt::PageAnimationResult::Swept, "Successful sweep result missing");
    const auto bands = updates();
    const int count = qMax(page.width(), page.height()) >= 1600 ? 12 : 10;
    require(int(bands.size()) == count && waits == count && completes == 1,
            "Sweep did not wait for every band and final completion");
    require(calls.back().request == kobalt::PageAnimation::waitComplete && calls.back().marker == original[6],
            "Last band was not completed before return");
    for (int i = 0; i < count; ++i) {
        const auto &band = bands[size_t(i)];
        require(band[4] == (dark ? 9u : mono ? 4u : 8u) && band[5] == 0, "Band has wrong waveform or update mode");
        require((band[7] & 0x8000) == (mono ? 0u : 0x8000u), "CFA_SKIP is wrong for the panel");
        const int index = direction > 0 ? count - 1 - i : i;
        const int length = vertical ? page.height() : page.width();
        const int start = length * index / count, end = length * (index + 1) / count;
        const Update expected{uint32_t(page.top() + (vertical ? start : 0)),
                              uint32_t(page.left() + (vertical ? 0 : start)),
                              uint32_t(vertical ? page.width() : end - start),
                              uint32_t(vertical ? end - start : page.height()),
                              band[4],
                              band[5],
                              band[6],
                              band[7],
                              band[8]};
        require(band == expected, "Band does not cover its assigned part of the page");
    }
    require(bands.back()[6] == original[6], "Last band lost the caller's marker");
    require(argument == original, "Animation modified the caller's request");
    animation.cancel();
    require(animation.result() == kobalt::PageAnimationResult::None, "Cancel kept a stale sweep result");
}
void failure(int sendNumber, int waitNumber, int completeNumber) {
    reset();
    failSend = sendNumber;
    failWait = waitNumber;
    failComplete = completeNumber;
    const QRect page(0, 0, 1072, 1448);
    const Update original{0, 0, 1072, 1448, 4, 1, 83, 0, 0};
    auto argument = original;
    kobalt::PageAnimation animation;
    animation.arm(1, page, false);
    errno = 0;
    require(animation.submit(ioctl, 3, kobalt::PageAnimation::send, argument.data()) == -1 && errno == EIO,
            "Failed ioctl was not propagated");
    require(animation.result() == kobalt::PageAnimationResult::Fallback, "Failed sweep result missing");
    require(updates().back() == original, "Failed sweep did not submit the whole original update");
    require(completes >= 1 && calls.back().request == kobalt::PageAnimation::waitComplete &&
                calls.back().marker == original[6],
            "Fallback did not wait for the original update");
    require(argument == original, "Fallback modified the caller's request");
}
void repairTracking() {
    kobalt::nextAnimationIoctl = ioctl;
    const Update first{20, 30, 80, 60, 10, 1, 101, 0, 0};
    const Update second{120, 140, 90, 70, 10, 1, 102, 0, 0};
    auto a = first, b = second;
    reset();
    kobalt::beginPageRepair();
    require(kobalt_animation_ioctl(3, kobalt::PageAnimation::send, a.data()) == 0, "First repair failed");
    require(kobalt_animation_ioctl(3, kobalt::PageAnimation::send, b.data()) == 0, "Second repair failed");
    require(kobalt::endPageRepair() && completes == 1 && calls.back().marker == second[6],
            "Repair did not wait for its last marker");
    require(updates() == std::vector<Update>({first, second}) && a == first && b == second,
            "Repair changed its native requests");
    reset();
    failSend = 2;
    kobalt::beginPageRepair();
    require(kobalt_animation_ioctl(3, kobalt::PageAnimation::send, a.data()) == 0, "First repair failed early");
    require(kobalt_animation_ioctl(3, kobalt::PageAnimation::send, b.data()) == -1, "Rejected repair send was hidden");
    require(!kobalt::endPageRepair() && completes == 1 && calls.back().marker == first[6],
            "Rejected repair did not report failure or wait for prior work");
    require(sends == 2 && a == first && b == second, "Rejected repair changed the native requests");
    reset();
    failComplete = 1;
    kobalt::beginPageRepair();
    require(kobalt_animation_ioctl(3, kobalt::PageAnimation::send, a.data()) == 0, "Repair send failed unexpectedly");
    require(!kobalt::endPageRepair() && completes == 1, "Completion failure was hidden");
    reset();
    kobalt::beginPageRepair();
    require(!kobalt::endPageRepair() && completes == 0, "Empty repair was reported as complete");
    require(kobalt_animation_ioctl(3, kobalt::PageAnimation::send, a.data()) == 0 && completes == 0,
            "Untracked native update was changed");
    kobalt::nextAnimationIoctl = nullptr;
}
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    for (bool mono : {false, true})
        for (bool dark : {false, true})
            for (bool vertical : {false, true})
                for (int direction : {-1, 1})
                    sweep(mono, dark, vertical, direction,
                          vertical ? QRect(0, 0, 1072, 1448) : QRect(0, 0, 1448, 1072));
    sweep(false, false, false, 1, QRect(0, 0, 1600, 1200));
    for (int failurePoint : {1, 5, 10}) {
        failure(failurePoint, 0, 0);
        failure(0, failurePoint, 0);
    }
    failure(0, 0, 1);
    for (bool dark : {false, true})
        for (uint32_t waveform : {2u, 3u, 8u, 9u, 10u, 11u, 257u}) {
            if (dark && waveform == 9)
                continue;
            reset();
            kobalt::PageAnimation animation;
            animation.arm(1, QRect(0, 0, 1072, 1448), dark);
            Update update{0, 0, 1072, 1448, waveform, 1, 83, 0, 0};
            require(animation.submit(ioctl, 3, kobalt::PageAnimation::send, update.data()) == 0, "Passthrough failed");
            require(updates().size() == 1 && updates().front() == update &&
                        animation.result() == kobalt::PageAnimationResult::Passthrough,
                    "Cleanup, colour or unrecognized request was swept");
        }
    reset();
    kobalt::PageAnimation light;
    light.arm(1, QRect(0, 0, 1072, 1448), false);
    Update partialLight{0, 0, 1072, 1448, 4, 0, 83, 0, 0};
    light.submit(ioctl, 3, kobalt::PageAnimation::send, partialLight.data());
    require(updates().size() == 1 && updates().front() == partialLight, "Light partial cleanup was swept");
    reset();
    kobalt::PageAnimation animation;
    animation.arm(1, QRect(0, 0, 1072, 1448), true);
    Update cleanup{0, 0, 1072, 1448, 9, 0, 83, 0, 0};
    animation.submit(ioctl, 3, kobalt::PageAnimation::send, cleanup.data());
    require(updates().size() == 1 && updates().front() == cleanup, "Dark partial cleanup was swept");
    repairTracking();
    std::puts("PASS: native page animation, completion, fallback and passthrough");
}
