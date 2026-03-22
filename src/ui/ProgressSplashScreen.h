#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;

namespace qodex::ui {

class ProgressSplashScreen final : public QWidget {
    Q_OBJECT

public:
    explicit ProgressSplashScreen(const QString &title, QWidget *parent = nullptr);

    void setStatus(const QString &message, int progress);
    void showCentered();

private:
    void centerOnScreen();

    QLabel *m_titleLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QProgressBar *m_progressBar = nullptr;
};

}  // namespace qodex::ui
