#pragma once

#include <QWidget>

class QTemporaryFile;
class QWebEngineView;

namespace qodex::ui {

class ThreadTranscriptPane final : public QWidget {
    Q_OBJECT

public:
    explicit ThreadTranscriptPane(QWidget *parent = nullptr);

    void setTranscriptHtml(const QString &html);

private:
    QTemporaryFile *m_tempFile = nullptr;
    QWebEngineView *m_view = nullptr;
};

}  // namespace qodex::ui
