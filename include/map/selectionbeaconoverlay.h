/**
 * @file      selectionbeaconoverlay.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Locator for the current selection: a flash on change, then a
 *         persistent beacon for as long as the selection stands.
 *
 *         A selected conduit two pixels long is invisible at model-wide zoom
 *         — the selection halo scales with the geometry, so "what did I just
 *         select, and where is it?" has no answer until you zoom. This
 *         overlay answers it in SCREEN space: the ring is a fixed pixel size
 *         at every zoom, so it stays legible no matter how small the feature
 *         itself has become.
 *
 *         Two phases:
 *           - flash   — an expanding, fading ring for ~800 ms after the
 *                       selection changes, to pull the eye;
 *           - beacon  — a steady ring that remains until the selection
 *                       changes again.
 *
 *         Zoom-dependent aggregation: when the whole selection collapses to
 *         less than a beacon's width on screen (i.e. zoomed out), ONE ring is
 *         drawn at its centre rather than N rings stacked on the same pixel.
 *         Zoomed in far enough to tell the features apart, each gets its own.
 *
 *         Off-screen selections draw nothing by design — "Zoom to Selection"
 *         (Ctrl+Shift+J) already serves that case.
 *
 *         Painted from MapCanvas' overlay stage, above the composited QSG
 *         frame, so a 2D mesh can never hide it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_SELECTIONBEACONOVERLAY_H
#define OPENSWMMVIS_MAP_SELECTIONBEACONOVERLAY_H

#include <QColor>
#include <QElapsedTimer>
#include <QObject>
#include <QPointF>
#include <QTimer>
#include <QVector>

#include <functional>

class QPainter;

/*! Fixed screen geometry — the whole point is that these do NOT scale. */
namespace SelectionBeacon
{
inline constexpr int kBeaconRadiusPx = 14;   ///< steady ring radius
inline constexpr int kFlashMaxRadiusPx = 46; ///< ring radius at end of flash
inline constexpr int kFlashDurationMs = 800;
inline constexpr int kFlashFrameMs = 33; ///< ~30 fps while flashing
inline constexpr int kMaxBeacons = 250;  ///< beyond this, aggregate
}

class SelectionBeaconOverlay : public QObject
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QObject owner, usually canvas.
     * @note Initializes empty anchors, gold colour and33msGUI timer. Timeout stops flash at
     *   elapsed>=800ms then emits one final repaint; all methods/timer/painter access serialized on
     *   owning GUI thread.
     */
    explicit SelectionBeaconOverlay(QObject *parent = nullptr);

    /*! Replaces the anchor set (scene coords) and restarts the flash.
     *  An empty set clears the overlay and stops the animation.
     * @param[in] sceneAnchors Scene-map positions copied verbatim; nonfinite projections later
     *   skipped.
     * @note Always replaces/restarts800msflash even equal nonempty vector. Empty stops timer/flash.
     *   Both paths emit needsRepaint once immediately; does not compare engine selection or change
     *   source geometry.
     */
    void setAnchors(const QVector<QPointF> &sceneAnchors);

    /**
     * @return Whether stored anchors vector is empty, not whether any anchor projects
     *   finite/on-screen or flash is active.
     */
    [[nodiscard]] bool isEmpty() const { return m_anchors.isEmpty(); }

    /*! Accent colour; defaults to the selection colour the caller passes.
     * @param[in] c Valid accent colour; invalid/equal ignored.
     * @note Change emits needsRepaint. Painter replaces alpha for ring/ticks/flash, so supplied
     *   alpha is not preserved uniformly; default gold(255, 200, 0). Does not restart flash.
     */
    void setColor(const QColor &c);

    /*! \brief Draws the beacon(s) in DEVICE pixels.
     *  \param toPixel maps scene coords → device pixels (MapCanvas supplies
     *         the same lambda the profile overlays use).
     *  \param viewport used to skip anchors outside the visible extent.
     * @param[in] p Borrowed active painter already in callback pixel coordinate space;
     *   save/restored render state.
     * @note Empty anchors/callback no-op. Projects once, skips nonfinite results, aggregates
     *   when>250finite points or full projected span<=28pixels both axes. Aggregate center includes
     *   off-screen anchors; only resulting centers within viewport+46pxslack drawn.
     * @note Draws steady14pxring/cross plus eased flash14→46px during800ms, overwriting alpha per
     *   component. Does not reset painter transform, apply DPR conversion or schedule repaint;
     *   caller must align callback and painter pixel spaces.
     */
    void paint(QPainter &p, const std::function<QPointF(const QPointF &)> &toPixel,
               const QSize &viewport);

signals:
    /*! Emitted while the flash animates so the canvas repaints its overlay
     *  stage. Stops firing once the flash has finished.
     * @note Emitted by anchor/valid colour edits and approximately30Hzflash timer, including final
     *   steady-only repaint. No timer emits after flash stopped; paint itself does not request
     *   another frame.
     */
    void needsRepaint();

private:
    /*! 0 → flash just started, 1 → finished. >1 means no flash running.
     * @return Elapsed/800mswhile flash and clock active, unclamped and potentially>=1before next
     *   timer; 2.0when inactive/invalid clock. Query does not stop timer or mutate flash state.
     */
    [[nodiscard]] double flashProgress() const;

    QVector<QPointF> m_anchors;
    QColor m_color = QColor(255, 200, 0);
    QElapsedTimer m_flashClock;
    bool m_flashing = false;
    QTimer m_frameTimer;
};

#endif // OPENSWMMVIS_MAP_SELECTIONBEACONOVERLAY_H
