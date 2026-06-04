/*!
 * \file   streetprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM street cross-section ([STREETS]).
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
 */
#ifndef OPENSWMMVIS_STREET_STREETPROVIDER_H
#define OPENSWMMVIS_STREET_STREETPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::street {

class StreetProvider : public QObject
{
    Q_OBJECT

public:
    explicit StreetProvider(QString name, QObject *parent = nullptr);
    ~StreetProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    QString name() const noexcept { return m_name; }

    // ── Geometry (legacy TStreet field order) ───────────────────────────────

    double crownWidth()       const noexcept { return m_crownWidth; }       ///< Tcrown
    double curbHeight()       const noexcept { return m_curbHeight; }       ///< Hcurb
    double crossSlope()       const noexcept { return m_crossSlope; }       ///< Sx (%)
    double roadRoughness()    const noexcept { return m_roadRoughness; }    ///< nRoad
    double gutterDepression() const noexcept { return m_gutterDepression; } ///< a
    double gutterWidth()      const noexcept { return m_gutterWidth; }      ///< W
    int    sides()            const noexcept { return m_sides; }            ///< 1 or 2
    double backingWidth()     const noexcept { return m_backingWidth; }     ///< Tback
    double backingSlope()     const noexcept { return m_backingSlope; }     ///< Sback (%)
    double backingRoughness() const noexcept { return m_backingRoughness; } ///< nBack

    // ── Mutators (emit changed signals) ─────────────────────────────────────

    void setName(QString newName);

    void setCrownWidth(double v);
    void setCurbHeight(double v);
    void setCrossSlope(double v);
    void setRoadRoughness(double v);
    void setGutterDepression(double v);
    void setGutterWidth(double v);
    void setSides(int v);
    void setBackingWidth(double v);
    void setBackingSlope(double v);
    void setBackingRoughness(double v);

signals:
    void nameChanged(QString prev, QString now);
    /*! \brief Any geometry field changed (list label / preview repaint). */
    void paramsChanged();

private:
    QString m_name;

    double m_crownWidth       = 30.0;
    double m_curbHeight       = 0.5;
    double m_crossSlope       = 2.0;
    double m_roadRoughness    = 0.016;
    double m_gutterDepression = 0.0;
    double m_gutterWidth      = 0.0;
    int    m_sides            = 2;
    double m_backingWidth     = 0.0;
    double m_backingSlope     = 0.0;
    double m_backingRoughness = 0.0;
};

} // namespace openswmmvis::street

#endif // OPENSWMMVIS_STREET_STREETPROVIDER_H
