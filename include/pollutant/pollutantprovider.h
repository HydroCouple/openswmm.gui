/*!
 * \file   pollutantprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM pollutant ([POLLUTANTS]).
 *
 * One PollutantProvider per `[POLLUTANTS]` entry. Owned by a project-scoped
 * PollutantRegistry. Mirrors StreetProvider in shape — scalar fields with a
 * paramsChanged() signal so the editor's list pane and field form refresh in
 * lock-step. Engine I/O walks `swmm_pollutant_*` from openswmm_pollutants.h.
 *
 * Units (0=MG/L, 1=UG/L, 2=#/L) are write-once at engine creation time — the
 * engine has no `swmm_pollutant_set_units`, so changing units only takes
 * effect for a pollutant that does not yet exist in the engine.
 *
 * Co-pollutant is stored by name (empty = none) and resolved to an engine
 * index at save time.
 */
#ifndef OPENSWMMVIS_POLLUTANT_POLLUTANTPROVIDER_H
#define OPENSWMMVIS_POLLUTANT_POLLUTANTPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::pollutant {

class PollutantProvider : public QObject
{
    Q_OBJECT

public:
    explicit PollutantProvider(QString name, QObject *parent = nullptr);
    ~PollutantProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────
    QString name() const noexcept { return m_name; }

    // ── Scalar properties ───────────────────────────────────────────────────
    int     units()        const noexcept { return m_units; }        ///< 0=MG/L,1=UG/L,2=#/L
    double  rainConc()     const noexcept { return m_rainConc; }
    double  gwConc()       const noexcept { return m_gwConc; }
    double  initConc()     const noexcept { return m_initConc; }
    double  rdiiConc()     const noexcept { return m_rdiiConc; }
    double  kDecay()       const noexcept { return m_kDecay; }        ///< 1/day
    double  mwt()          const noexcept { return m_mwt; }           ///< molecular weight
    bool    snowOnly()     const noexcept { return m_snowOnly; }
    QString coPollutant()  const noexcept { return m_coPollutant; }   ///< empty = none
    double  coFraction()   const noexcept { return m_coFraction; }

    // ── Mutators (emit changed signals) ─────────────────────────────────────
    void setName(QString newName);
    void setUnits(int v);
    void setRainConc(double v);
    void setGwConc(double v);
    void setInitConc(double v);
    void setRdiiConc(double v);
    void setKDecay(double v);
    void setMwt(double v);
    void setSnowOnly(bool v);
    void setCoPollutant(QString name);
    void setCoFraction(double v);

signals:
    void nameChanged(QString prev, QString now);
    void paramsChanged();

private:
    QString m_name;

    int     m_units       = 0;
    double  m_rainConc    = 0.0;
    double  m_gwConc      = 0.0;
    double  m_initConc    = 0.0;
    double  m_rdiiConc    = 0.0;
    double  m_kDecay      = 0.0;
    double  m_mwt         = 0.0;
    bool    m_snowOnly    = false;
    QString m_coPollutant;
    double  m_coFraction  = 0.0;
};

} // namespace openswmmvis::pollutant

#endif // OPENSWMMVIS_POLLUTANT_POLLUTANTPROVIDER_H
