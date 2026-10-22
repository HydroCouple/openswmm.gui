/**
 * @file      runpathresolver.cpp
 * @brief     Strategy C (GUI-only, no engine [OPTIONS] persistence).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice QB.2 — implementation. See runpathresolver.h for design notes.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "simulation/runpathresolver.h"

#include <QDir>
#include <QFileInfo>
#include <QLatin1Char>
#include <QList>
#include <QPair>
#include <QSettings>
#include <QVariant>

namespace openswmmvis
{

QString resolveRunOutputPath(const QString &inpPath, const QString &override_, RunOutputKind kind)
{
    if (inpPath.isEmpty())
        return {};

    const QFileInfo inpFi(inpPath);
    const QString siblingExt =
        (kind == RunOutputKind::Rpt) ? QStringLiteral(".rpt") : QStringLiteral(".out");

    // Tier 1 — explicit override.
    const QString cleanOverride = override_.trimmed();
    if (!cleanOverride.isEmpty()) {
        // Absolute path: pass through. QDir::isAbsolutePath handles both
        // POSIX and Windows-drive paths.
        if (QDir::isAbsolutePath(cleanOverride))
            return QDir::cleanPath(cleanOverride);

        // Relative path: resolve against the .inp's directory so the
        // override remains portable when the user moves the project
        // tree around.
        return QDir::cleanPath(inpFi.absoluteDir().absoluteFilePath(cleanOverride));
    }

    // Tier 2 — sibling default. Matches today's SWMMVis::runActiveProject
    // behaviour exactly so callers that haven't set an override see no
    // change.
    return inpFi.absoluteDir().filePath(inpFi.completeBaseName() + siblingExt);
}

QString resolveRunOutputPathFromSettings(const QString &inpPath, RunOutputKind kind)
{
    if (inpPath.isEmpty())
        return {};

    // Slice AA-4 key layout — preserved verbatim so existing QSettings
    // round-trips work without migration.
    const QString settingsKey =
        QStringLiteral("SWMMVis/Project/%1/%2")
            .arg(inpPath, (kind == RunOutputKind::Rpt) ? QStringLiteral("ReportFilePath")
                                                       : QStringLiteral("OutputFilePath"));

    QSettings s;
    const QString override_ = s.value(settingsKey).toString();
    return resolveRunOutputPath(inpPath, override_, kind);
}

void moveProjectSettings(const QString &fromPath, const QString &toPath)
{
    if (toPath.isEmpty() || fromPath == toPath)
        return;
    const auto group = [](const QString &path) {
        return QStringLiteral("SWMMVis/Project/%1").arg(path);
    };

    QSettings s;
    QList<QPair<QString, QVariant>> carried;
    if (!fromPath.isEmpty()) {
        s.beginGroup(group(fromPath));
        for (const QString &key : s.allKeys())
            carried.append({key, s.value(key)});
        s.endGroup();
    }

    s.remove(group(toPath));
    s.beginGroup(group(toPath));
    for (const auto &[key, value] : std::as_const(carried))
        s.setValue(key, value);
    s.endGroup();
}

} // namespace openswmmvis
