#include "interfaces/reader/ui/controls/buttons.h"
#include "interfaces/reader/ui/controls/panels.h"
#include "interfaces/reader/ui/dialogs/modals.h"
#include "start_screen.h"
#include "canvas.h"
#include "interfaces/reader/views/opening.h"
#include <QScrollArea>
#include <QScroller>
#include "interfaces/reader/ui/style/fonts.h"
#include "support/strings.h"
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include <QPainter>
#include <QTextLayout>
#include <QFileInfo>
#include <QImageReader>
#include <QBuffer>
#include <QTimer>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QApplication>
#include "ui/input.h"

namespace kobalt::android {
namespace {
class LastBookButton final : public ui::Button {
public:
    explicit LastBookButton(QWidget *parent) : ui::Button(parent) {
        setObjectName("androidLastBook");
        setMinimumHeight(126);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        hold.setSingleShot(true);
        connect(&hold, &QTimer::timeout, this, [this] {
            if (!isDown() || !isVisible() || !isEnabled() || !forgetRequested)
                return;
            held = true;
            setDown(false);
            forgetRequested();
        });
    }
    QString title, author;
    QImage cover;
    std::function<void()> forgetRequested;

protected:
    void mousePressEvent(QMouseEvent *event) override {
        held = false;
        ui::Button::mousePressEvent(event);
        if (event->button() == Qt::LeftButton && isDown()) {
            press = ui::mousePosition(event).toPoint();
            hold.start(550);
        }
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if ((ui::mousePosition(event).toPoint() - press).manhattanLength() > QApplication::startDragDistance())
            hold.stop();
        ui::Button::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        hold.stop();
        if (held) {
            held = false;
            setDown(false);
            event->accept();
            return;
        }
        ui::Button::mouseReleaseEvent(event);
    }
    void hideEvent(QHideEvent *event) override {
        hold.stop();
        ui::Button::hideEvent(event);
    }
    void leaveEvent(QEvent *event) override {
        hold.stop();
        ui::Button::leaveEvent(event);
    }
    void contextMenuEvent(QContextMenuEvent *event) override {
        hold.stop();
        held = true;
        setDown(false);
        if (forgetRequested)
            forgetRequested();
        event->accept();
    }
    void paintEvent(QPaintEvent *event) override {
        ui::Button::paintEvent(event);
        QPainter painter(this);
        painter.setPen(Qt::black);
        const QRect thumbnail(16, (height() - 84) / 2, 56, 84);
        if (!cover.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform);
            const auto size = cover.size().scaled(thumbnail.size(), Qt::KeepAspectRatio);
            painter.drawImage(QRect(thumbnail.center() - QPoint(size.width() / 2, size.height() / 2), size), cover);
        } else {
            painter.drawRect(thumbnail);
            painter.setFont(ui::brandFont(10));
            painter.drawLine(thumbnail.left() + 6, thumbnail.top() + 28, thumbnail.right() - 6, thumbnail.top() + 28);
            painter.drawText(thumbnail.adjusted(6, 31, -6, -26), Qt::TextWordWrap, title);
            painter.drawLine(thumbnail.left() + 6, thumbnail.bottom() - 24, thumbnail.right() - 6,
                             thumbnail.bottom() - 24);
        }
        const int left = 88, width = qMax(1, this->width() - left - 36);
        auto caption = ui::captionFont(12);
        caption.setLetterSpacing(QFont::AbsoluteSpacing, 1);
        auto font = ui::interfaceFont(22);
        font.setBold(true);
        const auto authorFont = ui::interfaceFont(18);
        QTextLayout layout(title, font);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        QList<QTextLine> lines;
        qreal titleHeight = 0;
        for (int row = 0; row < 2; ++row) {
            auto line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(width);
            lines.append(line);
            titleHeight += line.height();
        }
        layout.endLayout();
        const int captionHeight = QFontMetrics(caption).height();
        const int authorHeight = author.isEmpty() ? 0 : QFontMetrics(authorFont).height();
        const qreal blockHeight = captionHeight + 5 + titleHeight + (authorHeight ? 4 + authorHeight : 0);
        qreal y = (height() - blockHeight) / 2;
        painter.setFont(caption);
        painter.drawText(QRectF(left, y, width, captionHeight), Qt::AlignLeft | Qt::AlignTop,
                         strings::get("android.reopen_last_book").toUpper());
        y += captionHeight + 5;
        painter.setFont(font);
        for (int row = 0; row < lines.size(); ++row) {
            const auto line = lines[row];
            if (row == 1 && line.textStart() + line.textLength() < title.size()) {
                painter.drawText(QRectF(left, y, width, line.height()), Qt::AlignLeft | Qt::AlignTop,
                                 QFontMetrics(font).elidedText(title.mid(line.textStart()), Qt::ElideRight, width));
            } else
                line.draw(&painter, QPointF(left, y));
            y += line.height();
        }
        if (authorHeight) {
            painter.setPen(QColor("#444444"));
            painter.setFont(authorFont);
            painter.drawText(QRectF(left, y + 4, width, authorHeight), Qt::AlignLeft | Qt::AlignTop,
                             painter.fontMetrics().elidedText(author, Qt::ElideRight, width));
        }
        painter.setPen(QPen(Qt::black, 1.5));
        const int x = this->width() - 23, cy = height() / 2;
        painter.drawLine(x - 4, cy - 6, x + 2, cy);
        painter.drawLine(x + 2, cy, x - 4, cy + 6);
    }

private:
    QTimer hold;
    QPoint press;
    bool held = false;
};
}
StartScreen::StartScreen(PresentationFactory presentation, QWidget *parent)
    : QStackedWidget(parent), presentation(std::move(presentation)) {
    intro = new QScrollArea(this);
    intro->setObjectName("androidIntro");
    addWidget(intro);
    setObjectName("androidStart");
    intro->setFrameShape(QFrame::NoFrame);
    QScroller::grabGesture(intro->viewport(), QScroller::LeftMouseButtonGesture);
    intro->setWidgetResizable(true);
    intro->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *page = new QWidget;
    auto *rows = new QVBoxLayout(page);
    rows->setContentsMargins(27, 24, 27, 24);
    auto *content = new QWidget(page);
    content->setMaximumWidth(640);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    // These widgets use logical pixels. Safe-area margins belong to the host.
    auto *brandRow = new QHBoxLayout;
    brandRow->setSpacing(18);
    auto *mark = new ui::BrandIcon(content);
    mark->setFixedSize(36, 40);
    brandRow->addWidget(mark);
    auto *brand = new QLabel(strings::get("app.wordmark"), content);
    brand->setFont(ui::brandFont(42));
    brandRow->addWidget(brand);
    brandRow->addStretch();
    column->addLayout(brandRow);
    column->addSpacing(28);
    message = new QLabel(content);
    message->setObjectName("androidStartTitle");
    message->setFont(ui::brandFont(32, true));
    message->setTextFormat(Qt::PlainText);
    message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    message->setWordWrap(true);
    message->setAlignment(Qt::AlignLeft);
    column->addWidget(message);
    column->addSpacing(48);
    column->addStretch(1);
    auto *lastBook = new LastBookButton(content);
    lastBook->forgetRequested = [this] {
        askToForget();
    };
    reopen = lastBook;
    reopen->hide();
    column->addWidget(reopen);
    auto *actions = new QWidget(content);
    auto *buttons = new QVBoxLayout(actions);
    buttons->setContentsMargins(0, 28, 0, 0);
    buttons->setSpacing(10);
    detail = new QLabel(strings::get("android.start_hint"), actions);
    detail->setObjectName("androidStartDetail");
    detail->setFont(ui::interfaceFont(14));
    detail->setTextFormat(Qt::PlainText);
    detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    detail->setWordWrap(true);
    buttons->addWidget(detail);
    buttons->addSpacing(2);
    open = new ui::Button(strings::get("android.open_epub"), actions);
    open->setObjectName("androidOpenBook");
    open->setMinimumHeight(56);
    open->setFont(ui::interfaceFont(18));
    buttons->addWidget(open);
    auto *exit = new ui::Button(strings::get("android.exit"), actions);
    exit->setObjectName("androidExit");
    exit->setMinimumHeight(56);
    exit->setFont(ui::interfaceFont(18));
    buttons->addWidget(exit);
    column->addWidget(actions);
    rows->addWidget(content, 1);
    intro->setWidget(page);
    QPalette colors = palette();
    colors.setColor(QPalette::Window, Qt::white);
    colors.setColor(QPalette::Base, Qt::white);
    colors.setColor(QPalette::WindowText, Qt::black);
    colors.setColor(QPalette::Text, Qt::black);
    setPalette(colors);
    page->setAutoFillBackground(true);
    setStyleSheet("QWidget{background:white;color:black;}QLabel{color:black;background:transparent;}"
                  "QPushButton{padding:8px 12px;border:1px solid black;border-radius:0;background:white;color:black;}"
                  "QPushButton#androidOpenBook{background:black;color:white;}QLabel#androidStartDetail{color:#555555;}"
                  "QPushButton:pressed,QPushButton#androidOpenBook:pressed{background:#dedede;color:black;}");
    connect(open, &QPushButton::clicked, this, &StartScreen::openRequested);
    connect(exit, &QPushButton::clicked, this, &StartScreen::exitRequested);
    connect(reopen, &QPushButton::clicked, this, [this] {
        emit reopenRequested(lastPath);
    });
    showReady();
}
void StartScreen::setLastBook(const QJsonObject &book) {
    if (modalCanvas)
        modalCanvas->hide();
    lastPath = book.value("path").toString();
    auto *card = static_cast<LastBookButton *>(reopen);
    card->title = book.value("title").toString().trimmed();
    if (card->title.isEmpty())
        card->title = QFileInfo(lastPath).completeBaseName();
    card->author = book.value("author").toString();
    const auto encoded = book.value("cover").toString().toLatin1();
    card->cover = {};
    if (!encoded.isEmpty() && encoded.size() <= 192 * 1024) {
        auto bytes = QByteArray::fromBase64(encoded);
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const auto size = reader.size();
        if (size.width() > 0 && size.height() > 0 && size.width() <= 120 && size.height() <= 180)
            card->cover = reader.read();
    }
    card->setAccessibleName(strings::get("android.reopen_last_book") + ": " + card->title + ", " + card->author);
    card->setVisible(!lastPath.isEmpty());
    card->update();
}
void StartScreen::askToForget() {
    if (lastPath.isEmpty())
        return;
    if (!forgetModal) {
        // Reader components use physical pixels; the startup page uses logical pixels.
        modalCanvas = new WidgetCanvas(this);
        modalCanvas->setObjectName("androidRecentModalCanvas");
        modalCanvas->setStyleSheet("QGraphicsView{background:transparent;border:0;}");
        modalCanvas->viewport()->setAutoFillBackground(false);
        auto *host = new QWidget;
        host->setWindowFlags(Qt::Widget);
        host->setPalette(palette());
        host->setAttribute(Qt::WA_NoSystemBackground);
        modalCanvas->setGeometry(rect());
        forgetModal = new ui::ChoiceModal(
            host, {strings::get("android.remove_last_description"), strings::get("android.remove_last_title"), {}},
            {strings::get("common.cancel"), "androidForgetCancel"},
            {strings::get("android.remove_last_action"), "androidForgetConfirm"});
        forgetModal->card()->setPalette(palette());
        forgetModal->dismissed = [this] {
            modalCanvas->hide();
            reopen->setFocus();
        };
        connect(forgetModal->secondaryButton(), &QPushButton::clicked, this, [this] {
            forgetModal->dismissed();
        });
        connect(forgetModal->primaryButton(), &QPushButton::clicked, this, &StartScreen::forgetRequested);
        // Complete the subtree before embedding it; proxy resize can disable new children.
        modalCanvas->setContent(host);
        modalCanvas->fitContent();
        host->show();
    }
    forgetModal->descriptionLabel()->setText(strings::get("android.remove_last_description"));
    modalCanvas->show();
    modalCanvas->raise();
    modalCanvas->fitContent();
    forgetModal->fitCard();
    forgetModal->show();
    forgetModal->raise();
    forgetModal->setFocus();
}
bool StartScreen::dismissModal() {
    if (!modalCanvas || !modalCanvas->isVisible())
        return false;
    forgetModal->dismissed();
    return true;
}
void StartScreen::showForgetError() {
    if (!forgetModal)
        return;
    forgetModal->descriptionLabel()->setText(strings::get("android.remove_last_error"));
    forgetModal->fitCard();
}
void StartScreen::resizeEvent(QResizeEvent *event) {
    QStackedWidget::resizeEvent(event);
    if (modalCanvas) {
        modalCanvas->setGeometry(rect());
        modalCanvas->fitContent();
    }
}
StartScreen::~StartScreen() = default;
void StartScreen::showReady() {
    message->setText(strings::get("android.start_title").replace(" starts", "\nstarts"));
    detail->setText(strings::get("android.start_hint"));
    open->setText(strings::get("android.open_epub"));
    setCurrentWidget(intro);
    intro->verticalScrollBar()->setValue(0);
}
void StartScreen::showOpening() {
    if (modalCanvas)
        modalCanvas->hide();
    // Recreate the view to reset progress and the delayed hint for each book.
    if (opening)
        removeWidget(opening->window());
    opening = createEmbeddedOpening(this, presentation);
    connect(opening->window(), &QDialog::rejected, this, &StartScreen::exitRequested);
    addWidget(opening->window());
    setCurrentWidget(opening->window());
}
void StartScreen::setProgress(int percent) {
    if (opening && currentWidget() == opening->window())
        opening->setProgress(percent);
}
void StartScreen::showError(const QString &error) {
    message->setText(strings::get("opening.failed"));
    detail->setText(error);
    open->setText(strings::get("android.choose_another_epub"));
    open->setEnabled(true);
    setCurrentWidget(intro);
    intro->verticalScrollBar()->setValue(0);
}
}
