/*!
 * \file   isublayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Toggleable visual aspect of an output layer.
 *
 *         Plan reference: RENDERING_OUTPUT_SUBLAYERS_PLAN.md §3.
 *
 *         An ISublayer is one paint pass with its own visibility, opacity,
 *         style bag, legend contribution, and (importantly) a self-declared
 *         isDynamic() flag that tells AnimationController whether to
 *         invalidate this sublayer on time-step changes.
 *
 *         Each output layer (SWMMResultsLayer, SWMM2DResultsLayer, …) owns
 *         an ordered list of ISublayers; paint order = list order
 *         (bottom-up). The user can re-order via the layer-tree dock
 *         (Slice S3) and toggle individual sublayers via the third tier
 *         of LayerTreeModel.
 *
 *         The sublayer model refines the IFeatureRenderer abstraction
 *         (§J.2). A SingleSymbolRenderer produces a one-or-two-sublayer
 *         default mix (marker + line); a GraduatedRenderer adds a
 *         ColorRamp sublayer on top. Existing renderer code keeps
 *         working — it just produces sublayers under the hood instead of
 *         monolithic paint passes.
 *
 *         Style bags (style()) are QObjects with Q_PROPERTYs so the
 *         existing QPropertyModel-backed editor stack drives the UI
 *         (Slice S3 SublayerStyleDialog).
 *
 *         Cross-slice: Slice S1 (interface only). Concrete sublayers
 *         (ConduitLineSublayer, NodeMarkerSublayer, DepthColorRampSublayer,
 *         IsolineSublayer, ContourBandSublayer, VelocityVectorSublayer)
 *         land in S2 / S5.
 */
#ifndef OPENSWMM_RENDER_ISUBLAYER_H
#define OPENSWMM_RENDER_ISUBLAYER_H

#include "render/legendsymbolitem.h"

#include <QDateTime>
#include <QList>
#include <QMatrix4x4>
#include <QObject>
#include <QRectF>
#include <QString>

class QSGNode;

namespace OpenSWMM::Render
{

class SublayerStyle;

/*!
 * \struct SublayerContext
 * \brief  Per-frame parameters handed to ISublayer::buildOrUpdateNode().
 *
 *         Built once per QSG updatePaintNode() call by the parent layer
 *         renderer (SWMMLayerQSGRenderer, SWMM2DMeshQSGRenderer) and
 *         passed unchanged to every sublayer in the layer's list.
 *
 *         pixelsPerSceneUnit is the absolute X-axis scale of the view
 *         matrix — sublayers consume this via screenpixels.h helpers to
 *         emit constant-on-screen geometry.
 *
 *         currentTime / currentPeriod are populated by AnimationController
 *         on each tick. Sublayers with isDynamic() == false ignore them.
 */
struct SublayerContext
{
    QMatrix4x4    viewMatrix;          /*!< Full view transform (scene → clip). */
    double        pixelsPerSceneUnit = 1.0; /*!< |viewMatrix.m11()| for the inverse-zoom math. */
    double        devicePixelRatio   = 1.0; /*!< For Hi-DPI screens. */
    QRectF        exposedSceneRect;    /*!< Visible scene-space bounds for culling. */
    QDateTime     currentTime;         /*!< Animation tick — ignored if sublayer is static. */
    int           currentPeriod = -1;  /*!< Animation period index — -1 = no animation. */
};

/*!
 * \class ISublayer
 * \brief One toggleable visual aspect of a results / model layer.
 */
class ISublayer : public QObject
{
    Q_OBJECT

public:
    /*!
     * \enum Kind
     * \brief Coarse taxonomy of sublayer paint behaviours.
     *
     *        The UI uses Kind to choose an icon for the layer-tree row
     *        and to gate which Q_PROPERTYs are meaningful (e.g. an
     *        ArrowKind sublayer's style bag has arrow-specific knobs).
     */
    enum Kind
    {
        MarkerKind = 0,       /*!< Points (nodes, gages). */
        LineKind,             /*!< Lines (conduits, edges). */
        FillKind,             /*!< Polygons (subcatchments, mesh triangles, flat fill). */
        ArrowKind,            /*!< Marker-along-line (flow-direction arrows). */
        ColorRampFillKind,    /*!< Graduated polygon fill (depth heat-map). */
        IsolineKind,          /*!< Marching-squares line contours. */
        ContourBandKind,      /*!< Marching-squares filled bands. */
        VectorGlyphKind,      /*!< Velocity arrows (RT0 reconstruction). */
    };
    Q_ENUM(Kind)

    explicit ISublayer(QObject *parent = nullptr) : QObject(parent) {}
    ~ISublayer() override = default;

    // ── Identity / taxonomy ────────────────────────────────────────────
    [[nodiscard]] virtual Kind    kind() const        = 0;
    [[nodiscard]] virtual QString id() const          = 0; /*!< Stable within parent layer. */
    [[nodiscard]] virtual QString displayName() const = 0; /*!< Shown in the layer-tree row. */

    // ── Visibility / opacity ──────────────────────────────────────────
    [[nodiscard]] virtual bool  isVisible() const  = 0;
    virtual void                setVisible(bool)   = 0;
    [[nodiscard]] virtual qreal opacity() const    = 0; /*!< 0..1; multiplies the parent layer's opacity. */
    virtual void                setOpacity(qreal)  = 0;

    // ── Animation integration ─────────────────────────────────────────
    /*!
     * \brief Whether this sublayer's output depends on the current
     *        animation period.
     *
     *        AnimationController::currentPeriodChanged invalidates only
     *        the sublayers that return true here, leaving static
     *        sublayers' cached QSG geometry untouched (plan §2 Decision 3).
     */
    [[nodiscard]] virtual bool isDynamic() const = 0;

    // ── Style bag ─────────────────────────────────────────────────────
    /*!
     * \brief Returns the property-bag QObject that owns this sublayer's
     *        style knobs.
     *
     *        Heap-owned by the sublayer (lifetime tied to the sublayer).
     *        Used by SublayerStyleDialog via QPropertyModel to build the
     *        editor automatically from Q_PROPERTY metadata.
     *
     *        Returning nullptr is legal for sublayers with no
     *        user-editable knobs (e.g. a fixed-style debug overlay).
     */
    [[nodiscard]] virtual SublayerStyle *style() = 0;

    // ── Legend contribution (§J.5 + plan §4.3) ────────────────────────
    /*!
     * \brief Returns the legend rows this sublayer contributes.
     *
     *        Each returned LegendSymbolItem must have its sublayerId
     *        field set to id() so right-click → "Edit Sublayer Style…"
     *        can jump back to this sublayer. The parent layer's
     *        legendSymbolItems() concatenates the per-sublayer lists.
     */
    [[nodiscard]] virtual QList<LegendSymbolItem> legendSymbolItems() const = 0;

    // ── QSG paint ─────────────────────────────────────────────────────
    /*!
     * \brief Build (or update) the QSG node tree for this sublayer.
     *
     * \param existing  Previously-returned root node, or nullptr on first call.
     *                  The sublayer may return the same node (mutated) or a
     *                  new one. The caller releases the old node if a different
     *                  pointer is returned.
     * \param ctx       Per-frame context (zoom, animation period, exposed rect).
     *
     * \return Root QSGNode for this sublayer's contribution. Must be a child
     *         of ctx.parentNode by the time the function returns (the parent
     *         layer renderer enforces this).
     *
     *         Returning nullptr is legal — indicates "nothing to draw this
     *         frame" (e.g. hidden, fully transparent, or empty).
     */
    virtual QSGNode *buildOrUpdateNode(QSGNode *existing,
                                       const SublayerContext &ctx) = 0;

    /*!
     * \brief Public entry point hosts call to request a re-render.
     *
     *        Equivalent to emitting the invalidated() signal directly,
     *        but routed through a slot so external callers don't reach
     *        into another QObject's signal. The base implementation
     *        simply emits invalidated(); subclasses may override to add
     *        bookkeeping (e.g. mark a dirty bit so the next
     *        buildOrUpdateNode() actually does work).
     */
    Q_INVOKABLE virtual void invalidate() { emit invalidated(); }

signals:
    /*!
     * \brief Style or visibility changed in a way the renderer must observe.
     *
     *        Concrete sublayers re-emit this from their style bag's
     *        styleChanged() signal and from their setVisible/setOpacity
     *        setters.
     */
    void invalidated();
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_ISUBLAYER_H
