#pragma once
#include "../style/theme.h"
#include <QCheckBox>

namespace kobalt::ui {
class DescribedOption : public QCheckBox {
public:
    DescribedOption(const QString &text, const QString &description, QWidget *parent);
    int heightForWidth(int width) const override;
    enum class Indicator { Toggle, Square, Radio };
    struct Presentation {
        Presentation();
        Indicator indicator = Indicator::Toggle;
        Edges dividers;
        int horizontalInset = 0, minimumRowHeight = 116;
        int labelPixels, descriptionPixels;
        QSize indicatorSize;
        bool operator==(const Presentation &other) const;
    };
    void setPresentation(const Presentation &value);
    const Presentation &presentation() const {
        return appearance;
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *) override;
    bool hitButton(const QPoint &position) const override;
    void paintEvent(QPaintEvent *) override;

private:
    Presentation appearance;
    QString description;
};
}
