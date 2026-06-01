/*!
 * \file   depthcolorrampsublayer.h
 * \brief  Dynamic graduated colour-ramp fill over a 2D mesh.
 *         Plan §3 — second sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.2.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_DEPTHCOLORRAMPSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_DEPTHCOLORRAMPSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class DepthColorRampStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute       READ attribute       WRITE setAttribute       NOTIFY styleChanged)
    Q_PROPERTY(double  minValue        READ minValue        WRITE setMinValue        NOTIFY styleChanged)
    Q_PROPERTY(double  maxValue        READ maxValue        WRITE setMaxValue        NOTIFY styleChanged)
    Q_PROPERTY(QColor  lowColor        READ lowColor        WRITE setLowColor        NOTIFY styleChanged)
    Q_PROPERTY(QColor  highColor       READ highColor       WRITE setHighColor       NOTIFY styleChanged)
    Q_PROPERTY(QColor  belowMinColor   READ belowMinColor   WRITE setBelowMinColor   NOTIFY styleChanged)
    Q_PROPERTY(QColor  aboveMaxColor   READ aboveMaxColor   WRITE setAboveMaxColor   NOTIFY styleChanged)
    Q_PROPERTY(bool    useLogScale     READ useLogScale     WRITE setUseLogScale     NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:minValue",      "Range")
    Q_CLASSINFO("group:maxValue",      "Range")
    Q_CLASSINFO("group:lowColor",      "Ramp")
    Q_CLASSINFO("group:highColor",     "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:useLogScale",   "Range")

public:
    explicit DepthColorRampStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] QString attribute()     const { return m_attribute; }
    [[nodiscard]] double  minValue()      const { return m_minValue; }
    [[nodiscard]] double  maxValue()      const { return m_maxValue; }
    [[nodiscard]] QColor  lowColor()      const { return m_lowColor; }
    [[nodiscard]] QColor  highColor()     const { return m_highColor; }
    [[nodiscard]] QColor  belowMinColor() const { return m_belowMinColor; }
    [[nodiscard]] QColor  aboveMaxColor() const { return m_aboveMaxColor; }
    [[nodiscard]] bool    useLogScale()   const { return m_useLogScale; }

    void setAttribute(const QString &v);
    void setMinValue(double v);
    void setMaxValue(double v);
    void setLowColor(const QColor &v);
    void setHighColor(const QColor &v);
    void setBelowMinColor(const QColor &v);
    void setAboveMaxColor(const QColor &v);
    void setUseLogScale(bool v);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QString m_attribute     = QStringLiteral("depth");
    double  m_minValue      = 0.0;
    double  m_maxValue      = 1.0;
    QColor  m_lowColor      = QColor( 60, 100, 200, 200);   // bluish
    QColor  m_highColor     = QColor(200, 220, 255, 200);   // pale blue
    QColor  m_belowMinColor = QColor(  0,   0,   0,   0);   // transparent (dry)
    QColor  m_aboveMaxColor = QColor(255, 255, 255, 200);
    bool    m_useLogScale   = false;
};

class DepthColorRampSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit DepthColorRampSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return ColorRampFillKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Depth color ramp"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return true; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] DepthColorRampStyle *rampStyle() const { return m_style; }

private:
    QString               m_id;
    bool                  m_visible = true;
    qreal                 m_opacity = 1.0;
    DepthColorRampStyle  *m_style;
};

} // namespace OpenSWMM::Render

#endif
