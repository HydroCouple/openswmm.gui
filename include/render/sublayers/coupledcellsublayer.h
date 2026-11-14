/**
 * @file coupledcellsublayer.h
 * @brief Cell footprints carrying explicit [2D_TRIANGLE_NODE_MAP] couplings.
 * @license GPL-3.0-or-later
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_COUPLEDCELLSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_COUPLEDCELLSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include <QColor>
#include <QJsonObject>

namespace OpenSWMM::Render
{

class CoupledCellStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor fillColor READ fillColor WRITE setFillColor NOTIFY styleChanged)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY styleChanged)
    Q_PROPERTY(
        double outlineWidthPx READ outlineWidthPx WRITE setOutlineWidthPx NOTIFY styleChanged)
public:
    explicit CoupledCellStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}
    QColor fillColor() const { return m_fill; }
    QColor outlineColor() const { return m_outline; }
    double outlineWidthPx() const { return m_width; }
    void setFillColor(const QColor &color);
    void setOutlineColor(const QColor &color);
    void setOutlineWidthPx(double width);
    QJsonObject toJson() const override;
    void fromJson(const QJsonObject &json) override;

private:
    QColor m_fill{255, 140, 0, 65};
    QColor m_outline{255, 140, 0, 235};
    double m_width = 2.0;
};

class CoupledCellSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit CoupledCellSublayer(QString id, QObject *parent = nullptr);
    Kind kind() const override { return FillKind; }
    QString id() const override { return m_id; }
    QString displayName() const override { return tr("Coupled Cells"); }
    bool isVisible() const override { return m_visible; }
    void setVisible(bool visible) override;
    qreal opacity() const override { return m_opacity; }
    void setOpacity(qreal opacity) override;
    bool isDynamic() const override { return false; }
    SublayerStyle *style() override { return m_style; }
    CoupledCellStyle *coupledStyle() const { return m_style; }
    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override
    {
        return existing; // Geometry belongs to the mesh renderer.
    }

private:
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    CoupledCellStyle *m_style;
};

} // namespace OpenSWMM::Render
#endif
