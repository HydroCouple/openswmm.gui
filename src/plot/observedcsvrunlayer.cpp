/*!
 * \file   observedcsvrunlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/observedcsvrunlayer.h"

#include "plot/swmmjuliandatetime.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::plot {

namespace {

QChar guessDelimiter(const QString& sampleLine)
{
    // Highest-count among tab / comma / semicolon wins.
    int tabs = sampleLine.count(QLatin1Char('\t'));
    int commas = sampleLine.count(QLatin1Char(','));
    int semis = sampleLine.count(QLatin1Char(';'));
    if (tabs >= commas && tabs >= semis && tabs > 0) return QLatin1Char('\t');
    if (semis > commas && semis > 0)                  return QLatin1Char(';');
    return QLatin1Char(',');
}

bool tryParseTimestamp(const QString& s, double& jOut, double fallbackBase)
{
    static const QString kFormats[] = {
        QStringLiteral("yyyy-MM-ddTHH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("MM/dd/yyyy HH:mm:ss"),
        QStringLiteral("MM/dd/yyyy HH:mm"),
        QStringLiteral("MM/dd/yyyy"),
        QStringLiteral("dd/MM/yyyy HH:mm"),
    };

    QString trimmed = s.trimmed();
    for (const QString& fmt : kFormats) {
        QDateTime dt = QDateTime::fromString(trimmed, fmt);
        if (!dt.isValid()) continue;
        if (dt.timeSpec() == Qt::LocalTime) dt.setTimeSpec(Qt::UTC);
        jOut = dateTimeToSwmmJulian(dt);
        return std::isfinite(jOut);
    }

    // Hours-since-start numeric fallback (only if fallbackBase is valid).
    bool ok = false;
    const double hours = trimmed.toDouble(&ok);
    if (ok && std::isfinite(fallbackBase)) {
        jOut = fallbackBase + hours / 24.0;
        return true;
    }
    return false;
}

UnitSystem inferUnitSystemFromHeaders(const QStringList& headers)
{
    static const QRegularExpression usRx(
        QStringLiteral("(_|/|\\s)(ft|ft3|ft\\^3|cfs|fps)(s|/s|\\^3/s|\\b)"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString& h : headers) {
        if (usRx.match(h).hasMatch()) return UnitSystem::US;
    }
    return UnitSystem::SI;
}

} // namespace

bool ObservedCsvRunLayer::parseRow_(const QString& line,
                                     QChar delim,
                                     double& timeJulianOut,
                                     std::vector<double>& valuesOut,
                                     double hoursSinceStartFallbackBase)
{
    const QStringList cells = line.split(delim);
    if (cells.size() < 2) return false;

    if (!tryParseTimestamp(cells.first(), timeJulianOut,
                            hoursSinceStartFallbackBase)) return false;

    valuesOut.assign(static_cast<std::size_t>(cells.size() - 1),
                     std::numeric_limits<double>::quiet_NaN());
    for (int c = 1; c < cells.size(); ++c) {
        const QString s = cells.at(c).trimmed();
        if (s.isEmpty()) continue;
        bool ok = false;
        const double v = s.toDouble(&ok);
        if (ok) valuesOut[static_cast<std::size_t>(c - 1)] = v;
    }
    return true;
}

std::unique_ptr<ObservedCsvRunLayer> ObservedCsvRunLayer::load(
    const QString& path,
    PlotAttribute defaultAttr,
    UnitSystem unitSys,
    QString *errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut) *errorOut = f.errorString();
        return {};
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);

    // Sniff header.
    QString headerLine;
    while (!in.atEnd()) {
        headerLine = in.readLine();
        if (!headerLine.trimmed().isEmpty() && !headerLine.startsWith('#'))
            break;
    }
    if (headerLine.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("File is empty");
        return {};
    }

    const QChar delim = guessDelimiter(headerLine);
    const QStringList headers = headerLine.split(delim);
    if (headers.size() < 2) {
        if (errorOut) *errorOut = QStringLiteral("Need at least 2 columns (timestamp + value)");
        return {};
    }

    auto layer = std::unique_ptr<ObservedCsvRunLayer>(new ObservedCsvRunLayer);
    layer->m_path         = path;
    layer->m_label        = QFileInfo(path).completeBaseName();
    layer->m_defaultAttr  = defaultAttr;
    // If the caller passed a non-default unit explicitly, honor it; otherwise
    // sniff from headers.
    layer->m_unit         = (unitSys != UnitSystem::SI) ? unitSys
                                                         : inferUnitSystemFromHeaders(headers);

    // Are these CSV "headers" actually a data row? Try parsing the first as a row.
    double probeJulian = std::nan("");
    std::vector<double> probeVals;
    const bool headerLooksLikeData = parseRow_(headerLine, delim, probeJulian,
                                                probeVals, std::nan(""));

    QStringList valueLabels;
    if (headerLooksLikeData) {
        // Synthetic labels.
        valueLabels.reserve(headers.size() - 1);
        for (int c = 1; c < headers.size(); ++c)
            valueLabels << QStringLiteral("col_%1").arg(c);
        // Treat the first line as data.
        layer->m_timesJulian.push_back(probeJulian);
        for (int c = 0; c < valueLabels.size(); ++c)
            layer->m_byLabel[valueLabels.at(c)].push_back(
                c < static_cast<int>(probeVals.size())
                    ? probeVals[c]
                    : std::numeric_limits<double>::quiet_NaN());
    } else {
        valueLabels.reserve(headers.size() - 1);
        for (int c = 1; c < headers.size(); ++c)
            valueLabels << headers.at(c).trimmed();
        for (const QString& lbl : valueLabels)
            layer->m_byLabel.insert(lbl, {});
    }
    layer->m_labels = valueLabels;

    // Pull remaining rows.
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty() || line.startsWith('#')) continue;

        double tJ = std::nan("");
        std::vector<double> cells;
        if (!parseRow_(line, delim, tJ, cells, std::nan(""))) continue;

        layer->m_timesJulian.push_back(tJ);
        for (int c = 0; c < valueLabels.size(); ++c) {
            const double v = c < static_cast<int>(cells.size())
                                ? cells[c]
                                : std::numeric_limits<double>::quiet_NaN();
            layer->m_byLabel[valueLabels.at(c)].push_back(v);
        }
    }

    if (layer->m_timesJulian.empty()) {
        if (errorOut) *errorOut = QStringLiteral("No valid data rows parsed");
        return {};
    }
    return layer;
}

QString ObservedCsvRunLayer::scenarioName() const
{
    return QStringLiteral("Observed — %1").arg(m_label);
}

double ObservedCsvRunLayer::startDateJulian() const
{
    if (m_timesJulian.empty()) return std::nan("");
    return m_timesJulian.front();
}

int ObservedCsvRunLayer::periodCount() const
{
    return static_cast<int>(m_timesJulian.size());
}

int ObservedCsvRunLayer::reportStepSeconds() const
{
    if (m_timesJulian.size() < 2) return 0;
    const double dDays = m_timesJulian[1] - m_timesJulian[0];
    return static_cast<int>(std::round(dDays * 86400.0));
}

QString ObservedCsvRunLayer::persistenceKey() const
{
    return QStringLiteral("observed://") + m_path;
}

bool ObservedCsvRunLayer::supportsAttribute(PlotAttribute attr) const
{
    // Observed data is attribute-agnostic; we only know it carries numbers.
    // Restrict to non-mesh, non-Unknown attributes so it lands on a sensible
    // chart row.
    if (attr == PlotAttribute::Unknown) return false;
    if (isMesh2DAttribute(attr)) return false;
    return true;
}

void ObservedCsvRunLayer::getSeriesAt(const ObjectRef& ref,
                                       PlotAttribute attr,
                                       SeriesData& out) const
{
    out.ok = false;
    out.timesJulian.clear();
    out.values.clear();
    out.errorMessage.clear();

    if (ref.kind != ObjectRef::Kind::Observed) {
        out.errorMessage = QStringLiteral("ObjectRef is not Observed");
        return;
    }
    if (!supportsAttribute(attr)) {
        out.errorMessage = QStringLiteral("Attribute not supported by observed source");
        return;
    }
    auto it = m_byLabel.find(ref.name);
    if (it == m_byLabel.end()) {
        out.errorMessage = QStringLiteral("Observed column '%1' not found").arg(ref.name);
        return;
    }

    out.timesJulian = m_timesJulian;
    out.values.assign(it.value().begin(), it.value().end());
    out.ok = !out.timesJulian.empty();
}

} // namespace openswmmvis::plot
