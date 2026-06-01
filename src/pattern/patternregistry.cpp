/*!
 * \file   patternregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "pattern/patternregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_tables.h>

namespace openswmmvis::pattern {

PatternRegistry::PatternRegistry(QObject *parent)
    : QObject(parent)
{
}

PatternRegistry::~PatternRegistry() = default;

PatternProvider *PatternRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

PatternProvider *PatternRegistry::create(const QString &name, PatternType type)
{
    if (name.isEmpty() || hasName(name)) return nullptr;

    auto *p = new PatternProvider(name, type, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);

    // Keep the name index in sync if someone renames the provider directly.
    // The registry's `rename()` path goes through setName too, but a direct
    // setName from elsewhere should also re-key the hash.
    connect(p, &PatternProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });

    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const QByteArray idUtf8 = name.toUtf8();
        // If the engine already has the pattern (e.g. it was loaded from the
        // model and we're re-creating a view-side provider) the add is a no-op
        // path — swmm_pattern_add returns SWMM_ERR_BADPARAM on duplicate IDs.
        if (swmm_pattern_index(eng, idUtf8.constData()) < 0)
            swmm_pattern_add(eng, idUtf8.constData(), static_cast<int>(type));
    }

    emit providerAdded(p);
    return p;
}

PatternProvider *PatternRegistry::duplicate(const QString &srcName,
                                              const QString &newName)
{
    PatternProvider *src = findByName(srcName);
    if (!src) return nullptr;
    if (newName.isEmpty() || hasName(newName)) return nullptr;

    PatternProvider *clone = create(newName, src->type());
    if (!clone) return nullptr;

    QVector<double> copied = src->factors();
    clone->setAllFactors(std::move(copied));

    // Mirror the factors to the engine immediately (create() only added the
    // pattern shell). Engine factors otherwise stay at whatever was set when
    // swmm_pattern_add was called — currently empty.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const QByteArray idUtf8 = newName.toUtf8();
        const int idx = swmm_pattern_index(eng, idUtf8.constData());
        if (idx >= 0) {
            const auto &f = clone->factors();
            swmm_pattern_set_factors(eng, idx, f.constData(), f.size());
        }
    }
    return clone;
}

void PatternRegistry::remove(PatternProvider *p)
{
    if (!p || !m_providers.contains(p)) return;

    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const QByteArray idUtf8 = p->name().toUtf8();
        const int idx = swmm_pattern_index(eng, idUtf8.constData());
        if (idx >= 0) swmm_pattern_remove(eng, idx);
    }

    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool PatternRegistry::rename(PatternProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    if (p->name().compare(newName, Qt::CaseInsensitive) == 0) {
        // Same name (possibly different case) — apply directly.
        if (m_engineHandle) {
            auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
            const QByteArray prevUtf8 = p->name().toUtf8();
            const int idx = swmm_pattern_index(eng, prevUtf8.constData());
            if (idx >= 0)
                swmm_pattern_rename(eng, idx, newName.toUtf8().constData());
        }
        p->setName(newName);
        return true;
    }
    if (hasName(newName)) return false;   // Collision with a different provider.

    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const QByteArray prevUtf8 = p->name().toUtf8();
        const int idx = swmm_pattern_index(eng, prevUtf8.constData());
        if (idx >= 0) {
            const int rc = swmm_pattern_rename(eng, idx,
                                                newName.toUtf8().constData());
            // SWMM_ERR_BADPARAM here indicates an engine-side collision (a
            // pattern was added directly to the engine since loadFromEngine).
            // Reject the rename and leave both sides untouched.
            if (rc != SWMM_OK) return false;
        }
    }
    p->setName(newName);                  // nameChanged signal re-keys m_byLowerName.
    return true;
}

int PatternRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);

    const int n = swmm_pattern_count(eng);
    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_pattern_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        int typeInt = 0;
        if (swmm_pattern_get_type(eng, i, &typeInt) != SWMM_OK) continue;
        const auto type = static_cast<PatternType>(typeInt);

        PatternProvider *p = create(id, type);
        if (!p) continue;

        int fc = 0;
        if (swmm_pattern_get_factor_count(eng, i, &fc) != SWMM_OK || fc <= 0) {
            ++added;
            continue;
        }

        QVector<double> factors;
        factors.reserve(fc);
        for (int j = 0; j < fc; ++j) {
            double v = 0.0;
            if (swmm_pattern_get_factor(eng, i, j, &v) != SWMM_OK) v = 0.0;
            factors.push_back(v);
        }
        if (factors.size() == p->factorCount())
            p->setAllFactors(std::move(factors));
        ++added;
    }
    return added;
}

int PatternRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    int written = 0;

    for (PatternProvider *p : std::as_const(m_providers)) {
        if (!p) continue;

        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_pattern_index(eng, idUtf8.constData());
        if (idx < 0) {
            // Engine doesn't have this pattern yet — add it (BUILDING state).
            if (swmm_pattern_add(eng, idUtf8.constData(),
                                  static_cast<int>(p->type())) != SWMM_OK)
                continue;
            idx = swmm_pattern_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }

        const auto &f = p->factors();
        if (swmm_pattern_set_factors(eng, idx, f.constData(), f.size()) == SWMM_OK)
            ++written;
    }
    return written;
}

} // namespace openswmmvis::pattern
