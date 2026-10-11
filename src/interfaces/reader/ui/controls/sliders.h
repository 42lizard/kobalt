#pragma once
#include <QSlider>
#include <QVBoxLayout>
#include <QVector>
#include <functional>

namespace kobalt::ui {
class SettingSlider : public QSlider {
public:
    SettingSlider(QWidget *parent, int minimum, int maximum, int step = 1);
    std::function<void(int)> committed;
    bool interacting() const {
        return tracking;
    }
    enum class MarkerMode { Positions, Stops };
    // Stops are evenly spaced and snap pointer gestures. Buttons retain singleStep().
    void setMarkers(const QVector<int> &values, MarkerMode mode = MarkerMode::Positions);
    void nudge(int steps);

protected:
    void paintEvent(QPaintEvent *) override;
    bool event(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    bool tracking = false;
    int original = 0;
    QVector<int> markers;
    MarkerMode markerMode = MarkerMode::Positions;
    int positionForValue(int value) const;
    void moveTo(qreal x);
    void cancel();
    void finish();
};
SettingSlider *sliderRow(QWidget *parent, QVBoxLayout *rows, const char *name, const char *labelName,
                         const QString &label, const char *lessName, const char *moreName, int minimum, int maximum,
                         int step, std::function<void(int)> apply, const QString &prefix = {},
                         const QString &suffix = {});
}
