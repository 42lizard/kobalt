#pragma once
#include "../style/fonts.h"
#include "ui/highlight_paint.h"
#include "../style/theme.h"
#include "../style/styles.h"
#include "../../../../support/strings.h"
#include <QAbstractButton>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QVBoxLayout>
#include <QRegion>
#include <QVariant>
#include <functional>

namespace kobalt::ui {
class HighlightSwatch : public QAbstractButton {
public:
    HighlightSwatch(QString key, QString label, QWidget *parent) : QAbstractButton(parent), key(std::move(key)) {
        setObjectName("rrAnnotationColor_" + this->key);
        setText(label);
        setAccessibleName(label);
        setCheckable(true);
        setFixedHeight(px(110));
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFont(interfaceFont(px(23)));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto ink = palette().color(QPalette::WindowText);
        const int pen = px(2), ring = px(5);
        const int inset = ring + (pen + 1) / 2 + px(2);
        const int diameter = qMax(px(24), qMin(px(58), width() - 2 * inset));
        const QRect chip((width() - diameter) / 2, inset, diameter, diameter);
        p.setPen(QPen(ink, pen));
        p.setBrush(highlightTint(key));
        p.drawEllipse(chip);
        if (isChecked()) {
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(chip.adjusted(-ring, -ring, ring, ring));
            p.setPen(QPen(Qt::black, px(3)));
            p.drawLine(chip.center() + QPoint(-px(12), 0), chip.center() + QPoint(-px(3), px(10)));
            p.drawLine(chip.center() + QPoint(-px(3), px(10)), chip.center() + QPoint(px(14), -px(13)));
        }
        p.setPen(ink);
        auto labelFont = font();
        labelFont.setPixelSize(qMin(px(23), qMax(px(16), width() / 3)));
        p.setFont(labelFont);
        p.drawText(QRect(0, px(76), width(), px(30)), Qt::AlignCenter, text());
    }

private:
    QString key;
};
// Presentation only: the owner supplies selection and handles persistence.
class HighlightPalette : public QWidget {
public:
    HighlightPalette(const QString &selected, std::function<void(QString)> changed, QWidget *parent)
        : QWidget(parent), selectedColor(selected) {
        setObjectName("rrAnnotationPalette");
        parent->installEventFilter(this);
        setFixedHeight(compactHeight());
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(px(28), px(14), px(28), px(10));
        layout->setSpacing(px(4));
        auto *row = new QHBoxLayout;
        row->setSpacing(px(12));
        layout->addLayout(row);
        for (const auto &key : {"yellow", "green", "blue", "pink", "gray"}) {
            auto *swatch = new HighlightSwatch(key,
                                               (QString(key) == "yellow"  ? strings::get("annotations.color_yellow")
                                                : QString(key) == "green" ? strings::get("annotations.color_green")
                                                : QString(key) == "blue"  ? strings::get("annotations.color_blue")
                                                : QString(key) == "pink"  ? strings::get("annotations.color_pink")
                                                                          : strings::get("annotations.color_gray")),
                                               this);
            swatch->setChecked(selected == key);
            row->addWidget(swatch);
            connect(swatch, &QAbstractButton::clicked, this, [this, swatch, key, changed] {
                if (selectedColor == key) {
                    swatch->setChecked(true);
                    return;
                }
                setSelectedColor(QString::fromLatin1(key));
                changed(QString::fromLatin1(key));
            });
        }
        error = new QLabel(this);
        error->setObjectName("rrAnnotationPaletteError");
        error->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        error->setWordWrap(true);
        error->setFont(chromeFont(px(23)));
        readerStyle(error, QStringLiteral("font-family:'@ui-caption-family@';") + styles::font(px(23)));
        layout->addWidget(error);
        error->hide();
    }

    static int compactHeight() {
        return px(134);
    }
    void setError(const QString &message) {
        error->setText(message);
        error->setVisible(!message.isEmpty());
        error->ensurePolished();
        measureError();
    }

    ~HighlightPalette() override {
        retireRepairBounds();
    }
    void setSelectedColor(const QString &selected) {
        selectedColor = selected;
        for (auto *button : findChildren<QAbstractButton *>()) {
            button->setChecked(button->objectName() == "rrAnnotationColor_" + selected);
        }
    }

private:
    QString selectedColor;
    QLabel *error = nullptr;
    void measureError() {
        int extra = 0;
        if (error && !error->isHidden()) {
            const int room = qMax(1, width() - px(56));
            extra = qMax(error->sizeHint().height(), error->heightForWidth(room)) + px(4);
        }
        const int wanted = compactHeight() + extra;
        if (height() != wanted) {
            setFixedHeight(wanted);
        }
    }
    void retireRepairBounds() {
        window()->setProperty("reader_palette_repairs",
                              QVariant::fromValue(window()->property("reader_palette_repairs").value<QRegion>() |
                                                  window()->property("reader_palette_regions").value<QRegion>()));
        window()->setProperty("reader_palette_regions", QVariant::fromValue(QRegion()));
    }

protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (isVisible() && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) {
            syncRepairBounds();
        }
        return QWidget::eventFilter(object, event);
    }
    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        measureError();
        if (isVisible()) {
            syncRepairBounds();
        }
    }
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        syncRepairBounds();
    }
    void hideEvent(QHideEvent *event) override {
        retireRepairBounds();
        QWidget::hideEvent(event);
    }

public:
    void syncRepairBounds() {
        const QRegion region(QRect(mapTo(window(), QPoint()), size()));
        window()->setProperty("reader_palette_repairs",
                              QVariant::fromValue(window()->property("reader_palette_repairs").value<QRegion>() |
                                                  window()->property("reader_palette_regions").value<QRegion>() |
                                                  region));
        window()->setProperty("reader_palette_regions", QVariant::fromValue(region));
    }
};
}
