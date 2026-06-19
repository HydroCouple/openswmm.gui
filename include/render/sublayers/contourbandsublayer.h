/*!
 * \file   contourbandsublayer.h
 * \brief  Dynamic marching-squares filled-contour-bands sublayer over a 2D mesh.
 *         Plan §3 — fifth sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.5.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_CONTOURBANDSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_CONTOURBANDSUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class ContourBandStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute     READ attribute     WRITE setAttribute     NOTIFY styleChanged)
    Q_PROPERTY(int     bandCount     READ bandCount     WRITE setBandCount     NOTIFY styleChanged)
    // VS.8 — colour source: a named built-in/custom ramp ("" = use the
    // two-colour lowColor→highColor gradient), optionally inverted.
    Q_PROPERTY(QString colorRampName READ colorRampName WRITE setColorRampName NOTIFY styleChanged)
    Q_PROPERTY(bool    invertRamp    READ invertRamp    WRITE setInvertRamp    NOTIFY styleChanged)
    Q_PROPERTY(QColor  lowColor      READ lowColor      WRITE setLowColor      NOTIFY styleChanged)
    Q_PROPERTY(QColor  highColor     READ highColor     WRITE setHighColor     NOTIFY styleChanged)
    Q_PROPERTY(QColor  belowMinColor READ belowMinColor WRITE setBelowMinColor NOTIFY styleChanged)
    Q_PROPERTY(QColor  aboveMaxColor READ aboveMaxColor WRITE setAboveMaxColor NOTIFY styleChanged)
    Q_PROPERTY(bool    smoothBands   READ smoothBands   WRITE setSmoothBands   NOTIFY styleChanged)
    // VS.8 — fixed classification range. Off = auto-track the layer's
    // [dryDepth, maxDepth] range (legacy behaviour).
    Q_PROPERTY(bool    useCustomRange READ useCustomRange WRITE setUseCustomRange NOTIFY styleChanged)
    Q_PROPERTY(double  rangeMin       READ rangeMin       WRITE setRangeMin       NOTIFY styleChanged)
    Q_PROPERTY(double  rangeMax       READ rangeMax       WRITE setRangeMax       NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:bandCount",     "Classification")
    Q_CLASSINFO("group:useCustomRange","Classification")
    Q_CLASSINFO("group:rangeMin",      "Classification")
    Q_CLASSINFO("group:rangeMax",      "Classification")
    Q_CLASSINFO("group:colorRampName", "Ramp")
    Q_CLASSINFO("group:invertRamp",    "Ramp")
    Q_CLASSINFO("group:lowColor",      "Ramp")
    Q_CLASSINFO("group:highColor",     "Ramp")
    Q_CLASSINFO("group:belowMinColor", "Out of range")
    Q_CLASSINFO("group:aboveMaxColor", "Out of range")
    Q_CLASSINFO("group:smoothBands",   "Rendering")

public:
    // Slice US.2 — the classification knobs (band count, ramp, invert,
    // custom range) now live in a shared ClassificationScheme; the legacy
    // Q_PROPERTY accessors are pure forwards into it so the property grid +
    // every QSG / legend call site keep working while the scheme drives the
    // method-aware level edges.
    explicit ContourBandStyle(QObject *parent = nullptr);

    [[nodiscard]] QString attribute() const     { return m_attribute; }
    [[nodiscard]] int     bandCount() const     { return m_scheme.classCount(); }
    [[nodiscard]] QString colorRampName() const { return m_scheme.rampName(); }
    [[nodiscard]] bool    invertRamp() const    { return m_scheme.invertRamp(); }
    [[nodiscard]] QColor  lowColor() const      { return m_scheme.lowColor(); }
    [[nodiscard]] QColor  highColor() const     { return m_scheme.highColor(); }
    [[nodiscard]] QColor  belowMinColor() const { return m_belowMinColor; }
    [[nodiscard]] QColor  aboveMaxColor() const { return m_aboveMaxColor; }
    [[nodiscard]] bool    smoothBands() const   { return m_smoothBands; }
    [[nodiscard]] bool    useCustomRange() const { return m_scheme.useCustomRange(); }
    [[nodiscard]] double  rangeMin() const       { return m_scheme.rangeMin(); }
    [[nodiscard]] double  rangeMax() const       { return m_scheme.rangeMax(); }

    void setAttribute(const QString &v);
    void setBandCount(int v);
    void setColorRampName(const QString &v);
    void setInvertRamp(bool v);
    void setLowColor(const QColor &v);
    void setHighColor(const QColor &v);
    void setBelowMinColor(const QColor &v);
    void setAboveMaxColor(const QColor &v);
    void setSmoothBands(bool v);
    void setUseCustomRange(bool v);
    void setRangeMin(double v);
    void setRangeMax(double v);

    /*! Slice US.2 — the embedded classification scheme (band classification
     *  method, class count, ramp, range, per-class overrides). */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &s);

    /*! VS.8 — band colour at the midpoint of band \p bandIndex out of
     *  \p bandCount. Forwards to scheme().colorForClass — samples the named
     *  ramp (inverted when requested) or lerps lowColor→highColor when the
     *  ramp name is empty. Opaque — callers apply the sublayer opacity. */
    [[nodiscard]] QColor colorForBand(int bandIndex, int bandCount) const;

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QString             m_attribute     = QStringLiteral("depth");
    QColor              m_belowMinColor = QColor(  0,   0,   0,   0);
    QColor              m_aboveMaxColor = QColor(255, 255, 255, 200);
    bool                m_smoothBands   = true;
    ClassificationScheme m_scheme;
};

class ContourBandSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit ContourBandSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return ContourBandKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Contour bands"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return true; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] ContourBandStyle *bandStyle() const { return m_style; }

private:
    QString            m_id;
    bool               m_visible = false; // off by default per plan §3
    qreal              m_opacity = 0.85;
    ContourBandStyle  *m_style;
};

} // namespace OpenSWMM::Render

#endif
