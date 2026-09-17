/*!
 * \file   meshbcrenderingprefs.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Q_PROPERTY-wrapped view onto PreferencesManager's 2D-mesh
 * boundary-condition edge defaults. Drives the Preferences dialog's
 * Rendering page via QPropertyModel — getters call
 * PreferencesManager::meshBcColor / meshBcWidthPx, setters call the
 * matching setters, so every edit routes back through the singleton and
 * fires preferenceChanged exactly like a hand-coded picker would.
 *
 * These are DEFAULTS ONLY. SWMM2DMeshLayer seeds a freshly-created mesh's
 * Boundary Conditions sublayer (MeshBcStyle) from them — colorByType seeds
 * the sublayer's initial visibility; a project load, a .swmm-style.json
 * import, or any per-layer edit overwrites the seeded values and is
 * persisted with the layer. Changing a preference therefore affects meshes
 * opened afterwards, not meshes already styled — the same contract
 * nodePen()/linkPen() have.
 *
 * Wall has a colour but no width: Wall edges ARE the interior wireframe and
 * are drawn at the edge style's own lineWidthPx.
 */

#ifndef MESHBCRENDERINGPREFS_H
#define MESHBCRENDERINGPREFS_H

#include <QColor>
#include <QObject>

class MeshBcRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool   colorByType          READ colorByType          WRITE setColorByType          NOTIFY colorByTypeChanged)

    Q_PROPERTY(QColor wallColor            READ wallColor            WRITE setWallColor            NOTIFY changed)
    Q_PROPERTY(QColor normalFlowColor      READ normalFlowColor      WRITE setNormalFlowColor      NOTIFY changed)
    Q_PROPERTY(QColor stageConstColor      READ stageConstColor      WRITE setStageConstColor      NOTIFY changed)
    Q_PROPERTY(QColor stageSeriesColor     READ stageSeriesColor     WRITE setStageSeriesColor     NOTIFY changed)
    Q_PROPERTY(QColor flowConstColor       READ flowConstColor       WRITE setFlowConstColor       NOTIFY changed)
    Q_PROPERTY(QColor flowSeriesColor      READ flowSeriesColor      WRITE setFlowSeriesColor      NOTIFY changed)
    Q_PROPERTY(QColor ratingCurveColor     READ ratingCurveColor     WRITE setRatingCurveColor     NOTIFY changed)

    Q_PROPERTY(double normalFlowWidthPx    READ normalFlowWidthPx    WRITE setNormalFlowWidthPx    NOTIFY changed)
    Q_PROPERTY(double stageConstWidthPx    READ stageConstWidthPx    WRITE setStageConstWidthPx    NOTIFY changed)
    Q_PROPERTY(double stageSeriesWidthPx   READ stageSeriesWidthPx   WRITE setStageSeriesWidthPx   NOTIFY changed)
    Q_PROPERTY(double flowConstWidthPx     READ flowConstWidthPx     WRITE setFlowConstWidthPx     NOTIFY changed)
    Q_PROPERTY(double flowSeriesWidthPx    READ flowSeriesWidthPx    WRITE setFlowSeriesWidthPx    NOTIFY changed)
    Q_PROPERTY(double ratingCurveWidthPx   READ ratingCurveWidthPx   WRITE setRatingCurveWidthPx   NOTIFY changed)

    Q_CLASSINFO("group:colorByType",        "General")
    Q_CLASSINFO("group:wallColor",          "Colours")
    Q_CLASSINFO("group:normalFlowColor",    "Colours")
    Q_CLASSINFO("group:stageConstColor",    "Colours")
    Q_CLASSINFO("group:stageSeriesColor",   "Colours")
    Q_CLASSINFO("group:flowConstColor",     "Colours")
    Q_CLASSINFO("group:flowSeriesColor",    "Colours")
    Q_CLASSINFO("group:ratingCurveColor",   "Colours")
    Q_CLASSINFO("group:normalFlowWidthPx",  "Widths (px)")
    Q_CLASSINFO("group:stageConstWidthPx",  "Widths (px)")
    Q_CLASSINFO("group:stageSeriesWidthPx", "Widths (px)")
    Q_CLASSINFO("group:flowConstWidthPx",   "Widths (px)")
    Q_CLASSINFO("group:flowSeriesWidthPx",  "Widths (px)")
    Q_CLASSINFO("group:ratingCurveWidthPx", "Widths (px)")

public:
    explicit MeshBcRenderingPrefs(QObject *parent = nullptr);

    [[nodiscard]] bool   colorByType() const;
    void setColorByType(bool on);

    [[nodiscard]] QColor wallColor() const;
    [[nodiscard]] QColor normalFlowColor() const;
    [[nodiscard]] QColor stageConstColor() const;
    [[nodiscard]] QColor stageSeriesColor() const;
    [[nodiscard]] QColor flowConstColor() const;
    [[nodiscard]] QColor flowSeriesColor() const;
    [[nodiscard]] QColor ratingCurveColor() const;

    void setWallColor(const QColor &c);
    void setNormalFlowColor(const QColor &c);
    void setStageConstColor(const QColor &c);
    void setStageSeriesColor(const QColor &c);
    void setFlowConstColor(const QColor &c);
    void setFlowSeriesColor(const QColor &c);
    void setRatingCurveColor(const QColor &c);

    [[nodiscard]] double normalFlowWidthPx() const;
    [[nodiscard]] double stageConstWidthPx() const;
    [[nodiscard]] double stageSeriesWidthPx() const;
    [[nodiscard]] double flowConstWidthPx() const;
    [[nodiscard]] double flowSeriesWidthPx() const;
    [[nodiscard]] double ratingCurveWidthPx() const;

    void setNormalFlowWidthPx(double px);
    void setStageConstWidthPx(double px);
    void setStageSeriesWidthPx(double px);
    void setFlowConstWidthPx(double px);
    void setFlowSeriesWidthPx(double px);
    void setRatingCurveWidthPx(double px);

signals:
    void colorByTypeChanged(bool on);
    void changed();
};

#endif // MESHBCRENDERINGPREFS_H
