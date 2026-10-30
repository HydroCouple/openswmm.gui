/**
 * @file      meshnodesublayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Stylable mesh-vertex marker sublayer.
 *
 *         Static — isDynamic() == false. Vertex positions follow the mesh,
 *         not the animation period.
 *
 *         Replaces the previous boolean "showMeshNodes" toggle (no styling)
 *         with a property bag that exposes color, marker size, shape, and
 *         a distinct color for SWMM-coupled (tagged) vertices. Hidden by
 *         default to preserve the existing visual.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_MESHNODESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_MESHNODESUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class MeshNodeStyle : public SublayerStyle
{
    Q_OBJECT
public:
    enum MarkerShape { Circle = 0, Square, Triangle, Diamond };
    Q_ENUM(MarkerShape)

private:
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(double markerSizePx READ markerSizePx WRITE setMarkerSizePx NOTIFY styleChanged)
    Q_PROPERTY(MarkerShape shape READ shape WRITE setShape NOTIFY styleChanged)
    Q_PROPERTY(QColor outlineColor READ outlineColor WRITE setOutlineColor NOTIFY styleChanged)
    Q_PROPERTY(
        double outlineWidthPx READ outlineWidthPx WRITE setOutlineWidthPx NOTIFY styleChanged)
    // Tagged (SWMM-coupled) vertex styling moved to CoupledNodeStyle
    // (couplednodesublayer.h) — coupled markers are their own sublayer now.
    // Legacy "tagged*" JSON keys migrate in
    // SWMM2DMeshLayer::onSublayersJsonLoaded.

    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:markerSizePx", "Symbology")
    Q_CLASSINFO("group:shape", "Symbology")
    Q_CLASSINFO("group:outlineColor", "Outline")
    Q_CLASSINFO("group:outlineWidthPx", "Outline")

public:
    /** @brief Construct gray circular mesh markers with a translucent white outline.
     * @param parent Optional QObject owner. Size defaults to three logical pixels,
     * outline width 0.5; construction emits no styleChanged(). */
    explicit MeshNodeStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    /** @brief Read the marker fill color.
     * @return Stored QColor copy, initially RGBA (40,40,40,220). */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read the marker size in logical screen pixels.
     * @return Stored size, initially three; finite setter inputs clamp to [0.5,50]. */
    [[nodiscard]] double markerSizePx() const { return m_markerSizePx; }
    /** @brief Read the local four-shape marker selection.
     * @return Stored enum, initially Circle; unknown integer values can be loaded/stored. */
    [[nodiscard]] MarkerShape shape() const { return m_shape; }
    /** @brief Read the marker outline color.
     * @return Stored QColor copy, initially RGBA (255,255,255,220). */
    [[nodiscard]] QColor outlineColor() const { return m_outlineColor; }
    /** @brief Read outline width in logical screen pixels.
     * @return Stored width, initially 0.5; finite setter inputs clamp to [0,10]. */
    [[nodiscard]] double outlineWidthPx() const { return m_outlineWidthPx; }

    /** @brief Replace marker fill color and notify on exact change.
     * @param v Color copied without validation, including invalid QColor values.
     * Equal input is a no-op; an effective change emits styleChanged() once. */
    void setColor(const QColor &v)
    {
        if (m_color == v)
            return;
        m_color = v;
        setDirty();
    }
    /** @brief Clamp marker size and notify on a shifted fuzzy comparison change.
     * @param v Finite logical-pixel size, clamped to [0.5,50] through qBound().
     * No explicit finiteness check occurs; emits styleChanged() once on change. */
    void setMarkerSizePx(double v);
    /** @brief Store the local marker-shape enum without range validation.
     * @param v Shape value; exact changes emit styleChanged() once. */
    void setShape(MarkerShape v)
    {
        if (m_shape == v)
            return;
        m_shape = v;
        setDirty();
    }
    /** @brief Replace outline color and notify on exact change.
     * @param v Color copied without validation, including invalid QColor values. */
    void setOutlineColor(const QColor &v)
    {
        if (m_outlineColor == v)
            return;
        m_outlineColor = v;
        setDirty();
    }
    /** @brief Clamp outline width and notify when it changes fuzzily.
     * @param v Finite logical-pixel width clamped to [0,10]; zero suppresses width.
     * Uses qFuzzyCompare after adding one, with no explicit finiteness check. */
    void setOutlineWidthPx(double v);

    /** @brief Serialize marker colors, size, shape and outline width.
     * @return Owned JSON with ARGB colors, integer shape, and logical-pixel dimensions. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Apply present marker fields and emit styleChanged() once unconditionally.
     * @param j Style object; missing fields retain current values, invalid color
     * strings are ignored, size/width clamp as in their setters, and shape integers
     * are not range-checked. Legacy tagged-node fields are ignored here and require
     * host-level migration. No per-property intermediate notifications occur. */
    void fromJson(const QJsonObject &j) override;

private:
    QColor m_color = QColor(40, 40, 40, 220);
    double m_markerSizePx = 3.0;
    MarkerShape m_shape = Circle;
    QColor m_outlineColor = QColor(255, 255, 255, 220);
    double m_outlineWidthPx = 0.5;
};

class MeshNodeSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a hidden static marker sublayer and its owned MeshNodeStyle.
     * @param id_ Stable ID moved verbatim; the host must ensure uniqueness.
     * @param parent Optional QObject owner. Opacity starts at one; styleChanged()
     * is forwarded to invalidated(). Construction emits no invalidation. */
    explicit MeshNodeSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify the sublayer paint family.
     * @return MarkerKind. */
    Kind kind() const override { return MarkerKind; }
    /** @brief Read the stable ID supplied at construction.
     * @return Owned unchanged ID text, including empty text if supplied. */
    QString id() const override { return m_id; }
    /** @brief Read the translated layer-tree display label.
     * @return Localized Mesh Vertices text. */
    QString displayName() const override { return tr("Mesh Vertices"); }

    /** @brief Read this contribution's visibility independently of its host.
     * @return Stored flag, initially false. */
    bool isVisible() const override { return m_visible; }
    /** @brief Store visibility and emit invalidated() once on exact change.
     * @param v True enables this contribution; false hides it. */
    void setVisible(bool v) override;
    /** @brief Read the sublayer opacity multiplier.
     * @return Stored dimensionless opacity, initially one; host opacity is separate. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Clamp opacity and invalidate on a shifted fuzzy comparison change.
     * @param o Finite dimensionless opacity clamped to [0,1]; no explicit nonfinite check occurs. */
    void setOpacity(qreal o) override;

    /** @brief Report that mesh vertex geometry is independent of animation time.
     * @return False unconditionally. */
    bool isDynamic() const override { return false; }

    /** @brief Access the sublayer-owned marker property bag.
     * @return Borrowed non-null style; do not delete it or retain beyond this sublayer's lifetime. */
    SublayerStyle *style() override { return m_style; }

    /** @brief Build one base marker legend row regardless of visibility.
     * @return Owned Mesh Vertices row with sublayer ID, fill color, sizePx, shape
     * and sublayer opacity. Outline settings are not included; host aggregation filters visibility. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Leave the supplied scene-graph node unchanged.
     * @param existing Borrowed existing root, possibly null, returned verbatim.
     * @param ctx Per-frame context, intentionally ignored. Actual mesh marker geometry is
     * built by the host renderer; this function allocates, deletes and updates nothing.
     * @return existing, without changing ownership or parent attachment. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Access the concrete marker style without transferring ownership.
     * @return Borrowed non-null MeshNodeStyle, valid for the sublayer's lifetime. */
    [[nodiscard]] MeshNodeStyle *nodeStyle() const { return m_style; }

private:
    QString m_id;
    bool m_visible = false; // matches historic showMeshNodes default
    qreal m_opacity = 1.0;
    MeshNodeStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
