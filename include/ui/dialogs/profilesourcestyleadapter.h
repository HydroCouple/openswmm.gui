/**
 * @file      profilesourcestyleadapter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tiny QObject adapter exposing the profile-style Q_PROPERTYs of a
 *         SWMMResultsLayer in isolation, so a QPropertyModel-backed editor
 *         can edit them without surfacing the layer's unrelated properties.
 *
 *         All getters / setters proxy through to the bound layer via
 *         QPointer.  When the layer is null the getters return defaults and
 *         setters are no-ops — the model still renders, just inert.  Each
 *         setter triggers the bound layer's existing per-property setter,
 *         which fires `profileStyleChanged()` / `profileLineColorChanged()`
 *         signals; the adapter forwards those as Q_PROPERTY NotifySignals
 *         so QPropertyModel refreshes immediately.
 *
 *         Usage:
 *           auto *adapter = new ProfileSourceStyleAdapter(parent);
 *           adapter->setLayer(layer);
 *           propertyModel->setData(QVariant::fromValue<QObject *>(adapter));
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_SOURCE_STYLE_ADAPTER_H
#define PROFILE_SOURCE_STYLE_ADAPTER_H

#include <QBrush>
#include <QColor>
#include <QObject>
#include <QPen>
#include <QPointer>

class SWMMResultsLayer;

class ProfileSourceStyleAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor lineColor READ lineColor WRITE setLineColor NOTIFY changed)
    Q_PROPERTY(QPen hglLinePen READ hglLinePen WRITE setHglLinePen NOTIFY changed)
    Q_PROPERTY(QBrush hglFillBrush READ hglFillBrush WRITE setHglFillBrush NOTIFY changed)
    Q_PROPERTY(QPen eglLinePen READ eglLinePen WRITE setEglLinePen NOTIFY changed)
    // EGL renders as a line only — no fill brush (no physical meaning).
    Q_PROPERTY(QPen maxHglLinePen READ maxHglLinePen WRITE setMaxHglLinePen NOTIFY changed)
    Q_PROPERTY(QBrush maxHglFillBrush READ maxHglFillBrush WRITE setMaxHglFillBrush NOTIFY changed)
    Q_PROPERTY(QPen maxEglLinePen READ maxEglLinePen WRITE setMaxEglLinePen NOTIFY changed)
public:
    /**
     * @param[in] parent Optional QObject owner; caller manages parentless adapter.
     * @brief Construct unbound live style proxy, with no layer creation or source mutation.
     *   GUI-thread use; no locking.
     */
    explicit ProfileSourceStyleAdapter(QObject *parent = nullptr);

    /*! Binds the adapter to \p layer.  Pass nullptr to detach.  Disconnects
     *  signals from the previously-bound layer and rewires to the new one.
     * @param[in] layer Optional borrowed result layer guarded with QPointer; null detaches, no
     *   ownership transfer.
     * @brief On changed pointer, disconnect old layer-to-this signals, connect both profile
     *   style/color notifications and emit changed once for rebinding.
     * @note Same pointer no-op. Automatic pointer clearing on destruction does not emit changed
     *   itself; getters then return defaults. No copy of style, persistence, engine mutation or
     *   undo.
 */
    void setLayer(SWMMResultsLayer *layer);
    /**
     * @return Borrowed guarded bound layer or null; no ownership transfer or guarantee beyond
     *   current check.
     */
    [[nodiscard]] SWMMResultsLayer *layer() const { return m_layer.data(); }

    /**
     * @return Owned Qt value copy of bound layer categorical profile source color, or
     *   default-constructed QColor when unbound/deleted. Default QColor is invalid; no fallback
     *   color assignment. No mutation or output data read.
     */
    [[nodiscard]] QColor lineColor() const;
    /**
     * @return Owned Qt value copy of bound layer current hydraulic-grade line style, or
     *   default-constructed QPen when unbound/deleted. Preserves full width/dash/alpha/pattern,
     *   using plot logical-pixel style conventions rather than model length units. No mutation or
     *   output data read.
     */
    [[nodiscard]] QPen hglLinePen() const;
    /**
     * @return Owned Qt value copy of bound layer current hydraulic-grade under-fill, or
     *   default-constructed QBrush when unbound/deleted. Preserves full width/dash/alpha/pattern,
     *   using plot logical-pixel style conventions rather than model length units. No mutation or
     *   output data read.
     */
    [[nodiscard]] QBrush hglFillBrush() const;
    /**
     * @return Owned Qt value copy of bound layer current energy-grade line style, or
     *   default-constructed QPen when unbound/deleted. Preserves full width/dash/alpha/pattern,
     *   using plot logical-pixel style conventions rather than model length units. No mutation or
     *   output data read.
     */
    [[nodiscard]] QPen eglLinePen() const;
    /**
     * @return Owned Qt value copy of bound layer maximum hydraulic-grade envelope line style, or
     *   default-constructed QPen when unbound/deleted. Preserves full width/dash/alpha/pattern,
     *   using plot logical-pixel style conventions rather than model length units. No mutation or
     *   output data read.
     */
    [[nodiscard]] QPen maxHglLinePen() const;
    /**
     * @return Owned Qt value copy of bound layer maximum hydraulic-grade under-fill, or
     *   default-constructed QBrush when unbound/deleted. Preserves full width/dash/alpha/pattern,
     *   using plot logical-pixel style conventions rather than model length units. No mutation or
     *   output data read.
     */
    [[nodiscard]] QBrush maxHglFillBrush() const;
    /**
     * @return Owned Qt value copy of bound layer maximum energy-grade envelope line style, or
     *   default-constructed QPen when unbound/deleted. Preserves full width/dash/alpha/pattern,
     *   using plot logical-pixel style conventions rather than model length units. No mutation or
     *   output data read.
     */
    [[nodiscard]] QPen maxEglLinePen() const;

    /**
     * @param[in] v Copied categorical profile source color forwarded without extra
     *   validity/range/style normalization.
     * @brief When bound, invoke corresponding layer setter immediately; null binding no-op, equal
     *   layer value emits no change.
     * @note Layer notification is forwarded as changed; categorical color does NOT reset
     *   independent HGL/EGL pens or brushes. No undo, persistence, engine write, acceptance staging
     *   or result status in adapter; host may persist in changed receiver.
     */
    void setLineColor(const QColor &v);
    /**
     * @param[in] v Copied current hydraulic-grade line style forwarded without extra
     *   validity/range/style normalization.
     * @brief When bound, invoke corresponding layer setter immediately; null binding no-op, equal
     *   layer value emits no change.
     * @note Layer notification is forwarded as changed; preserves other independent profile style
     *   fields. No undo, persistence, engine write, acceptance staging or result status in adapter;
     *   host may persist in changed receiver.
     */
    void setHglLinePen(const QPen &v);
    /**
     * @param[in] v Copied current hydraulic-grade under-fill forwarded without extra
     *   validity/range/style normalization.
     * @brief When bound, invoke corresponding layer setter immediately; null binding no-op, equal
     *   layer value emits no change.
     * @note Layer notification is forwarded as changed; preserves other independent profile style
     *   fields. No undo, persistence, engine write, acceptance staging or result status in adapter;
     *   host may persist in changed receiver.
     */
    void setHglFillBrush(const QBrush &v);
    /**
     * @param[in] v Copied current energy-grade line style forwarded without extra
     *   validity/range/style normalization.
     * @brief When bound, invoke corresponding layer setter immediately; null binding no-op, equal
     *   layer value emits no change.
     * @note Layer notification is forwarded as changed; preserves other independent profile style
     *   fields. No undo, persistence, engine write, acceptance staging or result status in adapter;
     *   host may persist in changed receiver.
     */
    void setEglLinePen(const QPen &v);
    /**
     * @param[in] v Copied maximum hydraulic-grade envelope line style forwarded without extra
     *   validity/range/style normalization.
     * @brief When bound, invoke corresponding layer setter immediately; null binding no-op, equal
     *   layer value emits no change.
     * @note Layer notification is forwarded as changed; preserves other independent profile style
     *   fields. No undo, persistence, engine write, acceptance staging or result status in adapter;
     *   host may persist in changed receiver.
     */
    void setMaxHglLinePen(const QPen &v);
    /**
     * @param[in] v Copied maximum hydraulic-grade under-fill forwarded without extra
     *   validity/range/style normalization.
     * @brief When bound, invoke corresponding layer setter immediately; null binding no-op, equal
     *   layer value emits no change.
     * @note Layer notification is forwarded as changed; preserves other independent profile style
     *   fields. No undo, persistence, engine write, acceptance staging or result status in adapter;
     *   host may persist in changed receiver.
     */
    void setMaxHglFillBrush(const QBrush &v);
    /**
     * @param[in] v Copied maximum energy-grade envelope line style forwarded without extra
     *   validity/range/style normalization.
     * @brief When bound, invoke corresponding layer setter immediately; null binding no-op, equal
     *   layer value emits no change.
     * @note Layer notification is forwarded as changed; preserves other independent profile style
     *   fields. No undo, persistence, engine write, acceptance staging or result status in adapter;
     *   host may persist in changed receiver.
     */
    void setMaxEglLinePen(const QPen &v);

signals:
    /*! Unified change signal — fired for any property change so a single
     *  NOTIFY hookup keeps the editor in sync.  Fine-grained signals on the
     *  layer (profileStyleChanged / profileLineColorChanged) feed this.
     * @note Also emitted once for different setLayer binding, including null detach; same-pointer
     *   bind and automatic source destruction do not explicitly emit. Forward both layer
     *   color/style signals without coalescing, so one wider operation may notify repeatedly.
     *   Receivers reread all values; no commit/dirty/ownership payload.
 */
    void changed();

private:
    QPointer<SWMMResultsLayer> m_layer;
};

#endif // PROFILE_SOURCE_STYLE_ADAPTER_H
