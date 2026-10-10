#pragma once
#include "interfaces/reader/ui/dialogs/modals.h"
#include "interfaces/reader_interface.h"
#include <QStackedWidget>
#include <QJsonObject>

class QScrollArea;

class QLabel;
class QPushButton;

namespace kobalt::ui {
class ChoiceModal;
}

namespace kobalt::android {
class WidgetCanvas;
// Qt-only launch UI. The Android host supplies document picking and exit.
class StartScreen final : public QStackedWidget {
    Q_OBJECT
public:
    explicit StartScreen(PresentationFactory presentation, QWidget *parent = nullptr);
    ~StartScreen() override;
    void showReady();
    void setLastBook(const QJsonObject &book);
    void showOpening();
    void setProgress(int percent);
    void showError(const QString &error);
    void showForgetError();
    bool dismissModal();
signals:
    void openRequested();
    void reopenRequested(QString path);
    void exitRequested();
    void forgetRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QScrollArea *intro;
    std::unique_ptr<ReaderOpeningView> opening;
    PresentationFactory presentation;
    QLabel *message, *detail;
    QPushButton *open, *reopen;
    QString lastPath;
    ui::ChoiceModal *forgetModal = nullptr;
    WidgetCanvas *modalCanvas = nullptr;
    void askToForget();
};
}
