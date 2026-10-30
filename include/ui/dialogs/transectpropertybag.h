/**
 * @file      transectpropertybag.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — QObject Q_PROPERTY surface backing the
 *         TransectEditorDialog middle-pane property tree (QPropertyModel).
 *
 * Bidirectional wiring: on bind(), the bag pulls current values from the
 * bound TransectProvider; setter slots push grouped values directly to the
 * provider. The optional layer binding is retained as context and is not
 * used by the current push helpers. Property changes flow back through the provider's
 * signals → bag's `setX(currentValue)` which is a no-op if the value did
 * not change, breaking the feedback loop.
 *
 * Exposed groups (the displayLabelFor() prefix drives section headers in
 * the QPropertyModel tree):
 *
 *   Roughness             — nLeftBank, nRightBank, nChannel
 *   Bank Stations         — xLeftBank, xRightBank
 *   Encroachment Stations — xLeftEncroachment, xRightEncroachment
 *   Modifiers             — stationMultiplier, elevationOffset, meanderFactor
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_TRANSECTPROPERTYBAG_H
#define OPENSWMMVIS_UI_DIALOGS_TRANSECTPROPERTYBAG_H

#include <QObject>
#include <QPointer>
#include <QString>

class SWMMModelLayer;

namespace openswmmvis::transect
{
class TransectProvider;
}

namespace openswmmvis::ui
{

class TransectPropertyBag : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double nLeftBank READ nLeftBank WRITE setNLeftBank NOTIFY nLeftBankChanged)
    Q_PROPERTY(double nRightBank READ nRightBank WRITE setNRightBank NOTIFY nRightBankChanged)
    Q_PROPERTY(double nChannel READ nChannel WRITE setNChannel NOTIFY nChannelChanged)

    Q_PROPERTY(double xLeftBank READ xLeftBank WRITE setXLeftBank NOTIFY xLeftBankChanged)
    Q_PROPERTY(double xRightBank READ xRightBank WRITE setXRightBank NOTIFY xRightBankChanged)

    Q_PROPERTY(double xLeftEncroachment READ xLeftEncroachment WRITE setXLeftEncroachment NOTIFY
                   xLeftEncroachmentChanged)
    Q_PROPERTY(double xRightEncroachment READ xRightEncroachment WRITE setXRightEncroachment NOTIFY
                   xRightEncroachmentChanged)

    Q_PROPERTY(double stationMultiplier READ stationMultiplier WRITE setStationMultiplier NOTIFY
                   stationMultiplierChanged)
    Q_PROPERTY(double elevationOffset READ elevationOffset WRITE setElevationOffset NOTIFY
                   elevationOffsetChanged)
    Q_PROPERTY(
        double meanderFactor READ meanderFactor WRITE setMeanderFactor NOTIFY meanderFactorChanged)

public:
    /**
     * @param[in] parent Optional QObject owner; caller manages parentless adapter.
     * @brief Initialize unbound zero roughness/stations/offset and unit station/meander multipliers
     *   without writes/signals.
     * @note GUI-thread adapter, no locks or auto-clear on provider destruction.
     */
    explicit TransectPropertyBag(QObject *parent = nullptr);

    /**
     * @param[in] p Optional borrowed guarded transect provider; null explicitly resets cache.
     * @param[in] layer Optional borrowed guarded model layer stored for context; current push
     *   helpers do not use it, invoke layer apply helpers or create undo commands.
     * @brief Disconnect all old provider-to-this connections, subscribe four grouped signals, and
     *   refresh cache.
     * @note Live provider hydration suppresses pushes but can emit changed-field signals; null
     *   reset emits ALL ten signals unconditionally. Same binding still reconnects. QPointer nulls
     *   on destruction but no destroyed-handler resets stale cache automatically.
     */
    void bind(openswmmvis::transect::TransectProvider *p, SWMMModelLayer *layer = nullptr);
    /**
     * @return Borrowed guarded provider or null; no ownership transfer, live data query or
     *   guarantee beyond current pointer check.
     */
    openswmmvis::transect::TransectProvider *provider() const noexcept;

    /**
     * @param[in] property Exact case-sensitive raw Q_PROPERTY identifier.
     * @return Translated section-prefixed label for known roughness/station/modifier properties,
     *   otherwise empty. No units appended, number conversion, provider mutation or visibility
     *   filtering.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

    /**
     * @return Cached left-overbank Manning roughness n; raw authored coefficient. No live query,
     *   validation or conversion; cache can outlive provider binding.
     */
    double nLeftBank() const noexcept { return m_nLeft; }
    /**
     * @return Cached right-overbank Manning roughness n; raw authored coefficient. No live query,
     *   validation or conversion; cache can outlive provider binding.
     */
    double nRightBank() const noexcept { return m_nRight; }
    /**
     * @return Cached main-channel Manning roughness n; raw authored coefficient. No live query,
     *   validation or conversion; cache can outlive provider binding.
     */
    double nChannel() const noexcept { return m_nChannel; }
    /**
     * @return Cached left channel-bank station in authored ft or m, before station multiplier. No
     *   live query, validation or conversion; cache can outlive provider binding.
     */
    double xLeftBank() const noexcept { return m_xLeftBank; }
    /**
     * @return Cached right channel-bank station in authored ft or m, before station multiplier. No
     *   live query, validation or conversion; cache can outlive provider binding.
     */
    double xRightBank() const noexcept { return m_xRightBank; }
    /**
     * @return Cached left encroachment station in authored ft or m. No live query, validation or
     *   conversion; cache can outlive provider binding.
     */
    double xLeftEncroachment() const noexcept { return m_xLeftEnc; }
    /**
     * @return Cached right encroachment station in authored ft or m. No live query, validation or
     *   conversion; cache can outlive provider binding.
     */
    double xRightEncroachment() const noexcept { return m_xRightEnc; }
    /**
     * @return Cached dimensionless authored station scale, not applied to stored sample points by
     *   this adapter. No live query, validation or conversion; cache can outlive provider binding.
     */
    double stationMultiplier() const noexcept { return m_xFactor; }
    /**
     * @return Cached additive authored elevation offset in ft or m, not applied to stored points
     *   here. No live query, validation or conversion; cache can outlive provider binding.
     */
    double elevationOffset() const noexcept { return m_yFactor; }
    /**
     * @return Cached dimensionless channel meander/length factor. No live query, validation or
     *   conversion; cache can outlive provider binding.
     */
    double meanderFactor() const noexcept { return m_lengthFactor; }

public slots:
    /**
     * @param[in] v Left-overbank Manning roughness n; raw authored coefficient.
     * @brief Store exact unequal cache value, emit nLeftBankChanged, then push complete roughness
     *   group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setNLeftBank(double v);
    /**
     * @param[in] v Right-overbank Manning roughness n; raw authored coefficient.
     * @brief Store exact unequal cache value, emit nRightBankChanged, then push complete roughness
     *   group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setNRightBank(double v);
    /**
     * @param[in] v Main-channel Manning roughness n; raw authored coefficient.
     * @brief Store exact unequal cache value, emit nChannelChanged, then push complete roughness
     *   group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setNChannel(double v);
    /**
     * @param[in] v Left channel-bank station in authored ft or m, before station multiplier.
     * @brief Store exact unequal cache value, emit xLeftBankChanged, then push complete bank group
     *   when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setXLeftBank(double v);
    /**
     * @param[in] v Right channel-bank station in authored ft or m, before station multiplier.
     * @brief Store exact unequal cache value, emit xRightBankChanged, then push complete bank group
     *   when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setXRightBank(double v);
    /**
     * @param[in] v Left encroachment station in authored ft or m.
     * @brief Store exact unequal cache value, emit xLeftEncroachmentChanged, then push complete
     *   encroach group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setXLeftEncroachment(double v);
    /**
     * @param[in] v Right encroachment station in authored ft or m.
     * @brief Store exact unequal cache value, emit xRightEncroachmentChanged, then push complete
     *   encroach group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setXRightEncroachment(double v);
    /**
     * @param[in] v Dimensionless authored station scale, not applied to stored sample points by
     *   this adapter.
     * @brief Store exact unequal cache value, emit stationMultiplierChanged, then push complete
     *   modifiers group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setStationMultiplier(double v);
    /**
     * @param[in] v Additive authored elevation offset in ft or m, not applied to stored points
     *   here.
     * @brief Store exact unequal cache value, emit elevationOffsetChanged, then push complete
     *   modifiers group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setElevationOffset(double v);
    /**
     * @param[in] v Dimensionless channel meander/length factor.
     * @brief Store exact unequal cache value, emit meanderFactorChanged, then push complete
     *   modifiers group when not suppressed.
     * @note Equal is no-op; NaN remains unequal. No finite/physical/order/range validation, unit
     *   conversion or engine mutation here. Push directly updates local provider metadata,
     *   including companion cached fields; bound layer is unused, no undo/dirty handling in
     *   adapter. With null provider, only cache/signal changes occur.
     */
    void setMeanderFactor(double v);

signals:
    /**
     * @brief Publish cached left-overbank Manning roughness n; raw authored coefficient.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void nLeftBankChanged(double);
    /**
     * @brief Publish cached right-overbank Manning roughness n; raw authored coefficient.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void nRightBankChanged(double);
    /**
     * @brief Publish cached main-channel Manning roughness n; raw authored coefficient.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void nChannelChanged(double);
    /**
     * @brief Publish cached left channel-bank station in authored ft or m, before station
     *   multiplier.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void xLeftBankChanged(double);
    /**
     * @brief Publish cached right channel-bank station in authored ft or m, before station
     *   multiplier.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void xRightBankChanged(double);
    /**
     * @brief Publish cached left encroachment station in authored ft or m.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void xLeftEncroachmentChanged(double);
    /**
     * @brief Publish cached right encroachment station in authored ft or m.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void xRightEncroachmentChanged(double);
    /**
     * @brief Publish cached dimensionless authored station scale, not applied to stored sample
     *   points by this adapter.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void stationMultiplierChanged(double);
    /**
     * @brief Publish cached additive authored elevation offset in ft or m, not applied to stored
     *   points here.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void elevationOffsetChanged(double);
    /**
     * @brief Publish cached dimensionless channel meander/length factor.
     * @note Unnamed argument is new double value; emitted before provider push, on changed
     *   hydration, and unconditionally during null-provider full reset. Not engine-write
     *   confirmation or independent project dirty notification.
     */
    void meanderFactorChanged(double);

private slots:
    /**
     * @brief If provider lives, hydrate only its roughness triple through cache setters with pushes
     *   suppressed.
     * @note Can emit changed-field signals but no provider/engine write. Null is no-op; suppression
     *   cleared at end without restoring prior value.
     */
    void onProviderRoughnessChanged_();
    /**
     * @brief If provider lives, hydrate only its bank station pair through cache setters with
     *   pushes suppressed.
     * @note Can emit changed-field signals but no provider/engine write. Null is no-op; suppression
     *   cleared at end without restoring prior value.
     */
    void onProviderBankStationsChanged_();
    /**
     * @brief If provider lives, hydrate only its encroachment station pair through cache setters
     *   with pushes suppressed.
     * @note Can emit changed-field signals but no provider/engine write. Null is no-op; suppression
     *   cleared at end without restoring prior value.
     */
    void onProviderEncroachmentChanged_();
    /**
     * @brief If provider lives, hydrate only its station/elevation/meander triple through cache
     *   setters with pushes suppressed.
     * @note Can emit changed-field signals but no provider/engine write. Null is no-op; suppression
     *   cleared at end without restoring prior value.
     */
    void onProviderModifiersChanged_();

private:
    /**
     * @brief Pull all ten cached values from provider under push suppression; live-source
     *   notifications emit only on inequality.
     * @note Null provider directly restores zero roughness/stations/offset and unit scale/meander
     *   while emitting ALL ten signals even if unchanged. No source writes, prior suppression
     *   restoration or automatic invocation on provider destruction.
     */
    void refreshFromProvider_();
    /**
     * @brief Submit three cached Manning coefficients directly to live provider setRoughness; null
     *   provider is no-op.
     * @note No suppression check INSIDE helper (callers check), validation, layer apply helper,
     *   undo, engine save or success result. Provider grouped signal can synchronously refresh
     *   adapter with feedback suppressed.
     */
    void pushRoughness_();
    /**
     * @brief Submit cached left/right bank stations directly to live provider setBankStations; null
     *   provider is no-op.
     * @note No suppression check INSIDE helper (callers check), validation, layer apply helper,
     *   undo, engine save or success result. Provider grouped signal can synchronously refresh
     *   adapter with feedback suppressed.
     */
    void pushBank_();
    /**
     * @brief Submit cached encroachment stations directly to live provider setEncroachmentStations;
     *   null provider is no-op.
     * @note No suppression check INSIDE helper (callers check), validation, layer apply helper,
     *   undo, engine save or success result. Provider grouped signal can synchronously refresh
     *   adapter with feedback suppressed.
     */
    void pushEncroach_();
    /**
     * @brief Submit cached station multiplier, elevation offset and meander factor directly to live
     *   provider setModifiers; null provider is no-op.
     * @note No suppression check INSIDE helper (callers check), validation, layer apply helper,
     *   undo, engine save or success result. Provider grouped signal can synchronously refresh
     *   adapter with feedback suppressed.
     */
    void pushModifiers_();

    QPointer<openswmmvis::transect::TransectProvider> m_provider;
    QPointer<SWMMModelLayer> m_layer;

    double m_nLeft = 0.0;
    double m_nRight = 0.0;
    double m_nChannel = 0.0;
    double m_xLeftBank = 0.0;
    double m_xRightBank = 0.0;
    double m_xLeftEnc = 0.0;
    double m_xRightEnc = 0.0;
    double m_xFactor = 1.0;
    double m_yFactor = 0.0;
    double m_lengthFactor = 1.0;

    bool m_suppressPush = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TRANSECTPROPERTYBAG_H
