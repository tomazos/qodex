#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include <optional>

#include "CodexProtocol.h"

class QComboBox;
class QLabel;
class QLineEdit;

namespace qodex::ui {

class ThreadSettingsDialog final : public QDialog {
    Q_OBJECT

public:
    enum class Mode {
        Create,
        Edit,
        Fork,
    };

    struct ModelOption {
        QString model;
        QString displayName;
        bool isDefault = false;
        qodex::codex::ReasoningEffort defaultReasoningEffort = qodex::codex::ReasoningEffort::Medium;
        QList<qodex::codex::Ref<qodex::codex::ReasoningEffortOption>> supportedReasoningEfforts;
    };

    struct InstructionOption {
        QString key;
        QString name;
        bool isDefault = false;
    };

    struct Selection {
        QString threadName;
        std::optional<QString> model;
        std::optional<qodex::codex::ReasoningEffort> reasoningEffort;
        std::optional<QString> instructionKey;
    };

    explicit ThreadSettingsDialog(Mode mode, QWidget *parent = nullptr);

    void setModelOptions(const QList<ModelOption> &options);
    void setInstructionOptions(const QList<InstructionOption> &options);
    void setInitialSelection(const Selection &selection);
    void setHelperText(const QString &text);

    [[nodiscard]] Selection selection() const;

private:
    void rebuildModelCombo();
    void rebuildReasoningCombo();
    void rebuildInstructionCombo();
    [[nodiscard]] const ModelOption *selectedModelOption() const;
    [[nodiscard]] const ModelOption *modelOptionFor(const QString &model) const;
    [[nodiscard]] QString optionalModelLabel() const;
    [[nodiscard]] QString optionalInstructionLabel() const;
    [[nodiscard]] QString disabledReasoningLabel() const;
    [[nodiscard]] QString optionalReasoningLabel() const;

    Mode m_mode = Mode::Create;
    QList<ModelOption> m_modelOptions;
    QList<InstructionOption> m_instructionOptions;
    Selection m_initialSelection;
    QLineEdit *m_threadNameEdit = nullptr;
    QComboBox *m_modelCombo = nullptr;
    QComboBox *m_reasoningCombo = nullptr;
    QComboBox *m_instructionCombo = nullptr;
    QLabel *m_helperLabel = nullptr;
};

}  // namespace qodex::ui
