/*!
 * \file   inletregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for InletProvider instances.
 *
 * Mirrors TransectRegistry: `loadFromEngine` reads every field of every
 * `[INLETS]` entry (`swmm_inlet_get_design` + `swmm_inlet_get_comment`), and
 * `saveToEngine` writes every provider back unconditionally — the engine is
 * the sink, the registry the source of truth while a project is open.
 * `remove` deletes the engine copy too (`swmm_inlet_delete`), so a deleted
 * design cannot reappear in the written INP; `impactSummary` surfaces the
 * engine's referential-impact report for the delete confirmation.
 */
#ifndef OPENSWMMVIS_INLET_INLETREGISTRY_H
#define OPENSWMMVIS_INLET_INLETREGISTRY_H

#include "inlet/inletprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::inlet {

class InletRegistry : public QObject
{
    Q_OBJECT

public:
    explicit InletRegistry(QObject *parent = nullptr);
    ~InletRegistry() override;

    QVector<InletProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    InletProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    InletProvider *create(const QString &name);
    void remove(InletProvider *p);
    bool rename(InletProvider *p, const QString &newName);

    /*! \brief Human-readable summary of what references \p p, from
     *  `swmm_inlet_analyze_impact`. Empty when nothing references it (or
     *  when the registry has no engine bound). */
    QString impactSummary(InletProvider *p) const;

    int loadFromEngine(void *engineHandle);
    int saveToEngine(void *engineHandle);
    int saveToEngine();

    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::inlet::InletProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::inlet::InletProvider *provider);
    void providerRenamed(openswmmvis::inlet::InletProvider *provider,
                         const QString &prevName, const QString &newName);
    void providerParamsChanged(openswmmvis::inlet::InletProvider *provider);

private:
    void wireProviderSignals_(InletProvider *p);

    QVector<InletProvider *>        m_providers;
    QHash<QString, InletProvider *> m_byLowerName;
    void                           *m_engineHandle = nullptr;
};

} // namespace openswmmvis::inlet

#endif // OPENSWMMVIS_INLET_INLETREGISTRY_H
