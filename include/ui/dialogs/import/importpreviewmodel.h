/**
 * @file      importpreviewmodel.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — read-only table model over an ImportPlan: one row per
 *            feature, columns Action | Name | Detail.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — read-only table model over an
 * ImportPlan: one row per feature, columns Action | Name | Detail.
 * After execution the same model displays the result plan (actions in
 * past tense via setResultMode).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPREVIEWMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPREVIEWMODEL_H

#include "ui/dialogs/import/importplan.h"

#include <QAbstractTableModel>

namespace openswmmvis::import
{

class ImportPreviewModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { ActionCol = 0, NameCol = 1, DetailCol = 2, ColCount = 3 };

    /**
     * @param[in] parent Optional QObject owner; model owns a COPY of plan, no source/engine
     *   ownership.
     * @brief Construct empty read-only preview table with future-tense mode, GUI-thread model
     *   access. No planning/execution.
     */
    explicit ImportPreviewModel(QObject *parent = nullptr);

    /**
     * @param[in] plan Borrowed plan copied including counters AS-IS, no recount or payload/action
     *   validation.
     * @param[in] resultMode True past-tense labels, false future-tense; affects text only.
     * @note begin/endResetModel invalidates indexes/selection, does not execute any Create/Update
     *   or mutate input. Caller ensures counters/actions accurately reflect execution.
     */
    void setPlan(const ImportPlan &plan, bool resultMode = false);
    /**
     * @brief Reset owned plan to empty and future-tense mode, invalidating indexes. No source
     *   rollback, execution cancellation or engine write.
     */
    void clear();

    /**
     * @return Borrowed owned plan snapshot reference, contents replaced by setPlan/clear and
     *   destroyed with model. Copy for independent/worker lifetime; no mutation.
     */
    [[nodiscard]] const ImportPlan &plan() const { return m_plan; }
    /**
     * @return Stored display-tense flag, no evidence import actually executed/succeeded.
     */
    [[nodiscard]] bool resultMode() const { return m_resultMode; }

    /*! "N to create, M to update, K skipped, E errors" (or past tense).
     * @return Empty for zero items, otherwise translated counts from stored counters in future/past
     *   tense. Does not recount actions or distinguish cancellation from completed execution; stale
     *   counters displayed unchanged. No source query.
     */
    [[nodiscard]] QString summaryText() const;

    // QAbstractTableModel ---------------------------------------------
    /**
     * @param[in] parent Flat-table valid parent zero.
     * @return Owned plan item count for root, no execution/source scan.
     */
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Flat-table valid parent zero.
     * @return Three Action/Name/Detail columns for root, even empty plan.
     */
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] idx Expected current preview index; invalid/out-of-upper-row-range invalid,
     *   foreign/stale unsupported.
     * @param[in] role DisplayRole, action tint BackgroundRole, Detail tooltip; all other roles
     *   invalid.
     * @return Action tense from mode, exact name or fid fallback, semicolon-joined messages,
     *   newline tooltip, fixed soft action brush. Background tint applies regardless column if
     *   valid row; unknown cast action no matching label/tint. Read-only, no source/engine mutation
     *   or outcome validation.
     */
    [[nodiscard]] QVariant data(const QModelIndex &idx, int role) const override;
    /**
     * @param[in] section Logical action/name/detail column, invalid horizontal display empty.
     * @param[in] o Horizontal display labels, other orientation base result.
     * @param[in] role DisplayRole recognized, others base.
     * @return Translated Action/Name/Detail or base result; no source access.
     */
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    ImportPlan m_plan;
    bool m_resultMode = false;
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPREVIEWMODEL_H
