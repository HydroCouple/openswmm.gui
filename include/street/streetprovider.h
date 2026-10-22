/**
 * @file      streetprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC model for a single SWMM street cross-section ([STREETS]).
 *
 * One StreetProvider per `[STREETS]` entry in the project. Owned by a
 * project-scoped StreetRegistry. Mirrors TransectProvider in shape, but a
 * street is fully parametric — ten scalar fields (legacy SWMM-GUI
 * Uproject.pas TStreet.Data[0..9]) rather than a station-elevation list, so
 * there is no monotonicity invariant. Every mutator emits a Qt signal so the
 * editor's list pane, the field form, and the section preview refresh in
 * lock-step ([[feedback_mvc_synchronized_uis]]).
 *
 * Field order + defaults match legacy DefStreet (Uproject.pas:621):
 *   crown width 30, curb height 0.5, cross slope 2, n_road 0.016,
 *   gutter depression 0, gutter width 0, sides 2,
 *   backing width 0, backing slope 0, backing n 0.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_STREET_STREETPROVIDER_H
#define OPENSWMMVIS_STREET_STREETPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::street
{

class StreetProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit StreetProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~StreetProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }

    // ── Geometry (legacy TStreet field order) ───────────────────────────────

    /**
     * @brief Read a local parametric street value.
     * @return Crown-to-curb road width in project length units (ft or m).
     */
    double crownWidth() const noexcept { return m_crownWidth; } ///< Tcrown
    /**
     * @brief Read a local parametric street value.
     * @return Curb height in project length units (ft or m).
     */
    double curbHeight() const noexcept { return m_curbHeight; } ///< Hcurb
    /**
     * @brief Read a local parametric street value.
     * @return Roadway cross slope in percent.
     */
    double crossSlope() const noexcept { return m_crossSlope; } ///< Sx (%)
    /**
     * @brief Read a local parametric street value.
     * @return Road surface Manning roughness n.
     */
    double roadRoughness() const noexcept { return m_roadRoughness; } ///< nRoad
    /**
     * @brief Read a local parametric street value.
     * @return Gutter depression depth in project length units (ft or m).
     */
    double gutterDepression() const noexcept { return m_gutterDepression; } ///< a
    /**
     * @brief Read a local parametric street value.
     * @return Gutter width in project length units (ft or m).
     */
    double gutterWidth() const noexcept { return m_gutterWidth; } ///< W
    /**
     * @brief Read a local parametric street value.
     * @return Number of road sides, one or two.
     */
    int sides() const noexcept { return m_sides; } ///< 1 or 2
    /**
     * @brief Read a local parametric street value.
     * @return Backing area width in project length units (ft or m).
     */
    double backingWidth() const noexcept { return m_backingWidth; } ///< Tback
    /**
     * @brief Read a local parametric street value.
     * @return Backing area slope in percent.
     */
    double backingSlope() const noexcept { return m_backingSlope; } ///< Sback (%)
    /**
     * @brief Read a local parametric street value.
     * @return Backing area Manning roughness n.
     */
    double backingRoughness() const noexcept { return m_backingRoughness; } ///< nBack

    // ── Mutators (emit changed signals) ─────────────────────────────────────

    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName Nonempty new name copied verbatim, with no trimming/uniqueness/engine validation; empty is ignored.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);

    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Crown-to-curb road width in project length units (ft or m).
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setCrownWidth(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Curb height in project length units (ft or m).
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setCurbHeight(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Roadway cross slope in percent.
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setCrossSlope(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Road surface Manning roughness n.
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setRoadRoughness(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Gutter depression depth in project length units (ft or m).
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setGutterDepression(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Gutter width in project length units (ft or m).
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setGutterWidth(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Number of road sides, one or two.
     * @details Values other than one or two are ignored. Equal values are a no-op.
     */
    void setSides(int v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Backing area width in project length units (ft or m).
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setBackingWidth(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Backing area slope in percent.
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setBackingSlope(double v);
    /**
     * @brief Store a street geometry parameter and notify paramsChanged() when changed.
     * @param[in] v Backing area Manning roughness n.
     * @details No bounds/finiteness checks or engine writes are performed here; engine save validates the combined cross-section. Equal values are a no-op.
     */
    void setBackingRoughness(double v);

signals:
    /**
     * @brief Notify observers after the stored identifier changes.
     * @param[in] prev Previous identifier.
     * @param[in] now New identifier, already stored when emitted.
     */
    void nameChanged(QString prev, QString now);
    /*! \brief Any geometry field changed (list label / preview repaint).
     * @brief Notify observers after authoring parameter changes; does not itself guarantee a successful engine write.
     */
    void paramsChanged();

private:
    QString m_name;

    double m_crownWidth = 30.0;
    double m_curbHeight = 0.5;
    double m_crossSlope = 2.0;
    double m_roadRoughness = 0.016;
    double m_gutterDepression = 0.0;
    double m_gutterWidth = 0.0;
    int m_sides = 2;
    double m_backingWidth = 0.0;
    double m_backingSlope = 0.0;
    double m_backingRoughness = 0.0;
};

} // namespace openswmmvis::street

#endif // OPENSWMMVIS_STREET_STREETPROVIDER_H
