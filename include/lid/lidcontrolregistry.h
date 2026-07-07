/*!
 * \file   lidcontrolregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for LidControlProvider instances.
 *
 * Mirrors InletRegistry. Engine has layer setters but no getters, so
 * loadFromEngine recovers names only; saveToEngine writes a control only when
 * it is new or has been edited (dirty), preventing clobber of untouched
 * existing controls.
 */
#ifndef OPENSWMMVIS_LID_LIDCONTROLREGISTRY_H
#define OPENSWMMVIS_LID_LIDCONTROLREGISTRY_H

#include "lid/lidcontrolprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::lid {

class LidControlRegistry : public QObject
{
    Q_OBJECT

public:
    explicit LidControlRegistry(QObject *parent = nullptr);
    ~LidControlRegistry() override;

    QVector<LidControlProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    LidControlProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    LidControlProvider *create(const QString &name);
    void remove(LidControlProvider *p);
    bool rename(LidControlProvider *p, const QString &newName);

    int loadFromEngine(void *engineHandle);
    int saveToEngine(void *engineHandle);
    int saveToEngine();

    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::lid::LidControlProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::lid::LidControlProvider *provider);
    void providerRenamed(openswmmvis::lid::LidControlProvider *provider,
                         const QString &prevName, const QString &newName);
    void providerParamsChanged(openswmmvis::lid::LidControlProvider *provider);

private:
    void wireProviderSignals_(LidControlProvider *p);

    QVector<LidControlProvider *>        m_providers;
    QHash<QString, LidControlProvider *> m_byLowerName;
    void                                *m_engineHandle = nullptr;
};

} // namespace openswmmvis::lid

#endif // OPENSWMMVIS_LID_LIDCONTROLREGISTRY_H
