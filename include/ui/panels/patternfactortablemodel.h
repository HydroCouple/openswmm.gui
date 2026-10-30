/**
 * @file      patternfactortablemodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.2 — QAbstractTableModel over one PatternProvider.
 *
 * Schema is fixed: one column "Factor" with N rows where N = the bound
 * provider's `factorCount()` (12 monthly / 7 daily / 24 hourly / 24 weekend).
 * Vertical-header text uses `PatternProvider::rowLabel(type, i)` so the
 * grid reads "Jan / Feb / …" or "Sun / Mon / …" or "00:00 / 01:00 / …"
 * depending on the bound pattern's type.
 *
 * `setData` validates via `PatternProvider::setFactor` (no negative values,
 * in-range index); the model relies on the provider's `mutationRejected`
 * signal for surface-level error feedback. Live drag-edit (slider, future)
 * should bypass the model and call `setFactorLive` directly to avoid undo
 * churn.
 *
 * Subscribes to the provider's `factorChanged` / `factorsChanged` /
 * `typeChanged` signals so external mutations refresh the view automatically.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_PATTERNFACTORTABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_PATTERNFACTORTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>

namespace openswmmvis::pattern
{
class PatternProvider;
}

namespace openswmmvis::ui
{

class PatternFactorTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QObject owner, caller owns parentless model.
     * @brief Create unbound GUI-thread model, without loading files or mutating engine/providers.
     * @note Qt model notifications and all bound-provider operations must be serialized in owner
     *   thread; no locking. Binding and editor/undo ownership configured separately.
     */
    explicit PatternFactorTableModel(QObject *parent = nullptr);
    /**
     * @brief Destroy model-owned connections/cache; borrowed providers/registry and undo stack
     *   remain externally owned. No automatic save/undo commit or engine mutation.
     */
    ~PatternFactorTableModel() override;

    /*! \brief Bind to a provider (or nullptr to clear). Resets the model.
     * @param[in] p Optional borrowed pattern provider guarded by QPointer, null clears.
     * @note Equality no-op. begin/end model reset around disconnect old provider→this hooks,
     *   replace pointer/connect mutation signals. No data clone/ownership transfer or
     *   destroyed→reset hook: guarded null alone can leave view until next model query.
     */
    void setProvider(openswmmvis::pattern::PatternProvider *p);
    /**
     * @return Borrowed guarded provider or null, no ownership transfer/lifetime pin.
     */
    openswmmvis::pattern::PatternProvider *provider() const noexcept;

    /**
     * @param[in] parent Any valid parent suppresses rows for flat model.
     * @return Provider factorCount at root, 0 if unbound/deleted. No engine fetch/cache mutation.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Valid parent yields 0, invalid root yields schema width.
     * @return 1 (Factor) at root even unbound, 0 for valid parent.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected this model root cell; validates provider, index and row/column 0
     *   range.
     * @param[in] role Display/Edit returns raw double; TextAlignment right/vertical-centre; other
     *   roles invalid.
     * @return Dimensionless provider multiplier at row, not a percent. Invalid QVariant for bad
     *   cell/unsupported role, no input mutation.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] section Zero-based section, vertical shown 1 based; caller normally supplies
     *   current range.
     * @param[in] orientation Horizontal axis labels or vertical row labels.
     * @param[in] role Only DisplayRole supported.
     * @return Horizontal Factor only section 0, vertical type-derived calendar/hour label or 1
     *   based string unbound. No local vertical bounds validation. Unsupported role invalid; no
     *   mutation.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Expected current model cell; validates index/provider/column 0, not row
     *   bounds.
     * @return Enabled|Selectable|Editable when checks pass, NoItemFlags otherwise. No actual
     *   provider edit permission/value-validity guarantee.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] index Existing valid row required; provider validates factor row.
     * @param[in] value QVariant convertible to double; native units/dimensionless factor unchanged.
     * @param[in] role Only EditRole accepted.
     * @return False for failed model/conversion checks, otherwise provider setter result (including
     *   semantic rejection). Calls setFactor, provider validates nonnegative/finite/index
     *   constraints. No local undo command, engine flush or custom error message.
     * @note Provider mutation/rejection signals own downstream feedback; connected slots emit model
     *   dataChanged on successful mutation. Caller must not infer every editable cell accepts every
     *   value.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

private slots:
    /**
     * @param[in] i Provider-reported valid changed row expected, not clamped.
     * @note Emits one-cell Display/Edit dataChanged; no storage mutation/reset.
     */
    void onProviderFactorChanged_(int i);
    /**
     * @note Bound nonempty provider emits Display/Edit dataChanged over entire column;
     *   missing/empty no-op. No row-count reset.
     */
    void onProviderFactorsChanged_();
    /**
     * @note Full begin/end reset after provider type change so monthly/daily/hour count reflects
     *   current provider. No model-owned storage mutation or numeric conversion.
     */
    void onProviderTypeChanged_();

private:
    QPointer<openswmmvis::pattern::PatternProvider> m_provider;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_PATTERNFACTORTABLEMODEL_H
