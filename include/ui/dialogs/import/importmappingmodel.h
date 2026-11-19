/**
 * @file      importmappingmodel.h
 * @brief     FEATURE_LAYER_TO_SWMM_IMPORT — QAbstractTableModel over the attribute mapping table.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * FEATURE_LAYER_TO_SWMM_IMPORT — QAbstractTableModel over the attribute
 * mapping table. Rows = target attributes of the chosen kind; columns:
 *
 *   0 Target Attribute  (read-only; bold + "•" marker when required)
 *   1 Source Column     (combo delegate over the layer's field names)
 *   2 Default Value     (free-text; coerced/validated by the planner)
 *
 * The model owns its mapping/metadata values and edits that ImportMapping in
 * place (MVC: the mapping is the model state, this class adapts it to
 * the view).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPINGMODEL_H
#define OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPINGMODEL_H

#include "ui/dialogs/import/importmapping.h"

#include <QAbstractTableModel>
#include <QStringList>
#include <QStyledItemDelegate>

namespace openswmmvis::import
{

class ImportMappingModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { TargetCol = 0, SourceCol = 1, DefaultCol = 2, ColCount = 3 };

    /**
     * @param[in] parent Optional QObject owner; model OWNS its ImportMapping/spec/field-list
     *   values, no source-layer/provider ownership.
     * @brief Construct zero-row GUI-thread mapping table; default mapping Junction kind but no
     *   attributes until reset. No source/engine write.
     */
    explicit ImportMappingModel(QObject *parent = nullptr);

    /*! Rebuild rows for \p kind, preserving bindings whose target keys
     *  survive the kind switch, and re-list \p sourceFields in the
     *  combo delegate.
     * @param[in] kind Requested target type stored verbatim, registry supplies current row
     *   metadata.
     * @param[in] sourceFields Copied raw field-name list in delegate order, exact membership used
     *   to keep source bindings.
     * @note Reset rows, preserve FIRST old binding for surviving exact target key, clearing
     *   sourceField if absent exact-case new list, retaining its default. New attributes unbound,
     *   disappeared keys discarded, nonbinding options preserved. End reset then mappingEdited
     *   always; no autoMatch, default conversion/source validation or engine write.
     */
    void reset(TargetKind kind, const QStringList &sourceFields);

    /*! Replace bindings wholesale (preset load). Field bindings whose
     *  source field is absent from the current layer are cleared and
     *  reported in \p droppedOut.
     * @param[in] bindings Borrowed replacement rows; first matching exact target key chosen for
     *   each existing target, unknown keys ignored.
     * @param[out] droppedOut Optional list APPENDED with absent nonempty source-field names, not
     *   cleared/deduplicated.
     * @note Reset table, clear every current binding then copy matching default and exact-present
     *   source field. Missing source field cleared but default retained; mapped target
     *   rows/specs/options not replaced. Always mappingEdited after reset, no coercion/source write
     *   or validation gate.
     */
    void applyBindings(const QVector<AttributeBinding> &bindings,
                       QStringList *droppedOut = nullptr);

    /*! Case-insensitive auto-match of source fields to attribute keys /
     *  labels. Only fills rows that are currently unmapped. Returns the
     *  number of rows matched.
     * @return Count of rows newly assigned a field, zero emits no mappingEdited.
     * @note For sourceField-empty rows (even ones with default), choose FIRST source field matching
     *   key/translated label case-insensitively; name also accepts id. No trim/punctuation
     *   normalization or exact-case priority pass. Preserve default, emit per-source-cell
     *   dataChanged then one mappingEdited if any, no source mutation.
     */
    int autoMatch();

    /**
     * @return Borrowed model-owned immutable value reference, valid until model destruction; later
     *   reset/edit changes contents. Copy before worker use. No notification/mutation.
     */
    [[nodiscard]] const ImportMapping &mapping() const { return m_mapping; }
    /*! Mutable access for the dialog's option widgets (endpoint /
     *  conflict flags live on the same value object). Views of THIS
     *  model are unaffected by those fields, so no signals fire.
     * @return Mutable model-owned value reference. Caller handles invalidating
     *   preview/notifications and preserves binding-row alignment with attributes; arbitrary
     *   vector/kind changes can violate methods indexed in parallel. Options-only mutations
     *   intentionally emit nothing. Not safe concurrent access.
     */
    [[nodiscard]] ImportMapping &mappingRef() { return m_mapping; }

    /**
     * @return Value copy of latest reset field list, order/duplicates/spelling unchanged; not live
     *   source schema query.
     */
    [[nodiscard]] QStringList sourceFields() const { return m_sourceFields; }
    /**
     * @return Borrowed owned target spec vector, invalidated by reset/reallocation/destruction; no
     *   source identity or persistent index guarantee.
     */
    [[nodiscard]] const QVector<TargetAttribute> &attributes() const { return m_attributes; }

    /*! Empty when every required row is satisfied; otherwise a
     *  human-readable reason (drives the dialog's inline error label +
     *  Preview/Import enablement).
     * @return First required mapping error, enforcing name source-column (constant forbidden), then
     *   fields-only link endpoints requirement. Empty when these MAPPING checks pass, not proof
     *   source values/geometry/names/default coercion/snap strategy can succeed. All endpoint
     *   strategies disabled can pass and later planner errors. No mutation.
     */
    [[nodiscard]] QString validationError() const;

    // QAbstractTableModel ---------------------------------------------
    /**
     * @param[in] parent Flat-table valid parent yields zero.
     * @return Cached attributes size for root, no engine/source query.
     */
    [[nodiscard]] int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Flat-table valid parent yields zero.
     * @return Three Target/Source/Default columns for root, even zero rows.
     */
    [[nodiscard]] int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] idx Expected current model index, invalid/out-of-upper-row-range empty;
     *   foreign/stale indexes unsupported.
     * @param[in] role Display/Edit, required Target Font or Target Tooltip; others invalid.
     * @return Target label with required bullet, raw source name or translated (not mapped) for
     *   BOTH Display/Edit, raw default QVariant. Target tooltip enum codes, fixed-stage side
     *   effect, otherwise project-unit claim. Heat metadata actually SI/°C per registry, no unit
     *   conversion here. Does not coerce defaults/read source/engine or mutate mapping.
     */
    [[nodiscard]] QVariant data(const QModelIndex &idx, int role) const override;
    /**
     * @param[in] section Logical target/source/default column, other horizontal display empty.
     * @param[in] o Horizontal labels; other orientations delegated to QAbstractTableModel base.
     * @param[in] role DisplayRole labels only, other roles base.
     * @return Translated Target Attribute/Source Column/Default Value or base result, no source
     *   access.
     */
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation o, int role) const override;
    /**
     * @param[in] idx Index column inspected WITHOUT validity or row-bounds check; caller supplies
     *   current model index.
     * @return Enabled/selectable always, editable for Source/Default column numbers. Does not gate
     *   required/default/type validation or source lifetime. No mutation.
     */
    [[nodiscard]] Qt::ItemFlags flags(const QModelIndex &idx) const override;
    /**
     * @param[in] idx Current model Source/Default cell, invalid/out-of-upper-row-range/other col
     *   rejected.
     * @param[in] value Source exact name/empty/(not mapped) sentinel; default converted to TRIMMED
     *   QString regardless initial numeric type, blank invalid.
     * @param[in] role EditRole only accepted.
     * @return True accepted source/default update or equal source; false unsupported/missing source
     *   field. Equality source no notification; Default emits even same normalized value.
     * @note ensureBinding can append missing row before later column rejection if caller broke
     *   mapping alignment. Source checks exact field list, translated sentinel maps empty. Emit
     *   dataChanged and mappingEdited after change, no coercion/required gate or engine/source
     *   write.
     */
    bool setData(const QModelIndex &idx, const QVariant &value, int role) override;

signals:
    /*! Emitted after any binding edit — the dialog revalidates.
     * @note Emitted after reset/applyBindings, actual autoMatch hits and accepted cell edits
     *   (default even equality), not mappingRef options changes. Requests dialog preview
     *   invalidation/revalidation; no engine/source effect by signal itself.
     */
    void mappingEdited();

private:
    ImportMapping m_mapping;
    QVector<TargetAttribute> m_attributes;
    QStringList m_sourceFields;
};

/*! Combo-box delegate for the Source Column cells: field names plus a
 *  leading "(not mapped)" entry. */
class SourceFieldDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /**
     * @param[in] model Optional borrowed RAW mapping model used to copy source fields when editor
     *   opens; must outlive delegate.
     * @param[in] parent Optional QObject owner; independent of model ownership.
     * @brief Store binding only, no editor/source mutation. GUI-thread delegate; parentless
     *   lifetime managed by caller.
     */
    explicit SourceFieldDelegate(ImportMappingModel *model, QObject *parent = nullptr);

    /**
     * @param[in] parent Optional QWidget owner for returned combo.
     * @param[in] opt Ignored style option, implementation unnamed.
     * @param[in] idx Ignored source index, implementation unnamed.
     * @return New noneditable combo with translated not-mapped first then bound model source field
     *   names snapshot, no per-cell validation/source query or model write. Caller/view owns
     *   editor.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;
    /**
     * @param[in] editor Optional borrowed combo, wrong/null ignored.
     * @param[in] idx Current cell EditRole string, exact-text lookup.
     * @note Choose matching field or row zero fallback, no signal blocker/coercion or source/model
     *   write. Unknown current value silently represented not mapped.
     */
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;
    /**
     * @param[in] editor Optional borrowed combo, wrong/null no-op.
     * @param[in] model Optional mutable destination, null no-op; may differ from model used for
     *   choices.
     * @param[in] idx Current destination forwarded unchanged.
     * @note Submit empty QString for combo index zero, otherwise currentText via EditRole, ignoring
     *   setter result. No own undo, validation/source write or ownership transfer.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override;

private:
    ImportMappingModel *m_model = nullptr;
};

} // namespace openswmmvis::import

#endif // OPENSWMMVIS_UI_DIALOGS_IMPORT_IMPORTMAPPINGMODEL_H
