/*!
 * \file   meshedgesublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Stylable wireframe-edge sublayer for the 2D mesh.
 *
 *         Static — isDynamic() == false. Edge geometry depends on the mesh
 *         topology, not on the animation period.
 *
 *         Replaces the hard-coded edge pass in SWMM2DMeshQSGRenderer with
 *         a property bag the user can edit through the layer-tree
 *         "Edit Sublayer Style..." dialog. The slope-driven thin/wide
 *         split shipped in AU.6 is preserved as an opt-in advanced toggle
 *         (useSlopeDrivenWidth); the default behaviour is a uniform
 *         lineWidthPx so the new style maps onto a single intuitive knob.
 */
#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHEDGESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHEDGESUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

class MeshEdgeStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor       color                READ color                WRITE setColor                NOTIFY styleChanged)
    Q_PROPERTY(double       lineWidthPx          READ lineWidthPx          WRITE setLineWidthPx          NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern          READ dashPattern          WRITE setDashPattern          NOTIFY styleChanged)
    Q_PROPERTY(bool         useSlopeDrivenWidth  READ useSlopeDrivenWidth  WRITE setUseSlopeDrivenWidth  NOTIFY styleChanged)
    Q_PROPERTY(double       slopeBreak           READ slopeBreak           WRITE setSlopeBreak           NOTIFY styleChanged)
    Q_PROPERTY(double       wideWidthPx          READ wideWidthPx          WRITE setWideWidthPx          NOTIFY styleChanged)
    Q_PROPERTY(QColor       wideColor            READ wideColor            WRITE setWideColor            NOTIFY styleChanged)

    // ── Boundary-condition colouring ───────────────────────────────────
    // When colorByBC is on, every mesh edge takes its colour from the BC
    // type stored in the layer's flat [tri*3 + edgeLocal] BC vector.
    // Interior edges resolve to Wall (they are Wall by construction), so
    // bcWallColor governs the mesh interior and the six remaining colours
    // light up only the boundary ring.
    //
    // Seven explicit colour properties rather than a ClassificationScheme:
    // the scheme's two modes (Continuous / Classified) are both numeric,
    // and forcing a 7-value enum through manual breaks would put fake
    // numeric edges in the legend labels. The enum is frozen at seven
    // (mesh::MeshBCTypes::Type), so there is no open-ended growth here.
    Q_PROPERTY(bool   colorByBC          READ colorByBC          WRITE setColorByBC          NOTIFY styleChanged)
    // Width is per type for the same reason colour is: a rating-curve reach
    // and a constant-stage reach are different things, and on a whole-domain
    // view the ring is the only thing drawn, so relative weight is the
    // cheapest way to rank them. Wall has no width property — Wall edges ARE
    // the interior wireframe and keep lineWidthPx / wideWidthPx.
    Q_PROPERTY(double bcNormalFlowWidthPx  READ bcNormalFlowWidthPx  WRITE setBcNormalFlowWidthPx  NOTIFY styleChanged)
    Q_PROPERTY(double bcStageConstWidthPx  READ bcStageConstWidthPx  WRITE setBcStageConstWidthPx  NOTIFY styleChanged)
    Q_PROPERTY(double bcStageTSWidthPx     READ bcStageTSWidthPx     WRITE setBcStageTSWidthPx     NOTIFY styleChanged)
    Q_PROPERTY(double bcFlowConstWidthPx   READ bcFlowConstWidthPx   WRITE setBcFlowConstWidthPx   NOTIFY styleChanged)
    Q_PROPERTY(double bcFlowTSWidthPx      READ bcFlowTSWidthPx      WRITE setBcFlowTSWidthPx      NOTIFY styleChanged)
    Q_PROPERTY(double bcRatingCurveWidthPx READ bcRatingCurveWidthPx WRITE setBcRatingCurveWidthPx NOTIFY styleChanged)
    Q_PROPERTY(QColor bcWallColor        READ bcWallColor        WRITE setBcWallColor        NOTIFY styleChanged)
    Q_PROPERTY(QColor bcNormalFlowColor  READ bcNormalFlowColor  WRITE setBcNormalFlowColor  NOTIFY styleChanged)
    Q_PROPERTY(QColor bcStageConstColor  READ bcStageConstColor  WRITE setBcStageConstColor  NOTIFY styleChanged)
    Q_PROPERTY(QColor bcStageTSColor     READ bcStageTSColor     WRITE setBcStageTSColor     NOTIFY styleChanged)
    Q_PROPERTY(QColor bcFlowConstColor   READ bcFlowConstColor   WRITE setBcFlowConstColor   NOTIFY styleChanged)
    Q_PROPERTY(QColor bcFlowTSColor      READ bcFlowTSColor      WRITE setBcFlowTSColor      NOTIFY styleChanged)
    Q_PROPERTY(QColor bcRatingCurveColor READ bcRatingCurveColor WRITE setBcRatingCurveColor NOTIFY styleChanged)
    // Slice US (mesh) — edges can be classified by slope through the shared
    // ClassificationScheme. The default 2-class scheme mirrors the legacy
    // thin/wide slope split (colour = colour, wideColor = high class); the
    // renderer keeps the historic two-tier path until the scheme is
    // customized. useSlopeDrivenWidth stays the on/off gate.
    Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classification READ scheme WRITE setScheme NOTIFY styleChanged)

    Q_CLASSINFO("group:color",               "Symbology")
    Q_CLASSINFO("group:lineWidthPx",         "Symbology")
    Q_CLASSINFO("group:dashPattern",         "Symbology")
    Q_CLASSINFO("group:useSlopeDrivenWidth", "Slope emphasis")
    Q_CLASSINFO("group:slopeBreak",          "Slope emphasis")
    Q_CLASSINFO("group:wideWidthPx",         "Slope emphasis")
    Q_CLASSINFO("group:wideColor",           "Slope emphasis")
    Q_CLASSINFO("group:classification",      "Classification")
    Q_CLASSINFO("group:colorByBC",           "Boundary conditions")
    Q_CLASSINFO("group:bcNormalFlowWidthPx",  "Boundary conditions")
    Q_CLASSINFO("group:bcStageConstWidthPx",  "Boundary conditions")
    Q_CLASSINFO("group:bcStageTSWidthPx",     "Boundary conditions")
    Q_CLASSINFO("group:bcFlowConstWidthPx",   "Boundary conditions")
    Q_CLASSINFO("group:bcFlowTSWidthPx",      "Boundary conditions")
    Q_CLASSINFO("group:bcRatingCurveWidthPx", "Boundary conditions")
    Q_CLASSINFO("group:bcWallColor",         "Boundary conditions")
    Q_CLASSINFO("group:bcNormalFlowColor",   "Boundary conditions")
    Q_CLASSINFO("group:bcStageConstColor",   "Boundary conditions")
    Q_CLASSINFO("group:bcStageTSColor",      "Boundary conditions")
    Q_CLASSINFO("group:bcFlowConstColor",    "Boundary conditions")
    Q_CLASSINFO("group:bcFlowTSColor",       "Boundary conditions")
    Q_CLASSINFO("group:bcRatingCurveColor",  "Boundary conditions")

public:
    /*! Number of BC types (mesh::MeshBCTypes::Type). Kept as a plain
     *  constant so this render header does not pull in mesh/meshbctype.h;
     *  a static_assert in the .cpp locks the two together. */
    static constexpr int kBcTypeCount = 7;

    explicit MeshEdgeStyle(QObject *parent = nullptr);

    [[nodiscard]] QColor       color() const               { return m_color; }
    [[nodiscard]] double       lineWidthPx() const         { return m_lineWidthPx; }
    [[nodiscard]] Qt::PenStyle dashPattern() const         { return m_dashPattern; }
    [[nodiscard]] bool         useSlopeDrivenWidth() const { return m_useSlopeDrivenWidth; }
    [[nodiscard]] double       slopeBreak() const          { return m_slopeBreak; }
    [[nodiscard]] double       wideWidthPx() const         { return m_wideWidthPx; }
    [[nodiscard]] QColor       wideColor() const           { return m_wideColor; }

    void setColor(const QColor &v)              { if (m_color == v) return; m_color = v; setDirty(); }
    void setLineWidthPx(double v);
    void setDashPattern(Qt::PenStyle v)         { if (m_dashPattern == v) return; m_dashPattern = v; setDirty(); }
    void setUseSlopeDrivenWidth(bool v)         { if (m_useSlopeDrivenWidth == v) return; m_useSlopeDrivenWidth = v; setDirty(); }
    void setSlopeBreak(double v);
    void setWideWidthPx(double v);
    void setWideColor(const QColor &v)          { if (m_wideColor == v) return; m_wideColor = v; setDirty(); }

    [[nodiscard]] bool   colorByBC() const          { return m_colorByBC; }
    [[nodiscard]] double bcNormalFlowWidthPx() const  { return m_bcWidths[1]; }
    [[nodiscard]] double bcStageConstWidthPx() const  { return m_bcWidths[2]; }
    [[nodiscard]] double bcStageTSWidthPx() const     { return m_bcWidths[3]; }
    [[nodiscard]] double bcFlowConstWidthPx() const   { return m_bcWidths[4]; }
    [[nodiscard]] double bcFlowTSWidthPx() const      { return m_bcWidths[5]; }
    [[nodiscard]] double bcRatingCurveWidthPx() const { return m_bcWidths[6]; }
    [[nodiscard]] QColor bcWallColor() const        { return m_bcColors[0]; }
    [[nodiscard]] QColor bcNormalFlowColor() const  { return m_bcColors[1]; }
    [[nodiscard]] QColor bcStageConstColor() const  { return m_bcColors[2]; }
    [[nodiscard]] QColor bcStageTSColor() const     { return m_bcColors[3]; }
    [[nodiscard]] QColor bcFlowConstColor() const   { return m_bcColors[4]; }
    [[nodiscard]] QColor bcFlowTSColor() const      { return m_bcColors[5]; }
    [[nodiscard]] QColor bcRatingCurveColor() const { return m_bcColors[6]; }

    void setColorByBC(bool v)                   { if (m_colorByBC == v) return; m_colorByBC = v; setDirty(); }
    void setBcNormalFlowWidthPx(double v)  { setBcWidth(1, v); }
    void setBcStageConstWidthPx(double v)  { setBcWidth(2, v); }
    void setBcStageTSWidthPx(double v)     { setBcWidth(3, v); }
    void setBcFlowConstWidthPx(double v)   { setBcWidth(4, v); }
    void setBcFlowTSWidthPx(double v)      { setBcWidth(5, v); }
    void setBcRatingCurveWidthPx(double v) { setBcWidth(6, v); }
    void setBcWallColor(const QColor &v)        { setBcColor(0, v); }
    void setBcNormalFlowColor(const QColor &v)  { setBcColor(1, v); }
    void setBcStageConstColor(const QColor &v)  { setBcColor(2, v); }
    void setBcStageTSColor(const QColor &v)     { setBcColor(3, v); }
    void setBcFlowConstColor(const QColor &v)   { setBcColor(4, v); }
    void setBcFlowTSColor(const QColor &v)      { setBcColor(5, v); }
    void setBcRatingCurveColor(const QColor &v) { setBcColor(6, v); }

    /*! Colour for BC type index \p t (0..kBcTypeCount-1). Out-of-range
     *  indices fold onto Wall so a corrupt/forward-version BC value cannot
     *  read past the array. */
    [[nodiscard]] QColor bcColorForType(int t) const
    { return m_bcColors[(t >= 0 && t < kBcTypeCount) ? t : 0]; }

    /*! Width in px for BC type index \p t, same folding rule as
     *  bcColorForType(). Index 0 (Wall) is never consulted by the renderer —
     *  Wall edges are drawn by the wireframe pass at lineWidthPx. */
    [[nodiscard]] double bcWidthForType(int t) const
    { return m_bcWidths[(t >= 0 && t < kBcTypeCount) ? t : 0]; }

    /*! Indexed BC setters. Public because callers that seed a whole style
     *  from user preferences (SWMM2DMeshLayer) loop over the type enum
     *  rather than naming each of the thirteen typed setters above.
     *  Out-of-range indices are ignored. */
    void setBcColor(int idx, const QColor &v);
    void setBcWidth(int idx, double v);

    /*! The embedded slope classification scheme. The default 2-class scheme
     *  reproduces the legacy thin/wide split; the renderer colours edges by
     *  slope class when the scheme is customized past that default. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const ClassificationScheme &s);

    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;

private:
    /*! Seed the 2-class scheme from the legacy slopeBreak/color/wideColor so
     *  the default look (and pre-scheme saved styles) are preserved. */
    void seedSchemeFromLegacy();



    QColor       m_color               = QColor(0, 0, 0, 130);
    double       m_lineWidthPx         = 0.35;
    Qt::PenStyle m_dashPattern         = Qt::SolidLine;
    bool         m_useSlopeDrivenWidth = true;
    double       m_slopeBreak          = 0.35;   // fraction of maxSlope above which edges go wide
    double       m_wideWidthPx         = 0.90;
    QColor       m_wideColor           = QColor(0, 0, 0, 210);
    ClassificationScheme m_scheme;

    // Off by default: an existing project renders byte-identically until the
    // user opts in. m_bcColors[0] (Wall) defaults to m_color for the same
    // reason — enabling the toggle lights up the boundary without repainting
    // the interior. Defaults are set in the ctor (see the .cpp for the
    // stage-cool / flow-warm rationale).
    bool   m_colorByBC = false;
    QColor m_bcColors[kBcTypeCount];
    double m_bcWidths[kBcTypeCount];
};

class MeshEdgeSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit MeshEdgeSublayer(QString id_, QObject *parent = nullptr);

    Kind    kind() const override        { return LineKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return tr("Mesh Edges"); }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override;
    qreal opacity() const override       { return m_opacity; }
    void  setOpacity(qreal o) override;

    bool isDynamic() const override      { return false; }

    SublayerStyle *style() override      { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override;
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &) override;

    [[nodiscard]] MeshEdgeStyle *edgeStyle() const { return m_style; }

    /*! \brief BC type indices actually present in the host mesh.
     *
     *  Pushed by SWMM2DMeshLayer (the model) whenever the BC vector changes,
     *  so legendSymbolItems() — which is const and takes no context — can
     *  emit one row per *present* type instead of seven rows of which five
     *  are usually dead. Wall is always treated as present. */
    void setBcTypesPresent(const QSet<int> &types);
    [[nodiscard]] const QSet<int> &bcTypesPresent() const { return m_bcTypesPresent; }

private:
    QString        m_id;
    bool           m_visible = true;
    qreal          m_opacity = 1.0;
    MeshEdgeStyle *m_style;
    QSet<int>      m_bcTypesPresent { 0 };   ///< Wall is always present
};

} // namespace OpenSWMM::Render

#endif
