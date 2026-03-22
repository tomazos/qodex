#include "ui/ThreadTranscriptFormatter.h"

#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QUrl>

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

QString katexCssUrl() {
    return QUrl::fromLocalFile(QStringLiteral("/usr/share/javascript/katex/katex.min.css")).toString();
}

QString katexScriptUrl() {
    return QUrl::fromLocalFile(QStringLiteral("/usr/share/javascript/katex/katex.min.js")).toString();
}

QString katexAutoRenderUrl() {
    return QUrl::fromLocalFile(QStringLiteral("/usr/share/javascript/katex/contrib/auto-render.js")).toString();
}

QString escapeMarkdownSource(const QString &text) {
    return text.toHtmlEscaped();
}

QString renderMarkdownFragment(const QString &markdown) {
    return QTextDocumentFragment::fromMarkdown(
               escapeMarkdownSource(markdown),
               QTextDocument::MarkdownDialectGitHub
    )
        .toHtml();
}

QString escapeAttribute(const QString &text) {
    QString escaped = text.toHtmlEscaped();
    escaped.replace(u'"', QStringLiteral("&quot;"));
    return escaped;
}

QString imageHtml(const QString &url, const QString &label) {
    const QString safeUrl = escapeAttribute(url);
    const QString safeLabel = label.toHtmlEscaped();
    return QStringLiteral(
               "<figure class=\"input-image\">"
               "<a href=\"%1\"><img src=\"%1\" alt=\"%2\"></a>"
               "<figcaption><a href=\"%1\">%2</a></figcaption>"
               "</figure>"
           )
        .arg(safeUrl, safeLabel);
}

QString linkHtml(const QString &url, const QString &label, const QString &className = QString()) {
    const QString safeUrl = escapeAttribute(url);
    const QString safeLabel = label.toHtmlEscaped();
    const QString classAttribute = className.isEmpty()
        ? QString()
        : QStringLiteral(" class=\"%1\"").arg(className.toHtmlEscaped());
    return QStringLiteral("<p><a%1 href=\"%2\">%3</a></p>").arg(classAttribute, safeUrl, safeLabel);
}

QString renderUserInput(const Ref<UserInput> &input) {
    if (!input) {
        return {};
    }

    switch (input->kind) {
    case UserInput::Kind::Text:
        if (const auto *text = std::get_if<Ref<UserInputText>>(&input->payload); text != nullptr && *text) {
            return renderMarkdownFragment((*text)->text);
        }
        return {};
    case UserInput::Kind::Mention:
        if (const auto *mention = std::get_if<Ref<UserInputMention>>(&input->payload);
            mention != nullptr && *mention) {
            if (!(*mention)->path.isEmpty()) {
                return linkHtml(
                    QUrl::fromLocalFile((*mention)->path).toString(),
                    !(*mention)->name.isEmpty() ? QStringLiteral("@%1").arg((*mention)->name) : (*mention)->path,
                    QStringLiteral("inline-tag")
                );
            }
            if (!(*mention)->name.isEmpty()) {
                return renderMarkdownFragment(QStringLiteral("`@%1`").arg((*mention)->name));
            }
            return {};
        }
        return {};
    case UserInput::Kind::Skill:
        if (const auto *skill = std::get_if<Ref<UserInputSkill>>(&input->payload); skill != nullptr && *skill) {
            const QString label = !(*skill)->name.isEmpty() ? QStringLiteral("skill: %1").arg((*skill)->name)
                                                            : QStringLiteral("skill");
            if (!(*skill)->path.isEmpty()) {
                return linkHtml(
                    QUrl::fromLocalFile((*skill)->path).toString(),
                    label,
                    QStringLiteral("inline-tag")
                );
            }
            return renderMarkdownFragment(QStringLiteral("`[%1]`").arg(label));
        }
        return {};
    case UserInput::Kind::Image:
        if (const auto *image = std::get_if<Ref<UserInputImage>>(&input->payload); image != nullptr && *image) {
            return imageHtml((*image)->url, (*image)->url);
        }
        return {};
    case UserInput::Kind::LocalImage:
        if (const auto *image = std::get_if<Ref<UserInputLocalImage>>(&input->payload);
            image != nullptr && *image) {
            const QString url = QUrl::fromLocalFile((*image)->path).toString();
            return imageHtml(url, (*image)->path);
        }
        return {};
    }

    return {};
}

QString formatUserMessageHtml(const Ref<ThreadItemUserMessage> &message) {
    if (!message) {
        return {};
    }

    QStringList parts;
    for (const Ref<UserInput> &input : message->content) {
        const QString formatted = renderUserInput(input).trimmed();
        if (!formatted.isEmpty()) {
            parts.append(formatted);
        }
    }
    return parts.join(QStringLiteral("\n"));
}

void appendBlock(QStringList &blocks, const QString &speaker, const QString &html, const QString &roleClass) {
    const QString trimmed = html.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    blocks.append(QStringLiteral(
                      "<article class=\"message %1\">"
                      "<h2 class=\"speaker\">%2</h2>"
                      "<div class=\"message-markdown\">%3</div>"
                      "</article>"
                  )
                      .arg(roleClass, speaker.toHtmlEscaped(), trimmed));
}

QString documentShell(const QString &bodyHtml) {
    return QStringLiteral(R"(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="%1">
  <style>
    :root {
      color-scheme: light dark;
      --bg: #f5f6f8;
      --fg: #16181d;
      --muted: #59606f;
      --line: #d7dbe3;
      --panel: #eef1f5;
      --user: #e7f2ff;
      --agent: #ffffff;
      --mono: "JetBrains Mono", "Cascadia Mono", "Ubuntu Sans Mono", "Noto Sans Mono", monospace;
      --sans: "Noto Sans", "Ubuntu Sans", "Segoe UI", sans-serif;
    }
    html, body {
      margin: 0;
      padding: 0;
      background: var(--bg);
      color: var(--fg);
      font-family: var(--sans);
      line-height: 1.55;
    }
    body {
      padding: 18px;
    }
    .transcript {
      max-width: 980px;
      margin: 0 auto;
    }
    .message {
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 14px 16px;
      margin: 0 0 14px;
      box-shadow: 0 1px 2px rgba(0, 0, 0, 0.04);
    }
    .message.user { background: var(--user); }
    .message.agent { background: var(--agent); }
    .speaker {
      margin: 0 0 10px;
      font-size: 0.82rem;
      letter-spacing: 0.06em;
      text-transform: uppercase;
      color: var(--muted);
    }
    .message-markdown :first-child { margin-top: 0; }
    .message-markdown :last-child { margin-bottom: 0; }
    .message-markdown pre,
    .message-markdown code {
      font-family: var(--mono);
    }
    .message-markdown pre {
      overflow-x: auto;
      padding: 12px 14px;
      border-radius: 8px;
      border: 1px solid var(--line);
      background: #edf0f4;
    }
    .message-markdown code {
      padding: 0.08em 0.3em;
      border-radius: 4px;
      background: #edf0f4;
    }
    .message-markdown pre code {
      padding: 0;
      background: transparent;
    }
    .message-markdown a {
      color: #0a66c2;
      text-decoration-thickness: 1px;
      text-underline-offset: 0.12em;
    }
    .message-markdown img {
      display: block;
      max-width: min(100%%, 960px);
      height: auto;
      margin: 0.75em 0;
      border: 1px solid var(--line);
      border-radius: 8px;
      background: #ffffff;
    }
    .message-markdown figure {
      margin: 0.75em 0;
    }
    .message-markdown figcaption {
      margin-top: 0.3em;
      font-size: 0.88rem;
      color: var(--muted);
      overflow-wrap: anywhere;
    }
    .message-markdown .inline-tag {
      font-family: var(--mono);
    }
    .message-markdown .katex {
      font-size: 1.04em;
    }
    .message-markdown .katex-display {
      margin: 0.9em 0;
      overflow-x: auto;
      overflow-y: hidden;
      padding: 0.12em 0;
    }
    .empty-state {
      border: 1px dashed var(--line);
      border-radius: 12px;
      padding: 18px;
      color: var(--muted);
      background: var(--panel);
    }
  </style>
  <script defer src="%2"></script>
  <script defer src="%3"></script>
  <script>
    function renderTranscriptMath() {
      if (typeof renderMathInElement !== 'function') {
        return;
      }
      renderMathInElement(document.body, {
        delimiters: [
          {left: '$$', right: '$$', display: true},
          {left: '\\\\[', right: '\\\\]', display: true},
          {left: '$', right: '$', display: false},
          {left: '\\\\(', right: '\\\\)', display: false}
        ],
        throwOnError: false
      });
    }
    document.addEventListener('DOMContentLoaded', renderTranscriptMath);
    window.addEventListener('load', renderTranscriptMath, { once: true });
  </script>
  <style>
    @media (prefers-color-scheme: dark) {
      :root {
        --bg: #16181d;
        --fg: #f4f6f8;
        --muted: #b8c0ce;
        --line: #363b46;
        --panel: #20242c;
        --user: #1a2a40;
        --agent: #20242c;
      }
    }
  </style>
</head>
<body>
  <main class="transcript">
    %4
  </main>
</body>
</html>)")
        .arg(katexCssUrl(), katexScriptUrl(), katexAutoRenderUrl(), bodyHtml);
}

}  // namespace

QString formatThreadTranscriptHtml(const Thread &thread) {
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
                    appendBlock(blocks, QStringLiteral("User"), formatUserMessageHtml(*message), QStringLiteral("user"));
                }
                break;
            case ThreadItem::Kind::AgentMessage:
                if (const auto *message = std::get_if<Ref<ThreadItemAgentMessage>>(&item->payload);
                    message != nullptr && *message) {
                    appendBlock(
                        blocks,
                        QStringLiteral("Agent"),
                        renderMarkdownFragment((*message)->text),
                        QStringLiteral("agent")
                    );
                }
                break;
            default:
                break;
            }
        }
    }

    if (blocks.isEmpty()) {
        return documentShell(
            QStringLiteral("<section class=\"empty-state\">No user or agent messages are available for this thread.</section>")
        );
    }

    return documentShell(blocks.join(QStringLiteral("\n")));
}

}  // namespace qodex::ui
