#include "ui/ThreadSettingsDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace qodex::ui {

namespace {

QString dialogWindowTitle(const ThreadSettingsDialog::Mode mode) {
    switch (mode) {
    case ThreadSettingsDialog::Mode::Create:
        return QStringLiteral("Create New Thread");
    case ThreadSettingsDialog::Mode::Edit:
        return QStringLiteral("Edit Thread Settings");
    case ThreadSettingsDialog::Mode::Fork:
        return QStringLiteral("Fork Thread");
    }

    return QStringLiteral("Thread Settings");
}

QString acceptButtonText(const ThreadSettingsDialog::Mode mode) {
    switch (mode) {
    case ThreadSettingsDialog::Mode::Create:
        return QStringLiteral("Create Thread");
    case ThreadSettingsDialog::Mode::Edit:
        return QStringLiteral("Apply");
    case ThreadSettingsDialog::Mode::Fork:
        return QStringLiteral("Fork Thread");
    }

    return QStringLiteral("OK");
}

QString humanizeReasoningEffort(const qodex::codex::ReasoningEffort effort) {
    using qodex::codex::ReasoningEffort;

    switch (effort) {
    case ReasoningEffort::NoneValue:
        return QStringLiteral("None");
    case ReasoningEffort::Minimal:
        return QStringLiteral("Minimal");
    case ReasoningEffort::Low:
        return QStringLiteral("Low");
    case ReasoningEffort::Medium:
        return QStringLiteral("Medium");
    case ReasoningEffort::High:
        return QStringLiteral("High");
    case ReasoningEffort::Xhigh:
        return QStringLiteral("XHigh");
    }

    return QStringLiteral("Unknown");
}

QString modelDisplayLabel(const ThreadSettingsDialog::ModelOption &option) {
    const QString displayName = option.displayName.trimmed();
    if (!displayName.isEmpty()) {
        return option.isDefault ? QStringLiteral("%1 (Default)").arg(displayName) : displayName;
    }

    return option.isDefault ? QStringLiteral("%1 (Default)").arg(option.model) : option.model;
}

int indexForComboData(const QComboBox *combo, const QVariant &data) {
    if (combo == nullptr) {
        return -1;
    }

    for (int index = 0; index < combo->count(); ++index) {
        if (combo->itemData(index) == data) {
            return index;
        }
    }

    return -1;
}

}  // namespace

ThreadSettingsDialog::ThreadSettingsDialog(const Mode mode, QWidget *parent)
    : QDialog(parent),
      m_mode(mode) {
    setWindowTitle(dialogWindowTitle(mode));
    setModal(true);
    resize(760, 360);
    setMinimumSize(680, 320);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);

    m_helperLabel = new QLabel(this);
    m_helperLabel->setWordWrap(true);
    layout->addWidget(m_helperLabel);

    auto *formLayout = new QFormLayout();
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    formLayout->setHorizontalSpacing(16);
    formLayout->setVerticalSpacing(12);

    m_threadNameEdit = new QLineEdit(this);
    switch (m_mode) {
    case Mode::Create:
        m_threadNameEdit->setPlaceholderText(QStringLiteral("Leave blank to use the thread's default title"));
        break;
    case Mode::Edit:
        m_threadNameEdit->setPlaceholderText(QStringLiteral("Leave blank to clear the stored thread name"));
        break;
    case Mode::Fork:
        m_threadNameEdit->setPlaceholderText(QStringLiteral("Leave blank to use the forked thread's default title"));
        break;
    }
    formLayout->addRow(QStringLiteral("Thread name"), m_threadNameEdit);

    m_modelCombo = new QComboBox(this);
    formLayout->addRow(QStringLiteral("Model"), m_modelCombo);

    m_reasoningCombo = new QComboBox(this);
    formLayout->addRow(QStringLiteral("Reasoning effort"), m_reasoningCombo);

    m_instructionCombo = new QComboBox(this);
    formLayout->addRow(QStringLiteral("Instructions"), m_instructionCombo);

    layout->addLayout(formLayout, 1);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    QPushButton *acceptButton = buttonBox->addButton(acceptButtonText(mode), QDialogButtonBox::AcceptRole);
    acceptButton->setDefault(true);
    layout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, [this](int) { rebuildReasoningCombo(); });

    switch (m_mode) {
    case Mode::Create:
        setHelperText(QStringLiteral("Create a new thread with the selected model, reasoning effort, and base instructions."));
        break;
    case Mode::Edit:
        setHelperText(
            QStringLiteral(
                "Leave any setting on \"Leave unchanged\" to preserve the thread's current value. Non-name settings only apply when the thread is not loaded."
            )
        );
        break;
    case Mode::Fork:
        setHelperText(
            QStringLiteral(
                "Leave any setting on \"Inherit from source thread\" to copy it from the source thread."
            )
        );
        break;
    }
}

void ThreadSettingsDialog::setModelOptions(const QList<ModelOption> &options) {
    m_modelOptions = options;
    rebuildModelCombo();
    rebuildReasoningCombo();
}

void ThreadSettingsDialog::setInstructionOptions(const QList<InstructionOption> &options) {
    m_instructionOptions = options;
    rebuildInstructionCombo();
}

void ThreadSettingsDialog::setInitialSelection(const Selection &selection) {
    m_initialSelection = selection;
    if (m_threadNameEdit != nullptr) {
        m_threadNameEdit->setText(selection.threadName);
    }
    rebuildModelCombo();
    rebuildReasoningCombo();
    rebuildInstructionCombo();
}

void ThreadSettingsDialog::setHelperText(const QString &text) {
    if (m_helperLabel != nullptr) {
        m_helperLabel->setText(text);
        m_helperLabel->setVisible(!text.trimmed().isEmpty());
    }
}

ThreadSettingsDialog::Selection ThreadSettingsDialog::selection() const {
    Selection value;
    if (m_threadNameEdit != nullptr) {
        value.threadName = m_threadNameEdit->text();
    }

    if (m_modelCombo != nullptr && m_modelCombo->currentData().isValid()) {
        value.model = m_modelCombo->currentData().toString();
    }

    if (m_reasoningCombo != nullptr && m_reasoningCombo->currentData().isValid()) {
        value.reasoningEffort =
            static_cast<qodex::codex::ReasoningEffort>(m_reasoningCombo->currentData().toInt());
    }

    if (m_instructionCombo != nullptr && m_instructionCombo->currentData().isValid()) {
        value.instructionKey = m_instructionCombo->currentData().toString();
    }

    return value;
}

void ThreadSettingsDialog::rebuildModelCombo() {
    if (m_modelCombo == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_modelCombo);
    const QVariant selectedModelData = m_initialSelection.model.has_value()
        ? QVariant::fromValue(m_initialSelection.model.value())
        : QVariant{};
    m_modelCombo->clear();

    if (m_mode != Mode::Create) {
        m_modelCombo->addItem(optionalModelLabel(), QVariant{});
    }

    int defaultIndex = -1;
    for (const ModelOption &option : m_modelOptions) {
        m_modelCombo->addItem(modelDisplayLabel(option), option.model);
        if (option.isDefault && defaultIndex < 0) {
            defaultIndex = m_modelCombo->count() - 1;
        }
    }

    int selectedIndex = indexForComboData(m_modelCombo, selectedModelData);
    if (selectedIndex < 0) {
        if (m_mode == Mode::Create) {
            selectedIndex = defaultIndex >= 0 ? defaultIndex : (m_modelCombo->count() > 0 ? 0 : -1);
        } else {
            selectedIndex = 0;
        }
    }

    if (selectedIndex >= 0) {
        m_modelCombo->setCurrentIndex(selectedIndex);
    }
}

void ThreadSettingsDialog::rebuildReasoningCombo() {
    if (m_reasoningCombo == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_reasoningCombo);
    m_reasoningCombo->clear();

    const ModelOption *modelOption = selectedModelOption();
    if (modelOption == nullptr) {
        m_reasoningCombo->addItem(disabledReasoningLabel(), QVariant{});
        m_reasoningCombo->setEnabled(false);
        return;
    }

    m_reasoningCombo->setEnabled(true);
    if (m_mode != Mode::Create) {
        m_reasoningCombo->addItem(optionalReasoningLabel(), QVariant{});
    }

    int defaultIndex = -1;
    for (const qodex::codex::Ref<qodex::codex::ReasoningEffortOption> &option : modelOption->supportedReasoningEfforts) {
        if (!option) {
            continue;
        }

        m_reasoningCombo->addItem(
            humanizeReasoningEffort(option->reasoningEffort),
            static_cast<int>(option->reasoningEffort)
        );
        if (option->reasoningEffort == modelOption->defaultReasoningEffort && defaultIndex < 0) {
            defaultIndex = m_reasoningCombo->count() - 1;
        }

        if (!option->description.trimmed().isEmpty()) {
            m_reasoningCombo->setItemData(
                m_reasoningCombo->count() - 1,
                option->description.trimmed(),
                Qt::ToolTipRole
            );
        }
    }

    if (m_initialSelection.reasoningEffort.has_value()) {
        const int selectedIndex = indexForComboData(
            m_reasoningCombo,
            static_cast<int>(m_initialSelection.reasoningEffort.value())
        );
        if (selectedIndex >= 0) {
            m_reasoningCombo->setCurrentIndex(selectedIndex);
            return;
        }
    }

    if (m_mode == Mode::Create) {
        if (defaultIndex >= 0) {
            m_reasoningCombo->setCurrentIndex(defaultIndex);
        } else if (m_reasoningCombo->count() > 0) {
            m_reasoningCombo->setCurrentIndex(0);
        }
        return;
    }

    m_reasoningCombo->setCurrentIndex(0);
}

void ThreadSettingsDialog::rebuildInstructionCombo() {
    if (m_instructionCombo == nullptr) {
        return;
    }

    const QSignalBlocker blocker(m_instructionCombo);
    const QVariant selectedInstructionData = m_initialSelection.instructionKey.has_value()
        ? QVariant::fromValue(m_initialSelection.instructionKey.value())
        : QVariant{};
    m_instructionCombo->clear();

    if (m_mode != Mode::Create) {
        m_instructionCombo->addItem(optionalInstructionLabel(), QVariant{});
    }

    int defaultIndex = -1;
    for (const InstructionOption &option : m_instructionOptions) {
        m_instructionCombo->addItem(option.name, option.key);
        if (option.isDefault && defaultIndex < 0) {
            defaultIndex = m_instructionCombo->count() - 1;
        }
    }

    int selectedIndex = indexForComboData(m_instructionCombo, selectedInstructionData);
    if (selectedIndex < 0) {
        if (m_mode == Mode::Create) {
            selectedIndex = defaultIndex >= 0 ? defaultIndex : (m_instructionCombo->count() > 0 ? 0 : -1);
        } else {
            selectedIndex = 0;
        }
    }

    if (selectedIndex >= 0) {
        m_instructionCombo->setCurrentIndex(selectedIndex);
    }
}

const ThreadSettingsDialog::ModelOption *ThreadSettingsDialog::selectedModelOption() const {
    if (m_modelCombo == nullptr || !m_modelCombo->currentData().isValid()) {
        return nullptr;
    }

    return modelOptionFor(m_modelCombo->currentData().toString());
}

const ThreadSettingsDialog::ModelOption *ThreadSettingsDialog::modelOptionFor(const QString &model) const {
    for (const ModelOption &option : m_modelOptions) {
        if (option.model == model) {
            return &option;
        }
    }

    return nullptr;
}

QString ThreadSettingsDialog::optionalModelLabel() const {
    switch (m_mode) {
    case Mode::Edit:
        return QStringLiteral("Leave unchanged");
    case Mode::Fork:
        return QStringLiteral("Inherit from source thread");
    case Mode::Create:
        break;
    }

    return QStringLiteral("Select a model");
}

QString ThreadSettingsDialog::optionalInstructionLabel() const {
    switch (m_mode) {
    case Mode::Edit:
        return QStringLiteral("Leave unchanged");
    case Mode::Fork:
        return QStringLiteral("Inherit from source thread");
    case Mode::Create:
        break;
    }

    return QStringLiteral("Select instructions");
}

QString ThreadSettingsDialog::disabledReasoningLabel() const {
    switch (m_mode) {
    case Mode::Edit:
        return QStringLiteral("Leave unchanged");
    case Mode::Fork:
        return QStringLiteral("Inherit from source thread");
    case Mode::Create:
        break;
    }

    return QStringLiteral("Select a model first");
}

QString ThreadSettingsDialog::optionalReasoningLabel() const {
    if (m_mode == Mode::Fork) {
        return QStringLiteral("Use model default");
    }
    if (m_mode == Mode::Edit) {
        return QStringLiteral("Use model default");
    }

    return QStringLiteral("Use model default");
}

}  // namespace qodex::ui
