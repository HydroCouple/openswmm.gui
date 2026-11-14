/**
 * @file      groundwatermarkersublayer.h
 * @brief     R7 (2026-10-30): mesh sublayer marking [GW_SOURCES] locations and
 *            [2D_AQUIFER_NODE] beds — style bag + legend. Geometry is built by the
 *            mesh layer's renderers from SWMM2DMeshLayer::groundwaterSceneMarkers().
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_GROUNDWATERMARKERSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_GROUNDWATERMARKERSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class GroundwaterMarkerStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor sourceColor READ sourceColor WRITE setSourceColor NOTIFY styleChanged)
    Q_PROPERTY(QColor bedColor READ bedColor WRITE setBedColor NOTIFY styleChanged)
    Q_PROPERTY(QColor noExchangeColor READ noExchangeColor WRITE setNoExchangeColor NOTIFY
                   styleChanged)
    Q_PROPERTY(double markerSizePx READ markerSizePx WRITE setMarkerSizePx NOTIFY styleChanged)

    Q_CLASSINFO("group:sourceColor", "Symbology")
    Q_CLASSINFO("group:bedColor", "Symbology")
    Q_CLASSINFO("group:noExchangeColor", "Symbology")
    Q_CLASSINFO("group:markerSizePx", "Symbology")

public:
    /** @param parent Optional QObject owner. */
    explicit GroundwaterMarkerStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    /** @return Fill of [GW_SOURCES] squares. */
    [[nodiscard]] QColor sourceColor() const { return m_sourceColor; }
    /** @return Fill of exchanging [2D_AQUIFER_NODE] bed diamonds. */
    [[nodiscard]] QColor bedColor() const { return m_bedColor; }
    /** @return Fill of `EXCHANGE NO` bed diamonds. */
    [[nodiscard]] QColor noExchangeColor() const { return m_noExchangeColor; }
    /** @return Marker size in logical pixels, [2, 40]. */
    [[nodiscard]] double markerSizePx() const { return m_markerSizePx; }

    /** @param v New color; emits styleChanged on a change. */
    void setSourceColor(const QColor &v);
    /** @param v New color; emits styleChanged on a change. */
    void setBedColor(const QColor &v);
    /** @param v New color; emits styleChanged on a change. */
    void setNoExchangeColor(const QColor &v);
    /** @param v Size clamped to [2, 40] logical pixels; emits styleChanged on a change. */
    void setMarkerSizePx(double v);

    /** @return Colors (ARGB) and marker size. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @param j Partial object; absent or invalid keys keep their current values. */
    void fromJson(const QJsonObject &j) override;

private:
    QColor m_sourceColor = QColor(0x1f, 0x5f, 0xd6, 235);     // blue
    QColor m_bedColor = QColor(0x0b, 0x8a, 0x6f, 235);        // teal
    QColor m_noExchangeColor = QColor(0x8c, 0x8c, 0x8c, 200); // gray
    double m_markerSizePx = 9.0;
};

class GroundwaterMarkerSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @param id_ Stable sublayer id. @param parent Optional QObject owner. */
    explicit GroundwaterMarkerSublayer(QString id_, QObject *parent = nullptr);

    Kind kind() const override { return MarkerKind; }
    QString id() const override { return m_id; }
    QString displayName() const override { return tr("Groundwater Markers"); }
    bool isVisible() const override { return m_visible; }
    void setVisible(bool v) override;
    qreal opacity() const override { return m_opacity; }
    void setOpacity(qreal o) override;
    bool isDynamic() const override { return false; }
    SublayerStyle *style() override { return m_style; }

    /** @return One legend row per marker kind (source, bed, bed with exchange off). */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @return @p existing unchanged; the mesh renderers build the geometry. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @return The owned style. */
    [[nodiscard]] GroundwaterMarkerStyle *markerStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    GroundwaterMarkerStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
