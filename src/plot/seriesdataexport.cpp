/*!
 * \file   seriesdataexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/seriesdataexport.h"

#include "core/swmmdatetime.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>
#include <limits>
#include <map>

namespace openswmmvis::plot {

namespace {

/*! CSV field quoting per RFC 4180 — only when the text needs it. */
QString csvQuoted(const QString& field)
{
    if (!field.contains(QLatin1Char(',')) &&
        !field.contains(QLatin1Char('"')) &&
        !field.contains(QLatin1Char('\n')))
        return field;
    QString escaped = field;
    escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

QString formatValue(double v)
{
    return QString::number(v, 'g', 12);
}

bool writeTextFile(const QString& path, const QString& text, QString* errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not open %1 for writing: %2")
                            .arg(path, f.errorString());
        return false;
    }
    QTextStream out(&f);
    out << text;
    return true;
}

} // namespace

QString seriesToCsvText(const QVector<ExportSeries>& series)
{
    // Union of timestamps across series, keyed on epoch msecs (the Julian →
    // QDateTime conversion is second-resolution, so equal instants from
    // different series collapse to one row even when the Julian doubles
    // differ in the last ULP).
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::map<qint64, std::vector<double>> rows;
    for (int s = 0; s < series.size(); ++s) {
        const ExportSeries& es = series.at(s);
        const std::size_t n = std::min(es.timesJulian.size(), es.values.size());
        for (std::size_t i = 0; i < n; ++i) {
            const QDateTime dt = core::swmmDateTimeToQDateTime(es.timesJulian[i]);
            if (!dt.isValid()) continue;
            auto it = rows.find(dt.toMSecsSinceEpoch());
            if (it == rows.end())
                it = rows.emplace(dt.toMSecsSinceEpoch(),
                                  std::vector<double>(series.size(), nan)).first;
            it->second[s] = es.values[i];
        }
    }

    QString text;
    QTextStream out(&text);
    out << "Date/Time";
    for (const ExportSeries& es : series)
        out << ',' << csvQuoted(es.name);
    out << '\n';
    for (const auto& [msecs, values] : rows) {
        out << QDateTime::fromMSecsSinceEpoch(msecs, Qt::UTC)
                   .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        for (double v : values) {
            out << ',';
            if (std::isfinite(v)) out << formatValue(v);
        }
        out << '\n';
    }
    return text;
}

QString seriesToDatText(const ExportSeries& series)
{
    QString text;
    QTextStream out(&text);
    out << ';' << series.name << '\n';
    const std::size_t n = std::min(series.timesJulian.size(),
                                   series.values.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(series.values[i])) continue;
        const QDateTime dt = core::swmmDateTimeToQDateTime(series.timesJulian[i]);
        if (!dt.isValid()) continue;
        out << dt.toString(QStringLiteral("MM/dd/yyyy HH:mm:ss"))
            << ' ' << formatValue(series.values[i]) << '\n';
    }
    return text;
}

bool writeSeriesCsv(const QString& path,
                    const QVector<ExportSeries>& series,
                    QString* errorOut)
{
    if (series.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("No series to export");
        return false;
    }
    return writeTextFile(path, seriesToCsvText(series), errorOut);
}

QStringList writeSeriesDat(const QString& path,
                           const QVector<ExportSeries>& series,
                           QString* errorOut)
{
    if (series.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("No series to export");
        return {};
    }

    if (series.size() == 1) {
        if (!writeTextFile(path, seriesToDatText(series.first()), errorOut))
            return {};
        return { path };
    }

    // One .dat per series: <dir>/<base>_<series token>.dat
    const QFileInfo fi(path);
    const QString dir  = fi.absolutePath();
    const QString base = fi.completeBaseName();
    QStringList written;
    for (const ExportSeries& es : series) {
        const QString filePath = QStringLiteral("%1/%2_%3.dat")
                                     .arg(dir, base, sanitizedFileToken(es.name));
        if (!writeTextFile(filePath, seriesToDatText(es), errorOut))
            return {};
        written << filePath;
    }
    return written;
}

QString sanitizedFileToken(const QString& name)
{
    static const QRegularExpression kUnsafe(
        QStringLiteral("[^A-Za-z0-9_-]+"));
    QString token = name;
    token.replace(kUnsafe, QStringLiteral("_"));
    token.remove(QRegularExpression(QStringLiteral("^_+|_+$")));
    return token.isEmpty() ? QStringLiteral("series") : token;
}

} // namespace openswmmvis::plot
