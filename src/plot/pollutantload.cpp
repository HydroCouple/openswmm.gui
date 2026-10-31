/**
 * @file      pollutantload.cpp
 * @brief     R4 — pollutant load and cumulative mass series (see header).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "plot/pollutantload.h"

#include "layers/speciesattributes.h"
#include "plot/seriesintegration.h"

#include <QObject>
#include <QUrl>

#include <cmath>
#include <limits>

namespace openswmmvis::plot::pollutantload
{

namespace
{
constexpr const char *kRatePrefix = "pollutant_load:";
constexpr const char *kCumulativePrefix = "pollutant_load_cum:";
constexpr double kLitresPerFt3 = 28.316846592;
constexpr double kLitresPerGallon = 3.785411784;
constexpr double kMgPerLb = 453592.37;

QString encoded(const QString &species)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(species));
}
} // namespace

double litresPerSecond(int flowUnitsCode)
{
    switch (flowUnitsCode) {
    case 0: // CFS
        return kLitresPerFt3;
    case 1: // GPM
        return kLitresPerGallon / 60.0;
    case 2: // MGD
        return 1.0e6 * kLitresPerGallon / 86400.0;
    case 3: // CMS
        return 1000.0;
    case 4: // LPS
        return 1.0;
    case 5: // MLD
        return 1.0e6 / 86400.0;
    default:
        return std::numeric_limits<double>::quiet_NaN();
    }
}

QString flowUnitsName(int flowUnitsCode)
{
    static const char *names[] = {"CFS", "GPM", "MGD", "CMS", "LPS", "MLD"};
    return flowUnitsCode >= 0 && flowUnitsCode < 6 ? QString::fromLatin1(names[flowUnitsCode])
                                                   : QString();
}

LoadUnits loadUnits(int flowUnitsCode, int concentrationCode)
{
    LoadUnits u;
    const double lps = litresPerSecond(flowUnitsCode);
    if (!std::isfinite(lps) || concentrationCode < 0 || concentrationCode > 2)
        return u;
    const bool si = flowUnitsCode >= 3;
    if (concentrationCode == 2) { // count/L -> counts
        u.rateUnits = QStringLiteral("count/d");
        u.massUnits = QStringLiteral("count");
        u.massPerFlowConcSecond = lps;
    } else {
        // mg/L x L/s = mg/s; ug/L is a thousandth of that.
        const double mgPerConcUnit = concentrationCode == 0 ? 1.0 : 1.0e-3;
        const double massPerMg = si ? 1.0e-6 : 1.0 / kMgPerLb;
        u.rateUnits = si ? QStringLiteral("kg/d") : QStringLiteral("lb/d");
        u.massUnits = si ? QStringLiteral("kg") : QStringLiteral("lb");
        u.massPerFlowConcSecond = lps * mgPerConcUnit * massPerMg;
    }
    u.valid = true;
    return u;
}

QString rateKey(const QString &species)
{
    return QString::fromLatin1(kRatePrefix) + encoded(species);
}

QString cumulativeKey(const QString &species)
{
    return QString::fromLatin1(kCumulativePrefix) + encoded(species);
}

bool parseKey(const QString &key, QString *species, bool *cumulative)
{
    // The cumulative prefix is checked first: it extends the rate prefix's stem.
    for (const bool cum : {true, false}) {
        const QString prefix = QString::fromLatin1(cum ? kCumulativePrefix : kRatePrefix);
        if (!key.startsWith(prefix))
            continue;
        const QString name = QUrl::fromPercentEncoding(key.mid(prefix.size()).toLatin1());
        if (name.isEmpty())
            return false;
        if (species)
            *species = name;
        if (cumulative)
            *cumulative = cum;
        return true;
    }
    return false;
}

bool isLoadDescriptor(const ResultDescriptor &d)
{
    return d.isMeshVariable() && parseKey(d.variableKey, nullptr, nullptr);
}

QString loadNote(bool link)
{
    const QString basis =
        link ? QObject::tr("Computed from the saved report-step values: signed link flow × "
                           "concentration, so the load is net of reversing flow (the .rpt Link "
                           "Pollutant Load Summary sums |flow|).")
             : QObject::tr("Computed from the saved report-step values: total inflow × "
                           "concentration.");
    return basis + QLatin1Char(' ') +
           QObject::tr("The cumulative load integrates those samples, each held over the report "
                       "interval ending at it. The .rpt load summaries integrate every routing "
                       "step and can differ substantially when the report step is long.");
}

ResultDescriptor rateDescriptor(const QString &species, const LoadUnits &units, bool link)
{
    const QString name = OpenSWMMVis::Species::speciesDisplayLabel(species);
    auto d = ResultDescriptor::forMeshVariable(
        rateKey(species),
        link ? QObject::tr("%1 net load (flow × concentration at report steps)").arg(name)
             : QObject::tr("%1 load (flow × concentration at report steps)").arg(name),
        units.rateUnits);
    d.variableNote = loadNote(link);
    return d;
}

ResultDescriptor cumulativeDescriptor(const QString &species, const LoadUnits &units, bool link)
{
    const QString name = OpenSWMMVis::Species::speciesDisplayLabel(species);
    auto d = ResultDescriptor::forMeshVariable(
        cumulativeKey(species),
        link ? QObject::tr("%1 net cumulative load (integrated from report-step samples)").arg(name)
             : QObject::tr("%1 cumulative load (integrated from report-step samples)").arg(name),
        units.massUnits);
    d.variableNote = loadNote(link);
    return d;
}

std::vector<double> rate(const std::vector<double> &flow, const std::vector<double> &conc,
                         const LoadUnits &units)
{
    if (!units.valid)
        return {};
    const std::size_t n = std::min(flow.size(), conc.size());
    std::vector<double> out(n, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < n; ++i)
        if (std::isfinite(flow[i]) && std::isfinite(conc[i]))
            out[i] = flow[i] * conc[i] * units.massPerFlowConcSecond * 86400.0;
    return out;
}

std::vector<double> cumulative(const std::vector<double> &timesJulian,
                               const std::vector<double> &flow, const std::vector<double> &conc,
                               double startJulian, int reportStepSeconds, const LoadUnits &units)
{
    if (!units.valid)
        return {};
    const std::size_t n = std::min({timesJulian.size(), flow.size(), conc.size()});
    std::vector<double> product(n, std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < n; ++i)
        if (std::isfinite(flow[i]) && std::isfinite(conc[i]))
            product[i] = flow[i] * conc[i] * units.massPerFlowConcSecond;
    return cumulativeIntegral(std::vector<double>(timesJulian.begin(), timesJulian.begin() + n),
                              product, startJulian, reportStepSeconds);
}

} // namespace openswmmvis::plot::pollutantload
