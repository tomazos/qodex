#include "ui/ThreadTranscriptFormatter.h"

#include <QStringList>

#include "CodexProtocol.h"

namespace qodex::ui {

namespace {

using qodex::codex::Ref;
using qodex::codex::Thread;
using qodex::codex::ThreadItem;
using qodex::codex::ThreadItemAgentMessage;
using qodex::codex::ThreadItemUserMessage;
using qodex::codex::Turn;
using qodex::codex::UserInput;
using qodex::codex::UserInputImage;
using qodex::codex::UserInputLocalImage;
using qodex::codex::UserInputMention;
using qodex::codex::UserInputSkill;
using qodex::codex::UserInputText;

QString formatUserInput(const Ref<UserInput> &input) {
    if (!input) {
        return {};
    }

    switch (input->kind) {
    case UserInput::Kind::Text:
        if (const auto *text = std::get_if<Ref<UserInputText>>(&input->payload); text != nullptr && *text) {
            return (*text)->text;
        }
        return {};
    case UserInput::Kind::Mention:
        if (const auto *mention = std::get_if<Ref<UserInputMention>>(&input->payload);
            mention != nullptr && *mention) {
            if (!(*mention)->name.isEmpty()) {
                return QStringLiteral("@%1").arg((*mention)->name);
            }
            return (*mention)->path;
        }
        return {};
    case UserInput::Kind::Skill:
        if (const auto *skill = std::get_if<Ref<UserInputSkill>>(&input->payload); skill != nullptr && *skill) {
            if (!(*skill)->name.isEmpty()) {
                return QStringLiteral("[skill: %1]").arg((*skill)->name);
            }
            return QStringLiteral("[skill]");
        }
        return {};
    case UserInput::Kind::Image:
        if (const auto *image = std::get_if<Ref<UserInputImage>>(&input->payload); image != nullptr && *image) {
            return QStringLiteral("[image: %1]").arg((*image)->url);
        }
        return {};
    case UserInput::Kind::LocalImage:
        if (const auto *image = std::get_if<Ref<UserInputLocalImage>>(&input->payload);
            image != nullptr && *image) {
            return QStringLiteral("[image: %1]").arg((*image)->path);
        }
        return {};
    }

    return {};
}

QString formatUserMessage(const Ref<ThreadItemUserMessage> &message) {
    if (!message) {
        return {};
    }

    QStringList parts;
    parts.reserve(message->content.size());
    for (const Ref<UserInput> &input : message->content) {
        const QString formatted = formatUserInput(input).trimmed();
        if (!formatted.isEmpty()) {
            parts.append(formatted);
        }
    }
    return parts.join(QStringLiteral("\n"));
}

void appendBlock(QStringList &blocks, const QString &speaker, const QString &text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    blocks.append(QStringLiteral("%1:\n%2").arg(speaker, trimmed));
}

}  // namespace

QString formatThreadTranscript(const Thread &thread) {
    QStringList blocks;

    for (const Ref<Turn> &turn : thread.turns) {
        if (!turn) {
            continue;
        }

        for (const Ref<ThreadItem> &item : turn->items) {
            if (!item) {
                continue;
            }

            switch (item->kind) {
            case ThreadItem::Kind::UserMessage:
                if (const auto *message = std::get_if<Ref<ThreadItemUserMessage>>(&item->payload);
                    message != nullptr) {
                    appendBlock(blocks, QStringLiteral("User"), formatUserMessage(*message));
                }
                break;
            case ThreadItem::Kind::AgentMessage:
                if (const auto *message = std::get_if<Ref<ThreadItemAgentMessage>>(&item->payload);
                    message != nullptr && *message) {
                    appendBlock(blocks, QStringLiteral("Agent"), (*message)->text);
                }
                break;
            default:
                break;
            }
        }
    }

    if (blocks.isEmpty()) {
        return QStringLiteral("No user or agent messages are available for this thread.");
    }

    return blocks.join(QStringLiteral("\n\n"));
}

}  // namespace qodex::ui
