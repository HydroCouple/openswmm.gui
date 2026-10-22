/**
 * @file      qualityfunctiontablemodels.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Table models for the unified Land Use editor's Buildup and
 *         Washoff tabs (iteration 4).
 *
 * Rows are the engine's pollutants; columns are the [BUILDUP]/[WASHOFF]
 * function parameters for one selected land use. Cells read and write the
 * engine directly (swmm_buildup_get/set, swmm_washoff_get/set) —
 * apply-as-you-go, matching the compound-edit dialogs. Call
 * refresh() after the pollutant set or the bound land use changes;
 * rows re-dimension automatically ("sections dynamically added/removed").
 *
 * EXT-buildup guard: when the function is EXT, coefficient C3 holds a
 * time-series TABLE INDEX, not a number (see QualityHandler.cpp) — the
 * cell is read-only with an explanatory tooltip so a numeric edit can
 * never corrupt it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_QUALITYFUNCTIONTABLEMODELS_H
#define OPENSWMMVIS_UI_QUALITYFUNCTIONTABLEMODELS_H

#include <QAbstractTableModel>
#include <QStringList>
#include <QVector>
#include <QStyledItemDelegate>

namespace openswmmvis::ui
{

/// Shared base: pollutant rows + one bound (engine, land use) target.
class QualityFunctionTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * @brief Create an unbound pollutant/bulk-species table with zero rows.
     * @param[in] parent Optional QObject owner; use with engine and views on their owning thread.
     */
    explicit QualityFunctionTableModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}

    /*! Bind the engine handle and land use index this table edits.
     *  luIndex < 0 unbinds (empty table).
     * @param[in] engineHandle Borrowed engine handle; null unbinds. Caller keeps the engine alive and serializes access.
     * @param[in] luIndex Zero-based land-use index; negative unbinds. Nonnegative bounds are left to engine getters/setters.
     * @note Resets the model and enumerates pollutants then non-wall reaction species. Failed reaction-species reads are omitted.
     */
    void bind(void *engineHandle, int luIndex);
    /**
     * @brief Reset/re-enumerate pollutants and bulk reaction species using the current engine/land-use binding.
     */
    void refresh(); ///< re-query pollutant rows (after add/remove/rename)

    /**
     * @brief Report the cached constituent row count.
     * @param[in] parent Root is invalid; valid parents have no rows.
     * @return Pollutants plus successfully enumerated bulk reaction species at root, otherwise zero.
     */
    int rowCount(const QModelIndex &parent = {}) const override;

    /**
     * @brief Access the engine borrowed by this adapter.
     * @return Bound opaque handle, or null when unbound; no ownership transfer.
     */
    [[nodiscard]] void *engineHandle() const { return m_engine; }
    /**
     * @brief Inspect the selected land-use index.
     * @return Stored engine index; negative means unbound.
     */
    [[nodiscard]] int landUseIndex() const { return m_luIndex; }

protected:
    /**
     * @brief Read an engine constituent identity for a table row.
     * @param[in] row Existing zero-based table row; reaction rows are mapped through the filtered bulk-species list.
     * @return UTF-8 decoded name or empty for unavailable identity/read failure.
     */
    QString pollutantName(int row) const;
    /// The engine constituent index of a row: pollutants first, then the
    /// bulk reaction species (pollutant count + species index), the
    /// surface-species convention of swmm_buildup_* / swmm_washoff_*.
    /**
     * @param[in] row Table row, expected nonnegative.
     * @return Pollutant index or pollutant count plus original reaction species index; -1 for an out-of-range reaction row. Negative pollutant-side rows pass through unchanged.
     */
    int constituentIndex(int row) const;

    void *m_engine = nullptr;
    int m_luIndex = -1;
    int m_rows = 0;
    int m_npollut = 0;
    QVector<int> m_species; ///< bulk reaction species indices, in row order
};

class BuildupTableModel : public QualityFunctionTableModel
{
    Q_OBJECT

public:
    enum Column { ColPollutant, ColFunction, ColC1, ColC2, ColC3, ColNormalizer, ColCount };

    using QualityFunctionTableModel::QualityFunctionTableModel;

    /**
     * @brief Expose six buildup columns.
     * @param[in] parent Root is invalid; valid parents have zero columns.
     * @return ColCount at root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Read buildup identity, selected function and raw engine parameters.
     * @param[in] index Existing table cell from this model.
     * @param[in] role DisplayRole/EditRole gives cell content; OptionsRole gives enum choices for supported selector columns. ToolTipRole explains the EXT C3 time-series index guard.
     * @return Requested value or invalid QVariant when unbound/unsupported. Engine getter failures leave local defaults at zero/NONE.
     * @note Coefficient units depend on the selected buildup function and engine input conventions; no conversion occurs. EXT C3 is a table index, never an editable coefficient.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief Submit an edited buildup row directly to the engine.
     * @param[in] index Existing editable table cell; the identity column is rejected.
     * @param[in] value Exact enum label for selectors, otherwise QVariant::toDouble() in raw engine units (failed numeric conversion becomes zero).
     * @param[in] role Only EditRole is accepted.
     * @return True after engine acceptance and row dataChanged(); false for bad binding/selector/column, EXT C3 edits, or engine rejection.
     * @note Numeric ranges/finite values are not checked here; the underlying setter stores raw coefficients. No undo command or project dirty notification is emitted by this adapter.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    /**
     * @brief Advertise editable parameter cells while preserving identity and EXT C3 read-only cells.
     * @param[in] index Index tested against the column/type editing policy.
     * @return Base table flags plus ItemIsEditable where supported; engine/binding absence alone does not suppress all edit flags.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Supply pollutant, function, C1, C2, C3 and normalizer headings.
     * @param[in] section Zero-based horizontal column.
     * @param[in] orientation Horizontal selects custom labels; other orientations delegate to Qt.
     * @param[in] role DisplayRole selects custom labels; other roles delegate to Qt.
     * @return Translated heading, invalid for unknown custom columns, or the base Qt header value.
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /**
     * @brief Provide selector labels in engine enum order.
     * @return NONE, POW, EXP, SAT, EXT.
     */
    static QStringList functionNames(); ///< NONE POW EXP SAT EXT
    /**
     * @brief Provide buildup normalization labels in engine enum order.
     * @return AREA then CURB.
     */
    static QStringList normalizerNames(); ///< AREA CURB
};

class WashoffTableModel : public QualityFunctionTableModel
{
    Q_OBJECT

public:
    enum Column {
        ColPollutant,
        ColFunction,
        ColCoeff,
        ColExponent,
        ColSweepEffic,
        ColBmpEffic,
        ColCount
    };

    using QualityFunctionTableModel::QualityFunctionTableModel;

    /**
     * @brief Expose six washoff columns.
     * @param[in] parent Root is invalid; valid parents have zero columns.
     * @return ColCount at root, otherwise zero.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @brief Read washoff identity, selected function and raw engine parameters.
     * @param[in] index Existing table cell from this model.
     * @param[in] role DisplayRole/EditRole gives cell content; OptionsRole gives enum choices for supported selector columns.
     * @return Requested value or invalid QVariant when unbound/unsupported. Engine getter failures leave local defaults at zero/NONE.
     * @note Coefficient units depend on the selected washoff function and engine input conventions; no conversion occurs. Sweep/BMP efficiency values use percent, conventionally 0–100.
     */
    QVariant data(const QModelIndex &index, int role) const override;
    /**
     * @brief Submit an edited washoff row directly to the engine.
     * @param[in] index Existing editable table cell; the identity column is rejected.
     * @param[in] value Exact enum label for selectors, otherwise QVariant::toDouble() in raw engine units (failed numeric conversion becomes zero).
     * @param[in] role Only EditRole is accepted.
     * @return True after engine acceptance and row dataChanged(); false for bad binding/selector/column, or engine rejection.
     * @note Numeric ranges/finite values are not checked here; the underlying setter stores raw coefficients. No undo command or project dirty notification is emitted by this adapter.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    /**
     * @brief Advertise editable parameter cells while preserving identity read-only cells.
     * @param[in] index Index tested against the column/type editing policy.
     * @return Base table flags plus ItemIsEditable where supported; engine/binding absence alone does not suppress all edit flags.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Supply pollutant, function, coefficient, exponent, sweep efficiency and BMP efficiency headings.
     * @param[in] section Zero-based horizontal column.
     * @param[in] orientation Horizontal selects custom labels; other orientations delegate to Qt.
     * @param[in] role DisplayRole selects custom labels; other roles delegate to Qt.
     * @return Translated heading, invalid for unknown custom columns, or the base Qt header value.
     */
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /**
     * @brief Provide selector labels in engine enum order.
     * @return NONE, EXP, RC, EMC.
     */
    static QStringList functionNames(); ///< NONE EXP RC EMC
};

/*! Combo editor for the enum columns (Function / Normalizer): the model
 *  hands the option list through OptionsRole. */
class EnumComboDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    static constexpr int OptionsRole = Qt::UserRole + 41;

    using QStyledItemDelegate::QStyledItemDelegate;

    /**
     * @brief Create a combo box for cells exposing OptionsRole, otherwise use Qt's default editor.
     * @param[in] parent Widget owner for the new editor.
     * @param[in] option Base delegate view options, forwarded for default editors.
     * @param[in] index Model cell whose OptionsRole provides selector labels.
     * @return Newly allocated parent-owned editor, or the base delegate's result.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    /**
     * @brief Populate a combo with EditRole text or delegate other editor types to Qt.
     * @param[in,out] editor Editor widget, expected non-null.
     * @param[in] index Source model cell.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @brief Submit selected combo text as EditRole or use Qt for other editor types.
     * @param[in] editor Editor widget, expected non-null.
     * @param[in,out] model Required target model receiving setData().
     * @param[in] index Target cell; a rejected model edit is not separately reported.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_QUALITYFUNCTIONTABLEMODELS_H
