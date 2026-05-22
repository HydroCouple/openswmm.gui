/*!
 * \file   profilesourcestyleadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tiny QObject adapter exposing the profile-style Q_PROPERTYs of a
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
    Q_PROPERTY(QColor lineColor       READ lineColor       WRITE setLineColor       NOTIFY changed)
    Q_PROPERTY(QPen   hglLinePen      READ hglLinePen      WRITE setHglLinePen      NOTIFY changed)
    Q_PROPERTY(QBrush hglFillBrush    READ hglFillBrush    WRITE setHglFillBrush    NOTIFY changed)
    Q_PROPERTY(QPen   eglLinePen      READ eglLinePen      WRITE setEglLinePen      NOTIFY changed)
    // EGL renders as a line only — no fill brush (no physical meaning).
    Q_PROPERTY(QPen   maxHglLinePen   READ maxHglLinePen   WRITE setMaxHglLinePen   NOTIFY changed)
    Q_PROPERTY(QBrush maxHglFillBrush READ maxHglFillBrush WRITE setMaxHglFillBrush NOTIFY changed)
    Q_PROPERTY(QPen   maxEglLinePen   READ maxEglLinePen   WRITE setMaxEglLinePen   NOTIFY changed)
public:
    explicit ProfileSourceStyleAdapter(QObject *parent = nullptr);

    /*! Binds the adapter to \p layer.  Pass nullptr to detach.  Disconnects
     *  signals from the previously-bound layer and rewires to the new one. */
    void setLayer(SWMMResultsLayer *layer);
    [[nodiscard]] SWMMResultsLayer *layer() const { return m_layer.data(); }

    [[nodiscard]] QColor lineColor()       const;
    [[nodiscard]] QPen   hglLinePen()      const;
    [[nodiscard]] QBrush hglFillBrush()    const;
    [[nodiscard]] QPen   eglLinePen()      const;
    [[nodiscard]] QPen   maxHglLinePen()   const;
    [[nodiscard]] QBrush maxHglFillBrush() const;
    [[nodiscard]] QPen   maxEglLinePen()   const;

    void setLineColor      (const QColor  &v);
    void setHglLinePen     (const QPen    &v);
    void setHglFillBrush   (const QBrush  &v);
    void setEglLinePen     (const QPen    &v);
    void setMaxHglLinePen  (const QPen    &v);
    void setMaxHglFillBrush(const QBrush  &v);
    void setMaxEglLinePen  (const QPen    &v);

signals:
    /*! Unified change signal — fired for any property change so a single
     *  NOTIFY hookup keeps the editor in sync.  Fine-grained signals on the
     *  layer (profileStyleChanged / profileLineColorChanged) feed this. */
    void changed();

private:
    QPointer<SWMMResultsLayer> m_layer;
};

#endif // PROFILE_SOURCE_STYLE_ADAPTER_H
