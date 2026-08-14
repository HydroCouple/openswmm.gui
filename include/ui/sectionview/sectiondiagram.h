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

/*! A filled + stroked polygon in model coordinates (y increases UPWARD). */
struct DiagramPoly
{
    QPolygonF   pts;
    DiagramRole role = DiagramRole::Conduit;
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

/*! One spoke of the plan-view inset: a link leaving/entering the node at
 *  `angleDeg` (math convention — 0° = +x / east, CCW positive). */
struct PlanSpoke
{
    double  angleDeg = 0.0;
    QString label;
    bool    inbound  = true;
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
     *  false → independent axis scaling (profiles, where a 120 m run and a
     *          3 m depth must both stay legible). */
    bool uniformScale = true;

    QVector<DiagramPoly>     polys;
    QVector<DiagramPolyline> polylines;
    QVector<DiagramGround>   grounds;
    QVector<DiagramDim>      dims;
    QVector<DiagramLeader>   leaders;
    QVector<PlanSpoke>       plan;      //!< Non-empty → draw the compass inset.

    [[nodiscard]] bool isEmpty() const
    {
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
void paintSectionDiagram(QPainter &painter, const QRectF &target,
                         const SectionDiagramModel &model,
                         const QPalette &palette);

/*! Resolve a role to its fill colour (exposed for tests + the icon renderer). */
[[nodiscard]] QColor diagramFillColor(DiagramRole role, const QPalette &palette);

/*! Resolve a role to its stroke colour. */
[[nodiscard]] QColor diagramStrokeColor(DiagramRole role, const QPalette &palette);

} // namespace openswmmvis::sectionview

#endif // OPENSWMMVIS_SECTIONVIEW_SECTIONDIAGRAM_H
