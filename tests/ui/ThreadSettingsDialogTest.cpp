#include <QtTest>

#include "CodexProtocol.h"
#include "ui/ThreadSettingsDialog.h"

namespace {

using qodex::codex::ReasoningEffort;
using qodex::codex::ReasoningEffortOption;
using qodex::codex::Ref;
using qodex::ui::ThreadSettingsDialog;

Ref<ReasoningEffortOption> reasoningOption(const ReasoningEffort effort) {
    Ref<ReasoningEffortOption> option = Ref<ReasoningEffortOption>::create();
    option->reasoningEffort = effort;
    return option;
}

ThreadSettingsDialog::ModelOption modelOption(
    const QString &model,
    const ReasoningEffort defaultReasoningEffort,
    const QList<ReasoningEffort> &supportedReasoningEfforts
) {
    ThreadSettingsDialog::ModelOption option;
    option.model = model;
    option.displayName = model;
    option.isDefault = true;
    option.defaultReasoningEffort = defaultReasoningEffort;
    for (const ReasoningEffort reasoningEffort : supportedReasoningEfforts) {
        option.supportedReasoningEfforts.append(reasoningOption(reasoningEffort));
    }
    return option;
}

}  // namespace

class ThreadSettingsDialogTest final : public QObject {
    Q_OBJECT

private slots:
    void initialSelectionOverridesReasoningComboDefault();
};

void ThreadSettingsDialogTest::initialSelectionOverridesReasoningComboDefault() {
    ThreadSettingsDialog dialog(ThreadSettingsDialog::Mode::Create);
    dialog.setModelOptions({
        modelOption(
            QStringLiteral("gpt-5.5"),
            ReasoningEffort::Medium,
            {ReasoningEffort::Medium, ReasoningEffort::Xhigh}
        ),
    });
    dialog.setInstructionOptions({
        ThreadSettingsDialog::InstructionOption{
            .key = QStringLiteral("codex-default"),
            .name = QStringLiteral("Codex Default"),
            .isDefault = true,
        },
    });

    ThreadSettingsDialog::Selection initialSelection;
    initialSelection.workingDirectory = QStringLiteral("/tmp");
    initialSelection.model = QStringLiteral("gpt-5.5");
    initialSelection.reasoningEffort = ReasoningEffort::Xhigh;
    initialSelection.instructionKey = QStringLiteral("codex-default");

    dialog.setInitialSelection(initialSelection);

    const ThreadSettingsDialog::Selection selection = dialog.selection();
    QCOMPARE(selection.model, QStringLiteral("gpt-5.5"));
    QVERIFY(selection.reasoningEffort == ReasoningEffort::Xhigh);
}

QTEST_MAIN(ThreadSettingsDialogTest)

#include "ThreadSettingsDialogTest.moc"
