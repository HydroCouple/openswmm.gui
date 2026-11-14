/**
 * @file      landuseprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     MVC model for a single SWMM land use ([LANDUSES]).
 *
 * One LandUseProvider per `[LANDUSES]` entry. Owned by a project-scoped
 * LandUseRegistry. Mirrors PollutantProvider. Scalar fields only — the engine
 * exposes street-sweeping interval (days) and removal fraction. Buildup and
 * washoff functions are per-(landuse × pollutant) and are edited separately
 * (see docs/HANDOFF_compile_verify_agent.md).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LANDUSE_LANDUSEPROVIDER_H
#define OPENSWMMVIS_LANDUSE_LANDUSEPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::landuse
{

class LandUseProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit LandUseProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~LandUseProvider() override;

    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }

    /**
     * @brief Read the local street-sweeping parameter.
     * @return Street-sweeping interval in days; a nonnegative input is expected by the engine.
     */
    double sweepInterval() const noexcept { return m_sweepInterval; } ///< days
    /**
     * @brief Read the local street-sweeping parameter.
     * @return Fraction of pollutant buildup removed during sweeping, expected in 0..1.
     */
    double sweepRemoval() const noexcept { return m_sweepRemoval; } ///< fraction 0..1

    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName New name copied verbatim, with no trimming/uniqueness/engine validation.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);
    /**
     * @brief Store a street-sweeping authoring value and notify when unequal.
     * @param[in] v Street-sweeping interval in days; a nonnegative input is expected by the engine.
     * @details No bounds/finiteness checking or engine write is performed; equal numeric values are a no-op.
     */
    void setSweepInterval(double v);
    /**
     * @brief Store a street-sweeping authoring value and notify when unequal.
     * @param[in] v Fraction of pollutant buildup removed during sweeping, expected in 0..1.
     * @details No bounds/finiteness checking or engine write is performed; equal numeric values are a no-op.
     */
    void setSweepRemoval(double v);

signals:
    /**
     * @brief Notify observers after the stored identifier changes.
     * @param[in] prev Previous identifier.
     * @param[in] now New identifier, already stored when emitted.
     */
    void nameChanged(QString prev, QString now);
    /**
     * @brief Notify observers after authoring parameter changes; does not itself guarantee a successful engine write.
     */
    void paramsChanged();

private:
    QString m_name;
    double m_sweepInterval = 0.0;
    double m_sweepRemoval = 0.0;
};

} // namespace openswmmvis::landuse

#endif // OPENSWMMVIS_LANDUSE_LANDUSEPROVIDER_H
