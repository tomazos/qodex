#include "ui/ThreadTranscriptPane.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace qodex::ui {

namespace {

class TranscriptPage final : public QWebEnginePage {
public:
    explicit TranscriptPage(QObject *parent = nullptr)
        : QWebEnginePage(parent) {
    }

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override {
        if (type == QWebEnginePage::NavigationTypeLinkClicked) {
            QDesktopServices::openUrl(url);
            return false;
        }
        return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
    }
};

}  // namespace

ThreadTranscriptPane::ThreadTranscriptPane(QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_view = new QWebEngineView(this);
    m_view->setPage(new TranscriptPage(m_view));
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, true);
    m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    layout->addWidget(m_view, 1);
}

void ThreadTranscriptPane::setTranscriptHtml(const QString &html) {
    if (m_view == nullptr || html.isEmpty()) {
        return;
    }

    delete m_tempFile;
    m_tempFile = new QTemporaryFile(
        QDir::tempPath() + QDir::separator() + QStringLiteral("qodex-transcript-XXXXXX.html"),
        this
    );
    m_tempFile->setAutoRemove(true);
    if (!m_tempFile->open()) {
        return;
    }

    m_tempFile->write(html.toUtf8());
    m_tempFile->flush();
    m_view->load(QUrl::fromLocalFile(QFileInfo(*m_tempFile).absoluteFilePath()));
}

}  // namespace qodex::ui
