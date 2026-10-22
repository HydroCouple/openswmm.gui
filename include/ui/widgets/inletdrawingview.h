/**
 * @file      inletdrawingview.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Engineering-drawing view of one inlet design (Inlets plan §2.3).
 *
 * `InletSceneBuilder::build` is a pure function of (provider, unit system,
 * theme, optional custom-curve points) → a fresh QGraphicsScene, so it is
 * unit-testable without a widget and reusable by the property panel and the
 * identify popup. `InletDrawingView` is the interactive host: wheel-zoom
 * about the cursor, middle-button drag pan, fit-on-first-show, and
 * `render(QPainter*)` for copy / export.
 *
 * Every drawing is to scale from the provider's values (with a minimum
 * on-screen extent so a degenerate 0-length design still renders) and carries
 * dimension callouts — extension lines, a dimension line with arrowheads and
 * a label such as "L = 2.00 ft". Each callout's text item is tagged with
 * `data(InletSceneBuilder::CalloutRole)`, so tests can count and read them.
 *
 * Callouts per type (the count the tests assert):
 *
 *   GRATE          L, W                                        → 2
 *   DROP GRATE     L, W                                        → 2
 *   CURB OPENING   L, h, throat angle                          → 3
 *   DROP CURB      L (×4 sides), h                             → 2
 *   COMBINATION    L_grate, W, L_curb, h, sweeper length       → 5
 *   SLOTTED DRAIN  L, w                                        → 2
 *   CUSTOM         curve name, curve kind                      → 2
 *
 * A GENERIC grate adds two more (open-area fraction and splash-over
 * velocity) to any type whose grate group is drawn.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_INLETDRAWINGVIEW_H
#define OPENSWMMVIS_UI_WIDGETS_INLETDRAWINGVIEW_H

#include <QColor>
#include <QGraphicsView>
#include <QPointF>
#include <QPointer>
#include <QVector>

class QGraphicsScene;
class QPalette;
class UnitSystem;

namespace openswmmvis::inlet
{
class InletProvider;
}

namespace openswmmvis::ui
{

/*! \brief Colours for the drawing. Taken from the widget palette — the same
 *  source `StreetSectionPreview` uses — so the drawing follows the app theme
 *  without a second palette to keep in sync. */
struct InletTheme {
    QColor background; ///< Scene backdrop.
    QColor outline;    ///< Structural lines (curb, grate frame, channel).
    QColor pavement;   ///< Pavement hatching.
    QColor accent;     ///< Grate bars, shaded sweeper, slot.
    QColor dimension;  ///< Dimension + extension lines and arrowheads.
    QColor text;       ///< Labels.

    /**
     * @brief Derive engineering-drawing colours from standard widget palette roles.
     * @param[in] pal Base/window-text/mid/highlight colours; values are copied.
     * @return Theme using Base background, WindowText outlines/text, Mid pavement/dimensions and Highlight accents.
     */
    static InletTheme fromPalette(const QPalette &pal);
};

/*! \brief Pure scene factory for one inlet design. */
class InletSceneBuilder
{
public:
    /*! \brief `QGraphicsItem::data()` key marking a dimension-callout label.
     *  The stored value is the label text. */
    static constexpr int CalloutRole = 0;

    /*! \brief Build the plan + section (or, for CUSTOM, the plotted curve)
     *  for \p design.
     *
     *  \param design       The inlet whose parameters are drawn.
     *  \param units        Unit system used for the callout suffixes; may be
     *                      null, in which case US units (ft, ft/s) are used.
     *  \param theme        Colours.
     *  \param customCurve  Points of the referenced capture curve, supplied by
     *                      the host from the curve registry. Only read for a
     *                      CUSTOM design; an empty list draws empty axes.
     *  \returns A new, unparented scene — the caller takes ownership.
     * @note Parameter values are drawn/labeled in their existing provider units; units supplies suffixes only and does not convert them. Scene scaling uses the largest length with minimum visual extents. No provider/engine state is mutated.
     */
    static QGraphicsScene *build(const openswmmvis::inlet::InletProvider &design,
                                 const UnitSystem *units, const InletTheme &theme,
                                 const QVector<QPointF> &customCurve = {});
};

class InletDrawingView : public QGraphicsView
{
    Q_OBJECT
public:
    /**
     * @brief Create an antialiased inlet drawing host and initially build a no-selection placeholder.
     * @param[in] parent Optional QWidget owner; scene/widget construction and provider access occur on the GUI thread.
     */
    explicit InletDrawingView(QWidget *parent = nullptr);
    /**
     * @brief Destroy the view under QGraphicsView lifetime rules; borrowed provider is not deleted.
     */
    ~InletDrawingView() override;

    /**
     * @brief Replace provider parameter-change subscriptions and rebuild/refit the drawing.
     * @param[in] p Borrowed inlet provider tracked by QPointer; null shows a placeholder and identical pointers are ignored.
     */
    void setProvider(openswmmvis::inlet::InletProvider *p);
    /**
     * @brief Access the currently observed inlet.
     * @return Borrowed live provider or null.
     */
    openswmmvis::inlet::InletProvider *provider() const noexcept;

    /*! \brief Points of the design's custom capture curve, from the host's
     *  curve registry. Triggers a rebuild.
     * @param[in] points Curve samples copied in supplied order with existing curve units; no sorting/finite validation or conversion occurs. Identical values are a no-op.
     */
    void setCustomCurve(const QVector<QPointF> &points);

public slots:
    /**
     * @brief Replace the scene from current provider/theme/custom-curve data, delete the previous scene and fit the result.
     * @note Missing provider draws a placeholder. Uses UnitSystem::instance() labels and the current widget palette; rebuilding discards prior user pan/zoom.
     */
    void rebuild();
    /**
     * @brief Fit the current scene rectangle while preserving aspect ratio and mark the view fitted.
     * @note Missing scene is a no-op.
     */
    void zoomToExtent();
    /**
     * @brief Multiply the current view scale by 1.25 on both axes.
     */
    void zoomIn();
    /**
     * @brief Divide the current view scale by 1.25 on both axes.
     */
    void zoomOut();

protected:
    /**
     * @brief Zoom under the pointer by 1.15 for positive vertical delta or its reciprocal for negative.
     * @param[in] event Required Qt event; zero vertical delta delegates to QGraphicsView, and delta magnitude does not change the step.
     */
    void wheelEvent(QWheelEvent *event) override;
    /**
     * @brief Begin middle-button hand panning by forwarding a synthetic left press to QGraphicsView.
     * @param[in] event Required Qt event; other presses use the base view handling.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @brief Finish middle-button panning with a synthetic left release and restore NoDrag mode.
     * @param[in] event Required Qt event; other releases delegate to QGraphicsView.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @brief Forward display handling and fit the scene only if no previous fit occurred.
     * @param[in] event Required Qt show event.
     */
    void showEvent(QShowEvent *event) override;
    /**
     * @brief Forward resize handling and always refit the scene.
     * @param[in] event Required Qt resize event; a resize discards user zoom/pan.
     */
    void resizeEvent(QResizeEvent *event) override;

private:
    QPointer<openswmmvis::inlet::InletProvider> m_provider;
    QVector<QPointF> m_customCurve;
    bool m_fitted = false; ///< Fit once on first show, then on resize.
    bool m_middlePanning = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_INLETDRAWINGVIEW_H
