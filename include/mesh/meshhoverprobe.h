/*!
 * \file   meshhoverprobe.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#ifndef OPENSWMMVIS_MESH_MESHHOVERPROBE_H
#define OPENSWMMVIS_MESH_MESHHOVERPROBE_H

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

class MapCanvas;
class SWMM2DMeshLayer;

namespace mesh {

class MeshHoverProbe : public QObject
{
    Q_OBJECT
public:
    explicit MeshHoverProbe(QObject *parent = nullptr);
    ~MeshHoverProbe() override;

    void setCanvas(MapCanvas *canvas);
    void setActiveMesh(SWMM2DMeshLayer *layer);

    [[nodiscard]] double lastZ()    const { return m_lastZ; }
    [[nodiscard]] bool   lastValid() const { return m_lastValid; }

signals:
    /*! \brief Emitted on every (throttled) cursor update. When the cursor
     *  is outside every triangle, \p finite is false and \p z is NaN. */
    void elevationChanged(double z, bool finite);

private slots:
    void onCursor(double mapX, double mapY);

private:
    QPointer<MapCanvas>       m_canvas;
    QPointer<SWMM2DMeshLayer> m_mesh;
    QElapsedTimer             m_throttle;
    double                    m_lastZ      = 0.0;
    bool                      m_lastValid  = false;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHHOVERPROBE_H
