/*!
 * \file   isolinesublayer.h
 * \brief  Dynamic marching-squares isoline sublayer over a 2D mesh.
 *         Plan §3 — fourth sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.4.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_ISOLINESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_ISOLINESUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <Qt>

#include <vector>

namespace OpenSWMM::Render
{

class IsolineStyle : public SublayerStyle
{
    Q_OBJECT
public:
    /*! VS.8 — how the iso-levels are generated. Count spreads
     *  isoValueCount levels evenly over the data range (legacy);
     *  FixedInterval emits levels at baseLevel + k·levelInterval (the
     *  ArcGIS/QGIS "contour interval + base contour" idiom). */
    enum class LevelMode { Count = 0, FixedInterval = 1 };
    Q_ENUM(LevelMode)

private:
    Q_PROPERTY(QString      attribute     READ attribute     WRITE setAttribute     NOTIFY styleChanged)
    Q_PROPERTY(LevelMode    levelMode     READ levelMode     WRITE setLevelMode     NOTIFY styleChanged)
    Q_PROPERTY(int          isoValueCount READ isoValueCount WRITE setIsoValueCount NOTIFY styleChanged)
    Q_PROPERTY(double       levelInterval READ levelInterval WRITE setLevelInterval NOTIFY styleChanged)
    Q_PROPERTY(double       baseLevel     READ baseLevel     WRITE setBaseLevel     NOTIFY styleChanged)
    Q_PROPERTY(double       lineWidthPx   READ lineWidthPx   WRITE setLineWidthPx   NOTIFY styleChanged)
    Q_PROPERTY(QColor       color         READ color         WRITE setColor         NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern   READ dashPattern   WRITE setDashPattern   NOTIFY styleChanged)
    // VS.8 — index contours: every Nth level draws with indexWidthPx
    // (0 = off). Standard topographic-map emphasis.
    Q_PROPERTY(int          indexEvery    READ indexEvery    WRITE setIndexEvery    NOTIFY styleChanged)
    Q_PROPERTY(double       indexWidthPx  READ indexWidthPx  WRITE setIndexWidthPx  NOTIFY styleChanged)
    Q_PROPERTY(bool         labels        READ labels        WRITE setLabels        NOTIFY styleChanged)
    Q_PROPERTY(int          labelDecimals READ labelDecimals WRITE setLabelDecimals NOTIFY styleChanged)
    Q_PROPERTY(double       labelFontPt   READ labelFontPt   WRITE setLabelFontPt   NOTIFY styleChanged)
    Q_PROPERTY(bool         labelHalo     READ labelHalo     WRITE setLabelHalo     NOTIFY styleChanged)

    Q_CLASSINFO("group:attribute",     "Classification")
    Q_CLASSINFO("group:levelMode",     "Classification")
    Q_CLASSINFO("group:isoValueCount", "Classification")
    Q_CLASSINFO("group:levelInterval", "Classification")
    Q_CLASSINFO("group:baseLevel",     "Classification")
    Q_CLASSINFO("group:lineWidthPx",   "Symbology")
    Q_CLASSINFO("group:color",         "Symbology")
    Q_CLASSINFO("group:dashPattern",   "Symbology")
    Q_CLASSINFO("group:indexEvery",    "Symbology")
    Q_CLASSINFO("group:indexWidthPx",  "Symbology")
    Q_CLASSINFO("group:labels",        "Labels")
    Q_CLASSINFO("group:labelDecimals", "Labels")
    Q_CLASSINFO("group:labelFontPt",   "Labels")
    Q_CLASSINFO("group:labelHalo",     "Labels")

public:
    // Slice US.2 — level count + classification method now live in a shared
    // ClassificationScheme; isoValueCount forwards to its class count. The
    // FixedInterval params + symbology + label knobs stay on the bag.
    explicit IsolineStyle(QObject *parent = nullptr);

    [[nodiscard]] QString      attribute() const     { return m_attribute; }
    [[nodiscard]] LevelMode    levelMode() const     { return m_levelMode; }
    [[nodiscard]] int          isoValueCount() const { return m_scheme.classCount(); }
    [[nodiscard]] double       levelInterval() const { return m_levelInterval; }
    [[nodiscard]] double       baseLevel() const     { return m_baseLevel; }
    [[nodiscard]] double       lineWidthPx() const   { return m_lineWidthPx; }
    [[nodiscard]] QColor       color() const         { return m_color; }
    [[nodiscard]] Qt::PenStyle dashPattern() const   { return m_dashPattern; }
    [[nodiscard]] int          indexEvery() const    { return m_indexEvery; }
    [[nodiscard]] double       indexWidthPx() const  { return m_indexWidthPx; }
    [[nodiscard]] bool         labels() const        { return m_labels; }
    [[nodiscard]] int          labelDecimals() const { return m_labelDecimals; }
    [[nodiscard]] double       labelFontPt() const   { return m_labelFontPt; }
    [[nodiscard]] bool         labelHalo() const     { return m_labelHalo; }

    void setAttribute(const QString &v);
    void setLevelMode(LevelMode v);
    void setIsoValueCount(int v);
    void setLevelInterval(double v);
    void setBaseLevel(double v);
    void setLineWidthPx(double v);
    void setColor(const QColor &v);
    void setDashPattern(Qt::PenStyle v);
    void setIndexEvery(int v);
    void setIndexWidthPx(double v);
    void setLabels(bool v);
    void setLabelDecimals(int v);
    void setLabelFontPt(double v);
    void setLabelHalo(bool v);

    /*! VS.8 / US.2 — generate the iso-level values for a data range according
     *  to levelMode. Count mode classifies via the embedded scheme (method-
     *  aware: EqualInterval mirrors Contour::evenlySpacedLevels; Quantile /
     *  Jenks / StdDev bin against \p samples when supplied). FixedInterval
     *  mode emits baseLevel + k·levelInterval for every k whose level lands
     *  strictly inside (vMin, vMax). Capped at 256 levels as a runaway guard
     *  for tiny intervals. */
    [[nodiscard]] std::vector<double> levelsForRange(double vMin, double vMax,
                                                     const QVector<double> &samples = {}) const;

    /*! Slice US.2 — the embedded classification scheme (level count + method
     *  + custom range). The ramp fields are unused — isolines paint a single
     *  line colour. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &s);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    QString      m_attribute     = QStringLiteral("depth");
    LevelMode    m_levelMode     = LevelMode::Count;
    double       m_levelInterval = 0.5;
    double       m_baseLevel     = 0.0;
    double       m_lineWidthPx   = 1.0;
    QColor       m_color         = QColor(10, 10, 10, 220);
    Qt::PenStyle m_dashPattern   = Qt::SolidLine;
    int          m_indexEvery    = 0;
    double       m_indexWidthPx  = 2.0;
    bool         m_labels        = false;
    int          m_labelDecimals = 2;
    double       m_labelFontPt   = 9.0;
    bool         m_labelHalo     = true;
    ClassificationScheme m_scheme;
};

class IsolineSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit IsolineSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return IsolineKind; }
    QString id() const override          { return m_id; }
    // Terrain isolines trace bed ELEVATION; results isolines trace water DEPTH.
    QString displayName() const override
    {
        const QString a = m_style ? m_style->attribute() : QString();
        if (a.compare(QLatin1String("elevation"), Qt::CaseInsensitive) == 0)
            return tr("Elevation Isolines");
        return tr("Depth Isolines");
    }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return true; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] IsolineStyle *isolineStyle() const { return m_style; }

private:
    QString       m_id;
    bool          m_visible = false; // off by default per plan §3
    qreal         m_opacity = 1.0;
    IsolineStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
