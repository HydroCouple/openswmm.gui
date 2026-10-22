/**
 * @file      couplednodesublayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     SWMM-coupled vertex marker sublayer for the 2D mesh.
 *
 *         Static — isDynamic() == false. The coupled set follows the
 *         [2D_VERTEX_NODE_MAP] tags, not the animation period.
 *
 *         Carved out of MeshNodeStyle (which used to carry
 *         highlightTagged / taggedColor / taggedSizePx as a group on the
 *         Mesh Vertices sublayer) so coupled-node markers are a
 *         first-class sublayer: their own layer-tree row, styling tab, and
 *         — new — visibility independent of the mesh-vertex markers.
 *         Legacy styles migrate in SWMM2DMeshLayer::onSublayersJsonLoaded.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SUBLAYERS_COUPLEDNODESUBLAYER_H
#define OPENSWMM_RENDER_SUBLAYERS_COUPLEDNODESUBLAYER_H

#include "render/isublayer.h"
#include "render/sublayerstyle.h"

#include <QColor>
#include <QJsonObject>
#include <QString>

namespace OpenSWMM::Render
{

class CoupledNodeStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY styleChanged)
    Q_PROPERTY(double markerSizePx READ markerSizePx WRITE setMarkerSizePx NOTIFY styleChanged)

    Q_CLASSINFO("group:color", "Symbology")
    Q_CLASSINFO("group:markerSizePx", "Symbology")

public:
    /** @brief Construct the historic orange coupling-marker appearance.
     * @param parent Optional QObject owner; destroys this style with itself.
     * Ordinary access occurs on the owning Qt thread. */
    explicit CoupledNodeStyle(QObject *parent = nullptr) : SublayerStyle(parent) {}

    /** @brief Read the coupling-marker color.
     * @return Stored value, initially RGBA(255,140,0,235). */
    [[nodiscard]] QColor color() const { return m_color; }
    /** @brief Read coupling-marker size in logical screen pixels.
     * @return Stored value, initially five; finite setter inputs clamp to [0.5,40]. */
    [[nodiscard]] double markerSizePx() const { return m_markerSizePx; }

    /** @brief Copy a marker color and emit styleChanged on QColor inequality.
     * @param v Color retained verbatim, including invalid QColor values. */
    void setColor(const QColor &v)
    {
        if (m_color == v)
            return;
        m_color = v;
        setDirty();
    }
    /** @brief Clamp marker size and emit styleChanged on a changed value.
     * @param v Finite logical pixel size clamped to [0.5,40] by qBound.
     * Uses qFuzzyCompare(current+1, input+1); no explicit NaN validation occurs. */
    void setMarkerSizePx(double v);

    /** @brief Serialize marker appearance.
     * @return Independent object with ARGB color and markerSizePx. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Read partial marker appearance and notify once unconditionally.
     * @param j Borrowed object; absent keys retain current fields, invalid color text
     * is ignored and size applies its setter bounds with the current value as conversion
     * fallback. Individual setters are bypassed; unknown keys are ignored. */
    void fromJson(const QJsonObject &j) override;

private:
    // Defaults mirror MeshNodeStyle's historic taggedColor / taggedSizePx so
    // migrated projects render identically.
    QColor m_color = QColor(0xff, 0x8c, 0x00, 235); // orange
    double m_markerSizePx = 5.0;
};

class CoupledNodeSublayer : public ISublayer
{
    Q_OBJECT
public:
    /** @brief Construct a visible, opacity-one static pass and its owned style.
     * @param id_ Stable identifier moved into storage without checking uniqueness.
     * @param parent Optional QObject owner; destroys this pass and child style.
     * Style changes forward to invalidated(); access occurs on the owning Qt thread. */
    explicit CoupledNodeSublayer(QString id_, QObject *parent = nullptr);

    /** @brief Identify the scene pass kind.
     * @return MarkerKind. */
    Kind kind() const override { return MarkerKind; }
    /** @brief Copy the immutable host identifier.
     * @return Construction-time ID without validation. */
    QString id() const override { return m_id; }
    /** @brief Describe this pass for the UI.
     * @return Translated Coupled Nodes label. */
    QString displayName() const override { return tr("Coupled Nodes"); }

    /** @brief Read local visibility.
     * @return Stored flag, initially true; host visibility is not consulted. */
    bool isVisible() const override { return m_visible; }
    /** @brief Replace visibility and emit invalidated() on an exact Boolean change.
     * @param v New visibility; no synchronous geometry construction occurs. */
    void setVisible(bool v) override;
    /** @brief Read local opacity.
     * @return Stored multiplier, initially one. */
    qreal opacity() const override { return m_opacity; }
    /** @brief Replace clamped opacity and emit invalidated() on a change.
     * @param o Finite multiplier clamped to [0,1]; NaN is not explicitly rejected.
     * Uses qFuzzyCompare(current+1, input+1) to suppress unchanged assignments. */
    void setOpacity(qreal o) override;

    /** @brief Report that this pass does not depend on the animation period.
     * @return False; hosts can still invalidate it after geometry/style changes. */
    bool isDynamic() const override { return false; }

    /** @brief Borrow the owned property bag through its base interface.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    SublayerStyle *style() override { return m_style; }

    /** @brief Build a context-free legend preview from style and host metadata.
     * @return One SWMM-coupled vertices row using a SimpleMarker color/sizePx and local opacity.
     * Hidden passes still return this preview; geometry and source tags are not queried. */
    QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Preserve an existing node while the host renderer builds actual geometry.
     * @param existing Borrowed node returned unchanged, including null.
     * @param ctx Unused rendering context.
     * @return existing without allocation or ownership transfer. */
    QSGNode *buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx) override;

    /** @brief Borrow the concrete owned style.
     * @return Child pointer valid until destruction; do not delete/reparent. */
    [[nodiscard]] CoupledNodeStyle *coupledStyle() const { return m_style; }

private:
    QString m_id;
    // Visible by default: the legacy behaviour (highlightTagged default true
    // with the mesh ctor turning vertices on) drew coupled markers on a
    // fresh mesh, and they are the primary 1D↔2D wiring cue.
    bool m_visible = true;
    qreal m_opacity = 1.0;
    CoupledNodeStyle *m_style;
};

} // namespace OpenSWMM::Render

#endif
