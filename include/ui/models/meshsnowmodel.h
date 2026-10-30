// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshsnowmodel.h
 * @brief     Table model for mesh snow pack assignments and overrides.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QAbstractTableModel>
#include <QStringList>
#include <QMap>
#include <array>
#include <limits>
#include <openswmm/engine/openswmm_snow2d.h>

namespace openswmmvis::ui
{
struct MeshSnowRow {
    int cell = -1; // zero-based; -1 is a tag/default
    QString tag = "*", pack = "NONE";
    std::array<double, 6> overrides;
    /**
     * @brief Initialize all six overrides to NaN, denoting inheritance; default scope is whole mesh with pack NONE.
     */
    MeshSnowRow() { overrides.fill(std::numeric_limits<double>::quiet_NaN()); }
};
// Staged, shared table/selection model. No widget edits write to the engine.
class MeshSnowModel final : public QAbstractTableModel
{
public:
    /**
     * @brief Create an empty staged mesh-snow table; edits do not write the engine.
     * @param[in] parent Optional QObject owner; use on its Qt/GUI thread.
     */
    explicit MeshSnowModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    /**
     * @brief Count staged assignment rows.
     * @param[in] p Root is invalid; valid parents have no children.
     * @return Draft row count at root, otherwise zero.
     */
    int rowCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : rows.size(); }
    /**
     * @brief Expose scope, cell/tag, snowpack and six override columns.
     * @param[in] p Root is invalid; valid parents have no columns.
     * @return Nine at root, otherwise zero.
     */
    int columnCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : 9; }
    /**
     * @brief Read staged assignment values and inheritance hints.
     * @param[in] i Existing nonnegative row/column 0–8 from this model.
     * @param[in] role Display/Edit gives values (one-based cell display); ToolTip explains precedence; Foreground marks NaN overrides gray.
     * @return Requested value; inherited overrides display Inherited but edit as an empty string. Unsupported roles return invalid QVariant.
     */
    QVariant data(const QModelIndex &i, int role = Qt::DisplayRole) const override;
    /**
     * @brief Supply scope/override labels with SI units and one-based row numbers.
     * @param[in] section Horizontal column 0–8 or vertical row.
     * @param[in] orientation Vertical gives numbering.
     * @param[in] role Only DisplayRole is supported.
     * @return Label/number, or invalid for an unsupported role/column.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @brief Advertise all draft columns except Scope as editable.
     * @param[in] i Model index whose validity/column is checked.
     * @return Enabled/selectable and, after column zero, editable; invalid indexes receive no flags.
     */
    Qt::ItemFlags flags(const QModelIndex &i) const override;
    /**
     * @brief Edit an existing draft row, deferring physical bounds and pack-existence checks to validate().
     * @param[in] i Existing nonnegative row and column 1–8.
     * @param[in] value Positive one-based cell number, nonempty safe tag/pack token, or finite override number; blank override means inheritance.
     * @param[in] role Only EditRole is accepted.
     * @return True after changing the row and emitting row-wide dataChanged(); false for malformed values/unsupported cell.
     * @note Override order/units: forest fraction, drift factor, ground heat W/m², extinction depth m, slope degrees, aspect degrees.
     */
    bool setData(const QModelIndex &i, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @brief Remove a contiguous range of staged assignments with Qt row notifications.
     * @param[in] first First zero-based row, nonnegative.
     * @param[in] count Positive number wholly inside rows.
     * @param[in] parent Must be invalid root.
     * @return True on removal, false leaves rows unchanged for invalid range/parent.
     */
    bool removeRows(int first, int count, const QModelIndex &parent = {}) override;
    /**
     * @brief Replace the first matching scope row or append a new draft assignment.
     * @param[in] row Cell identities match by zero-based cell; negative-cell scopes match by exact tag. No validation is performed.
     * @note Emits row dataChanged() on replacement or insertion notifications for append.
     */
    void upsert(const MeshSnowRow &row);
    /**
     * @brief Stage sampled pack classes or override values as cell-specific assignments.
     * @param[in] property -1 selects pack-class mapping; 0–5 selects forest/drift/ground-heat/extinction/slope/aspect.
     * @param[in] cells Zero-based cell IDs, all present in cellTags.
     * @param[in] values Numeric samples parallel to cells for property 0–5; SI units match setData().
     * @param[in] keys Classification keys parallel to cells for property -1.
     * @param[in] classPacks Mapping from classification key to an available pack name; pack matching is case-insensitive.
     * @param[in] cellTags Current per-cell region tags for inherited pack lookup.
     * @param[out] skipped Optional count initialized zero and incremented for unknown classes/invalid numeric samples.
     * @return Empty after committing the draft with model reset; diagnostic for invalid property/vector sizes/cell IDs leaves rows unchanged, though skipped may reflect earlier samples.
     * @note Numeric filters require forest [0,1], drift >=0, finite ground heat, extinction >0 m, slope [0,90) and aspect [0,360]. Slope/aspect pairing is checked later by validate().
     */
    QString stageSamples(int property, const QVector<int> &cells, const QVector<double> &values,
                         const QStringList &keys, const QMap<QString, QString> &classPacks,
                         const QStringList &cellTags, int *skipped = nullptr);
    /**
     * @brief Snapshot mesh-snow options, assignments and available pack names from an engine.
     * @param[in] engine Borrowed live engine; never modified.
     * @return Empty replaces the complete draft inside model reset; diagnostic preserves all previous model state on read failure.
     */
    QString load(SWMM_Engine engine);
    /**
     * @brief Validate draft identities, scope uniqueness, physical limits and slope/aspect pairing.
     * @param[in] cellTags Current mesh tags indexed by zero-based cell.
     * @return Empty for valid authored data; first diagnostic otherwise. Requires mode 0–2, step 0–3600 seconds and an active assignment when mode is YES.
     * @note NaN means inherited. Snowpack matching is case-insensitive; tags are exact. No engine mutation occurs.
     */
    QString validate(const QStringList &cellTags) const;
    /**
     * @brief Merge whole-mesh, exact-tag and explicit-cell assignments in that priority order.
     * @param[in] cell Zero-based target cell.
     * @param[in] tag Current region tag; wildcard is handled only in the whole-mesh pass.
     * @param[out] origin Optional translated description of the last matching scope, initially Unassigned.
     * @return Composite pack/overrides; each finite/non-NaN override replaces lower-priority values, and NONE explicitly disables snow. Duplicate scopes resolve in stored order.
     */
    MeshSnowRow effective(int cell, const QString &tag, QString *origin = nullptr) const;
    /**
     * @brief Encode draft options, tag/default rows and explicit-cell rows as engine section bodies.
     * @return Three strings without section headers; cell IDs become one-based and NaN overrides are omitted. Call validate() first; this method clamps mode but performs no complete validation.
     */
    std::array<QString, 3> sections() const;
    /**
     * @brief Snapshot all three authored mesh-snow section bodies through sized engine reads.
     * @param[in] engine Borrowed live handle.
     * @param[out] text Replaced only after all three sections are read successfully.
     * @return Empty on success, diagnostic on any failed size/data read.
     */
    static QString readSections(SWMM_Engine engine, std::array<QString, 3> &text);
    /**
     * @brief Atomically submit the three authored mesh-snow bodies through the engine strict parser.
     * @param[in] engine Borrowed editable BUILDING/OPENED engine.
     * @param[in] text Options, defaults and cells section bodies, without headers; empty bodies clear sections.
     * @return Empty after accepted atomic replacement, otherwise engine diagnostic or a lifecycle fallback message. No model/view signals are emitted here.
     */
    static QString applySections(SWMM_Engine engine, const std::array<QString, 3> &text);
    QStringList packs;
    QVector<MeshSnowRow> rows;
    int mode = 0;
    double step = 0;
};
}
