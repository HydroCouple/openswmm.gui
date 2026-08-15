/*!
 * \file   depthcolorrampsublayer.h
 * \brief  Dynamic graduated colour-ramp fill over a 2D mesh.
 *         Plan §3 — second sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.2.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_DEPTHCOLORRAMPSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_DEPTHCOLORRAMPSUBLAYER_H

#include "render/classificationscheme.h"
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
    // VS.8 — named built-in/custom ramp ("" = two-colour low→high
    // gradient, the legacy default), optionally inverted.
    Q_PROPERTY(QString colorRampName   READ colorRampName   WRITE setColorRampName   NOTIFY styleChanged)
    Q_PROPERTY(bool    invertRamp      READ invertRamp      WRITE setInvertRamp      NOTIFY styleChanged)
    Q_PROPERTY(QColor  lowColor        READ lowColor        WRITE setLowColor        NOTIFY styleChanged)
    Q_PROPERTY(QColor  highColor       READ highColor       WRITE setHighColor       NOTIFY styleChanged)
    Q_PROPERTY(QColor  belowMinColor   READ belowMinColor   WRITE setBelowMinColor   NOTIFY styleChanged)
    Q_PROPERTY(QColor  aboveMaxColor   READ aboveMaxColor   WRITE setAboveMaxColor   NOTIFY styleChanged)
    Q_PROPERTY(bool    useLogScale     READ useLogScale     WRITE setUseLogScale     NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:minValue",      "Range")
    Q_CLASSINFO("group:maxValue",      "Range")
    Q_CLASSINFO("group:colorRampName", "Ramp")
    Q_CLASSINFO("group:invertRamp",    "Ramp")
    Q_CLASSINFO("group:lowColor",      "Ramp")
    Q_CLASSINFO("group:highColor",     "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:useLogScale",   "Range")

public:
    // Slice US.2 — the ramp/range/class knobs live in a shared
    // ClassificationScheme (mode Continuous = smooth Gouraud, Classified =
    // graduated). The legacy accessors are pure forwards so the property grid
    // and every renderer / legend call site keep working.
    explicit DepthColorRampStyle(QObject *parent = nullptr);

    [[nodiscard]] QString attribute()     const { return m_attribute; }
    [[nodiscard]] double  minValue()      const { return m_scheme.rangeMin(); }
    [[nodiscard]] double  maxValue()      const { return m_scheme.rangeMax(); }
    [[nodiscard]] QString colorRampName() const { return m_scheme.rampName(); }
    [[nodiscard]] bool    invertRamp()    const { return m_scheme.invertRamp(); }
    [[nodiscard]] QColor  lowColor()      const { return m_scheme.lowColor(); }
    [[nodiscard]] QColor  highColor()     const { return m_scheme.highColor(); }
    [[nodiscard]] QColor  belowMinColor() const { return m_belowMinColor; }
    [[nodiscard]] QColor  aboveMaxColor() const { return m_aboveMaxColor; }
    [[nodiscard]] bool    useLogScale()   const { return m_useLogScale; }

    void setAttribute(const QString &v);
    void setMinValue(double v);
    void setMaxValue(double v);
    void setColorRampName(const QString &v);
    void setInvertRamp(bool v);
    void setLowColor(const QColor &v);
    void setHighColor(const QColor &v);
    void setBelowMinColor(const QColor &v);
    void setAboveMaxColor(const QColor &v);
    void setUseLogScale(bool v);

    /*! Slice US.2 — the embedded classification scheme (display mode, ramp,
     *  class count, range, per-class overrides). */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &s);

    /*! VS.8 / US.2 — colour at normalised ramp position \p f in [0,1]:
     *  forwards to scheme().colorAtF — samples the named ramp (inverted when
     *  requested) or lerps lowColor→highColor when the ramp name is empty. */
    [[nodiscard]] QColor colorAtF(double f) const;

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QString             m_attribute     = QStringLiteral("depth");
    QColor              m_belowMinColor = QColor(  0,   0,   0,   0);   // transparent (dry)
    QColor              m_aboveMaxColor = QColor(255, 255, 255, 200);
    bool                m_useLogScale   = false;
    ClassificationScheme m_scheme;
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
