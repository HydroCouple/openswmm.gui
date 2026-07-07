/*!
 * \file   landuseprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM land use ([LANDUSES]).
 *
 * One LandUseProvider per `[LANDUSES]` entry. Owned by a project-scoped
 * LandUseRegistry. Mirrors PollutantProvider. Scalar fields only — the engine
 * exposes street-sweeping interval (days) and removal fraction. Buildup and
 * washoff functions are per-(landuse × pollutant) and are edited separately
 * (see docs/HANDOFF_compile_verify_agent.md).
 */
#ifndef OPENSWMMVIS_LANDUSE_LANDUSEPROVIDER_H
#define OPENSWMMVIS_LANDUSE_LANDUSEPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::landuse {

class LandUseProvider : public QObject
{
    Q_OBJECT

public:
    explicit LandUseProvider(QString name, QObject *parent = nullptr);
    ~LandUseProvider() override;

    QString name() const noexcept { return m_name; }

    double sweepInterval() const noexcept { return m_sweepInterval; } ///< days
    double sweepRemoval()  const noexcept { return m_sweepRemoval; }  ///< fraction 0..1

    void setName(QString newName);
    void setSweepInterval(double v);
    void setSweepRemoval(double v);

signals:
    void nameChanged(QString prev, QString now);
    void paramsChanged();

private:
    QString m_name;
    double  m_sweepInterval = 0.0;
    double  m_sweepRemoval  = 0.0;
};

} // namespace openswmmvis::landuse

#endif // OPENSWMMVIS_LANDUSE_LANDUSEPROVIDER_H
