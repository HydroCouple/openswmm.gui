/*!
 * \file   inletregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for InletProvider instances.
 *
 * Mirrors PollutantRegistry. Engine has setters but no getters for inlet
 * params, so loadFromEngine only recovers names; saveToEngine writes a
 * provider only when it is new or has been edited (dirty), so untouched
 * existing inlets are never overwritten with form defaults.
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
