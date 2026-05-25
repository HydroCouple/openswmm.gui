/*!
 * \file   curveregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.1 — project-scoped factory + lookup for
 *         CurveProvider instances.
 *
 * Mirrors `TimeseriesRegistry` (Phase 6.7.3.2) and `PatternRegistry`
 * (Phase 6.7.2) in structure. Engine I/O shares the `swmm_table_*` API
 * with timeseries — filters by type code (anything != TIMESERIES = curve).
 */
#ifndef OPENSWMMVIS_CURVE_CURVEREGISTRY_H
#define OPENSWMMVIS_CURVE_CURVEREGISTRY_H

#include "curve/curveprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::curve {

class CurveRegistry : public QObject
{
    Q_OBJECT

public:
    explicit CurveRegistry(QObject *parent = nullptr);
    ~CurveRegistry() override;

    QVector<CurveProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    CurveProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name + type.
     *  \returns the new provider, or nullptr if the name collides. */
    CurveProvider *create(const QString &name, CurveType type);

    void remove(CurveProvider *p);
    bool rename(CurveProvider *p, const QString &newName);

    /*! \brief Populate from a live SWMM engine handle. Walks `swmm_table_*`,
     *  filters out TIMESERIES tables (type code 0). \returns count added. */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via
     *  `swmm_curve_add` (if new) + `swmm_table_clear` + `swmm_table_add_point`. */
    int saveToEngine(void *engineHandle);

signals:
    void providerAdded(openswmmvis::curve::CurveProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::curve::CurveProvider *provider);
    void providerRenamed(openswmmvis::curve::CurveProvider *provider,
                         const QString &prevName, const QString &newName);

private:
    QVector<CurveProvider *>          m_providers;
    QHash<QString, CurveProvider *>   m_byLowerName;
};

} // namespace openswmmvis::curve

#endif // OPENSWMMVIS_CURVE_CURVEREGISTRY_H
