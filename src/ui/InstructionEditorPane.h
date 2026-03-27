#pragma once

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QTimer;

namespace qodex::domain {
class InstructionCatalog;
struct InstructionDocument;
}

namespace qodex::ui {

class InstructionEditorPane final : public QWidget {
    Q_OBJECT

public:
    explicit InstructionEditorPane(qodex::domain::InstructionCatalog *catalog, QWidget *parent = nullptr);

    void viewInstruction(const QString &instructionKey);
    void editInstruction(const QString &instructionKey);
    void refreshIfCurrentInstruction(const QString &instructionKey);

private slots:
    bool savePendingChanges();

private:
    enum class Mode {
        View,
        Edit,
    };

    void openInstruction(const QString &instructionKey, Mode mode);
    void applyDocument(const qodex::domain::InstructionDocument &document, Mode mode);
    void updateHeader();

    qodex::domain::InstructionCatalog *m_catalog = nullptr;
    QString m_currentInstructionKey;
    QString m_currentInstructionName;
    bool m_currentInstructionEditable = false;
    Mode m_mode = Mode::View;
    bool m_loadingDocument = false;
    bool m_hasPendingChanges = false;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QPlainTextEdit *m_textEdit = nullptr;
    QTimer *m_saveTimer = nullptr;
};

}  // namespace qodex::ui
