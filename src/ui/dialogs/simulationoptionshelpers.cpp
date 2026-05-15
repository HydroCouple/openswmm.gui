/*!
 * \file   simulationoptionshelpers.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Static helper definitions for SimulationOptionsDialog. Split out of the
 * main .cpp so leaf QtTests (test_simulationoptionsdialog) can compile the
 * helpers without dragging in GDAL/OGR via the layer + CRS code that the
 * dialog's spatial tab needs.
 */
#include "ui/dialogs/simulationoptionsdialog.h"

#include <QDateTime>
#include <QString>

int SimulationOptionsDialog::parseEngineBool(const QString &s)
{
    const QString n = s.trimmed().toUpper();
    if (n == QStringLiteral("YES") || n == QStringLiteral("TRUE")  || n == QStringLiteral("1"))
        return Qt::Checked;
    if (n == QStringLiteral("NO")  || n == QStringLiteral("FALSE") || n == QStringLiteral("0"))
        return Qt::Unchecked;
    return Qt::PartiallyChecked;
}

QString SimulationOptionsDialog::engineBoolString(bool on)
{
    return on ? QStringLiteral("YES") : QStringLiteral("NO");
}

void SimulationOptionsDialog::formatEngineDateTime(const QDateTime &dt,
                                                   QString &out_date,
                                                   QString &out_time)
{
    out_date = dt.date().toString(QStringLiteral("MM/dd/yyyy"));
    out_time = dt.time().toString(QStringLiteral("HH:mm:ss"));
}

QDateTime SimulationOptionsDialog::parseEngineDateTime(const QString &date,
                                                       const QString &time)
{
    QDate d = QDate::fromString(date.trimmed(), QStringLiteral("MM/dd/yyyy"));
    QTime t = QTime::fromString(time.trimmed(), QStringLiteral("HH:mm:ss"));
    if (!d.isValid() || !t.isValid())
        return {};
    return QDateTime(d, t);
}
