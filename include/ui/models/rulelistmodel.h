/*!
 * \file   rulelistmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BR Phase 6.8.1 — list-pane Qt model for control rules.
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
 */
#ifndef OPENSWMMVIS_UI_MODELS_RULELISTMODEL_H
#define OPENSWMMVIS_UI_MODELS_RULELISTMODEL_H

#include <QAbstractListModel>
#include <QPointer>

class SWMMModelLayer;

namespace openswmmvis::controls {
class ControlRuleRegistry;
class ControlRuleProvider;
}

namespace openswmmvis::ui {

class RuleListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /*! \brief Construct over the layer + its (lazily-created) registry. The
     *  model holds `QPointer`s so a layer/registry swap during project
     *  switch falls through to an empty rowCount() rather than crashing. */
    explicit RuleListModel(SWMMModelLayer *layer, QObject *parent = nullptr);
    ~RuleListModel() override;

    int           rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant      data    (const QModelIndex &index,
                            int role = Qt::DisplayRole) const override;
    bool          setData (const QModelIndex &index, const QVariant &value,
                            int role = Qt::EditRole) override;
    bool          removeRows(int row, int count,
                              const QModelIndex &parent = QModelIndex()) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant      headerData(int section, Qt::Orientation orientation,
                              int role = Qt::DisplayRole) const override;

    /*! \brief Look up the row index of a rule by name, or -1 if not
     *  present. Used by `RulesEditorDialog::openForRule` to drive
     *  the list-view selection. */
    int indexOf(const QString &name) const;

    /*! \brief Read the rule name at a given row, or empty if out of
     *  bounds. */
    QString nameAt(int row) const;

private slots:
    void onProviderAdded_(openswmmvis::controls::ControlRuleProvider *p);
    void onProviderAboutToBeRemoved_(openswmmvis::controls::ControlRuleProvider *p);
    void onProviderRenamed_(openswmmvis::controls::ControlRuleProvider *p,
                              const QString &prev, const QString &now);
    void onProviderValidationChanged_();
    void onProviderBodyChanged_();

private:
    void bindRegistry_();
    int  rowOfProvider_(openswmmvis::controls::ControlRuleProvider *p) const;
    openswmmvis::controls::ControlRuleRegistry *registry_() const;

    QPointer<SWMMModelLayer>                    m_layer;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_RULELISTMODEL_H
