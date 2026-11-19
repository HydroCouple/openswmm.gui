/**
 * @file      meshregionaggregate.h
 * @brief     V6 — 2D result aggregates over a cell selection, a cell tag or
 *            the whole domain (sum, mean, area-weighted mean, mass).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * §1.1 item 6 / V6 and workplans/BELLINGE_GW_QUALITY_PHASE4B_AGENT_BRIEF_
 * 2026-10-30.md item 2. Pure functions over one frame of a cell field, so a
 * 2–4 cell fixture checks them against hand sums; `Mesh2DRunLayer::
 * getRegionSeries` drives them over every report frame (one read per frame
 * for all regions).
 *
 * Only Valid cells contribute — a Missing / NotApplicable cell is excluded,
 * never counted as zero, and a region with no contributing cell is NaN
 * (blank). Mass is concentration x water volume (depth x cell area) and is
 * offered only when both are available: a surface species concentration with
 * known mass-per-volume units and the surface depth; a waterless cell holds
 * no water and so contributes zero mass.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_RESULTS_MESHREGIONAGGREGATE_H
#define OPENSWMMVIS_RESULTS_MESHREGIONAGGREGATE_H

#include "io/mesh2dresultvariable.h"

#include <openswmm/engine/openswmm_engine.h>

#include <QString>
#include <QStringList>
#include <QVector>

#include <vector>

namespace openswmmvis::results
{

/*! How a region's cells are combined at each report frame. */
enum class MeshAggregate {
    Sum,              ///< sum of the valid cell values (variable units)
    Mean,             ///< arithmetic mean of the valid cell values
    AreaWeightedMean, ///< sum(value x area) / sum(area) over valid cells
    Mass              ///< sum(conc x depth x area): surface species only
};

/*! A named set of mesh cells (engine cell indices). */
struct MeshRegion {
    QString label;
    std::vector<int> cells;
};

/** @return Short label: "Sum", "Mean", "Area-weighted mean", "Mass". */
QString aggregateLabel(MeshAggregate aggregate);

/*!
 * \brief Mass per (concentration unit x m3), and the mass unit.
 * \param concentrationUnits Variable units, e.g. "mg/L", "MG/L", "ug/L",
 *        "g/m3", "count/L".
 * \param[out] factor kg (or count) per (unit x m3).
 * \param[out] massUnits "kg" or "count".
 * \return False for units that are not a mass (or count) per volume.
 */
bool massFactorForUnits(const QString &concentrationUnits, double *factor, QString *massUnits);

/*!
 * \brief True when \p v is a surface species concentration whose mass can
 *        be formed (conc x depth x area): `Mesh2_face_species_conc` with
 *        mass-per-volume units.
 */
bool massAvailableFor(const io::Mesh2DResultVariable &v);

/*!
 * \brief Units of an aggregate of a variable with units \p variableUnits.
 * \return The variable units for Sum / Mean / AreaWeightedMean; the mass
 *         unit for Mass (empty when the units are not mass per volume).
 */
QString aggregateUnits(MeshAggregate aggregate, const QString &variableUnits);

/*!
 * \brief Combine one frame over \p cells.
 * \param values Per-cell values (variable units).
 * \param status Optional per-cell status; null treats finite values as Valid.
 * \param cellArea Per-cell plan area, m2 (AreaWeightedMean, Mass).
 * \param depth Per-cell surface depth, m (Mass only; required for it).
 * \param massFactor From \ref massFactorForUnits (Mass only).
 * \param[out] contributing Cells that contributed (optional).
 * \param[out] missing Region cells whose value is Missing (optional): the
 *         aggregate then covers fewer cells than the region.
 * \return The aggregate, or NaN when no cell contributes (or inputs needed
 *         by the aggregate are missing).
 */
double aggregateFrame(const std::vector<float> &values,
                      const std::vector<io::Mesh2DValueStatus> *status,
                      const std::vector<double> &cellArea, const std::vector<float> *depth,
                      const std::vector<int> &cells, MeshAggregate aggregate, double massFactor,
                      int *contributing = nullptr, int *missing = nullptr);

/*!
 * \brief One region per distinct non-empty tag, sorted case-insensitively,
 *        labeled "Tag: <tag>".
 * \param cellTags Per-cell tags (engine cell order); empty = untagged.
 */
QVector<MeshRegion> tagRegions(const QStringList &cellTags);
/** @return The whole domain (cells 0..cellCount-1), labeled "Domain". */
MeshRegion domainRegion(int cellCount);

/*!
 * \brief Per-cell tags of the model mesh (engine cell order; "" = untagged).
 * \param engine Borrowed model engine; null or no mesh gives an empty list.
 */
QStringList cellTagsFromEngine(SWMM_Engine engine);

} // namespace openswmmvis::results

#endif // OPENSWMMVIS_RESULTS_MESHREGIONAGGREGATE_H
