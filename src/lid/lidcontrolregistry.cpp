#include <openswmm/engine/openswmm_edit.h>
/*!
 * \file   lidcontrolregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "lid/lidcontrolregistry.h"
#include <openswmm/engine/openswmm_pollutants.h>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::lid {

LidControlRegistry::LidControlRegistry(QObject *parent)
    : QObject(parent)
{
}

LidControlRegistry::~LidControlRegistry() = default;

LidControlProvider *LidControlRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void LidControlRegistry::wireProviderSignals_(LidControlProvider *p)
{
    if (!p) return;
    connect(p, &LidControlProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &LidControlProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

LidControlProvider *LidControlRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new LidControlProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void LidControlRegistry::remove(LidControlProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool LidControlRegistry::rename(LidControlProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Rename in the engine too. Without this, saveToEngine saw an unknown
    // name and ADDED a second LID control, orphaning the original with its
    // layer parameters and LID usages.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_lid_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_lid_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int LidControlRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_lid_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_lid_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;
        // Load authored parameters without marking the provider dirty.
        LidControlProvider *p = create(id);
        if (p) {
            int type = 0;
            swmm_lid_get_type(eng, i, &type);
            p->setType(type);
            if (type == 8) {
                QVector<SWMM_LidNodeLayer> rows;
                for (int j = 0; j < swmm_lid_node_layer_count(eng, i); ++j) {
                    SWMM_LidNodeLayer row{};
                    if (swmm_lid_node_layer_get(eng, i, j, &row) == SWMM_OK) rows.append(row);
                }
                p->setNodeLayers(rows);
                swmm_lid_richards_options_get(eng, i, &p->flowOptions);
                for (int j = 0; j < rows.size(); ++j) {
                    SWMM_LidRichardsMaterial material{};
                    swmm_lid_richards_material_get(eng, i, j, &material);
                    p->retention.append(material);
                }
                for(int j=0;j<swmm_lid_node_treatment_count(eng,i);++j) {
                    SWMM_LidLayerTreatment t{};
                    if(swmm_lid_node_treatment_get(eng,i,j,&t)==SWMM_OK)
                        p->treatments.append({t.layer,QString::fromUtf8(swmm_pollutant_id(eng,t.pollutant)),t.removal_percent,t.decay_per_day,QString::fromUtf8(t.expression)});
                }
            } else {
                double a, b, c, d, e, f;
                if (swmm_lid_get_surface(eng, i, &a, &b, &c) == SWMM_OK) {
                    p->setSurfStorage(a); p->setSurfRoughness(b); p->setSurfSlope(c);
                }
                if (swmm_lid_get_soil(eng, i, &a, &b, &c, &d, &e, &f) == SWMM_OK) {
                    p->setSoilThick(a); p->setSoilPorosity(b); p->setSoilFc(c); p->setSoilWp(d); p->setSoilKsat(e); p->setSoilKslope(f);
                }
                if (swmm_lid_get_storage(eng, i, &a, &b, &c) == SWMM_OK) {
                    p->setStorThick(a); p->setStorVoidFrac(b); p->setStorKsat(c);
                }
                if (swmm_lid_get_drain(eng, i, &a, &b, &c) == SWMM_OK) {
                    p->setDrainCoeff(a); p->setDrainExpon(b); p->setDrainOffset(c);
                }
            }
            p->clearDirty(); ++added;
        }
    }
    return added;
}

int LidControlRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int LidControlRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (LidControlProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_lid_index(eng, idUtf8.constData());
        const bool isNew = (idx < 0);
        if (isNew) {
            if (swmm_lid_add(eng, idUtf8.constData(), p->type()) != SWMM_OK)
                continue;
            idx = swmm_lid_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        } else if (!p->dirty()) {
            continue;  // existing + untouched — don't clobber
        }
        if (p->type() == 8) {
            const auto& rows = p->nodeLayers();
            QVector<QByteArray> expressions;expressions.reserve(p->treatments.size());
            QVector<SWMM_LidLayerTreatment> rules;
            for(const auto& t:p->treatments) {
                expressions.append(t.expression.toUtf8());
                rules.append({t.layer,swmm_pollutant_index(eng,t.pollutant.toUtf8().constData()),t.removal,t.decay,expressions.back().constData()});
            }
            if (swmm_lid_node_configure_flow(eng, idx, rows.constData(), rows.size(), rules.constData(), rules.size(),
                &p->flowOptions, p->retention.size() == rows.size() ? p->retention.constData() : nullptr) == SWMM_OK) {
                p->clearDirty(); ++written;
            } else if (isNew) {
                swmm_lid_delete(eng, idx, nullptr);
            }
            continue;
        }
        swmm_lid_set_surface(eng, idx, p->surfStorage(), p->surfRoughness(),
                             p->surfSlope());
        swmm_lid_set_soil(eng, idx, p->soilThick(), p->soilPorosity(),
                          p->soilFc(), p->soilWp(), p->soilKsat(), p->soilKslope());
        swmm_lid_set_storage(eng, idx, p->storThick(), p->storVoidFrac(),
                             p->storKsat());
        swmm_lid_set_drain(eng, idx, p->drainCoeff(), p->drainExpon(),
                           p->drainOffset());
        p->clearDirty();
        ++written;
    }
    return written;
}

} // namespace openswmmvis::lid
