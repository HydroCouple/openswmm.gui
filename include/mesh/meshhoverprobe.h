/**
 * @file      meshhoverprobe.h
 * @brief     Slice §V.VA — watches the active map canvas's cursor signal and emits the barycentric
 *            Z under the cursor for the active 2D mesh layer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice §V.VA — watches the active map canvas's cursor signal and emits
 * the barycentric Z under the cursor for the active 2D mesh layer.
 *
 * Wiring:
 *   - The probe attaches to one MapCanvas at a time via setCanvas().
 *   - Active mesh is selected via setActiveMesh(); typically driven by the
 *     toolbar's active-mesh combo.
 *   - On every MapCanvas::cursorPositionChanged(double mapX, double mapY)
 *     the probe samples Z (scene-space Y-flip applied), throttled to a
 *     minimum 16 ms interval to avoid GUI-thread thrash.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHHOVERPROBE_H
#define OPENSWMMVIS_MESH_MESHHOVERPROBE_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class MapCanvas;
class SWMM2DMeshLayer;

namespace mesh
{

class MeshHoverProbe : public QObject
{
    Q_OBJECT
public:
    /**
     * @brief Construct an inactive cursor probe and start its 16 millisecond throttle timer.
     * @param[in] parent Optional QObject owner, null leaves lifetime with caller. Must follow
     * QObject thread-affinity rules; use on the GUI thread with the canvas and layer.
     * @note Initially no canvas/layer is observed, lastValid() is false and lastZ() is zero. No
     * elevation signal is emitted by construction.
     */
    explicit MeshHoverProbe(QObject *parent = nullptr);
    /**
     * @brief Destroy the probe and let QObject disconnect its signal connections.
     * @note Canvas and mesh are guarded borrowed QPointers and are not deleted by the probe.
     * Destroy on its owning QObject thread; no final elevation notification is emitted.
     */
    ~MeshHoverProbe() override;

    /**
     * @brief Replace the borrowed canvas whose cursor-position signal drives sampling.
     * @param[in] canvas Optional GUI-thread canvas, null detaches; QPointer tracks destruction
     * without owning the canvas. Passing the current pointer does nothing.
     * @note Disconnects the old cursor connection and connects the new one. Does not reset
     * cached height/validity or throttle timer and emits no elevationChanged signal; no
     * immediate sample occurs.
     */
    void setCanvas(MapCanvas *canvas);
    /**
     * @brief Change the borrowed layer sampled on subsequent cursor events.
     * @param[in] layer Optional GUI-thread layer, null disables sampling; guarded without
     * ownership transfer.
     * @note An actual pointer change resets lastValid() false and lastZ() zero and emits
     * NaN/false once. Passing the current pointer does nothing, even if its geometry changed.
     * Does not reset the timer or sample immediately; use the probe's owning thread.
     */
    void setActiveMesh(SWMM2DMeshLayer *layer);

    /**
     * @brief Read the cached finite elevation or its zero invalid-state placeholder.
     * @return Last accepted finite z in the layer's vertical units when lastValid() is true,
     * otherwise zero under normal updates. This does not perform a sample; check lastValid()
     * rather than treating zero as unavailable. Owning-thread read only.
     */
    [[nodiscard]] double lastZ() const { return m_lastZ; }
    /**
     * @brief Read whether the cached hover elevation is currently valid.
     * @return Last sampled finiteness state, initially false; no resampling or layer lifetime
     * check occurs. A layer destruction is noticed on a later unthrottled cursor event, so this
     * is cached state only.
     */
    [[nodiscard]] bool lastValid() const { return m_lastValid; }

signals:
    /**
     * @brief Notify observers when the sampled elevation/validity changes or the active mesh
     * changes.
     * @param[in] z Sampled vertical elevation, normally finite when finite is true; NaN on
     * explicit clearing and whatever nonfinite sample the layer returns otherwise. Invalid
     * cached lastZ() remains zero.
     * @param[in] finite True for a finite sample, false when clearing or a sample is
     * unavailable.
     * @note Unchanged samples and throttled events emit nothing. No ownership transfer; Qt
     * connection semantics govern delivery, and setting a different active mesh emits
     * immediately.
     *
     * @details Emitted when a sampled elevation/validity changes. When the cursor is outside
     * every triangle, \p finite is false and \p z is NaN.
     */
    void elevationChanged(double z, bool finite);

private slots:
    /**
     * @brief Throttle a map-coordinate cursor event, sample scene geometry and update cached
     * elevation.
     * @param[in] mapX Cursor x in the canvas CRS/map units, converted directly to scene x.
     * @param[in] mapY Cursor y in canvas map units, negated for scene y; no CRS conversion
     * occurs here.
     * @note Events less than 16 milliseconds after the timer start/restart are ignored. Without
     * a layer, a previously valid state clears and emits NaN/false; an already invalid state
     * remains silent. With a layer, sampleZAt() uses its populated scene geometry, suppresses
     * unchanged samples and stores zero for nonfinite values. GUI-thread operation; does not
     * modify geometry or take layer ownership.
     */
    void onCursor(double mapX, double mapY);

private:
    QPointer<MapCanvas> m_canvas;
    QPointer<SWMM2DMeshLayer> m_mesh;
    QElapsedTimer m_throttle;
    double m_lastZ = 0.0;
    bool m_lastValid = false;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHHOVERPROBE_H
