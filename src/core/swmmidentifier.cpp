/*!
 * \file   swmmidentifier.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "core/swmmidentifier.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace openswmmvis {

QString swmmIdentifierProblem(const QString &name)
{
    if (name.isEmpty())
        return QCoreApplication::translate("SwmmIdentifier", "A name is required.");
    for (const QChar ch : name) {
        if (ch.isSpace())
            return QCoreApplication::translate(
                "SwmmIdentifier",
                "Names cannot contain spaces: the SWMM .inp format is "
                "whitespace-delimited, so \"%1\" would be saved as \"%2\". "
                "Use an underscore or CamelCase instead.")
                .arg(name, name.section(QRegularExpression(QStringLiteral("\\s")), 0, 0));
        if (ch == QLatin1Char(';'))
            return QCoreApplication::translate(
                "SwmmIdentifier",
                "Names cannot contain \";\": it starts a comment in .inp files.");
        if (ch == QLatin1Char('[') || ch == QLatin1Char(']'))
            return QCoreApplication::translate(
                "SwmmIdentifier",
                "Names cannot contain \"[\" or \"]\": they mark section "
                "headers in .inp files.");
    }
    return QString();
}

} // namespace openswmmvis
