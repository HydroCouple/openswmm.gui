/**
 * @file      meshbcrenderingprefs.h
 * @brief     Q_PROPERTY-wrapped view onto PreferencesManager's 2D-mesh boundary-condition edge
 *            defaults.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESHBCRENDERINGPREFS_H
#define MESHBCRENDERINGPREFS_H

#include <QColor>
#include <QObject>

class MeshBcRenderingPrefs : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool colorByType READ colorByType WRITE setColorByType NOTIFY colorByTypeChanged)

    Q_PROPERTY(QColor wallColor READ wallColor WRITE setWallColor NOTIFY changed)
    Q_PROPERTY(QColor normalFlowColor READ normalFlowColor WRITE setNormalFlowColor NOTIFY changed)
    Q_PROPERTY(QColor stageConstColor READ stageConstColor WRITE setStageConstColor NOTIFY changed)
    Q_PROPERTY(
        QColor stageSeriesColor READ stageSeriesColor WRITE setStageSeriesColor NOTIFY changed)
    Q_PROPERTY(QColor flowConstColor READ flowConstColor WRITE setFlowConstColor NOTIFY changed)
    Q_PROPERTY(QColor flowSeriesColor READ flowSeriesColor WRITE setFlowSeriesColor NOTIFY changed)
    Q_PROPERTY(
        QColor ratingCurveColor READ ratingCurveColor WRITE setRatingCurveColor NOTIFY changed)

    Q_PROPERTY(
        double normalFlowWidthPx READ normalFlowWidthPx WRITE setNormalFlowWidthPx NOTIFY changed)
    Q_PROPERTY(
        double stageConstWidthPx READ stageConstWidthPx WRITE setStageConstWidthPx NOTIFY changed)
    Q_PROPERTY(double stageSeriesWidthPx READ stageSeriesWidthPx WRITE setStageSeriesWidthPx NOTIFY
                   changed)
    Q_PROPERTY(
        double flowConstWidthPx READ flowConstWidthPx WRITE setFlowConstWidthPx NOTIFY changed)
    Q_PROPERTY(
        double flowSeriesWidthPx READ flowSeriesWidthPx WRITE setFlowSeriesWidthPx NOTIFY changed)
    Q_PROPERTY(double ratingCurveWidthPx READ ratingCurveWidthPx WRITE setRatingCurveWidthPx NOTIFY
                   changed)

    Q_CLASSINFO("group:colorByType", "General")
    Q_CLASSINFO("group:wallColor", "Colours")
    Q_CLASSINFO("group:normalFlowColor", "Colours")
    Q_CLASSINFO("group:stageConstColor", "Colours")
    Q_CLASSINFO("group:stageSeriesColor", "Colours")
    Q_CLASSINFO("group:flowConstColor", "Colours")
    Q_CLASSINFO("group:flowSeriesColor", "Colours")
    Q_CLASSINFO("group:ratingCurveColor", "Colours")
    Q_CLASSINFO("group:normalFlowWidthPx", "Widths (px)")
    Q_CLASSINFO("group:stageConstWidthPx", "Widths (px)")
    Q_CLASSINFO("group:stageSeriesWidthPx", "Widths (px)")
    Q_CLASSINFO("group:flowConstWidthPx", "Widths (px)")
    Q_CLASSINFO("group:flowSeriesWidthPx", "Widths (px)")
    Q_CLASSINFO("group:ratingCurveWidthPx", "Widths (px)")

public:
    /**
     * @brief Create a property facade for defaults used by newly styled mesh boundary edges.
     * @param[in] parent Optional Qt owner; access on the GUI thread.
     */
    explicit MeshBcRenderingPrefs(QObject *parent = nullptr);

    /**
     * @brief Read whether new boundary-edge styles initially color by condition type.
     * @return Current application default; existing layer styles are independent.
     */
    [[nodiscard]] bool colorByType() const;
    /**
     * @brief Save the initial type-coloring preference for new mesh styles.
     * @param[in] on True enables type coloring.
     * @details Equal values are ignored; accepted changes emit colorByTypeChanged.
     */
    void setColorByType(bool on);

    /**
     * @brief Read the wallColor default for subsequently configured mesh styles.
     * @return Saved valid color or the compiled boundary-type fallback.
     */
    [[nodiscard]] QColor wallColor() const;
    /**
     * @brief Read the normalFlowColor default for subsequently configured mesh styles.
     * @return Saved valid color or the compiled boundary-type fallback.
     */
    [[nodiscard]] QColor normalFlowColor() const;
    /**
     * @brief Read the stageConstColor default for subsequently configured mesh styles.
     * @return Saved valid color or the compiled boundary-type fallback.
     */
    [[nodiscard]] QColor stageConstColor() const;
    /**
     * @brief Read the stageSeriesColor default for subsequently configured mesh styles.
     * @return Saved valid color or the compiled boundary-type fallback.
     */
    [[nodiscard]] QColor stageSeriesColor() const;
    /**
     * @brief Read the flowConstColor default for subsequently configured mesh styles.
     * @return Saved valid color or the compiled boundary-type fallback.
     */
    [[nodiscard]] QColor flowConstColor() const;
    /**
     * @brief Read the flowSeriesColor default for subsequently configured mesh styles.
     * @return Saved valid color or the compiled boundary-type fallback.
     */
    [[nodiscard]] QColor flowSeriesColor() const;
    /**
     * @brief Read the ratingCurveColor default for subsequently configured mesh styles.
     * @return Saved valid color or the compiled boundary-type fallback.
     */
    [[nodiscard]] QColor ratingCurveColor() const;

    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] c Requested color; invalid QColor is refused by the store.
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setWallColor(const QColor &c);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] c Requested color; invalid QColor is refused by the store.
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setNormalFlowColor(const QColor &c);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] c Requested color; invalid QColor is refused by the store.
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setStageConstColor(const QColor &c);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] c Requested color; invalid QColor is refused by the store.
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setStageSeriesColor(const QColor &c);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] c Requested color; invalid QColor is refused by the store.
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setFlowConstColor(const QColor &c);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] c Requested color; invalid QColor is refused by the store.
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setFlowSeriesColor(const QColor &c);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] c Requested color; invalid QColor is refused by the store.
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setRatingCurveColor(const QColor &c);

    /**
     * @brief Read the normalFlowWidthPx default for subsequently configured mesh styles.
     * @return Boundary-edge width in screen pixels, using the saved or compiled per-type value.
     */
    [[nodiscard]] double normalFlowWidthPx() const;
    /**
     * @brief Read the stageConstWidthPx default for subsequently configured mesh styles.
     * @return Boundary-edge width in screen pixels, using the saved or compiled per-type value.
     */
    [[nodiscard]] double stageConstWidthPx() const;
    /**
     * @brief Read the stageSeriesWidthPx default for subsequently configured mesh styles.
     * @return Boundary-edge width in screen pixels, using the saved or compiled per-type value.
     */
    [[nodiscard]] double stageSeriesWidthPx() const;
    /**
     * @brief Read the flowConstWidthPx default for subsequently configured mesh styles.
     * @return Boundary-edge width in screen pixels, using the saved or compiled per-type value.
     */
    [[nodiscard]] double flowConstWidthPx() const;
    /**
     * @brief Read the flowSeriesWidthPx default for subsequently configured mesh styles.
     * @return Boundary-edge width in screen pixels, using the saved or compiled per-type value.
     */
    [[nodiscard]] double flowSeriesWidthPx() const;
    /**
     * @brief Read the ratingCurveWidthPx default for subsequently configured mesh styles.
     * @return Boundary-edge width in screen pixels, using the saved or compiled per-type value.
     */
    [[nodiscard]] double ratingCurveWidthPx() const;

    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] px Finite width in screen pixels, clamped by the store to [0.1,20].
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setNormalFlowWidthPx(double px);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] px Finite width in screen pixels, clamped by the store to [0.1,20].
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setStageConstWidthPx(double px);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] px Finite width in screen pixels, clamped by the store to [0.1,20].
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setStageSeriesWidthPx(double px);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] px Finite width in screen pixels, clamped by the store to [0.1,20].
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setFlowConstWidthPx(double px);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] px Finite width in screen pixels, clamped by the store to [0.1,20].
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setFlowSeriesWidthPx(double px);
    /**
     * @brief Forward a boundary-edge style default to PreferencesManager.
     * @param[in] px Finite width in screen pixels, clamped by the store to [0.1,20].
     * @details Equal values are skipped (fuzzy-equal for width). Emits changed after delegation; read the getter for the effective stored value.
     */
    void setRatingCurveWidthPx(double px);

signals:
    /**
     * @brief Report a changed initial boundary-coloring preference.
     * @param[in] on New default visibility for type-colored boundary edges.
     */
    void colorByTypeChanged(bool on);
    /**
     * @brief Notify property editors after a color or width request was delegated to the store.
     */
    void changed();
};

#endif // MESHBCRENDERINGPREFS_H
