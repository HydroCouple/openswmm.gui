/**
 * @file      meshedgesublayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Stylable wireframe-edge sublayer for the 2D mesh.
 *
 *         Static — isDynamic() == false. Edge geometry depends on the mesh
 *         topology, not on the animation period.
 *
 *         Replaces the hard-coded edge pass in SWMM2DMeshQSGRenderer with
 *         a property bag the user can edit through the layer-tree
 *         "Edit Sublayer Style..." dialog. The slope-driven thin/wide
 *         split shipped in AU.6 is preserved by useSlopeDrivenWidth, enabled
 *         by default. Turning it off selects the uniform lineWidthPx path.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHEDGESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHEDGESUBLAYER_H

#include "render/classificationscheme.h"
#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <Qt>

namespace OpenSWMM::Render
{

class MeshEdgeStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(double lineWidthPx READ lineWidthPx WRITE setLineWidthPx NOTIFY styleChanged)
    Q_PROPERTY(Qt::PenStyle dashPattern READ dashPattern WRITE setDashPattern NOTIFY styleChanged)
    Q_PROPERTY(bool useSlopeDrivenWidth READ useSlopeDrivenWidth WRITE setUseSlopeDrivenWidth NOTIFY
                   styleChanged)
    Q_PROPERTY(double slopeBreak READ slopeBreak WRITE setSlopeBreak NOTIFY styleChanged)
    Q_PROPERTY(double wideWidthPx READ wideWidthPx WRITE setWideWidthPx NOTIFY styleChanged)
    Q_PROPERTY(QColor wideColor READ wideColor WRITE setWideColor NOTIFY styleChanged)

    // Boundary-condition colouring moved to MeshBcStyle (meshbcsublayer.h)
    // — BC indicators are their own sublayer now, with per-type visibility.
    // Legacy "bc*"-prefixed JSON keys migrate in
    // SWMM2DMeshLayer::onSublayersJsonLoaded.
    // Slice US (mesh) — edges can be classified by slope through the shared
    // ClassificationScheme. The default 2-class scheme mirrors the legacy
    // thin/wide slope split (colour = colour, wideColor = high class); the
    // renderer keeps the historic two-tier path until the scheme is
    // customized. useSlopeDrivenWidth stays the on/off gate.
    Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classification READ scheme WRITE setScheme
                   NOTIFY styleChanged)

    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:lineWidthPx", "Symbology")
    Q_CLASSINFO("group:dashPattern", "Symbology")
    Q_CLASSINFO("group:useSlopeDrivenWidth", "Slope emphasis")
    Q_CLASSINFO("group:slopeBreak", "Slope emphasis")
    Q_CLASSINFO("group:wideWidthPx", "Slope emphasis")
    Q_CLASSINFO("group:wideColor", "Slope emphasis")
    Q_CLASSINFO("group:classification", "Classification")

public:
    /** @brief Construct translucent black wireframe styling and a two-class slope scheme.
     * @param parent Optional QObject owner. Base/wide widths default to 0.35/0.9
     * logical pixels, slope emphasis enabled, normalized split 0.35 and solid dash.
     * Seeds scheme values without emitting styleChanged(). */
    explicit MeshEdgeStyle(QObject *parent = nullptr);

    /** @brief Read the legacy base-edge color.
     * @return Stored QColor copy; changing it does not automatically reseed scheme overrides. */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read base wireframe width.
     * @return Logical-pixel width, initially 0.35, with finite input clamped to [0.1,20]. */
    [[nodiscard]] double lineWidthPx() const { return m_lineWidthPx; }
    /** @brief Read the stroke's dash-style selection.
     * @return Stored Qt enum, initially SolidLine; unknown values are accepted. */
    [[nodiscard]] Qt::PenStyle dashPattern() const { return m_dashPattern; }
    /** @brief Read the slope emphasis gate.
     * @return Stored flag, initially true; renderer interpretation is separate. */
    [[nodiscard]] bool useSlopeDrivenWidth() const { return m_useSlopeDrivenWidth; }
    /** @brief Read the legacy split in slope/maxSlope units.
     * @return Stored fraction, initially 0.35; finite setter inputs clamp to [0,1]. */
    [[nodiscard]] double slopeBreak() const { return m_slopeBreak; }
    /** @brief Read emphasized edge width.
     * @return Logical-pixel width, initially 0.9; finite setter inputs clamp to [0.1,20]. */
    [[nodiscard]] double wideWidthPx() const { return m_wideWidthPx; }
    /** @brief Read emphasized edge color.
     * @return Stored QColor copy, initially black with alpha 210. */
    [[nodiscard]] QColor wideColor() const { return m_wideColor; }

    /** @brief Store the base color and emit styleChanged() on exact change.
     * @param v Color copied verbatim, including invalid input. Embedded scheme
     * colors/overrides are retained rather than reseeded. */
    void setColor(const QColor &v)
    {
        if (m_color == v)
            return;
        m_color = v;
        setDirty();
    }
    /** @brief Clamp base width and notify when it changes fuzzily.
     * @param v Finite logical-pixel width clamped to [0.1,20]; no explicit finite check occurs.
     * Uses qFuzzyCompare after adding one, then emits styleChanged() once on change. */
    void setLineWidthPx(double v);
    /** @brief Store the dash-style enum and notify on exact change.
     * @param v Qt pen style stored without enum-range validation. */
    void setDashPattern(Qt::PenStyle v)
    {
        if (m_dashPattern == v)
            return;
        m_dashPattern = v;
        setDirty();
    }
    /** @brief Toggle slope emphasis and emit styleChanged() on exact change.
     * @param v True enables the renderer's slope styling; false requests uniform styling. */
    void setUseSlopeDrivenWidth(bool v)
    {
        if (m_useSlopeDrivenWidth == v)
            return;
        m_useSlopeDrivenWidth = v;
        setDirty();
    }
    /** @brief Clamp the legacy slope threshold and notify on fuzzy change.
     * @param v Finite dimensionless slope/maxSlope fraction clamped to [0,1].
     * Does not reseed the embedded scheme; no explicit nonfinite check occurs. */
    void setSlopeBreak(double v);
    /** @brief Clamp emphasized width and notify on fuzzy change.
     * @param v Finite logical-pixel width clamped to [0.1,20], without a finiteness guard. */
    void setWideWidthPx(double v);
    /** @brief Store emphasized color and notify on exact change.
     * @param v Color copied without validation or automatic scheme reseeding. */
    void setWideColor(const QColor &v)
    {
        if (m_wideColor == v)
            return;
        m_wideColor = v;
        setDirty();
    }

    /*! The embedded slope classification scheme. The default 2-class scheme
     *  reproduces the legacy thin/wide split; the renderer colours edges by
     *  slope class when the scheme is customized past that default.
     * @return Borrowed scheme valid for this style's lifetime; setters/loading can replace it. */
    [[nodiscard]] const ClassificationScheme &scheme() const { return m_scheme; }
    /** @brief Copy slope classification and emit styleChanged() only when configurations differ.
     * @param s Scheme copied without validation; equality follows ClassificationScheme::operator==().
     * Legacy slope/width/color fields remain unchanged. */
    void setScheme(const ClassificationScheme &s);

    /** @brief Serialize legacy widths/slope/colors and the embedded classification.
     * @return Owned JSON with ARGB colors, logical-pixel widths and enum integers. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Restore present legacy fields and classification, then notify once.
     * @param j Style object; missing legacy fields retain current values, invalid
     * color strings are ignored, finite widths/threshold clamp as their setters,
     * and dash enum integers are not validated. A classification object replaces
     * the scheme; when absent, seedSchemeFromLegacy() updates selected scheme
     * fields while retaining others. Legacy BC keys are ignored for host migration.
     * styleChanged() fires once unconditionally after assignment. */
    void fromJson(const QJsonObject &j) override;

private:
    /*! Seed the 2-class scheme from the legacy slopeBreak/color/wideColor so
     *  the default look (and pre-scheme saved styles) are preserved.
     * Updates mode, manual split, count, explicit [0,1] range, endpoint colors
     * and overrides zero/one on the existing scheme. Other configuration, labels,
     * inversion and dormant overrides are retained; no style signal is emitted. */
    void seedSchemeFromLegacy();

    QColor m_color = QColor(0, 0, 0, 130);
    double m_lineWidthPx = 0.35;
    Qt::PenStyle m_dashPattern = Qt::SolidLine;
    bool m_useSlopeDrivenWidth = true;
    double m_slopeBreak = 0.35; // fraction of maxSlope above which edges go wide
    double m_wideWidthPx = 0.90;
    QColor m_wideColor = QColor(0, 0, 0, 210);
    ClassificationScheme m_scheme;
};

class MeshEdgeSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a visible static wireframe sublayer and owned MeshEdgeStyle.
     * @param id_ Stable ID moved verbatim; the host must ensure uniqueness.
     * @param parent Optional QObject owner. Opacity starts at one; style changes
     * forward invalidated(). Construction emits no invalidation. */
    explicit MeshEdgeSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify the paint family.
     * @return LineKind. */
    Kind kind() const override { return LineKind; }
    /** @brief Read the immutable construction ID.
     * @return Owned text, including empty text if supplied. */
    QString id() const override { return m_id; }
    /** @brief Read the translated layer-tree label.
     * @return Localized Mesh Edges text. */
    QString displayName() const override { return tr("Mesh Edges"); }

    /** @brief Read sublayer visibility independently of its parent.
     * @return Stored flag, initially true. */
    bool isVisible() const override { return m_visible; }
    /** @brief Store visibility and invalidate once on exact change.
     * @param v True enables wireframe contribution; false hides it. */
    void setVisible(bool v) override;
    /** @brief Read sublayer opacity.
     * @return Dimensionless multiplier, initially one; host opacity is separate. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Clamp opacity and invalidate when it changes fuzzily.
     * @param o Finite dimensionless value clamped to [0,1], without an explicit finite check. */
    void setOpacity(qreal o) override;

    /** @brief Report that topology/edge rendering is independent of animation time.
     * @return False unconditionally. */
    bool isDynamic() const override { return false; }

    /** @brief Access the owned wireframe style.
     * @return Borrowed non-null style, valid for the sublayer's lifetime; do not delete it. */
    SublayerStyle *style() override { return m_style; }

    /** @brief Build one base wireframe legend row regardless of visibility.
     * @return Owned Mesh Edges row with sublayer ID, base color, widthPx and
     * opacity. Dash/slope classification are not represented; host aggregation filters visibility. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Leave the supplied scene-graph node unchanged.
     * @param existing Optional borrowed node returned verbatim, without ownership changes.
     * @param ctx Per-frame context, intentionally ignored; actual wireframe geometry is built
     * by the host mesh renderer. No node is created, updated, deleted or attached here.
     * @return existing, including nullptr when supplied. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Access the concrete wireframe style without transferring ownership.
     * @return Borrowed non-null style, valid for the sublayer's lifetime. */
    [[nodiscard]] MeshEdgeStyle *edgeStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = true;
    qreal m_opacity = 1.0;
    MeshEdgeStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
