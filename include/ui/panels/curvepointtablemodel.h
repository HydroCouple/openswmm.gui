/**
 * @file      curvepointtablemodel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.1 — QAbstractTableModel over one CurveProvider.
 *
 * Schema: 2 columns (X / Y) by N rows. Horizontal header text is type-driven
 * via `CurveProvider::xLabel(type)` / `yLabel(type)` so the grid reads
 * "Depth | Surface Area" or "Head | Flow" or "Volume | Flow" etc.
 *
 * `setData` routes through `CurveProvider::setYAt` (Y-only edit, X locked
 * to preserve monotonicity) or `setPointAt` (X edit — validated against
 * neighbours). The provider's `mutationRejected` signal surfaces UI errors.
 *
 * Subscribes to the provider's `pointsChanged` / `pointsInserted` /
 * `pointsRemoved` / `typeChanged` signals so external mutations refresh
 * the view automatically.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_CURVEPOINTTABLEMODEL_H
#define OPENSWMMVIS_UI_PANELS_CURVEPOINTTABLEMODEL_H

#include <QAbstractTableModel>
#include <QPointer>

namespace openswmmvis::curve
{
class CurveProvider;
}

namespace openswmmvis::ui
{

class CurvePointTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QObject owner, caller owns parentless model.
     * @brief Create unbound GUI-thread model, without loading files or mutating engine/providers.
     * @note Qt model notifications and all bound-provider operations must be serialized in owner
     *   thread; no locking. Subscribes global UnitSystem unitsChanged to horizontal header redraw,
     *   numeric data unchanged.
     */
    explicit CurvePointTableModel(QObject *parent = nullptr);
    /**
     * @brief Destroy model-owned connections/cache; borrowed providers/registry and undo stack
     *   remain externally owned. No automatic save/undo commit or engine mutation.
     */
    ~CurvePointTableModel() override;

    /**
     * @param[in] p Optional borrowed curve provider guarded by QPointer, null clears.
     * @note Equality no-op. begin/end model reset around disconnect old provider→this hooks,
     *   replace pointer/connect mutation signals. No data clone/ownership transfer or
     *   destroyed→reset hook: guarded null alone can leave view until next model query.
     */
    void setProvider(openswmmvis::curve::CurveProvider *p);
    /**
     * @return Borrowed guarded provider or null, no ownership transfer/lifetime pin.
     */
    openswmmvis::curve::CurveProvider *provider() const noexcept;

    /**
     * @param[in] parent Any valid parent suppresses rows for flat model.
     * @return Provider pointCount at root, 0 if unbound/deleted. No engine fetch/cache mutation.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Valid parent yields 0, invalid root yields schema width.
     * @return 2 (X/Y) at root even unbound, 0 for valid parent.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Expected this model root cell; validates provider, index and row/two-column
     *   range.
     * @param[in] role Display/Edit returns raw double; TextAlignment right/vertical-centre; other
     *   roles invalid.
     * @return Provider point X/Y in TYPE-specific native units, not converted; header titles supply
     *   units. Invalid QVariant for bad cell/unsupported role, no input mutation.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] section Zero-based section, vertical shown 1 based; caller normally supplies
     *   current range.
     * @param[in] orientation Horizontal axis labels or vertical row labels.
     * @param[in] role Only DisplayRole supported.
     * @return Provider type/global-unit xTitle/yTitle; no provider gives X for section 0 and Y for
     *   every other horizontal section. Vertical integer section+1 without range validation.
     *   Unsupported role invalid; no mutation.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    /**
     * @param[in] index Expected current model cell; validates index/provider only, not row/column
     *   bounds.
     * @return Enabled|Selectable|Editable when checks pass, NoItemFlags otherwise. No actual
     *   provider edit permission/value-validity guarantee.
     */
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    /**
     * @param[in] index Existing valid row required; X path reads pointAt BEFORE provider
     *   validation, so stale/out-of-range row unsafe.
     * @param[in] value QVariant convertible to double; native units/dimensionless factor unchanged.
     * @param[in] role Only EditRole accepted.
     * @return False for failed model/conversion checks, otherwise provider setter result (including
     *   semantic rejection). Y uses setYAt, X uses setPointAt preserving current Y. No local undo
     *   command, engine flush or custom error message.
     * @note Provider mutation/rejection signals own downstream feedback; connected slots emit model
     *   dataChanged on successful mutation. Caller must not infer every editable cell accepts every
     *   value.
     */
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

private slots:
    /**
     * @param[in] first First changed provider row, caller/provider supplies valid range.
     * @param[in] count Number changed; nonpositive ignored.
     * @note Emits Display/Edit dataChanged across both columns through first+count-1. No
     *   clamping/reset/data mutation.
     */
    void onPointsChanged_(int first, int count);
    /**
     * @param[in] at Provider-reported start row.
     * @param[in] count Positive contiguous row count; sum/count validity assumed.
     * @note Calls beginInsertRows/endInsertRows after provider signal, without changing storage
     *   itself. No local range/count guard or reset. Provider owns underlying mutation timing.
     */
    void onPointsInserted_(int at, int count);
    /**
     * @param[in] at Provider-reported start row.
     * @param[in] count Positive contiguous row count; sum/count validity assumed.
     * @note Calls beginRemoveRows/endRemoveRows after provider signal, without changing storage
     *   itself. No local range/count guard or reset. Provider owns underlying mutation timing.
     */
    void onPointsRemoved_(int at, int count);
    /**
     * @note Emit horizontal headerDataChanged 0..1 only; no row reset/data conversion. Values/type
     *   already changed in provider.
     */
    void onTypeChanged_();

private:
    QPointer<openswmmvis::curve::CurveProvider> m_provider;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_CURVEPOINTTABLEMODEL_H
