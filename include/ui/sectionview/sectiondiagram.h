/*!
 * \file   sectiondiagram.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer-agnostic section/profile diagram model + QPainter painter.
 *
 * Slice SP.2 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * A `SectionDiagramModel` is a plain value describing WHAT to draw in model
 * units (feet or metres — the diagram never converts): filled shapes, ground
 * hatch, dimension lines with arrowheads, leader lines with landing text, and
 * an optional plan-view compass inset. `paintSectionDiagram()` maps it into a
 * pixel rect and paints it.
 *
 * The split keeps every builder headlessly testable: a builder produces a
 * model from engine state with no QWidget, no palette and no event loop, and
 * tests assert on the model's contents rather than on rendered pixels.
 *
 * All colours come from the supplied QPalette, so the diagrams follow the
 * app theme (including dark mode) with no per-widget stylesheet.
 */

#ifndef OPENSWMMVIS_SECTIONVIEW_SECTIONDIAGRAM_H
#define OPENSWMMVIS_SECTIONVIEW_SECTIONDIAGRAM_H

#include <QPalette>
#include <QtGlobal>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>

class QPainter;

namespace openswmmvis::sectionview {

/*! Semantic fill/stroke roles. Resolved against the palette at paint time so
 *  the same model renders correctly in light and dark themes. */
enum class DiagramRole {
    Conduit,     //!< Pipe / channel interior.
    Structure,   //!< Manhole, chamber, vault walls.
    Soil,        //!< Native soil below / around a structure.
    Media,       //!< Engineered media (LID soil layer).
    Gravel,      //!< Void storage (LID storage layer, gravel bed).
    Vegetation,  //!< Planting / turf.
    Water,       //!< Ponded or flowing water.
    Muted,       //!< De-emphasised (unknown / not-yet-entered values).
    Accent       //!< Highlighted item (e.g. the geom being edited).
};

/*!
 * Material pattern drawn INSIDE a filled polygon, on top of its flat colour.
 *
 * This is what makes a LID layer stack read as engineered materials rather than
 * as coloured bands: a soil layer that is stippled and a storage layer full of
 * gravel outlines are distinguishable at a glance and in greyscale, which flat
 * fills separated only by hue are not.
 *
 * Patterns are generated procedurally from a fixed seed, so a given layer looks
 * identical on every repaint — a texture that reshuffles as the panel resizes
 * reads as noise, not as material.
 */
enum class DiagramTexture {
    None,
    Stipple,     //!< Fine speckle — engineered soil / planting media.
    Gravel,      //!< Loose rounded outlines — void storage, drainage stone.
    Aggregate,   //!< Angular chips — porous pavement, base course.
    Sand,        //!< Dense fine dots — sand / choking layer.
    Hatch,       //!< 45° lines — native soil, and unknown-value layers.
    Lattice,     //!< Cross-hatched grid — drainage mat / geocomposite.
    Brick        //!< Staggered joints — paver blocks.
};

/*! A filled + stroked polygon in model coordinates (y increases UPWARD). */
struct DiagramPoly
{
    QPolygonF   pts;
    DiagramRole role = DiagramRole::Conduit;
    /*! Material pattern drawn over the fill; clipped to the polygon. */
    DiagramTexture texture = DiagramTexture::None;
    /*! Open channels leave the top edge unstroked so they don't read as a
     *  closed conduit. Applies only when `pts` came from a section outline. */
    bool        openTop = false;
    /*! Draw with a dashed outline and a hatch fill — used for LID layers whose
     *  values the engine cannot read back. */
    bool        unknown = false;
    /*! Optional text drawn centred inside the polygon. */
    QString     insetLabel;
};

/*! A polyline (no fill), e.g. a centreline or a water surface. */
struct DiagramPolyline
{
    QPolygonF   pts;
    DiagramRole role   = DiagramRole::Muted;
    bool        dashed = false;
    QString     label;      //!< Drawn at the polyline's right end when set.
};

/*! Ground surface with the conventional hatch below it. */
struct DiagramGround
{
    double x0 = 0.0;
    double x1 = 0.0;
    double y  = 0.0;
};

/*!
 * A dimension line measuring `from`→`to`, offset perpendicular to that
 * direction by `pixelOffset` screen pixels (sign selects the side). Extension
 * lines are drawn from the measured points out to the dimension line, and the
 * text is drawn along it — so one struct covers vertical depths, horizontal
 * widths and the slope-parallel length of a pipe.
 */
struct DiagramDim
{
    QPointF from;
    QPointF to;
    QString text;
    double  pixelOffset = 28.0;
    bool    accent      = false;   //!< Highlight (e.g. the geom being edited).
};

/*! A leader line from a feature point out to a text label placed
 *  `pixelOffset` pixels away, with the conventional horizontal landing. */
struct DiagramLeader
{
    QPointF anchor;
    QString text;
    QPointF pixelOffset { 60.0, -28.0 };
};

/*!
 * A run of vegetation drawn standing on the segment `x0`→`x1` at height `y`.
 *
 * Planting is the fastest visual cue for which LID type is on screen — a
 * bioretention cell and an infiltration trench have near-identical layer
 * stacks and completely different surfaces.
 */
struct DiagramVegetation
{
    double x0 = 0.0;
    double x1 = 0.0;
    double y  = 0.0;
    /*! Plant height in MODEL units, so it scales with the drawing. */
    double height = 0.0;
    /*! Roughly how many plants to draw across the run; the painter spaces them
     *  evenly and jitters them deterministically. */
    int    count = 8;
    /*! Grass tufts (swales, turf) instead of shrubs (bioretention, gardens). */
    bool   grass = false;
};

/*!
 * A circle in model coordinates — an underdrain pipe in section, a barrel
 * fitting, a cleanout. Drawn filled + stroked like a poly.
 */
struct DiagramCircle
{
    QPointF     centre;
    double      radius = 0.0;
    DiagramRole role   = DiagramRole::Conduit;
    /*! Draw the conventional perforated-pipe ticks around the circumference. */
    bool        perforated = false;
};

/*!
 * A standalone annotated arrow in model coordinates — inflow, overflow,
 * infiltration into the native soil, evapotranspiration.
 */
struct DiagramArrow
{
    QPointF from;
    QPointF to;
    QString label;
    DiagramRole role = DiagramRole::Accent;
};

/*! One spoke of the plan-view inset: a link leaving/entering the node at
 *  `angleDeg` (math convention — 0° = +x / east, CCW positive). */
struct PlanSpoke
{
    double  angleDeg = 0.0;
    QString label;
    bool    inbound  = true;
};

/*!
 * \struct DiagramViewport
 * \brief User zoom / pan applied on top of the automatic fit.
 *
 * The painter always fits the model to the widget first; this is layered over
 * that result, so "zoom to extents" is simply a default-constructed viewport
 * and no state has to be recomputed when the model changes.
 *
 * Only the GEOMETRY is scaled — text keeps its point size, and dimension /
 * leader offsets keep their pixel lengths. That is the engineering-drawing
 * convention, and it is what makes zooming useful for legibility: the drawing
 * spreads out underneath labels that stay readable, instead of everything
 * growing together and staying equally cramped.
 */
struct DiagramViewport
{
    double  zoom = 1.0;   //!< 1.0 = fitted to the widget.
    QPointF panPx;        //!< Additional translation, in screen pixels.

    [[nodiscard]] bool isIdentity() const
    { return qFuzzyCompare(zoom, 1.0) && panPx.isNull(); }
};

/*!
 * \struct SectionDiagramModel
 * \brief Everything one preview pane draws, in model units.
 */
struct SectionDiagramModel
{
    QString title;      //!< Bold header, e.g. "C-12 — Cross-Section".
    QString subtitle;   //!< Secondary header, e.g. "CIRCULAR".
    QString footer;     //!< Single-line readout under the drawing.
    /*! Shown centred instead of the drawing when there is nothing to render
     *  (no selection, DUMMY section, geometry not resolvable). */
    QString emptyText;

    /*! Model-space extent to fit. Left empty → computed from the content. */
    QRectF bounds;

    /*! true  → equal px-per-unit on both axes (true-shape cross-sections).
     *  false → the vertical scale is exaggerated relative to the horizontal,
     *          by the ratio the two fields below resolve to. */
    bool uniformScale = true;

    /*!
     * Vertical exaggeration (V:H) to draw at, when `uniformScale` is false.
     *
     * 0 → choose automatically, capped by `maxVerticalExaggeration`.
     * >0 → use exactly this ratio; 1.0 is true scale.
     *
     * This exists because "stretch each axis to fill the box" — the obvious
     * way to fit a 120 m reach with 4 m of depth into a wide pane — silently
     * picks an exaggeration of 15 or more and makes a 0.25 % pipe look like a
     * 4 % one. A profile has to state its exaggeration, or it misinforms.
     */
    double verticalExaggeration = 0.0;

    /*! Cap on the automatic exaggeration. 0 → uncapped. */
    double maxVerticalExaggeration = 0.0;

    /*!
     * Width:height the DRAWN content should aim for, when choosing the
     * automatic exaggeration. 0 → fill the pane (the legacy behaviour, kept
     * for models whose x axis is not a real length).
     *
     * Deriving the ratio from the model's own proportions rather than from the
     * pane is what makes the automatic choice honest: a 120 m reach with 4.5 m
     * of depth is naturally 27:1, so a target of 6:1 asks for 4.4x and snaps
     * to 4x — the same answer at every dock size. Sizing to the pane instead
     * (whether by filling it or by chasing a pixel height) makes the apparent
     * gradient change as the user resizes, which is precisely the defect this
     * field exists to remove.
     */
    double targetDrawnAspect = 0.0;

    /*! Draw the achieved V:H ratio on the drawing.
     *
     *  Only meaningful when BOTH axes are real lengths. Node profiles put
     *  connecting pipes on a normalised x axis, where a ratio would be
     *  arithmetic on an arbitrary unit, so they leave this false. */
    bool annotateExaggeration = false;

    QVector<DiagramPoly>       polys;
    QVector<DiagramPolyline>   polylines;
    QVector<DiagramGround>     grounds;
    QVector<DiagramVegetation> vegetation;
    QVector<DiagramCircle>     circles;
    QVector<DiagramArrow>      arrows;
    QVector<DiagramDim>      dims;
    QVector<DiagramLeader>   leaders;
    QVector<PlanSpoke>       plan;      //!< Non-empty → draw the compass inset.

    [[nodiscard]] bool isEmpty() const
    {
        // Vegetation, circles and arrows are ornaments on something else, so
        // they deliberately do not count as content on their own.
        return polys.isEmpty() && polylines.isEmpty() && grounds.isEmpty();
    }

    /*! Union of every drawable's extent; used when `bounds` is left null. */
    [[nodiscard]] QRectF computeBounds() const;
};

/*!
 * \brief Paint \p model into \p target using \p palette for all colours.
 *
 * Reserves margins for the title / footer and for leader landings, fits the
 * model bounds into what remains, and declutters progressively as the target
 * shrinks: leaders drop out first, then dimensions, then the footer — so a
 * narrow dock still shows a correct (if bare) drawing rather than a pile of
 * overlapping text.
 *
 * The painter's state is saved and restored; no transform leaks out.
 */
/*!
 * \param fitRectOut  Optionally receives the pixel rect the model bounds were
 *        fitted into, BEFORE zoom/pan. A zooming host needs it to keep the
 *        point under the cursor stationary; reporting it beats recomputing it,
 *        because the fit reserves adaptive margins for leader and dimension
 *        text and a second copy of that logic would silently drift.
 *        Left untouched when the model is empty or too small to draw.
 * \param achievedExaggerationOut  Optionally receives the V:H ratio actually
 *        used (1.0 for a true-scale or uniform-scale drawing). Reporting it
 *        beats inferring it from rendered pixels — which is what tests would
 *        otherwise have to do, and what a zoom readout would need.
 */
void paintSectionDiagram(QPainter &painter, const QRectF &target,
                         const SectionDiagramModel &model,
                         const QPalette &palette,
                         const DiagramViewport &viewport = {},
                         QRectF *fitRectOut = nullptr,
                         double *achievedExaggerationOut = nullptr);

/*! Resolve a role to its fill colour (exposed for tests + the icon renderer). */
[[nodiscard]] QColor diagramFillColor(DiagramRole role, const QPalette &palette);

/*! Resolve a role to its stroke colour. */
[[nodiscard]] QColor diagramStrokeColor(DiagramRole role, const QPalette &palette);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_SECTIONDIAGRAM_H
