/**
 * @file      mesh2dderivedvariables.h
 * @brief     Groundwater variables computed in the GUI from saved or live 2D results.
 *
 * V1 of workplans/GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md
 * (D-V7): derived variables are computed from native per-cell fields and never
 * written back to the result file. Sources add them to their catalog and read
 * them through readDerivedVariableAt(), so map layers, cell plots, section
 * series and exports treat them like native variables.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_IO_MESH2DDERIVEDVARIABLES_H
#define OPENSWMMVIS_IO_MESH2DDERIVEDVARIABLES_H

#include "io/mesh2dresultvariable.h"
#include <QStringList>
#include <QVector>
#include <functional>
#include <vector>

namespace openswmmvis::io
{

/// Reads one native variable frame (a source's own readFaceVariableAt()).
using Mesh2DFaceVariableReader =
    std::function<bool(const Mesh2DResultVariable &, int, std::vector<float> &,
                       std::vector<Mesh2DValueStatus> &)>;

/**
 * @brief Derived groundwater variables available from a native catalog.
 * @param[in] native The source's native variables.
 * @param[out] warnings Optional list appended with derived variables that cannot be offered.
 * @return Depth to water table (ground − table elevation) and unsaturated thickness
 * (max(0, ground − table elevation); ground is the aquifer top, the cell bed elevation
 * `Mesh2_face_z`) when both inputs exist. Groundwater domain, `derived` set, inputs in
 * `derivedFrom`; temporal mode and frame count follow the table. Units are the inputs'
 * shared unit, or unknown when either input's unit is unknown. Inputs with different
 * known units are not combined.
 */
QVector<Mesh2DResultVariable> derivedGroundwaterVariables(
    const QVector<Mesh2DResultVariable> &native, QStringList *warnings = nullptr);

/**
 * @brief Compute one derived frame from its input frames (pure).
 * @param[in] dataset Derived dataset name of a variable from derivedGroundwaterVariables().
 * @param[in] ground,groundStatus Cell bed (ground) elevation and validity.
 * @param[in] table,tableStatus Water-table elevation and validity, same cell count.
 * @param[out] values,status Replaced with the derived values; a cell is Missing when either
 * input is not Valid or not finite.
 * @return False (outputs cleared) for an unknown dataset or mismatched input sizes.
 */
bool computeDerivedGroundwater(const QString &dataset, const std::vector<float> &ground,
                               const std::vector<Mesh2DValueStatus> &groundStatus,
                               const std::vector<float> &table,
                               const std::vector<Mesh2DValueStatus> &tableStatus,
                               std::vector<float> &values, std::vector<Mesh2DValueStatus> &status);

/**
 * @brief Read the inputs of a derived variable through @p read and compute one frame.
 * @param[in] derived A derived descriptor (see derivedGroundwaterVariables()).
 * @param[in] timeIdx Report frame of the time-varying input.
 * @param[in] read The source's native frame reader.
 * @param[out] values,status Replaced with the derived frame; cleared on failure.
 * @return False when an input frame cannot be read or @p derived is not derived.
 */
bool readDerivedVariableAt(const Mesh2DResultVariable &derived, int timeIdx,
                           const Mesh2DFaceVariableReader &read, std::vector<float> &values,
                           std::vector<Mesh2DValueStatus> &status);

} // namespace openswmmvis::io
#endif
