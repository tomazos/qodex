#pragma once

#include <QUrl>
#include <QWidget>

class QWebEngineView;

namespace qodex::ui {

class ThreadTranscriptPane final : public QWidget {
    Q_OBJECT

public:
    explicit ThreadTranscriptPane(QWidget *parent = nullptr);

    void setTranscriptHtml(const QString &html, const QUrl &baseUrl);

private:
    QWebEngineView *m_view = nullptr;
};

}  // namespace qodex::ui
