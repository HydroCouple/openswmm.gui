/**
 * @file      swmmhydrographpropertyadapter.h
 * @brief     Property-tree adapter for [HYDROGRAPHS] groups.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice DA.2 — Property-tree adapter for [HYDROGRAPHS] groups. The
 * engine stores one row per `(group, month, response)`; this adapter
 * surfaces the group identity (name + assigned rain gage + row count).
 * Editing per-month R/T/K parameters lands as the BS HydrographGroupEditor
 * behind an "Edit…" button.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMHYDROGRAPHPROPERTYADAPTER_H
#define SWMMHYDROGRAPHPROPERTYADAPTER_H

#include "ui/properties/dataobjectref.h"
#include "ui/properties/swmmdataobjectpropertyadapter.h"

class SWMMHydrographPropertyAdapter : public SWMMDataObjectPropertyAdapter
{
    Q_OBJECT
    // Slice BM.0-Browse-Edit (2026-05-25) — typed as DataObjectRef
    // (kind=RainGage) so the cell hosts the picker editor. Engine setter
    // is swmm_hydrograph_set_gage.
    Q_PROPERTY(DataObjectRef gageName READ gageNameRef WRITE setGageNameRef NOTIFY changed)
    Q_PROPERTY(int rowCount READ rowCount NOTIFY changed)
    Q_PROPERTY(QString rowSummary READ rowSummary NOTIFY changed)

public:
    using SWMMDataObjectPropertyAdapter::SWMMDataObjectPropertyAdapter;

    /*! Rain gage name assigned to this UH group (or empty if none).
     * @return Gage ID from the first readable exact matching group binding, or empty when unassigned/missing.
     */
    [[nodiscard]] QString gageName() const;
    /*! Slice BM.0-Browse-Edit — DataObjectRef wrapper of `gageName()`.
     * @return RainGage picker coordinate carrying borrowed engine/layer and the current binding name.
     */
    [[nodiscard]] DataObjectRef gageNameRef() const;
    /*! Number of `(month, response)` parameter rows for this group.
     * @return Number of readable RTK/abstraction records matching the group name exactly; zero when missing.
     */
    [[nodiscard]] int rowCount() const;
    /*! Human summary like "12 months × 3 responses".
     * @return Distinct month-count × response-count summary, a no-parameter-rows marker, or empty for absent engine/name; unreadable rows are skipped.
     */
    [[nodiscard]] QString rowSummary() const;

    /**
     * @brief Supply the property browser label for a supported stored property key.
     * @param[in] property Exact Q_PROPERTY key; comparisons are case-sensitive.
     * @return Translated label, including active-project units where implemented; empty for unknown keys. This does not inspect or change the property value.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    /*! Slice BM.0-Browse-Edit — writes the selected rain gage via
     *  `swmm_hydrograph_set_gage`. Empty `ref.currentName` clears the
     *  assignment (engine accepts a NULL gage name).
     * @brief Bind this unit-hydrograph group to the selected rain gage.
     * @param[in] ref currentName supplies the gage ID; empty clears via nullptr. Embedded pointers do not rebind the adapter.
     * @details Missing engine/group name is a no-op; emits changed only on successful engine binding.
     */
    void setGageNameRef(const DataObjectRef &ref);
};

#endif // SWMMHYDROGRAPHPROPERTYADAPTER_H
