/*!
 * \file   meshbcsublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Boundary-condition indicator sublayer for the 2D mesh.
 *
 *         Static — isDynamic() == false. The BC ring depends on the mesh
 *         topology and the authored [2D_BOUNDARY_CONDITIONS] rows, not on
 *         the animation period.
 *
 *         Carved out of MeshEdgeStyle (which used to carry colorByBC + the
 *         per-type colours/widths) so BC indicators are a first-class
 *         sublayer: their own layer-tree row with visibility/opacity, their
 *         own styling tab, and — new — per-BC-type visibility. The
 *         sublayer's own isVisible() replaces the old colorByBC master
 *         toggle; legacy styles migrate in
 *         SWMM2DMeshLayer::onSublayersJsonLoaded.
 *
 *         Seven explicit per-type properties rather than a
 *         ClassificationScheme: the scheme's modes are numeric, and forcing
 *         a 7-value enum through manual breaks would put fake numeric edges
 *         in legend labels. The enum is frozen at seven
 *         (mesh::MeshBCTypes::Type), so there is no open-ended growth.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHBCSUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHBCSUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QString>

namespace OpenSWMM::Render
{

class MeshBcStyle : public SublayerStyle
{
    Q_OBJECT
    // Wall governs the mesh INTERIOR: when the sublayer is visible and
    // wallVisible is on, interior wireframe edges are recoloured with
    // wallColor (the legacy colorByBC behaviour). Wall has no width — Wall
    // edges ARE the wireframe and keep MeshEdgeStyle's line widths.
    Q_PROPERTY(bool   wallVisible          READ wallVisible          WRITE setWallVisible          NOTIFY styleChanged)
    Q_PROPERTY(QColor wallColor            READ wallColor            WRITE setWallColor            NOTIFY styleChanged)
    Q_PROPERTY(bool   normalFlowVisible    READ normalFlowVisible    WRITE setNormalFlowVisible    NOTIFY styleChanged)
    Q_PROPERTY(QColor normalFlowColor      READ normalFlowColor      WRITE setNormalFlowColor      NOTIFY styleChanged)
    Q_PROPERTY(double normalFlowWidthPx    READ normalFlowWidthPx    WRITE setNormalFlowWidthPx    NOTIFY styleChanged)
    Q_PROPERTY(bool   stageConstVisible    READ stageConstVisible    WRITE setStageConstVisible    NOTIFY styleChanged)
    Q_PROPERTY(QColor stageConstColor      READ stageConstColor      WRITE setStageConstColor      NOTIFY styleChanged)
    Q_PROPERTY(double stageConstWidthPx    READ stageConstWidthPx    WRITE setStageConstWidthPx    NOTIFY styleChanged)
    Q_PROPERTY(bool   stageTSVisible       READ stageTSVisible       WRITE setStageTSVisible       NOTIFY styleChanged)
    Q_PROPERTY(QColor stageTSColor         READ stageTSColor         WRITE setStageTSColor         NOTIFY styleChanged)
    Q_PROPERTY(double stageTSWidthPx       READ stageTSWidthPx       WRITE setStageTSWidthPx       NOTIFY styleChanged)
    Q_PROPERTY(bool   flowConstVisible     READ flowConstVisible     WRITE setFlowConstVisible     NOTIFY styleChanged)
    Q_PROPERTY(QColor flowConstColor       READ flowConstColor       WRITE setFlowConstColor       NOTIFY styleChanged)
    Q_PROPERTY(double flowConstWidthPx     READ flowConstWidthPx     WRITE setFlowConstWidthPx     NOTIFY styleChanged)
    Q_PROPERTY(bool   flowTSVisible        READ flowTSVisible        WRITE setFlowTSVisible        NOTIFY styleChanged)
    Q_PROPERTY(QColor flowTSColor          READ flowTSColor          WRITE setFlowTSColor          NOTIFY styleChanged)
    Q_PROPERTY(double flowTSWidthPx        READ flowTSWidthPx        WRITE setFlowTSWidthPx        NOTIFY styleChanged)
    Q_PROPERTY(bool   ratingCurveVisible   READ ratingCurveVisible   WRITE setRatingCurveVisible   NOTIFY styleChanged)
    Q_PROPERTY(QColor ratingCurveColor     READ ratingCurveColor     WRITE setRatingCurveColor     NOTIFY styleChanged)
    Q_PROPERTY(double ratingCurveWidthPx   READ ratingCurveWidthPx   WRITE setRatingCurveWidthPx   NOTIFY styleChanged)

    Q_CLASSINFO("group:wallVisible",        "Wall / interior")
    Q_CLASSINFO("group:wallColor",          "Wall / interior")
    Q_CLASSINFO("group:normalFlowVisible",  "Normal flow")
    Q_CLASSINFO("group:normalFlowColor",    "Normal flow")
    Q_CLASSINFO("group:normalFlowWidthPx",  "Normal flow")
    Q_CLASSINFO("group:stageConstVisible",  "Stage (constant)")
    Q_CLASSINFO("group:stageConstColor",    "Stage (constant)")
    Q_CLASSINFO("group:stageConstWidthPx",  "Stage (constant)")
    Q_CLASSINFO("group:stageTSVisible",     "Stage (series)")
    Q_CLASSINFO("group:stageTSColor",       "Stage (series)")
    Q_CLASSINFO("group:stageTSWidthPx",     "Stage (series)")
    Q_CLASSINFO("group:flowConstVisible",   "Flow (constant)")
    Q_CLASSINFO("group:flowConstColor",     "Flow (constant)")
    Q_CLASSINFO("group:flowConstWidthPx",   "Flow (constant)")
    Q_CLASSINFO("group:flowTSVisible",      "Flow (series)")
    Q_CLASSINFO("group:flowTSColor",        "Flow (series)")
    Q_CLASSINFO("group:flowTSWidthPx",      "Flow (series)")
    Q_CLASSINFO("group:ratingCurveVisible", "Rating curve")
    Q_CLASSINFO("group:ratingCurveColor",   "Rating curve")
    Q_CLASSINFO("group:ratingCurveWidthPx", "Rating curve")

public:
    /*! Number of BC types (mesh::MeshBCTypes::Type). Kept as a plain
     *  constant so this render header does not pull in mesh/meshbctype.h;
     *  a static_assert in the .cpp locks the two together. */
    static constexpr int kBcTypeCount = 7;

    explicit MeshBcStyle(QObject *parent = nullptr);

    [[nodiscard]] bool   wallVisible() const          { return m_typeVisible[0]; }
    [[nodiscard]] QColor wallColor() const            { return m_colors[0]; }
    [[nodiscard]] bool   normalFlowVisible() const    { return m_typeVisible[1]; }
    [[nodiscard]] QColor normalFlowColor() const      { return m_colors[1]; }
    [[nodiscard]] double normalFlowWidthPx() const    { return m_widths[1]; }
    [[nodiscard]] bool   stageConstVisible() const    { return m_typeVisible[2]; }
    [[nodiscard]] QColor stageConstColor() const      { return m_colors[2]; }
    [[nodiscard]] double stageConstWidthPx() const    { return m_widths[2]; }
    [[nodiscard]] bool   stageTSVisible() const       { return m_typeVisible[3]; }
    [[nodiscard]] QColor stageTSColor() const         { return m_colors[3]; }
    [[nodiscard]] double stageTSWidthPx() const       { return m_widths[3]; }
    [[nodiscard]] bool   flowConstVisible() const     { return m_typeVisible[4]; }
    [[nodiscard]] QColor flowConstColor() const       { return m_colors[4]; }
    [[nodiscard]] double flowConstWidthPx() const     { return m_widths[4]; }
    [[nodiscard]] bool   flowTSVisible() const        { return m_typeVisible[5]; }
    [[nodiscard]] QColor flowTSColor() const          { return m_colors[5]; }
    [[nodiscard]] double flowTSWidthPx() const        { return m_widths[5]; }
    [[nodiscard]] bool   ratingCurveVisible() const   { return m_typeVisible[6]; }
    [[nodiscard]] QColor ratingCurveColor() const     { return m_colors[6]; }
    [[nodiscard]] double ratingCurveWidthPx() const   { return m_widths[6]; }

    void setWallVisible(bool v)                 { setBcTypeVisible(0, v); }
    void setWallColor(const QColor &v)          { setBcColor(0, v); }
    void setNormalFlowVisible(bool v)           { setBcTypeVisible(1, v); }
    void setNormalFlowColor(const QColor &v)    { setBcColor(1, v); }
    void setNormalFlowWidthPx(double v)         { setBcWidth(1, v); }
    void setStageConstVisible(bool v)           { setBcTypeVisible(2, v); }
    void setStageConstColor(const QColor &v)    { setBcColor(2, v); }
    void setStageConstWidthPx(double v)         { setBcWidth(2, v); }
    void setStageTSVisible(bool v)              { setBcTypeVisible(3, v); }
    void setStageTSColor(const QColor &v)       { setBcColor(3, v); }
    void setStageTSWidthPx(double v)            { setBcWidth(3, v); }
    void setFlowConstVisible(bool v)            { setBcTypeVisible(4, v); }
    void setFlowConstColor(const QColor &v)     { setBcColor(4, v); }
    void setFlowConstWidthPx(double v)          { setBcWidth(4, v); }
    void setFlowTSVisible(bool v)               { setBcTypeVisible(5, v); }
    void setFlowTSColor(const QColor &v)        { setBcColor(5, v); }
    void setFlowTSWidthPx(double v)             { setBcWidth(5, v); }
    void setRatingCurveVisible(bool v)          { setBcTypeVisible(6, v); }
    void setRatingCurveColor(const QColor &v)   { setBcColor(6, v); }
    void setRatingCurveWidthPx(double v)        { setBcWidth(6, v); }

    /*! Colour for BC type index \p t (0..kBcTypeCount-1). Out-of-range
     *  indices fold onto Wall so a corrupt/forward-version BC value cannot
     *  read past the array. */
    [[nodiscard]] QColor bcColorForType(int t) const
    { return m_colors[(t >= 0 && t < kBcTypeCount) ? t : 0]; }

    /*! Width in px for BC type index \p t, same folding rule. Index 0
     *  (Wall) is never consulted by the renderer — Wall edges are drawn by
     *  the wireframe pass at MeshEdgeStyle's widths. */
    [[nodiscard]] double bcWidthForType(int t) const
    { return m_widths[(t >= 0 && t < kBcTypeCount) ? t : 0]; }

    /*! Per-type visibility, same folding rule. All types default visible —
     *  the sublayer's own isVisible() is the master gate. */
    [[nodiscard]] bool bcTypeVisible(int t) const
    { return m_typeVisible[(t >= 0 && t < kBcTypeCount) ? t : 0]; }

    /*! Indexed setters. Public so callers that seed a whole style (prefs
     *  seeding, legacy migration) loop over the type enum rather than
     *  naming twenty typed setters. Out-of-range indices are ignored. */
    void setBcColor(int idx, const QColor &v);
    void setBcWidth(int idx, double v);
    void setBcTypeVisible(int idx, bool v);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

    /*! Migration: seed colours/widths from a legacy MeshEdgeStyle JSON
     *  object (the pre-split "bc*"-prefixed keys, including the even older
     *  single "bcWidthPx"). The caller decides sublayer visibility from the
     *  legacy "colorByBC" flag — this only copies the styling. */
    void seedFromLegacyEdgeJson(const QJsonObject &j);

private:
    QColor m_colors[kBcTypeCount];
    double m_widths[kBcTypeCount];
    bool   m_typeVisible[kBcTypeCount];
};

class MeshBcSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit MeshBcSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return LineKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Boundary Conditions"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return false; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] MeshBcStyle *bcStyle() const { return m_style; }

    /*! BC type indices actually present in the host mesh. Pushed by
     *  SWMM2DMeshLayer whenever the BC vector changes, so
     *  legendSymbolItems() emits one row per *present* type instead of
     *  seven rows of which five are usually dead. Wall is always present. */
    void setBcTypesPresent(const QSet<int> &types);
    [[nodiscard]] const QSet<int> &bcTypesPresent() const { return m_bcTypesPresent; }

    /*! Base wireframe width shown in the Wall legend row (Wall edges are
     *  drawn by the wireframe pass). Pushed by the layer alongside the
     *  present set. */
    void setWallLegendWidthPx(double px);

private:
    QString      m_id;
    // Hidden by default — mirrors the legacy colorByBC=false default, so a
    // fresh mesh renders byte-identically until the user opts in.
    bool         m_visible = false;
    qreal        m_opacity = 1.0;
    MeshBcStyle *m_style;
    QSet<int>    m_bcTypesPresent { 0 };   ///< Wall is always present
    double       m_wallLegendWidthPx = 0.35;
};

} // namespace OpenSWMM::Render

#endif
