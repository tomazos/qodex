#include "ui/InstructionEditorPane.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include "domain/InstructionCatalog.h"

namespace qodex::ui {

namespace {

QFont monospaceFont() {
    QFont font(QStringLiteral("monospace"));
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    return font;
}

}  // namespace

InstructionEditorPane::InstructionEditorPane(qodex::domain::InstructionCatalog *catalog, QWidget *parent)
    : QWidget(parent),
      m_catalog(catalog) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerLayout->addWidget(m_titleLabel, 1);

    m_modeLabel = new QLabel(this);
    m_modeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerLayout->addWidget(m_modeLabel, 0, Qt::AlignRight);

    layout->addLayout(headerLayout);

    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_textEdit->setFont(monospaceFont());
    layout->addWidget(m_textEdit, 1);

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(300);

    connect(m_saveTimer, &QTimer::timeout, this, &InstructionEditorPane::savePendingChanges);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loadingDocument || m_textEdit == nullptr || m_textEdit->isReadOnly()) {
            return;
        }

        m_hasPendingChanges = true;
        m_saveTimer->start();
    });

    m_titleLabel->setText(QStringLiteral("No instruction selected."));
    m_modeLabel->clear();
    m_textEdit->setReadOnly(true);
}

void InstructionEditorPane::viewInstruction(const QString &instructionKey) {
    openInstruction(instructionKey, Mode::View);
}

void InstructionEditorPane::editInstruction(const QString &instructionKey) {
    openInstruction(instructionKey, Mode::Edit);
}

void InstructionEditorPane::refreshIfCurrentInstruction(const QString &instructionKey) {
    if (instructionKey.isEmpty() || instructionKey != m_currentInstructionKey || m_catalog == nullptr) {
        return;
    }

    QString errorMessage;
    const auto document = m_catalog->instructionByKey(instructionKey, &errorMessage);
    if (!document.has_value()) {
        return;
    }

    m_currentInstructionName = document->name;
    m_currentInstructionEditable = document->isEditable;
    updateHeader();
}

bool InstructionEditorPane::savePendingChanges() {
    if (!m_hasPendingChanges || m_catalog == nullptr || m_currentInstructionKey.isEmpty() || m_textEdit == nullptr ||
        m_textEdit->isReadOnly()) {
        return true;
    }

    QString errorMessage;
    if (!m_catalog->updateInstructionContent(m_currentInstructionKey, m_textEdit->toPlainText(), &errorMessage)) {
        QMessageBox::warning(
            this,
            QStringLiteral("Save Instruction"),
            errorMessage.isEmpty() ? QStringLiteral("Failed to save instruction.") : errorMessage
        );
        return false;
    }

    m_hasPendingChanges = false;
    return true;
}

void InstructionEditorPane::openInstruction(const QString &instructionKey, const Mode mode) {
    if (instructionKey.isEmpty() || m_catalog == nullptr) {
        return;
    }

    if (!savePendingChanges()) {
        return;
    }

    QString errorMessage;
    const auto document = m_catalog->instructionByKey(instructionKey, &errorMessage);
    if (!document.has_value()) {
        QMessageBox::warning(
            this,
            QStringLiteral("Open Instruction"),
            errorMessage.isEmpty() ? QStringLiteral("Failed to load instruction.") : errorMessage
        );
        return;
    }

    applyDocument(*document, mode);
}

void InstructionEditorPane::applyDocument(const qodex::domain::InstructionDocument &document, const Mode mode) {
    m_loadingDocument = true;
    m_currentInstructionKey = document.key;
    m_currentInstructionName = document.name;
    m_currentInstructionEditable = document.isEditable;
    m_mode = mode;
    m_hasPendingChanges = false;
    if (m_saveTimer != nullptr) {
        m_saveTimer->stop();
    }

    updateHeader();
    m_textEdit->setReadOnly(mode != Mode::Edit || !document.isEditable);
    m_textEdit->setPlainText(document.content);
    m_loadingDocument = false;
}

void InstructionEditorPane::updateHeader() {
    if (m_titleLabel != nullptr) {
        m_titleLabel->setText(
            m_currentInstructionName.isEmpty() ? QStringLiteral("No instruction selected.") : m_currentInstructionName
        );
    }

    if (m_modeLabel != nullptr) {
        if (m_currentInstructionName.isEmpty()) {
            m_modeLabel->clear();
        } else if (m_mode == Mode::Edit && m_currentInstructionEditable) {
            m_modeLabel->setText(QStringLiteral("Editing"));
        } else {
            m_modeLabel->setText(QStringLiteral("Read-only"));
        }
    }
}

}  // namespace qodex::ui
