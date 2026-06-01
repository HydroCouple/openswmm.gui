/*!
 * \file   flowarrowsublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Direction-only flow-arrow sublayer for 2D results animation.
 *
 *         Sits alongside VelocityVectorSublayer in the 2D results mix.
 *         The difference is intent: VelocityVectorSublayer encodes
 *         magnitude as glyph length so the user can see how fast water
 *         moves; FlowArrowSublayer renders fixed-length arrows on a
 *         regular sample grid so the user can read flow *direction* at a
 *         glance regardless of magnitude (cleaner pattern reading at
 *         scale, particularly for shallow flow with tiny |v|).
 *
 *         Dynamic — isDynamic() == true. The renderer rebuilds the arrow
 *         field on every animation tick.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_FLOWARROWSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_FLOWARROWSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/colorramp.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class FlowArrowStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double arrowLengthPx   READ arrowLengthPx   WRITE setArrowLengthPx   NOTIFY styleChanged)
    Q_PROPERTY(double arrowSpacingPx  READ arrowSpacingPx  WRITE setArrowSpacingPx  NOTIFY styleChanged)
    Q_PROPERTY(double headSizePx      READ headSizePx      WRITE setHeadSizePx      NOTIFY styleChanged)
    Q_PROPERTY(double shaftWidthPx    READ shaftWidthPx    WRITE setShaftWidthPx    NOTIFY styleChanged)
    Q_PROPERTY(QColor color           READ color           WRITE setColor           NOTIFY styleChanged)
    Q_PROPERTY(QColor outlineColor    READ outlineColor    WRITE setOutlineColor    NOTIFY styleChanged)
    Q_PROPERTY(double dryDepthCutoff  READ dryDepthCutoff  WRITE setDryDepthCutoff  NOTIFY styleChanged)
    Q_PROPERTY(bool   placeAtCellCenters READ placeAtCellCenters WRITE setPlaceAtCellCenters NOTIFY styleChanged)
    // VS.7 — colour direction arrows by speed magnitude through a ramp.
    Q_PROPERTY(bool    colorByMagnitude  READ colorByMagnitude WRITE setColorByMagnitude NOTIFY styleChanged)
    Q_PROPERTY(QString colorRampName     READ colorRampName    WRITE setColorRampName    NOTIFY styleChanged)
    Q_PROPERTY(double  speedMinMps       READ speedMinMps      WRITE setSpeedMinMps      NOTIFY styleChanged)
    Q_PROPERTY(double  speedMaxMps       READ speedMaxMps      WRITE setSpeedMaxMps      NOTIFY styleChanged)

    Q_CLASSINFO("group:arrowLengthPx",      "Arrow")
    Q_CLASSINFO("group:headSizePx",         "Arrow")
    Q_CLASSINFO("group:shaftWidthPx",       "Arrow")
    Q_CLASSINFO("group:color",              "Arrow")
    Q_CLASSINFO("group:outlineColor",       "Arrow")
    Q_CLASSINFO("group:arrowSpacingPx",     "Placement")
    Q_CLASSINFO("group:placeAtCellCenters", "Placement")
    Q_CLASSINFO("group:dryDepthCutoff",     "Filtering")
    Q_CLASSINFO("group:colorByMagnitude",   "Colour")
    Q_CLASSINFO("group:colorRampName",      "Colour")
    Q_CLASSINFO("group:speedMinMps",        "Colour")
    Q_CLASSINFO("group:speedMaxMps",        "Colour")

public:
    explicit FlowArrowStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] double arrowLengthPx()      const { return m_arrowLengthPx; }
    [[nodiscard]] double arrowSpacingPx()     const { return m_arrowSpacingPx; }
    [[nodiscard]] double headSizePx()         const { return m_headSizePx; }
    [[nodiscard]] double shaftWidthPx()       const { return m_shaftWidthPx; }
    [[nodiscard]] QColor color()              const { return m_color; }
    [[nodiscard]] QColor outlineColor()       const { return m_outlineColor; }
    [[nodiscard]] double dryDepthCutoff()     const { return m_dryDepthCutoff; }
    [[nodiscard]] bool   placeAtCellCenters() const { return m_placeAtCellCenters; }
    [[nodiscard]] bool    colorByMagnitude() const  { return m_colorByMagnitude; }
    [[nodiscard]] QString colorRampName() const     { return m_colorRampName; }
    [[nodiscard]] double  speedMinMps() const       { return m_speedMinMps; }
    [[nodiscard]] double  speedMaxMps() const       { return m_speedMaxMps; }

    void setArrowLengthPx(double v);
    void setArrowSpacingPx(double v);
    void setHeadSizePx(double v);
    void setShaftWidthPx(double v);
    void setColor(const QColor &v)         { if (m_color == v) return; m_color = v; setDirty(); }
    void setOutlineColor(const QColor &v)  { if (m_outlineColor == v) return; m_outlineColor = v; setDirty(); }
    void setDryDepthCutoff(double v);
    void setPlaceAtCellCenters(bool v)     { if (m_placeAtCellCenters == v) return; m_placeAtCellCenters = v; setDirty(); }
    void setColorByMagnitude(bool v)       { if (m_colorByMagnitude == v) return; m_colorByMagnitude = v; setDirty(); }
    void setColorRampName(const QString &v){ if (m_colorRampName == v) return; m_colorRampName = v; setDirty(); }
    void setSpeedMinMps(double v)          { if (qFuzzyCompare(m_speedMinMps+1.0, v+1.0)) return; m_speedMinMps = v; setDirty(); }
    void setSpeedMaxMps(double v)          { if (qFuzzyCompare(m_speedMaxMps+1.0, v+1.0)) return; m_speedMaxMps = v; setDirty(); }

    /*! VS.7 — colour for a speed magnitude (m/s). Flat color() when
     *  colorByMagnitude is off, else the named ramp over [min,max]. */
    [[nodiscard]] QColor colorForSpeed(double speedMps) const;

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    double m_arrowLengthPx      = 18.0;
    double m_arrowSpacingPx     = 36.0;
    double m_headSizePx         = 6.0;
    double m_shaftWidthPx       = 1.6;
    QColor m_color              = QColor(20, 20, 20, 230);
    QColor m_outlineColor       = QColor(255, 255, 255, 200);
    double m_dryDepthCutoff     = 0.01;
    bool   m_placeAtCellCenters = false; // false = regular screen grid
    // VS.7 — colour-by-magnitude.
    bool    m_colorByMagnitude = false;
    QString m_colorRampName    = QStringLiteral("viridis");
    double  m_speedMinMps      = 0.0;
    double  m_speedMaxMps      = 2.0;
};

class FlowArrowSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit FlowArrowSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return ArrowKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Flow direction arrows"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return true; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] FlowArrowStyle *flowArrowStyle() const { return m_style; }

private:
    QString         m_id;
    bool            m_visible = false; // off by default — opt-in alongside VelocityVector
    qreal           m_opacity = 1.0;
    FlowArrowStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
