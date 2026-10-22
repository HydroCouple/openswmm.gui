/**
 * @file      ruleheader.cpp
 * @brief     header, read the way the engine and the legacy [CONTROLS] parser read it.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "controls/ruleheader.h"

#include "core/swmmidentifier.h"

#include <QCoreApplication>
#include <QLatin1Char>
#include <QStringList>

namespace openswmmvis::controls
{

QString ruleHeaderName(const QString &body, QString *problem)
{
    if (problem)
        problem->clear();

    // First non-blank line, without a trailing ';' comment.
    QString line;
    for (const QString &raw : body.split(QLatin1Char('\n'))) {
        QString t = raw;
        const int semi = t.indexOf(QLatin1Char(';'));
        if (semi >= 0)
            t.truncate(semi);
        t = t.trimmed();
        if (!t.isEmpty()) {
            line = t;
            break;
        }
    }

    const bool hasKeyword =
        line.size() > 4 && line.left(4).compare(QStringLiteral("RULE"), Qt::CaseInsensitive) == 0 &&
        line.at(4).isSpace();
    if (!hasKeyword) {
        if (problem)
            *problem = QCoreApplication::translate("RuleHeader",
                                                   "Rule text must begin with \"RULE <name>\".");
        return {};
    }

    const QString name = line.mid(4).trimmed();
    if (const QString why = swmmIdentifierProblem(name); !why.isEmpty()) {
        if (problem)
            *problem = why;
        return {};
    }
    return name;
}

} // namespace openswmmvis::controls
