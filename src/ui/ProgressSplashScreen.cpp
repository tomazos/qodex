#include "ui/ProgressSplashScreen.h"

#include <algorithm>

#include <QApplication>
#include <QGuiApplication>
#include <QLabel>
#include <QProgressBar>
#include <QScreen>
#include <QVBoxLayout>

namespace qodex::ui {

ProgressSplashScreen::ProgressSplashScreen(const QString &title, QWidget *parent)
    : QWidget(
          parent,
          Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
      ) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowModality(Qt::ApplicationModal);
    setObjectName(QStringLiteral("progressSplashScreen"));
    setMinimumWidth(440);
    setStyleSheet(QStringLiteral(
        "#progressSplashScreen {"
        "  background: palette(base);"
        "  border: 1px solid palette(mid);"
        "  border-radius: 14px;"
        "}"
    ));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(12);

    m_titleLabel = new QLabel(title, this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() + 2.0);
    m_titleLabel->setFont(titleFont);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setTextVisible(false);

    layout->addWidget(m_titleLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_progressBar);
}

void ProgressSplashScreen::setStatus(const QString &message, const int progress) {
    m_statusLabel->setText(message);
    m_progressBar->setValue(std::clamp(progress, 0, 100));
    QApplication::processEvents();
}

void ProgressSplashScreen::showCentered() {
    adjustSize();
    centerOnScreen();
    show();
    QApplication::processEvents();
}

void ProgressSplashScreen::centerOnScreen() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        return;
    }

    const QRect availableGeometry = screen->availableGeometry();
    move(
        availableGeometry.center().x() - width() / 2,
        availableGeometry.center().y() - height() / 2
    );
}

}  // namespace qodex::ui
