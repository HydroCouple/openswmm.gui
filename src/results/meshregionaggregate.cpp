/**
 * @file      meshregionaggregate.cpp
 * @brief     V6 — 2D result aggregates over cell regions (see header).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "results/meshregionaggregate.h"

#include <openswmm/engine/openswmm_2d.h>

#include <QObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::results
{

QString aggregateLabel(MeshAggregate aggregate)
{
    switch (aggregate) {
    case MeshAggregate::Sum:
        return QObject::tr("Sum");
    case MeshAggregate::Mean:
        return QObject::tr("Mean");
    case MeshAggregate::AreaWeightedMean:
        return QObject::tr("Area-weighted mean");
    case MeshAggregate::Mass:
        return QObject::tr("Mass");
    }
    return {};
}

bool massFactorForUnits(const QString &concentrationUnits, double *factor, QString *massUnits)
{
    QString u = concentrationUnits.trimmed().toUpper();
    u.replace(QChar(0x00B3), QLatin1Char('3')); // m³ -> m3
    double f = 0.0;
    QString mass = QStringLiteral("kg");
    if (u == QLatin1String("MG/L") || u == QLatin1String("G/M3"))
        f = 1.0e-3; // g per m3 -> kg
    else if (u == QLatin1String("UG/L") || u == QLatin1String("MG/M3"))
        f = 1.0e-6;
    else if (u == QLatin1String("KG/M3"))
        f = 1.0;
    else if (u == QLatin1String("COUNT/L") || u == QLatin1String("COUNTS/L") ||
             u == QLatin1String("#/L")) {
        f = 1000.0; // per liter -> per m3
        mass = QStringLiteral("count");
    } else
        return false;
    if (factor)
        *factor = f;
    if (massUnits)
        *massUnits = mass;
    return true;
}

bool massAvailableFor(const io::Mesh2DResultVariable &v)
{
    return v.dataset == QLatin1String("Mesh2_face_species_conc") && v.unitsKnown &&
           massFactorForUnits(v.units, nullptr, nullptr);
}

QString aggregateUnits(MeshAggregate aggregate, const QString &variableUnits)
{
    if (aggregate != MeshAggregate::Mass)
        return variableUnits;
    QString mass;
    return massFactorForUnits(variableUnits, nullptr, &mass) ? mass : QString();
}

double aggregateFrame(const std::vector<float> &values,
                      const std::vector<io::Mesh2DValueStatus> *status,
                      const std::vector<double> &cellArea, const std::vector<float> *depth,
                      const std::vector<int> &cells, MeshAggregate aggregate, double massFactor,
                      int *contributing, int *missing)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (contributing)
        *contributing = 0;
    if (missing) {
        *missing = 0;
        for (const int c : cells)
            if (c >= 0 && c < int(values.size()) &&
                (status ? c < int(status->size()) &&
                              (*status)[std::size_t(c)] == io::Mesh2DValueStatus::Missing
                        : !std::isfinite(values[std::size_t(c)])))
                ++*missing;
    }
    const bool mass = aggregate == MeshAggregate::Mass;
    if (mass && (!depth || !(massFactor > 0.0)))
        return nan;
    double sum = 0.0, weight = 0.0;
    int used = 0;
    for (const int c : cells) {
        if (c < 0 || c >= int(values.size()))
            continue;
        const auto state = status && c < int(status->size()) ? (*status)[std::size_t(c)]
                                                             : io::Mesh2DValueStatus::Valid;
        const double v = values[std::size_t(c)];
        const double area = c < int(cellArea.size()) ? cellArea[std::size_t(c)] : nan;
        if (mass) {
            // No water, no mass: a waterless cell is a known zero here.
            if (state == io::Mesh2DValueStatus::Waterless) {
                ++used;
                continue;
            }
            const double h = c < int(depth->size()) ? (*depth)[std::size_t(c)] : nan;
            if (state != io::Mesh2DValueStatus::Valid || !std::isfinite(v) || !std::isfinite(h) ||
                !std::isfinite(area))
                continue;
            sum += v * std::max(0.0, h) * area * massFactor;
            ++used;
            continue;
        }
        if (state != io::Mesh2DValueStatus::Valid || !std::isfinite(v))
            continue;
        if (aggregate == MeshAggregate::AreaWeightedMean) {
            if (!std::isfinite(area) || !(area > 0.0))
                continue;
            sum += v * area;
            weight += area;
        } else {
            sum += v;
        }
        ++used;
    }
    if (contributing)
        *contributing = used;
    if (used == 0)
        return nan;
    switch (aggregate) {
    case MeshAggregate::Mean:
        return sum / used;
    case MeshAggregate::AreaWeightedMean:
        return weight > 0.0 ? sum / weight : nan;
    default:
        return sum;
    }
}

QVector<MeshRegion> tagRegions(const QStringList &cellTags)
{
    QStringList tags;
    QSet<QString> seen;
    for (const QString &t : cellTags)
        if (!t.isEmpty() && !seen.contains(t)) {
            seen.insert(t);
            tags.append(t);
        }
    std::sort(tags.begin(), tags.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    QVector<MeshRegion> regions;
    for (const QString &t : tags) {
        MeshRegion r;
        r.label = QObject::tr("Tag: %1").arg(t);
        for (int c = 0; c < cellTags.size(); ++c)
            if (cellTags[c] == t)
                r.cells.push_back(c);
        regions.append(r);
    }
    return regions;
}

MeshRegion domainRegion(int cellCount)
{
    MeshRegion r;
    r.label = QObject::tr("Domain");
    r.cells.resize(std::size_t(std::max(0, cellCount)));
    for (int c = 0; c < cellCount; ++c)
        r.cells[std::size_t(c)] = c;
    return r;
}

QStringList cellTagsFromEngine(SWMM_Engine engine)
{
    int cells = 0;
    if (!engine || swmm_2d_cell_count(engine, &cells) != 0 || cells <= 0)
        return {};
    QStringList tags;
    tags.reserve(cells);
    for (int c = 0; c < cells; ++c) {
        char tag[256]{};
        if (swmm_2d_get_triangle_tag(engine, c, tag, int(sizeof tag)) != 0)
            return {};
        tags.append(QString::fromUtf8(tag));
    }
    return tags;
}

} // namespace openswmmvis::results
