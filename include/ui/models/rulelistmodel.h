/**
 * @file      rulelistmodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BR Phase 6.8.1 — list-pane Qt model for control rules.
 *
 * `QAbstractListModel` over `ControlRuleRegistry`. One row per provider.
 * Roles:
 *   - `Qt::DisplayRole`     → provider name (or `Rule N [unnamed]` sentinel
 *                             when the engine returned BADPARAM from
 *                             `swmm_control_get_id`).
 *   - `Qt::EditRole`        → provider name (inline-rename via
 *                             `QListView::edit` / F2).
 *   - `Qt::DecorationRole`  → one of three icon resources
 *                             (`:/swmmvis/RuleValid`,
 *                              `:/swmmvis/RuleInvalid`,
 *                              `:/swmmvis/RulePending`).
 *   - `Qt::ToolTipRole`     → empty when Valid; otherwise the validator's
 *                             cached error message.
 *
 * Mutations route through `SWMMModelLayer::applyControlRule*`:
 *   - `setData(EditRole)`   → `applyControlRuleRename`.
 *   - `removeRows`          → `applyControlRuleRemove` (single row at a
 *                             time; multi-select delete is out of scope).
 *   - `insertRows` is **not** implemented — creation happens via the
 *     dialog's create-card (`applyControlRuleAdd`), and the row arrives
 *     via the registry's `providerAdded` signal.
 *
 * The model is engine-backed (mirrors the engine via the registry) but
 * the dialog can wrap it in a `QSortFilterProxyModel` for the search-box
 * filtering called for in the plan. The proxy is the dialog's concern;
 * this header stays clean.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_MODELS_RULELISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_RULELISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

class SWMMModelLayer;

namespace openswmmvis::controls
{
class ControlRuleRegistry;
class ControlRuleProvider;
}

namespace openswmmvis::ui
{

class RuleListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /*! \brief Construct over the layer + its (lazily-created) registry. The
     *  model holds `QPointer`s so a layer/registry swap during project
     *  switch falls through to an empty rowCount() rather than crashing.
     * @param[in] layer Borrowed model layer tracked by QPointer; null yields an empty list. The registry is lazily requested from this layer.
     * @param[in] parent Optional QObject owner. Use on the GUI thread.
     */
    explicit RuleListModel(SWMMModelLayer *layer, QObject *parent = nullptr);
    /**
     * @brief Destroy the adapter without deleting its borrowed layer/registry.
     */
    ~RuleListModel() override;

    /**
     * @brief Count root control rules, lazily ensuring the layer registry.
     * @param[in] parent Root is invalid; valid parents return zero.
     * @return Registry provider count, or zero with no usable layer/registry.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Read rule identity, validation icon/error or text alignment.
     * @param[in] index Existing row index from this model.
     * @param[in] role DisplayRole/EditRole returns name; DecorationRole gives validation icon; ToolTipRole gives Invalid-state diagnostic with optional line; TextAlignmentRole gives left/centre.
     * @return Requested value or invalid QVariant for bad rows/unsupported roles.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief Rename a rule through the model layer's central control-edit path.
     * @param[in] index Existing rule row.
     * @param[in] value Requested name converted to a trimmed string; empty and unchanged names are rejected.
     * @param[in] role Only EditRole is supported.
     * @return True when applyControlRuleRename() accepts; false on bad input or rejection. Error text is not exposed here; registry notifications update views.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    /**
     * @brief Remove one control rule through the layer mutation path.
     * @param[in] row Existing zero-based rule row.
     * @param[in] count Must equal one.
     * @param[in] parent Must be the invalid root index.
     * @return applyControlRuleRemove() result, or false for invalid/unavailable rows.
     */
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    /**
     * @brief Advertise enabled/selectable/editable rule rows.
     * @param[in] index Index whose validity is tested.
     * @return The three flags for valid indexes; no flags otherwise.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @brief Supply the single horizontal Rule heading.
     * @param[in] section Only zero is supported.
     * @param[in] orientation Must be Horizontal.
     * @param[in] role Must be DisplayRole.
     * @return Translated Rule label, or invalid QVariant.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /*! \brief Look up the row index of a rule by name, or -1 if not
     *  present. Used by `RulesEditorDialog::openForRule` to drive
     *  the list-view selection.
     * @param[in] name Case-insensitive rule identity to locate.
     * @return First matching zero-based row, or -1 without a match/registry.
     */
    int indexOf(const QString &name) const;

    /*! \brief Read the rule name at a given row, or empty if out of
     *  bounds.
     * @param[in] row Zero-based current registry row.
     * @return Name copy, or empty for an invalid row or missing provider/registry.
     */
    QString nameAt(int row) const;

private slots:
    /**
     * @brief Announce the registry's last appended row and subscribe to its body/validation changes.
     * @param[in] p Newly appended borrowed provider; null or missing registry is ignored.
     */
    void onProviderAdded_(openswmmvis::controls::ControlRuleProvider *p);
    /**
     * @brief Announce a currently registered provider row's removal.
     * @param[in] p Borrowed provider; unresolved pointers are ignored.
     */
    void onProviderAboutToBeRemoved_(openswmmvis::controls::ControlRuleProvider *p);
    /**
     * @brief Refresh the renamed row's name display/edit roles.
     * @param[in] p Borrowed current provider.
     * @param[in] prev Previous identity, unused.
     * @param[in] now New identity, unused; data() reads the provider.
     */
    void onProviderRenamed_(openswmmvis::controls::ControlRuleProvider *p, const QString &prev,
                            const QString &now);
    /**
     * @brief Resolve the emitting provider and refresh its icon/error tooltip; unknown senders are ignored.
     */
    void onProviderValidationChanged_();
    /**
     * @brief Refresh the emitting provider's validation icon/tooltip after its body change; unknown senders are ignored.
     */
    void onProviderBodyChanged_();

private:
    /**
     * @brief Connect registry and existing-provider notifications using unique connections.
     * @note Lazily requests the layer's registry; absent layers/registries produce no subscriptions.
     */
    void bindRegistry_();
    /**
     * @brief Locate a borrowed rule-provider pointer in the lazy registry.
     * @param[in] p Provider pointer; null cannot match.
     * @return Zero-based row or -1 when absent/unavailable.
     */
    int rowOfProvider_(openswmmvis::controls::ControlRuleProvider *p) const;
    /**
     * @brief Request and cast the current layer's control registry.
     * @return Borrowed registry, or null without a live layer/compatible registry. May lazily create the registry.
     */
    openswmmvis::controls::ControlRuleRegistry *registry_() const;

    QPointer<SWMMModelLayer> m_layer;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_RULELISTMODEL_H
