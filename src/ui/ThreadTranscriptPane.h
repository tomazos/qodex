#pragma once

#include <QWidget>

class QPlainTextEdit;

namespace qodex::ui {

class ThreadTranscriptPane final : public QWidget {
    Q_OBJECT

public:
    explicit ThreadTranscriptPane(QWidget *parent = nullptr);

    void setTranscriptText(const QString &text);

private:
    QPlainTextEdit *m_textEdit = nullptr;
};

}  // namespace qodex::ui
