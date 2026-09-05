/*!
 * \file   inletdrawingview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Engineering-drawing view of one inlet design (Inlets plan §2.3).
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

namespace openswmmvis::inlet { class InletProvider; }

namespace openswmmvis::ui {

/*! \brief Colours for the drawing. Taken from the widget palette — the same
 *  source `StreetSectionPreview` uses — so the drawing follows the app theme
 *  without a second palette to keep in sync. */
struct InletTheme
{
    QColor background;   ///< Scene backdrop.
    QColor outline;      ///< Structural lines (curb, grate frame, channel).
    QColor pavement;     ///< Pavement hatching.
    QColor accent;       ///< Grate bars, shaded sweeper, slot.
    QColor dimension;    ///< Dimension + extension lines and arrowheads.
    QColor text;         ///< Labels.

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
     *  \returns A new, unparented scene — the caller takes ownership. */
    static QGraphicsScene *build(const openswmmvis::inlet::InletProvider &design,
                                  const UnitSystem *units,
                                  const InletTheme &theme,
                                  const QVector<QPointF> &customCurve = {});
};

class InletDrawingView : public QGraphicsView
{
    Q_OBJECT
public:
    explicit InletDrawingView(QWidget *parent = nullptr);
    ~InletDrawingView() override;

    void setProvider(openswmmvis::inlet::InletProvider *p);
    openswmmvis::inlet::InletProvider *provider() const noexcept;

    /*! \brief Points of the design's custom capture curve, from the host's
     *  curve registry. Triggers a rebuild. */
    void setCustomCurve(const QVector<QPointF> &points);

public slots:
    void rebuild();
    void zoomToExtent();
    void zoomIn();
    void zoomOut();

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QPointer<openswmmvis::inlet::InletProvider> m_provider;
    QVector<QPointF> m_customCurve;
    bool m_fitted        = false;   ///< Fit once on first show, then on resize.
    bool m_middlePanning = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_INLETDRAWINGVIEW_H
