#include "interfaces/reader/ui/controls/sliders.h"
#include "interfaces/reader/ui/controls/panels.h"
#include "interfaces/reader/ui/controls/options.h"
#include "support/strings.h"
#include "light_panel.h"
#include "platform/api/light_controls.h"
#include "interfaces/reader/ui/style/metrics.h"
#include "interfaces/reader/ui/style/styles.h"
#include "platform/api/kobo_refresh.h"
#include <QHideEvent>
#include <QHBoxLayout>
#include <QPainter>
#include "interfaces/reader/ui/controls/icons.h"
#include <utility>

namespace kobalt {
namespace m = ui::metrics;
namespace {
class SwipeIcon : public QWidget {
public:
    explicit SwipeIcon(QWidget *parent) : QWidget(parent) {
        setFixedSize(m::text::heading, m::text::heading);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        ui::paintOpeningIcon(painter, ui::ControlIcon::Swipe, rect(), palette().color(QPalette::WindowText));
    }
};
}
LightPanel::LightPanel(LightControls &light, QWidget *parent) : ui::Panel(parent, Qt::BottomEdge), light(light) {
    setObjectName("rrLightPanel");
    auto *rows = new QVBoxLayout(this);
    rows->setContentsMargins(m::panel::contentMargins);
    rows->setSpacing(m::control::rowGap);
    automaticBrightness = new ui::DescribedOption(strings::get("light_panel.automatic_brightness"),
                                                  strings::get("light_panel.automatic_brightness_description"), this);
    automaticBrightness->setObjectName("rrAutomaticBrightness");
    auto automaticPresentation = automaticBrightness->presentation();
    automaticPresentation.indicator = ui::DescribedOption::Indicator::Square;
    automaticPresentation.labelPixels = m::chrome::labelText;
    automaticPresentation.descriptionPixels = m::chrome::descriptionText;
    automaticPresentation.indicatorSize = {m::chrome::checkSize, m::chrome::checkSize};
    automaticBrightness->setPresentation(automaticPresentation);
    connect(automaticBrightness, &QCheckBox::clicked, this, [this](bool enabled) {
        result(this->light.setAutomaticBrightness(enabled));
        refreshValues();
    });
    brightnessSlider =
        ui::sliderRow(this, rows, "rrBrightness", "rrBrightnessValue", strings::get("light_panel.brightness"),
                      "rrBrightnessLess", "rrBrightnessMore", 0, 100, 1, {}, {}, "%");
    warmthSlider = ui::sliderRow(this, rows, "rrWarmth", "rrWarmthValue", strings::get("light_panel.warmth"),
                                 "rrWarmthLess", "rrWarmthMore", 0, 100, 1, {});
    brightnessValue = findChild<QLabel *>("rrBrightnessValue");
    warmthValue = findChild<QLabel *>("rrWarmthValue");
    warmthRow = warmthSlider->parentWidget();
    rows->addWidget(automaticBrightness);
    swipeTip = new QLabel(this);
    swipeTip->setObjectName("rrLightSwipeTip");
    swipeTip->setWordWrap(true);
    ui::readerStyle(swipeTip, ui::styles::font(m::text::secondary) +
                                 "font-style:italic;color:palette(placeholder-text);");
    swipeRow = new QWidget(this);
    auto *tipLayout = new QHBoxLayout(swipeRow);
    tipLayout->setContentsMargins(0, ui::px(4), 0, ui::px(10));
    tipLayout->setSpacing(m::spacing::row);
    tipLayout->addWidget(new SwipeIcon(swipeRow), 0, Qt::AlignTop);
    tipLayout->addWidget(swipeTip, 1);
    rows->insertWidget(0, swipeRow);
    error = new QLabel(strings::get("light_panel.could_not_save_light_settings"), this);
    error->setObjectName("rrLightError");
    error->setWordWrap(true);
    rows->addWidget(error);
    error->hide();
    // Automatic warmth and its bedtime stay in Nickel's settings. Moving the
    // warmth slider turns automatic warmth off, as Nickel's slider does.
    // Brightness changes reach the device while dragging. Native persistence
    // runs on release; interrupted drags restore the original level.
    connect(brightnessSlider, &QSlider::valueChanged, this, [this](int value) {
        brightnessValue->setText(QString::number(value) + '%');
        if (!updating && this->light.beginGesture()) {
            result(this->light.setGestureLevel(value) == value);
            automaticBrightness->setChecked(this->light.automaticBrightness());
        }
    });
    brightnessSlider->committed = [this](int) {
        this->light.finishGesture(true);
        refreshValues();
    };
    connect(warmthSlider, &QSlider::valueChanged, this, [this](int value) {
        if (!updating) {
            if (warmthOriginal < 0) {
                warmthOriginal = this->light.warmth();
                warmthAutomatic = this->light.automaticWarmth();
            }
            result(this->light.setWarmth(value) == value);
        }
        const int range = warmthSlider->maximum() - warmthSlider->minimum();
        warmthValue->setText(
            QString::number(range > 0 ? qRound(100.0 * (value - warmthSlider->minimum()) / range) : 0) + '%');
    });
    warmthSlider->committed = [this](int) {
        finishWarmth(true);
        refreshValues();
    };
    connect(&poll, &QTimer::timeout, this, [this] {
        refreshValues();
    });
    poll.setInterval(m::light::pollMilliseconds);
    for (auto *widget : findChildren<QWidget *>())
        platform::textRefresh(widget);
    platform::textRefresh(this);
    hide();
}
LightPanel::~LightPanel() {
    poll.stop();
    light.finishGesture(false);
    finishWarmth(false);
}
bool LightPanel::open() {
    if (!light.initialize())
        return false;
    error->hide();
    refreshValues();
    layoutPanel();
    show();
    raise();
    poll.start();
    if (changed)
        changed();
    return true;
}
void LightPanel::setEdgeGesturesEnabled(bool enabled) {
    if (edgeGesturesEnabled == enabled) {
        return;
    }
    edgeGesturesEnabled = enabled;
    swipeRow->setVisible(enabled);
    layoutPanel();
}
void LightPanel::layoutPanel() {
    layout()->activate();
    const int width = parentWidget()->width();
    const int height = layout()->hasHeightForWidth() ? layout()->totalHeightForWidth(width) : sizeHint().height();
    const QRect bounds = placement ? placement(height) : QRect(0, m::panel::toolbarHeight, width, height);
    const bool moved = geometry() != bounds;
    setGeometry(bounds);
    if (moved && isVisible() && changed)
        changed();
}
void LightPanel::result(bool success) {
    error->setVisible(!success);
    layoutPanel();
}
void LightPanel::refreshValues() {
    updating = true;
    automaticBrightness->setVisible(light.hasAutomaticBrightness());
    if (!brightnessSlider->interacting() && light.gesturing())
        light.finishGesture(false);
    if (!warmthSlider->interacting())
        finishWarmth(false);
    automaticBrightness->setChecked(light.automaticBrightness());
    if (!brightnessSlider->interacting())
        brightnessSlider->setValue(light.brightness());
    brightnessValue->setText(QString::number(brightnessSlider->value()) + '%');
    const bool warmth = light.hasWarmth();
    warmthRow->setVisible(warmth);
    swipeTip->setText(
        strings::get(warmth ? "settings.edge_light_description" : "settings.edge_brightness_description"));
    if (warmth) {
        if (!warmthSlider->interacting()) {
            warmthSlider->setRange(light.warmthMinimum(), light.warmthMaximum());
            warmthSlider->setSingleStep(light.warmthStep());
            warmthSlider->setValue(light.warmth());
        }
        // setValue does not emit when the current value is unchanged.
        const int range = light.warmthMaximum() - light.warmthMinimum();
        warmthValue->setText(QString::number(qRound(100.0 * (warmthSlider->value() - light.warmthMinimum()) / range)) +
                             '%');
    }
    updating = false;
    layoutPanel();
}
void LightPanel::finishWarmth(bool save) {
    if (warmthOriginal < 0)
        return;
    const int original = std::exchange(warmthOriginal, -1);
    if (save)
        result(light.saveWarmth());
    else {
        // The native slider disables automatic warmth on manual adjustment.
        // A cancelled touch must restore both the light and that preference.
        light.setWarmth(original);
        if (warmthAutomatic)
            light.setAutomaticWarmth(true);
    }
}
void LightPanel::hideEvent(QHideEvent *event) {
    updating = true;
    poll.stop();
    light.finishGesture(false);
    finishWarmth(false);
    ui::Panel::hideEvent(event);
    if (changed)
        changed();
}
}
