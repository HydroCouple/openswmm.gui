/**
 * @file      swmmlandusepropertyadapter.h
 * @brief     Property-tree adapter for [LANDUSES].
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [LANDUSES]. Scalar coverage
 * is sweepInterval + sweepRemoval; build-up / wash-off curve grids
 * land in the BP structured editor.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMLANDUSEPROPERTYADAPTER_H
#define SWMMLANDUSEPROPERTYADAPTER_H

#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMLandUsePropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    Q_PROPERTY(double sweepInterval READ sweepInterval WRITE setSweepInterval NOTIFY changed)
    Q_PROPERTY(double sweepRemoval READ sweepRemoval WRITE setSweepRemoval NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /**
     * @brief Read the street-sweeping interval in days.
     * @return Authored value or zero when the land use is missing/read fails.
     */
    [[nodiscard]] double sweepInterval() const;
    /**
     * @brief Read the dimensionless fraction of buildup removed by sweeping.
     * @return Authored value or zero when the land use is missing/read fails.
     */
    [[nodiscard]] double sweepRemoval() const;

    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /**
     * @brief Write the street-sweeping interval in days.
     * @param[in] v New value in the stated units; the adapter and current scalar engine setter do not clamp physical ranges.
     * @details Missing land use is a no-op; emits changed only when the engine accepts the write.
     */
    void setSweepInterval(double v);
    /**
     * @brief Write the dimensionless fraction of buildup removed by sweeping.
     * @param[in] v New value in the stated units; the adapter and current scalar engine setter do not clamp physical ranges.
     * @details Missing land use is a no-op; emits changed only when the engine accepts the write.
     */
    void setSweepRemoval(double v);

private:
    /**
     * @brief Resolve the stored object name against the current engine collection.
     * @return Zero-based live index, or -1 for a missing engine, empty name or unresolved ID; no index is cached across renames.
     */
    [[nodiscard]] int idx() const;
};

#endif // SWMMLANDUSEPROPERTYADAPTER_H
