/*!
 * \file   snowpackregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "snowpack/snowpackregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace openswmmvis::snowpack {

namespace {

/*! Copies \p n values read from one engine parameter group into the provider
 *  starting at Param index \p base. */
void setGroup_(SnowpackProvider *p, int base, const double *v, int n)
{
    for (int k = 0; k < n; ++k) p->setParam(base + k, v[k]);
}

} // namespace

SnowpackRegistry::SnowpackRegistry(QObject *parent)
    : QObject(parent)
{
}

SnowpackRegistry::~SnowpackRegistry() = default;

SnowpackProvider *SnowpackRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void SnowpackRegistry::wireProviderSignals_(SnowpackProvider *p)
{
    if (!p) return;
    connect(p, &SnowpackProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &SnowpackProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

SnowpackProvider *SnowpackRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new SnowpackProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void SnowpackRegistry::remove(SnowpackProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool SnowpackRegistry::rename(SnowpackProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Rename in the engine too. Without this, saveToEngine saw an unknown
    // name and ADDED a second snowpack, orphaning the original.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_snowpack_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_snowpack_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int SnowpackRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_snowpack_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_snowpack_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        SnowpackProvider *p = create(id);
        if (!p) continue;

        // Each engine group is read into a scratch array, then copied into the
        // provider only when the read succeeded.
        double v[7] = {};
        if (swmm_snowpack_get_plowable(eng, i, &v[0], &v[1], &v[2], &v[3],
                                       &v[4], &v[5], &v[6]) == SWMM_OK)
            setGroup_(p, SnowpackProvider::PlowableCmin, v, 7);
        if (swmm_snowpack_get_impervious(eng, i, &v[0], &v[1], &v[2], &v[3],
                                         &v[4], &v[5], &v[6]) == SWMM_OK)
            setGroup_(p, SnowpackProvider::ImperviousCmin, v, 7);
        if (swmm_snowpack_get_pervious(eng, i, &v[0], &v[1], &v[2], &v[3],
                                       &v[4], &v[5], &v[6]) == SWMM_OK)
            setGroup_(p, SnowpackProvider::PerviousCmin, v, 7);

        double r[6] = {};
        if (swmm_snowpack_get_removal(eng, i, &r[0], &r[1], &r[2],
                                      &r[3], &r[4], &r[5]) == SWMM_OK)
            setGroup_(p, SnowpackProvider::RemovalDsnow, r, 6);

        char buf[256] = {};
        if (swmm_snowpack_get_removal_subcatch(eng, i, buf, sizeof(buf)) == SWMM_OK)
            p->setRemovalSubcatch(QString::fromUtf8(buf));

        ++added;
    }
    return added;
}

int SnowpackRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int SnowpackRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (SnowpackProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_snowpack_index(eng, idUtf8.constData());
        if (idx < 0) {
            if (swmm_snowpack_add(eng, idUtf8.constData()) != SWMM_OK) continue;
            idx = swmm_snowpack_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }

        swmm_snowpack_set_plowable(eng, idx,
                                   p->param(SnowpackProvider::PlowableCmin),
                                   p->param(SnowpackProvider::PlowableCmax),
                                   p->param(SnowpackProvider::PlowableTbase),
                                   p->param(SnowpackProvider::PlowableFwFrac),
                                   p->param(SnowpackProvider::PlowableSd0),
                                   p->param(SnowpackProvider::PlowableFw0),
                                   p->param(SnowpackProvider::PlowableLast));
        swmm_snowpack_set_impervious(eng, idx,
                                     p->param(SnowpackProvider::ImperviousCmin),
                                     p->param(SnowpackProvider::ImperviousCmax),
                                     p->param(SnowpackProvider::ImperviousTbase),
                                     p->param(SnowpackProvider::ImperviousFwFrac),
                                     p->param(SnowpackProvider::ImperviousSd0),
                                     p->param(SnowpackProvider::ImperviousFw0),
                                     p->param(SnowpackProvider::ImperviousLast));
        swmm_snowpack_set_pervious(eng, idx,
                                   p->param(SnowpackProvider::PerviousCmin),
                                   p->param(SnowpackProvider::PerviousCmax),
                                   p->param(SnowpackProvider::PerviousTbase),
                                   p->param(SnowpackProvider::PerviousFwFrac),
                                   p->param(SnowpackProvider::PerviousSd0),
                                   p->param(SnowpackProvider::PerviousFw0),
                                   p->param(SnowpackProvider::PerviousLast));
        swmm_snowpack_set_removal(eng, idx,
                                  p->param(SnowpackProvider::RemovalDsnow),
                                  p->param(SnowpackProvider::RemovalFOut),
                                  p->param(SnowpackProvider::RemovalFImp),
                                  p->param(SnowpackProvider::RemovalFPerv),
                                  p->param(SnowpackProvider::RemovalFImelt),
                                  p->param(SnowpackProvider::RemovalFSubcatch));
        const QByteArray subUtf8 = p->removalSubcatch().toUtf8();
        swmm_snowpack_set_removal_subcatch(eng, idx, subUtf8.constData());
        ++written;
    }
    return written;
}

} // namespace openswmmvis::snowpack
