#include "MainWindow.h"

#include <QLabel>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow() {
    setWindowTitle("qodex");
    resize(960, 640);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    auto *headline = new QLabel("qodex", central);
    headline->setObjectName("headline");

    auto *body = new QLabel(
        "Bare-bones Qt shell is running.\n\n"
        "Next step: replace this placeholder with a Codex thread browser and transcript view.",
        central
    );
    body->setWordWrap(true);

    layout->addWidget(headline);
    layout->addWidget(body);
    layout->addStretch(1);

    central->setLayout(layout);
    setCentralWidget(central);
    statusBar()->showMessage("Ready");

    setStyleSheet(
        "QMainWindow { background: #f4efe6; }"
        "QLabel#headline { font-size: 28px; font-weight: 700; color: #1f1b16; }"
        "QLabel { color: #1f1b16; font-size: 16px; }"
    );
}
