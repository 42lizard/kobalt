#include "ui/screen_refresh.h"
#include <cstdio>
#include "check.h"

int main() {
    using kobalt::ScreenRefresh;
    using kobalt::refreshOnTurn;
    require(kobalt::screenRefresh(QString()) == ScreenRefresh::Smart, "A missing setting is not Smart");
    require(kobalt::screenRefresh("every page") == ScreenRefresh::Smart, "An unknown setting is not Smart");
    for (const auto mode : kobalt::screenRefreshModes)
        require(kobalt::screenRefresh(kobalt::screenRefreshValue(mode)) == mode, "A saved setting does not read back");
    // Smart flashes at a chapter start, but not within ten turns of a flash.
    require(!refreshOnTurn(ScreenRefresh::Smart, 9, true), "Smart flashed a chapter start within ten turns");
    require(refreshOnTurn(ScreenRefresh::Smart, 10, true), "Smart skipped a chapter start after ten turns");
    require(!refreshOnTurn(ScreenRefresh::Smart, 50, false), "Smart flashed outside a chapter start");
    require(refreshOnTurn(ScreenRefresh::Chapter, 1, true) && !refreshOnTurn(ScreenRefresh::Chapter, 50, false),
            "Every chapter does not flash at each chapter start only");
    // Page counts flash on the Nth turn since the last flash, chapter or not.
    require(!refreshOnTurn(ScreenRefresh::Pages6, 5, true) && refreshOnTurn(ScreenRefresh::Pages6, 6, false),
            "Every 6 pages does not flash on the sixth turn");
    require(!refreshOnTurn(ScreenRefresh::Pages10, 9, false) && refreshOnTurn(ScreenRefresh::Pages10, 10, false),
            "Every 10 pages does not flash on the tenth turn");
    require(!refreshOnTurn(ScreenRefresh::Pages20, 19, true) && refreshOnTurn(ScreenRefresh::Pages20, 20, false),
            "Every 20 pages does not flash on the twentieth turn");
    require(!refreshOnTurn(ScreenRefresh::Manual, 1000, true), "Manually flashed on a page turn");
    std::puts("PASS: screen refresh modes flash at chapter starts, every N turns or never");
}
