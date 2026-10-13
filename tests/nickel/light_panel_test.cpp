#include "interfaces/reader/panels/light_panel.h"
#include "interfaces/reader/ui/controls/options.h"
#include "interfaces/reader/ui/style/metrics.h"
#include "platform/api/light_controls.h"

#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QTest>

namespace kobalt::platform {
void textRefresh(QWidget *) {}
}
class Light final : public kobalt::LightControls {
public:
    bool supported = true, automatic = true, saveAllowed = true;
    int changes = 0, level = 50, original = -1;
    bool originalAutomatic = false;
    bool initialize() override {
        return true;
    }
    bool hasAutomaticBrightness() const override {
        return supported;
    }
    bool automaticBrightness() const override {
        return automatic;
    }
    bool setAutomaticBrightness(bool value) override {
        ++changes;
        if (!saveAllowed) {
            return false;
        }
        automatic = value;
        return true;
    }
    bool hasWarmth() const override {
        return false;
    }
    int warmth() const override {
        return 0;
    }
    int warmthMinimum() const override {
        return 0;
    }
    int warmthMaximum() const override {
        return 100;
    }
    int warmthStep() const override {
        return 1;
    }
    bool automaticWarmth() const override {
        return false;
    }
    int setWarmth(int value) override {
        return value;
    }
    bool saveWarmth() override {
        return true;
    }
    bool setAutomaticWarmth(bool) override {
        return true;
    }
    bool beginGesture() override {
        if (original < 0) {
            original = level;
            originalAutomatic = automatic;
        }
        return true;
    }
    int setGestureLevel(int value) override {
        if (automatic && !setAutomaticBrightness(false)) {
            return -1;
        }
        return level = value;
    }
    void finishGesture(bool save) override {
        if (original < 0) {
            return;
        }
        if (!save) {
            level = original;
            automatic = originalAutomatic;
        }
        original = -1;
    }
    int brightness() const override {
        return level;
    }
    bool gesturing() const override {
        return original >= 0;
    }
};
class LightPanelTest : public QObject {
    Q_OBJECT
private slots:
    void automaticControlFitsDensity_data() {
        QTest::addColumn<int>("ppi");
        QTest::addColumn<int>("width");
        QTest::newRow("300-ppi") << 300 << 1072;
        QTest::newRow("212-ppi") << 212 << 758;
    }
    void automaticControlFitsDensity() {
        QFETCH(int, ppi);
        QFETCH(int, width);
        const int previousDensity = kobalt::ui::density();
        struct RestoreDensity {
            int value;
            ~RestoreDensity() {
                kobalt::ui::setDensity(value);
            }
        } restore{previousDensity};
        kobalt::ui::setDensity(ppi);
        QWidget parent;
        parent.resize(width, 1448);
        parent.show();
        Light light;
        kobalt::LightPanel panel(light, &parent);
        QVERIFY(panel.open());
        auto *option =
            dynamic_cast<kobalt::ui::DescribedOption *>(panel.findChild<QCheckBox *>("rrAutomaticBrightness"));
        QVERIFY(option);
        QCOMPARE(option->presentation().indicator, kobalt::ui::DescribedOption::Indicator::Square);
        QCOMPARE(option->presentation().labelPixels,
                 panel.findChild<QLabel *>("rrBrightnessCaption")->font().pixelSize());
        QCOMPARE(option->presentation().labelPixels, int(kobalt::ui::metrics::chrome::labelText));
        QVERIFY(panel.rect().contains(option->geometry()));
        QVERIFY(option->height() >= option->heightForWidth(option->width()));
        auto *tip = panel.findChild<QLabel *>("rrLightSwipeTip");
        QVERIFY(tip->wordWrap());
        QVERIFY(tip->font().italic());
        QCOMPARE(tip->font().pixelSize(), int(kobalt::ui::metrics::text::secondary));
        QVERIFY(tip->height() >= tip->heightForWidth(tip->width()));
        QVERIFY(panel.rect().contains(QRect(tip->mapTo(&panel, QPoint()), tip->size())));
    }
    void externalSettingUpdatesWithoutWriting() {
        QWidget parent;
        parent.resize(768, 1024);
        parent.show();
        Light light;
        kobalt::LightPanel panel(light, &parent);
        QVERIFY(panel.open());
        auto *option = panel.findChild<QCheckBox *>("rrAutomaticBrightness");
        light.automatic = false;
        QTRY_VERIFY(!option->isChecked());
        QCOMPARE(light.changes, 0);
    }
    void swipeHintPlacementAndVisibility() {
        QWidget parent;
        parent.resize(768, 1024);
        parent.show();
        Light light;
        kobalt::LightPanel panel(light, &parent);
        QVERIFY(panel.open());
        auto *tip = panel.findChild<QLabel *>("rrLightSwipeTip");
        auto *brightness = panel.findChild<QSlider *>("rrBrightness");
        QVERIFY(tip->isVisible());
        QVERIFY(tip->mapTo(&panel, tip->rect().bottomLeft()).y() < brightness->parentWidget()->geometry().top());
        panel.setEdgeGesturesEnabled(false);
        QVERIFY(!tip->isVisible());
        panel.hide();
        QVERIFY(panel.open());
        QVERIFY(!tip->isVisible());
        panel.setEdgeGesturesEnabled(true);
        QVERIFY(tip->isVisible());
    }
    void capabilityAndNativeState() {
        QWidget parent;
        parent.resize(768, 1024);
        parent.show();
        Light light;
        kobalt::LightPanel panel(light, &parent);
        QVERIFY(panel.open());
        auto *option = panel.findChild<QCheckBox *>("rrAutomaticBrightness");
        QVERIFY(option && option->isVisible() && option->isChecked());
        QCOMPARE(light.changes, 0);
        option->click();
        QVERIFY(!light.automatic && !option->isChecked());
        option->click();
        QVERIFY(light.automatic && option->isChecked());
        QCOMPARE(light.changes, 2);
        panel.hide();
        light.supported = false;
        QVERIFY(panel.open());
        QVERIFY(!option->isVisible());
    }
    void failedToggleShowsErrorAndRetainsState() {
        QWidget parent;
        parent.resize(768, 1024);
        parent.show();
        Light light;
        light.saveAllowed = false;
        kobalt::LightPanel panel(light, &parent);
        QVERIFY(panel.open());
        auto *option = panel.findChild<QCheckBox *>("rrAutomaticBrightness");
        option->click();
        QVERIFY(option->isChecked());
        QVERIFY(panel.findChild<QLabel *>("rrLightError")->isVisible());
        QCOMPARE(light.level, 50);
    }
    void manualSliderAndCancellation() {
        QWidget parent;
        parent.resize(768, 1024);
        parent.show();
        Light light;
        kobalt::LightPanel panel(light, &parent);
        QVERIFY(panel.open());
        panel.findChild<QSlider *>("rrBrightness")->setValue(70);
        QVERIFY(!light.automatic);
        QVERIFY(!panel.findChild<QCheckBox *>("rrAutomaticBrightness")->isChecked());
        QCOMPARE(light.level, 70);
        panel.hide();
        QVERIFY(light.automatic);
        QCOMPARE(light.level, 50);
        QVERIFY(panel.open());
        QVERIFY(panel.findChild<QCheckBox *>("rrAutomaticBrightness")->isChecked());
    }
};
QTEST_MAIN(LightPanelTest)
#include "light_panel_test.moc"
