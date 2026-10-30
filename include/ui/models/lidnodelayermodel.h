// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      lidnodelayermodel.h
 * @brief     Draft table model for atomic editing of LID node layer stacks.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_LIDNODELAYERMODEL_H
#define OPENSWMMVIS_LIDNODELAYERMODEL_H
#include <QAbstractTableModel>
#include <QVector>
#include "lid/lidcontrolprovider.h"
#include <openswmm/engine/openswmm_infrastructure.h>
namespace openswmmvis::ui
{
// Draft model: Apply commits the complete stack atomically through the engine.
class LidNodeLayerModel : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;
    QVector<SWMM_LidNodeLayer> layers;
    QVector<SWMM_LidRichardsMaterial> retention;
    QVector<QVector<openswmmvis::lid::LidLayerTreatment>> treatments;
    /**
     * @brief Flatten per-layer draft treatments into engine-facing records.
     * @return Copies of treatment rows with layer numbers rewritten to one-based current layer positions.
     */
    QVector<openswmmvis::lid::LidLayerTreatment> treatmentRows() const;
    /**
     * @brief Replace per-layer treatment drafts, grouping supplied records by their one-based layer index.
     * @param[in] rows Treatment rows; out-of-range layer numbers are ignored. No engine mutation or Qt notification occurs.
     */
    void setTreatments(const QVector<openswmmvis::lid::LidLayerTreatment> &rows);
    /**
     * @brief Count draft stack layers.
     * @param[in] p Root is invalid; child indexes have no rows.
     * @return Layer count at root, otherwise zero.
     */
    int rowCount(const QModelIndex &p = {}) const override
    {
        return p.isValid() ? 0 : layers.size();
    }
    /**
     * @brief Expose layer kind, shared physical parameters and five Richards retention columns.
     * @param[in] p Root is invalid; child indexes have no columns.
     * @return Fifteen at root, otherwise zero.
     */
    int columnCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : 15; }
    /**
     * @brief Read applicable draft parameters for one stack layer.
     * @param[in] i Existing nonnegative row and column 0–14 from this model.
     * @param[in] role Only DisplayRole/EditRole is supported.
     * @return Kind keyword or raw engine-unit number; invalid for unsupported/inapplicable/unavailable entries. Retention alpha and specific storage use 1/m.
     */
    QVariant data(const QModelIndex &i, int role = Qt::DisplayRole) const override;
    /**
     * @brief Supply physical-parameter labels and one-based row numbers.
     * @param[in] section Horizontal parameter column 0–14 or vertical row.
     * @param[in] o Vertical selects numbering, horizontal column labels.
     * @param[in] role Only DisplayRole is supported.
     * @return Label/number, or invalid for unsupported role/column.
     */
    QVariant headerData(int section, Qt::Orientation o, int role = Qt::DisplayRole) const override;
    /**
     * @brief Advertise editable layer kinds and applicable physical/retention fields.
     * @param[in] i Valid model cell; provide an existing nonnegative row.
     * @return Base Qt table flags plus ItemIsEditable where applicable. Boundary kind cells are advertised editable but setData rejects boundary conversions.
     */
    Qt::ItemFlags flags(const QModelIndex &i) const override;
    /**
     * @brief Edit draft layer type or Richards parameters without writing the engine.
     * @param[in] i Existing row and parameter column from this model.
     * @param[in] v Kind keyword (case-insensitive), or numeric retention value.
     * @param[in] role Only EditRole is supported.
     * @return True for unchanged kind, MEDIA/AGGREGATE conversion or finite nonnegative retention edits; false for invalid/boundary edits.
     * @note Current ordinary numeric columns 1–9 always reject edits because their conversion-success flag is never populated. Accepted changes emit dataChanged() across the row.
     */
    bool setData(const QModelIndex &i, const QVariant &v, int role = Qt::EditRole) override;
    /**
     * @brief Replace the draft stack and clear all associated retention/treatment drafts.
     * @param[in] value Layer records moved into this model inside a reset notification. No engine validation occurs.
     */
    void setLayers(QVector<SWMM_LidNodeLayer> value);
    /**
     * @brief Insert a default draft layer, keeping new boundary rows at stack ends.
     * @param[in] kind SURFACE=0, MEDIA=1, AGGREGATE=2 or BOTTOM=3; invalid or duplicate boundary kinds are ignored.
     * @param[in] si True uses mm and mm/h defaults; false converts thickness/suction/conductivity defaults to inches and in/h. Richards retention starts zeroed.
     * @note Adds matching retention/treatment entries and emits row insertion notifications; stack validity is deferred to Apply.
     */
    void append(int kind, bool si = true);
    /**
     * @brief Remove one draft layer and its aligned retention/treatments.
     * @param[in] row Zero-based current layer; invalid rows are ignored. Emits row removal notifications.
     */
    void remove(int row);
    /**
     * @brief Reorder a draft layer with its retention/treatments.
     * @param[in] row Existing source index.
     * @param[in] delta Signed offset to the destination; out-of-range source/destination is ignored.
     * @note Caller should request a nonzero offset. This operation does not enforce boundary ordering; complete-stack validation occurs on Apply.
     */
    void move(int row, int delta);
    /**
     * @brief Count MEDIA and AGGREGATE layers in the draft.
     * @return Number of kinds 1 or 2; excludes boundary layers.
     */
    int mediaCount() const;
    /**
     * @brief Resize the draft's combined MEDIA/AGGREGATE count.
     * @param[in] count Desired count, at least one; lower values are ignored. Excess media are removed from the end; added rows use MEDIA defaults.
     * @param[in] si Unit convention for newly added rows, as in append().
     */
    void setMediaCount(int count, bool si = true);

private:
    /**
     * @brief Map a shared table column to the layer-kind-specific params array.
     * @param[in] kind Supported layer kind 0–3.
     * @param[in] column Physical column 0–9.
     * @return Parameter offset, or -1 when the kind/column is invalid or inapplicable.
     */
    static int parameter(int kind, int column);
};
}
#endif
