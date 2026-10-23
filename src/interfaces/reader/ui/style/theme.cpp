#include "theme.h"
#include "metrics.h"
#include "fonts.h"
#include <QPainter>
#include <QVariant>

namespace kobalt::ui {
namespace m = metrics;
namespace {
QRectF pixelAlignedRect(const QPainter &painter, const QRectF &rect) {
    const auto transform = painter.deviceTransform();
    if (transform.type() > QTransform::TxScale || transform.m11() <= 0 || transform.m22() <= 0)
        return rect;
    const QRectF device = transform.mapRect(rect);
    const QRectF aligned(qRound(device.left()), qRound(device.top()),
                         qMax(1, qRound(device.right()) - qRound(device.left())),
                         qMax(1, qRound(device.bottom()) - qRound(device.top())));
    return transform.inverted().mapRect(aligned);
}
}
void paintEdges(QPainter &painter, const QRect &rect, Edges edges, const QColor &ink, int width) {
    if (rect.isEmpty() || width <= 0 || !edges)
        return;
    const auto transform = painter.deviceTransform();
    const bool scaled = transform.type() <= QTransform::TxScale && transform.m11() > 0 && transform.m22() > 0;
    const QRectF bounds = pixelAlignedRect(painter, rect);
    const qreal horizontal = scaled ? qMax(1, qRound(width * transform.m22())) / transform.m22() : width;
    const qreal vertical = scaled ? qMax(1, qRound(width * transform.m11())) / transform.m11() : width;
    if (edges.testFlag(Qt::TopEdge))
        painter.fillRect(QRectF(bounds.left(), bounds.top(), bounds.width(), horizontal), ink);
    if (edges.testFlag(Qt::BottomEdge))
        painter.fillRect(QRectF(bounds.left(), bounds.bottom() - horizontal, bounds.width(), horizontal), ink);
    if (edges.testFlag(Qt::LeftEdge))
        painter.fillRect(QRectF(bounds.left(), bounds.top(), vertical, bounds.height()), ink);
    if (edges.testFlag(Qt::RightEdge))
        painter.fillRect(QRectF(bounds.right() - vertical, bounds.top(), vertical, bounds.height()), ink);
}
void paintButtonShape(QPainter &painter, const QRect &rect, const QColor &paper, const QColor &fill,
                      const QColor &border) {
    painter.fillRect(rect, paper);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(border, m::control::border));
    painter.setBrush(fill);
    const qreal inset = int(m::control::border) / 2.0;
    painter.drawRoundedRect(pixelAlignedRect(painter, rect).adjusted(inset, inset, -inset, -inset),
                            m::control::buttonRadius, m::control::buttonRadius);
    painter.restore();
}
bool darkTheme(const QWidget *widget) {
    for (auto *ancestor = widget; ancestor; ancestor = ancestor->parentWidget()) {
        const auto theme = ancestor->property("reader_dark");
        if (theme.isValid())
            return theme.toBool();
    }
    return false;
}
QColor dividerColor(const QWidget *widget) {
    return QColor(darkTheme(widget) ? m::chrome::dividerDark : m::chrome::dividerLight);
}
QColor sectionColor(const QWidget *widget) {
    return QColor(darkTheme(widget) ? "#333333" : "#dddddd");
}
QColor noteColor(const QWidget *widget) {
    return QColor(darkTheme(widget) ? m::text::explanationDark : m::text::explanationLight);
}
void paintHeaderRules(QPainter &painter, const QWidget *view) {
    painter.fillRect(0, m::chrome::statusHeight - m::control::border, view->width(), m::control::border,
                     view->palette().color(QPalette::Mid));
    painter.fillRect(0, m::chrome::headerHeight - m::chrome::heavyRule, view->width(), m::chrome::heavyRule,
                     view->palette().color(QPalette::WindowText));
}
void readerStyle(QWidget *widget, const QString &source) {
    widget->setProperty("reader_style", source);
    const bool dark = darkTheme(widget);
    auto style = QString("QPushButton{border-radius:%1px;} ").arg(int(m::control::buttonRadius));
    style += source.contains('{') ? source : QString("*{%1}").arg(source);
    const auto escapedFamily = [](QString name) {
        name.replace('\\', "\\\\");
        name.replace('\"', "\\\"");
        name.replace('\'', "\\'");
        return name;
    };
    style.replace("@ui-font-family@", escapedFamily(interfaceFont().family()));
    style.replace("@ui-caption-family@", escapedFamily(chromeFont().family()));
    style.replace("@header-footer-font@", "\"" + escapedFamily(headerFooterFont().family()) + "\"");
    style += " QPushButton:disabled,QPushButton:checked:disabled,QPushButton:pressed:disabled{"
             "color:palette(mid);background:palette(window);border-color:palette(midlight);}";
    style.replace("palette(window-text)", dark ? "white" : "black");
    style.replace("palette(window)", dark ? "black" : "white");
    style.replace("palette(midlight)", dark ? "#333333" : "#dddddd");
    style.replace("palette(mid)", "#999999");
    style.replace("palette(light)", dark ? m::chrome::dividerDark : m::chrome::dividerLight);
    style.replace("palette(placeholder-text)", dark ? "#cccccc" : "#333333");
    style.replace("palette(dark)", dark ? m::control::darkOutline : m::control::lightOutline);
    if (widget->styleSheet() != style)
        widget->setStyleSheet(style);
}
}
