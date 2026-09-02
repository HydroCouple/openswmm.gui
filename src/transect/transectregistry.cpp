/*!
 * \file   transectregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "transect/transectregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_edit.h>   // swmm_transect_delete

#include <QDebug>   // qWarning for setAllPoints rejection

namespace openswmmvis::transect {

TransectRegistry::TransectRegistry(QObject *parent)
    : QObject(parent)
{
}

TransectRegistry::~TransectRegistry() = default;

TransectProvider *TransectRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void TransectRegistry::wireProviderSignals_(TransectProvider *p)
{
    if (!p) return;
    connect(p, &TransectProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &TransectProvider::pointsChanged, this,
            [this, p](int, int) { emit providerPointsChanged(p); });
    connect(p, &TransectProvider::pointsInserted, this,
            [this, p](int, int) { emit providerPointsChanged(p); });
    connect(p, &TransectProvider::pointsRemoved, this,
            [this, p](int, int) { emit providerPointsChanged(p); });
    connect(p, &TransectProvider::commentsChanged, this,
            [this, p]() { emit providerMetadataChanged(p); });
    connect(p, &TransectProvider::roughnessChanged, this,
            [this, p]() { emit providerMetadataChanged(p); });
    connect(p, &TransectProvider::bankStationsChanged, this,
            [this, p]() { emit providerMetadataChanged(p); });
    connect(p, &TransectProvider::encroachmentStationsChanged, this,
            [this, p]() { emit providerMetadataChanged(p); });
    connect(p, &TransectProvider::modifiersChanged, this,
            [this, p]() { emit providerMetadataChanged(p); });
}

TransectProvider *TransectRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new TransectProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void TransectRegistry::remove(TransectProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);

    // Delete the ENGINE copy too (perf-plan Phase A3, and a bug fix: without
    // this the engine kept the transect forever — saveToEngine only ever
    // adds/updates — so a "deleted" transect reappeared in the written INP).
    // Same engine-first pattern as rename() above; swmm_transect_delete also
    // resets IRREGULAR links that referenced it.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx =
            swmm_transect_index(eng, p->name().toUtf8().constData());
        if (idx >= 0) swmm_transect_delete(eng, idx, nullptr);
    }

    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool TransectRegistry::rename(TransectProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Rename in the engine too. Without this, saveToEngine saw an unknown
    // name and ADDED a second transect, orphaning the original along with
    // every IRREGULAR link still pointing at it.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_transect_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_transect_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int TransectRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;  // remember for the no-arg saveToEngine()

    const int n = swmm_transect_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_transect_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        TransectProvider *p = create(id);
        if (!p) continue;

        // Comments.
        char cbuf[1024] = {};
        if (swmm_transect_get_comments(eng, i, cbuf, sizeof(cbuf)) == SWMM_OK)
            p->setComments(QString::fromUtf8(cbuf));

        // Roughness.
        double nL = 0.0, nR = 0.0, nC = 0.0;
        if (swmm_transect_get_roughness(eng, i, &nL, &nR, &nC) == SWMM_OK)
            p->setRoughness(nL, nR, nC);

        // Bank stations.
        double xLb = 0.0, xRb = 0.0;
        if (swmm_transect_get_bank_stations(eng, i, &xLb, &xRb) == SWMM_OK)
            p->setBankStations(xLb, xRb);

        // Encroachment stations.
        double xLe = 0.0, xRe = 0.0;
        if (swmm_transect_get_encroachment_stations(eng, i, &xLe, &xRe) == SWMM_OK)
            p->setEncroachmentStations(xLe, xRe);

        // Modifiers.
        double xF = 1.0, yF = 0.0, lF = 1.0;
        if (swmm_transect_get_modifiers(eng, i, &xF, &yF, &lF) == SWMM_OK)
            p->setModifiers(xF, yF, lF);

        // Stations.
        const int npts = swmm_transect_get_station_count(eng, i);
        if (npts > 0) {
            QVector<TransectPoint> pts;
            pts.reserve(npts);
            for (int j = 0; j < npts; ++j) {
                double s = 0.0, e = 0.0;
                if (swmm_transect_get_station(eng, i, j, &s, &e) != SWMM_OK) continue;
                pts.push_back({s, e});
            }
            // Bypass invariant: engine source-of-truth is authoritative.
            QString reason;
            const bool ok = p->setAllPoints(std::move(pts), &reason);
            if (!ok) {
                qWarning().noquote()
                    << "[transect-load]" << id
                    << "setAllPoints rejected:" << reason;
            }
        }
        ++added;
    }
    return added;
}

int TransectRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int TransectRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (TransectProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_transect_index(eng, idUtf8.constData());
        if (idx < 0) {
            if (swmm_transect_add(eng, idUtf8.constData()) != SWMM_OK) continue;
            idx = swmm_transect_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }
        swmm_transect_set_comments(eng, idx, p->comments().toUtf8().constData());
        swmm_transect_set_roughness(eng, idx, p->nLeftBank(), p->nRightBank(), p->nChannel());
        swmm_transect_set_bank_stations(eng, idx, p->xLeftBank(), p->xRightBank());
        swmm_transect_set_encroachment_stations(eng, idx,
            p->xLeftEncroachment(), p->xRightEncroachment());
        swmm_transect_set_modifiers(eng, idx,
            p->stationMultiplier(), p->elevationOffset(), p->meanderFactor());
        swmm_transect_clear_stations(eng, idx);
        for (const auto &pt : p->points())
            swmm_transect_add_station(eng, idx, pt.station, pt.elevation);
        ++written;
    }
    return written;
}

} // namespace openswmmvis::transect
