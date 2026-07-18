/*!
 * \file   importpreviewmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — read-only table model over an
 * ImportPlan: one row per feature, columns Action | Name | Detail.
 * After execution the same model displays the result plan (actions in
 * past tense via setResultMode).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPREVIEWMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPREVIEWMODEL_H

#include "ui/dialogs/import/importplan.h"

#include <QAbstractTableModel>

namespace openswmmvis::import {

class ImportPreviewModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { ActionCol = 0, NameCol = 1, DetailCol = 2, ColCount = 3 };

    explicit ImportPreviewModel(QObject *parent = nullptr);

    void setPlan(const ImportPlan &plan, bool resultMode = false);
    void clear();

    [[nodiscard]] const ImportPlan &plan() const { return m_plan; }
    [[nodiscard]] bool resultMode() const { return m_resultMode; }

    /*! "N to create, M to update, K skipped, E errors" (or past tense). */
    [[nodiscard]] QString summaryText() const;

    // QAbstractTableModel ---------------------------------------------
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex &idx, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation o,
                                      int role) const override;

private:
    ImportPlan m_plan;
    bool       m_resultMode = false;
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTPREVIEWMODEL_H
