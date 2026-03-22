#include "ui/ThreadTranscriptPane.h"

#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace qodex::ui {

ThreadTranscriptPane::ThreadTranscriptPane(QWidget *parent)
    : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setUndoRedoEnabled(false);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEdit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_textEdit, 1);
}

void ThreadTranscriptPane::setTranscriptText(const QString &text) {
    if (m_textEdit == nullptr) {
        return;
    }

    m_textEdit->setPlainText(text);
    m_textEdit->moveCursor(QTextCursor::Start);
}

}  // namespace qodex::ui
