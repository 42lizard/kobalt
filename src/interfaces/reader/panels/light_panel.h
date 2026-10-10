#pragma once
#include "interfaces/reader/ui/controls/sliders.h"
#include "interfaces/reader/ui/controls/panels.h"
#include <QTimer>

namespace kobalt {
class LightControls;
// The reader owns the light controller and destroys this panel before it.
class LightPanel : public ui::Panel {
public:
    LightPanel(LightControls &light, QWidget *parent);
    ~LightPanel() override;
    bool open();
    std::function<void()> changed;
    std::function<QRect(int)> placement;

protected:
    void hideEvent(QHideEvent *event) override;

private:
    LightControls &light;
    ui::SettingSlider *brightnessSlider, *warmthSlider;
    QLabel *brightnessValue, *warmthValue, *swipeTip, *error;
    QWidget *warmthRow;
    QTimer poll;
    bool updating = false, warmthAutomatic = false;
    int warmthOriginal = -1;
    void finishWarmth(bool save);
    void refreshValues();
    void layoutPanel();
    void result(bool success);
};
}
