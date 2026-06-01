/*!
 * \file   velocityvectorsublayer.h
 * \brief  Dynamic velocity-arrow glyph sublayer over a 2D mesh.
 *         Plan §3 — third sublayer in the SWMM2DResultsLayer default mix.
 *         Slice S5.3.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_VELOCITYVECTORSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_VELOCITYVECTORSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"
#include "render/colorramp.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class VelocityVectorStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double glyphLengthScalePxPerMps READ glyphLengthScalePxPerMps WRITE setGlyphLengthScalePxPerMps NOTIFY styleChanged)
    Q_PROPERTY(double glyphLengthMinPx         READ glyphLengthMinPx         WRITE setGlyphLengthMinPx         NOTIFY styleChanged)
    Q_PROPERTY(double glyphLengthMaxPx         READ glyphLengthMaxPx         WRITE setGlyphLengthMaxPx         NOTIFY styleChanged)
    Q_PROPERTY(double glyphSpacingPx           READ glyphSpacingPx           WRITE setGlyphSpacingPx           NOTIFY styleChanged)
    Q_PROPERTY(double headSizePx               READ headSizePx               WRITE setHeadSizePx               NOTIFY styleChanged)
    Q_PROPERTY(QColor color                    READ color                    WRITE setColor                    NOTIFY styleChanged)
    Q_PROPERTY(double dryDepthCutoff           READ dryDepthCutoff           WRITE setDryDepthCutoff           NOTIFY styleChanged)
    // VS.7 — colour arrows by speed magnitude through a ramp.
    Q_PROPERTY(bool    colorByMagnitude        READ colorByMagnitude         WRITE setColorByMagnitude         NOTIFY styleChanged)
    Q_PROPERTY(QString colorRampName           READ colorRampName            WRITE setColorRampName            NOTIFY styleChanged)
    Q_PROPERTY(double  speedMinMps             READ speedMinMps              WRITE setSpeedMinMps              NOTIFY styleChanged)
    Q_PROPERTY(double  speedMaxMps             READ speedMaxMps              WRITE setSpeedMaxMps              NOTIFY styleChanged)

    Q_CLASSINFO("group:glyphLengthScalePxPerMps", "Glyph")
    Q_CLASSINFO("group:glyphLengthMinPx",         "Glyph")
    Q_CLASSINFO("group:glyphLengthMaxPx",         "Glyph")
    Q_CLASSINFO("group:glyphSpacingPx",           "Placement")
    Q_CLASSINFO("group:headSizePx",               "Glyph")
    Q_CLASSINFO("group:color",                    "Glyph")
    Q_CLASSINFO("group:dryDepthCutoff",           "Filtering")
    Q_CLASSINFO("group:colorByMagnitude",         "Colour")
    Q_CLASSINFO("group:colorRampName",            "Colour")
    Q_CLASSINFO("group:speedMinMps",              "Colour")
    Q_CLASSINFO("group:speedMaxMps",              "Colour")

public:
    explicit VelocityVectorStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    [[nodiscard]] double glyphLengthScalePxPerMps() const { return m_glyphLengthScalePxPerMps; }
    [[nodiscard]] double glyphLengthMinPx() const         { return m_glyphLengthMinPx; }
    [[nodiscard]] double glyphLengthMaxPx() const         { return m_glyphLengthMaxPx; }
    [[nodiscard]] double glyphSpacingPx() const           { return m_glyphSpacingPx; }
    [[nodiscard]] double headSizePx() const               { return m_headSizePx; }
    [[nodiscard]] QColor color() const                    { return m_color; }
    [[nodiscard]] double dryDepthCutoff() const           { return m_dryDepthCutoff; }
    [[nodiscard]] bool    colorByMagnitude() const         { return m_colorByMagnitude; }
    [[nodiscard]] QString colorRampName() const            { return m_colorRampName; }
    [[nodiscard]] double  speedMinMps() const              { return m_speedMinMps; }
    [[nodiscard]] double  speedMaxMps() const              { return m_speedMaxMps; }

    void setGlyphLengthScalePxPerMps(double v);
    void setGlyphLengthMinPx(double v);
    void setGlyphLengthMaxPx(double v);
    void setGlyphSpacingPx(double v);
    void setHeadSizePx(double v);
    void setColor(const QColor &v);
    void setDryDepthCutoff(double v);
    void setColorByMagnitude(bool v);
    void setColorRampName(const QString &v);
    void setSpeedMinMps(double v);
    void setSpeedMaxMps(double v);

    /*! VS.7 — colour for a given speed magnitude (m/s). When
     *  colorByMagnitude is off, returns the flat `color()`. Otherwise samples
     *  the named ramp over [speedMinMps, speedMaxMps]. */
    [[nodiscard]] QColor colorForSpeed(double speedMps) const;

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    double m_glyphLengthScalePxPerMps = 20.0;
    double m_glyphLengthMinPx         = 4.0;
    double m_glyphLengthMaxPx         = 40.0;
    double m_glyphSpacingPx           = 30.0;
    double m_headSizePx               = 5.0;
    QColor m_color                    = QColor(20, 20, 20, 220);
    double m_dryDepthCutoff           = 0.01;
    // VS.7 — colour-by-magnitude.
    bool    m_colorByMagnitude = false;
    QString m_colorRampName    = QStringLiteral("viridis");
    double  m_speedMinMps      = 0.0;
    double  m_speedMaxMps      = 2.0;
};

class VelocityVectorSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit VelocityVectorSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return VectorGlyphKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Velocity vectors"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return true; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] VelocityVectorStyle *vectorStyle() const { return m_style; }

private:
    QString               m_id;
    bool                  m_visible = false; // default off per plan §3
    qreal                 m_opacity = 1.0;
    VelocityVectorStyle  *m_style;
};

} // namespace OpenSWMM::Render

#endif
