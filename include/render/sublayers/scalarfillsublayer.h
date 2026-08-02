/*!
 * \file   scalarfillsublayer.h
 * \brief  Per-cell (flat) and per-vertex (Gouraud) scalar-fill sublayers over a
 *         2D mesh. Unlike ContourBandSublayer (which marching-squares the field
 *         into discrete band polygons), these fill the existing triangle mesh
 *         directly:
 *           - CellDepthFillSublayer    — one flat colour per cell (cell-centre
 *             value), classified or continuous.
 *           - SmoothDepthFillSublayer  — a colour per mesh vertex, interpolated
 *             across each triangle by the GPU (Gouraud). Continuous gives a
 *             seam-free gradient; classified bins each vertex.
 *         Both reuse the shared ScalarFillStyle (attribute + ClassificationScheme
 *         whose ClassMode is the Continuous/Classified toggle).
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_SCALARFILLSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_SCALARFILLSUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

/*! Shared style for the two scalar-fill sublayers. The Continuous/Classified
 *  toggle is the embedded ClassificationScheme's ClassMode; in Continuous mode
 *  the renderer samples the ramp by raw value (colorForValue), in Classified
 *  mode it bins by class (colorForClass). */
class ScalarFillStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QString attribute     READ attribute     WRITE setAttribute     NOTIFY styleChanged)
    Q_PROPERTY(bool    classified    READ classified    WRITE setClassified    NOTIFY styleChanged)
    Q_PROPERTY(int     bandCount     READ bandCount     WRITE setBandCount     NOTIFY styleChanged)
    Q_PROPERTY(QString colorRampName READ colorRampName WRITE setColorRampName NOTIFY styleChanged)
    Q_PROPERTY(bool    invertRamp    READ invertRamp    WRITE setInvertRamp    NOTIFY styleChanged)
    Q_PROPERTY(QColor  lowColor      READ lowColor      WRITE setLowColor      NOTIFY styleChanged)
    Q_PROPERTY(QColor  highColor     READ highColor     WRITE setHighColor     NOTIFY styleChanged)
    Q_PROPERTY(bool    useCustomRange READ useCustomRange WRITE setUseCustomRange NOTIFY styleChanged)
    Q_PROPERTY(double  rangeMin       READ rangeMin       WRITE setRangeMin       NOTIFY styleChanged)
    Q_PROPERTY(double  rangeMax       READ rangeMax       WRITE setRangeMax       NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:classified",    "Classification")
    Q_CLASSINFO("group:bandCount",     "Classification")
    Q_CLASSINFO("group:useCustomRange","Classification")
    Q_CLASSINFO("group:rangeMin",      "Classification")
    Q_CLASSINFO("group:rangeMax",      "Classification")
    Q_CLASSINFO("group:colorRampName", "Ramp")
    Q_CLASSINFO("group:invertRamp",    "Ramp")
    Q_CLASSINFO("group:lowColor",      "Ramp")
    Q_CLASSINFO("group:highColor",     "Ramp")

public:
    explicit ScalarFillStyle(QObject *parent = nullptr);

    [[nodiscard]] QString attribute() const     { return m_attribute; }
    [[nodiscard]] bool    classified() const
    { return m_scheme.mode() == ClassificationScheme::ClassMode::Classified; }
    [[nodiscard]] int     bandCount() const     { return m_scheme.classCount(); }
    [[nodiscard]] QString colorRampName() const { return m_scheme.rampName(); }
    [[nodiscard]] bool    invertRamp() const    { return m_scheme.invertRamp(); }
    [[nodiscard]] QColor  lowColor() const       { return m_scheme.lowColor(); }
    [[nodiscard]] QColor  highColor() const      { return m_scheme.highColor(); }
    [[nodiscard]] bool    useCustomRange() const { return m_scheme.useCustomRange(); }
    [[nodiscard]] double  rangeMin() const       { return m_scheme.rangeMin(); }
    [[nodiscard]] double  rangeMax() const       { return m_scheme.rangeMax(); }

    void setAttribute(const QString &v);
    void setClassified(bool v);
    void setBandCount(int v);
    void setColorRampName(const QString &v);
    void setInvertRamp(bool v);
    void setLowColor(const QColor &v);
    void setHighColor(const QColor &v);
    void setUseCustomRange(bool v);
    void setRangeMin(double v);
    void setRangeMax(double v);

    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &s);

    /*! Continuous colour for a raw value over [dataMin, dataMax]. */
    [[nodiscard]] QColor colorForValue(double v, double dataMin, double dataMax) const
    { return m_scheme.colorForValue(v, dataMin, dataMax); }
    /*! Classified colour at the midpoint of class \p classIndex of \p count. */
    [[nodiscard]] QColor colorForClass(int classIndex, int count) const
    { return m_scheme.colorForClass(classIndex, std::max(1, count)); }

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QString              m_attribute = QStringLiteral("depth");
    ClassificationScheme m_scheme;
};

/*! One flat colour per mesh cell, from the cell-centre value. */
class CellDepthFillSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit CellDepthFillSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return FillKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override
    {
        const QString a = m_style ? m_style->attribute() : QString();
        if (a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0)
            return tr("Cell Elevation Fill");
        return tr("Cell Depth Fill");
    }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;
    bool  isDynamic() const override     { return true; }

    SublayerStyle *style() override      { return m_style; }
    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] ScalarFillStyle *fillStyle() const { return m_style; }

private:
    QString          m_id;
    bool             m_visible = false;
    qreal            m_opacity = 1.0;
    ScalarFillStyle *m_style;
};

/*! A colour per mesh vertex, interpolated across each triangle by the GPU
 *  (Gouraud). Continuous mode gives a smooth seam-free gradient — the
 *  artifact-free alternative to marching-squares contour bands. */
class SmoothDepthFillSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit SmoothDepthFillSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return ColorRampFillKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override
    {
        const QString a = m_style ? m_style->attribute() : QString();
        if (a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0)
            return tr("Smooth Elevation Fill");
        return tr("Smooth Depth Fill");
    }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;
    bool  isDynamic() const override     { return true; }

    SublayerStyle *style() override      { return m_style; }
    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] ScalarFillStyle *fillStyle() const { return m_style; }

private:
    QString          m_id;
    bool             m_visible = false;
    qreal            m_opacity = 0.60;  // basemap stays visible under the fill
    ScalarFillStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
