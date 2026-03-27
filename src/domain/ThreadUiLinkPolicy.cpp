#include "domain/ThreadUiLinkPolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

namespace qodex::domain {

namespace {

using qodex::threadui::ipc::common::LINK_ACTION_KIND_NONE;
using qodex::threadui::ipc::common::LINK_ACTION_KIND_OPEN;
using qodex::threadui::ipc::common::LINK_ACTION_KIND_OPEN_EXTERNALLY;
using qodex::threadui::ipc::common::LINK_KIND_FILE;
using qodex::threadui::ipc::common::LINK_KIND_MAILTO;
using qodex::threadui::ipc::common::LINK_KIND_UNKNOWN;
using qodex::threadui::ipc::common::LINK_KIND_WEB;
using qodex::threadui::ipc::common::ResolvedLink;

struct ParsedLocationFragment {
    bool hasLine = false;
    qint64 line = 0;
    bool hasColumn = false;
    qint64 column = 0;
};

QString fileLocationDisplay(const QString &path, const ParsedLocationFragment &location) {
    QString display = path;
    if (location.hasLine) {
        display += QStringLiteral(":%1").arg(location.line);
        if (location.hasColumn) {
            display += QStringLiteral(":%1").arg(location.column);
        }
    }
    return display;
}

QString normalizeUrlString(const QUrl &url) {
    return url.adjusted(QUrl::NormalizePathSegments).toString(QUrl::FullyEncoded);
}

ParsedLocationFragment parseLocationFragment(const QString &fragment) {
    static const QRegularExpression lineColumnPattern(QStringLiteral("^L(\\d+)(?:C(\\d+))?$"));

    ParsedLocationFragment parsed;
    const QRegularExpressionMatch match = lineColumnPattern.match(fragment.trimmed());
    if (!match.hasMatch()) {
        return parsed;
    }

    bool ok = false;
    const qint64 line = match.captured(1).toLongLong(&ok);
    if (!ok || line <= 0) {
        return parsed;
    }

    parsed.hasLine = true;
    parsed.line = line;

    if (!match.captured(2).isEmpty()) {
        const qint64 column = match.captured(2).toLongLong(&ok);
        if (ok && column > 0) {
            parsed.hasColumn = true;
            parsed.column = column;
        }
    }

    return parsed;
}

bool shouldTreatAsLocalPathWithoutScheme(const QString &rawHref) {
    if (rawHref.isEmpty()) {
        return false;
    }

    if (QDir::isAbsolutePath(rawHref)) {
        return true;
    }

    const int colonIndex = rawHref.indexOf(QLatin1Char(':'));
    if (colonIndex <= 0) {
        return true;
    }

    return rawHref.size() >= 2 && rawHref.at(1) == QLatin1Char(':') && QDir::isAbsolutePath(rawHref);
}

ResolvedLink makeBaseResolvedLink(const QString &rawHref) {
    ResolvedLink resolvedLink;
    resolvedLink.set_raw_href(rawHref.toStdString());
    resolvedLink.set_kind(LINK_KIND_UNKNOWN);
    resolvedLink.set_default_action(LINK_ACTION_KIND_NONE);
    return resolvedLink;
}

void setTooltip(ResolvedLink *resolvedLink, const QStringList &lines) {
    if (resolvedLink == nullptr) {
        return;
    }

    resolvedLink->set_tooltip(lines.join(QLatin1Char('\n')).toStdString());
}

ResolvedLink resolveWebLink(const QString &rawHref, const QUrl &url) {
    ResolvedLink resolvedLink = makeBaseResolvedLink(rawHref);
    const QString normalizedHref = normalizeUrlString(url);
    resolvedLink.set_kind(LINK_KIND_WEB);
    resolvedLink.set_normalized_href(normalizedHref.toStdString());
    resolvedLink.set_default_action(LINK_ACTION_KIND_OPEN_EXTERNALLY);
    resolvedLink.set_can_open_externally(true);
    setTooltip(
        &resolvedLink,
        {
            normalizedHref,
            QStringLiteral("Default: Open externally"),
        }
    );
    return resolvedLink;
}

ResolvedLink resolveMailtoLink(const QString &rawHref, const QUrl &url) {
    ResolvedLink resolvedLink = makeBaseResolvedLink(rawHref);
    const QString normalizedHref = normalizeUrlString(url);
    resolvedLink.set_kind(LINK_KIND_MAILTO);
    resolvedLink.set_normalized_href(normalizedHref.toStdString());
    resolvedLink.set_default_action(LINK_ACTION_KIND_OPEN_EXTERNALLY);
    resolvedLink.set_can_open_externally(true);
    setTooltip(
        &resolvedLink,
        {
            normalizedHref,
            QStringLiteral("Default: Open externally"),
        }
    );
    return resolvedLink;
}

ResolvedLink resolveFileLink(
    const QString &rawHref,
    const QString &resolvedPath,
    const QString &fragment
) {
    ResolvedLink resolvedLink = makeBaseResolvedLink(rawHref);
    const QFileInfo fileInfo(resolvedPath);
    const QString absolutePath = fileInfo.absoluteFilePath();
    const QUrl fileUrl = [&absolutePath, &fragment] {
        QUrl url = QUrl::fromLocalFile(absolutePath);
        if (!fragment.isEmpty()) {
            url.setFragment(fragment);
        }
        return url;
    }();
    const ParsedLocationFragment location = parseLocationFragment(fragment);

    resolvedLink.set_kind(LINK_KIND_FILE);
    resolvedLink.set_normalized_href(normalizeUrlString(fileUrl).toStdString());
    resolvedLink.set_resolved_path(absolutePath.toStdString());
    resolvedLink.set_exists(fileInfo.exists());
    resolvedLink.set_is_directory(fileInfo.isDir());
    resolvedLink.set_can_copy_resolved_path(!absolutePath.isEmpty());
    if (location.hasLine) {
        resolvedLink.set_has_line(true);
        resolvedLink.set_line(location.line);
    }
    if (location.hasColumn) {
        resolvedLink.set_has_column(true);
        resolvedLink.set_column(location.column);
    }

    QStringList tooltipLines;
    tooltipLines << QString::fromStdString(resolvedLink.normalized_href());
    tooltipLines << QStringLiteral("Resolved: %1").arg(fileLocationDisplay(absolutePath, location));
    tooltipLines << QStringLiteral("Exists: %1").arg(fileInfo.exists() ? QStringLiteral("yes") : QStringLiteral("no"));

    if (fileInfo.exists()) {
        resolvedLink.set_can_open(true);
        resolvedLink.set_default_action(LINK_ACTION_KIND_OPEN);
        if (!fileInfo.isDir()) {
            resolvedLink.set_can_reveal_in_folder(true);
        }
        tooltipLines << QStringLiteral("Default: %1").arg(fileInfo.isDir()
                                                              ? QStringLiteral("Open folder")
                                                              : QStringLiteral("Open file"));
    } else {
        tooltipLines << QStringLiteral("Default: No action");
    }

    setTooltip(&resolvedLink, tooltipLines);
    return resolvedLink;
}

ResolvedLink resolveUnknownLink(const QString &rawHref, const QString &message) {
    ResolvedLink resolvedLink = makeBaseResolvedLink(rawHref);
    if (!rawHref.trimmed().isEmpty()) {
        resolvedLink.set_normalized_href(rawHref.trimmed().toStdString());
    }
    setTooltip(&resolvedLink, {rawHref.trimmed(), message});
    return resolvedLink;
}

}  // namespace

qodex::threadui::ipc::common::ResolvedLink ThreadUiLinkPolicy::resolveLink(
    const QString &rawHref,
    const QString &cwd
) const {
    const QString trimmedHref = rawHref.trimmed();
    if (trimmedHref.isEmpty()) {
        return resolveUnknownLink(trimmedHref, QStringLiteral("Link target is empty."));
    }

    if (!shouldTreatAsLocalPathWithoutScheme(trimmedHref)) {
        const QUrl parsedUrl(trimmedHref);
        if (!parsedUrl.isValid() || parsedUrl.scheme().trimmed().isEmpty()) {
            return resolveUnknownLink(trimmedHref, QStringLiteral("Link target could not be parsed."));
        }

        const QString scheme = parsedUrl.scheme().trimmed().toLower();
        if (scheme == QStringLiteral("http") || scheme == QStringLiteral("https")) {
            return resolveWebLink(trimmedHref, parsedUrl);
        }
        if (scheme == QStringLiteral("mailto")) {
            return resolveMailtoLink(trimmedHref, parsedUrl);
        }
        if (scheme == QStringLiteral("file")) {
            return resolveFileLink(trimmedHref, parsedUrl.toLocalFile(), parsedUrl.fragment());
        }

        return resolveUnknownLink(trimmedHref, QStringLiteral("Unsupported link protocol."));
    }

    const QUrl relativeUrl(trimmedHref);
    const QString decodedPath = QUrl::fromPercentEncoding(relativeUrl.path().toUtf8());
    const QString fragment = relativeUrl.fragment();
    QString resolvedPath = decodedPath;
    if (!QDir::isAbsolutePath(resolvedPath)) {
        if (cwd.trimmed().isEmpty()) {
            return resolveUnknownLink(trimmedHref, QStringLiteral("Relative link cannot be resolved without a thread cwd."));
        }

        resolvedPath = QDir(cwd).absoluteFilePath(resolvedPath);
    }

    return resolveFileLink(trimmedHref, resolvedPath, fragment);
}

}  // namespace qodex::domain
