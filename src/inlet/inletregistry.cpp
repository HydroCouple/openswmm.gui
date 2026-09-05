/*!
 * \file   inletregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "inlet/inletregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_edit.h>   // swmm_inlet_delete / analyze_impact

#include <QStringList>

#include <cstring>

namespace openswmmvis::inlet {

namespace {

bool usesGrateGroup(InletType t)
{
    return t == InletType::Grate || t == InletType::DropGrate
        || t == InletType::Combo;
}

bool usesCurbGroup(InletType t)
{
    return t == InletType::Curb || t == InletType::DropCurb
        || t == InletType::Combo;
}

/*! \brief Engine design → provider design.
 *
 *  `swmm_inlet_set_design` stores only the fields the type actually uses and
 *  zeroes the rest, so reading a GRATE back yields curb_length = 0. Copying
 *  those zeros into the provider would leave the user unable to switch the
 *  design to CURB (every subsequent edit would fail the "> 0" validation with
 *  no way to enter the first of the pair). The inapplicable groups therefore
 *  keep `InletDesignData`'s defaults, exactly as a freshly created design has.
 */
InletDesignData fromEngineDesign(const SWMM_InletDesign &d)
{
    InletDesignData out;
    out.type = static_cast<InletType>(d.type);

    if (usesGrateGroup(out.type)) {
        out.grateLength = d.grate_length;
        out.grateWidth  = d.grate_width;
        out.grateType   = static_cast<GrateType>(d.grate_type);
        out.openArea    = d.open_area;
        out.splashVeloc = d.splash_veloc;
    }
    if (usesCurbGroup(out.type)) {
        out.curbLength = d.curb_length;
        out.curbHeight = d.curb_height;
        out.throat     = static_cast<ThroatType>(d.throat);
    }
    if (out.type == InletType::Slotted) {
        out.slotLength = d.slot_length;
        out.slotWidth  = d.slot_width;
    }
    if (out.type == InletType::Custom) {
        out.curveId   = QString::fromUtf8(d.curve_id);
        out.curveKind = static_cast<InletCurveKind>(d.curve_kind);
    }
    return out;
}

SWMM_InletDesign toEngineDesign(const InletDesignData &d)
{
    SWMM_InletDesign out{};
    out.type         = static_cast<int>(d.type);
    out.grate_length = d.grateLength;
    out.grate_width  = d.grateWidth;
    out.grate_type   = static_cast<int>(d.grateType);
    out.open_area    = d.openArea;
    out.splash_veloc = d.splashVeloc;
    out.curb_length  = d.curbLength;
    out.curb_height  = d.curbHeight;
    out.throat       = static_cast<int>(d.throat);
    out.slot_length  = d.slotLength;
    out.slot_width   = d.slotWidth;
    out.curve_kind   = static_cast<int>(d.curveKind);

    const QByteArray curve = d.curveId.toUtf8();
    const int n = qMin<int>(curve.size(), int(sizeof(out.curve_id)) - 1);
    if (n > 0) std::memcpy(out.curve_id, curve.constData(), size_t(n));
    out.curve_id[n > 0 ? n : 0] = '\0';
    return out;
}

} // namespace

InletRegistry::InletRegistry(QObject *parent)
    : QObject(parent)
{
}

InletRegistry::~InletRegistry() = default;

InletProvider *InletRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void InletRegistry::wireProviderSignals_(InletProvider *p)
{
    if (!p) return;
    connect(p, &InletProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &InletProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
    connect(p, &InletProvider::commentsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

InletProvider *InletRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new InletProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

QString InletRegistry::impactSummary(InletProvider *p) const
{
    if (!p || !m_engineHandle) return {};
    auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
    const int idx = swmm_inlet_index(eng, p->name().toUtf8().constData());
    if (idx < 0) return {};

    SWMM_ImpactReport report{};
    if (swmm_inlet_analyze_impact(eng, idx, &report) != SWMM_OK) return {};

    int usages = 0, other = 0;
    for (int i = 0; i < report.n_entries; ++i) {
        if (report.entries[i].obj_type == SWMM_REF_INLET_USAGE) ++usages;
        else                                                     ++other;
    }
    swmm_impact_report_free(&report);

    QStringList parts;
    if (usages) parts << tr("%n inlet placement(s)", nullptr, usages);
    if (other)  parts << tr("%n other reference(s)", nullptr, other);
    return parts.join(QStringLiteral(", "));
}

void InletRegistry::remove(InletProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);

    // Delete the ENGINE copy too — saveToEngine only ever adds/updates, so a
    // Qt-side-only delete left the design in the written INP (mirrors the
    // transect / land-use registries).
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_inlet_index(eng, p->name().toUtf8().constData());
        if (idx >= 0) swmm_inlet_delete(eng, idx, nullptr);
    }

    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool InletRegistry::rename(InletProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Rename in the engine too. Without this, saveToEngine saw an unknown
    // name and ADDED a second inlet, orphaning the original with its
    // parameters and node usages.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_inlet_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_inlet_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int InletRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_inlet_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_inlet_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        InletProvider *p = create(id);
        if (!p) continue;

        SWMM_InletDesign d{};
        if (swmm_inlet_get_design(eng, i, &d) == SWMM_OK)
            p->setDesign(fromEngineDesign(d));

        char cbuf[1024] = {};
        if (swmm_inlet_get_comment(eng, i, cbuf, int(sizeof(cbuf))) == SWMM_OK)
            p->setComments(QString::fromUtf8(cbuf));

        ++added;
    }
    return added;
}

int InletRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int InletRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (InletProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_inlet_index(eng, idUtf8.constData());
        if (idx < 0) {
            if (swmm_inlet_add(eng, idUtf8.constData(),
                               inletTypeKeyword(p->type())) != SWMM_OK)
                continue;
            idx = swmm_inlet_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }
        const SWMM_InletDesign d = toEngineDesign(p->design());
        swmm_inlet_set_design(eng, idx, &d);
        swmm_inlet_set_comment(eng, idx, p->comments().toUtf8().constData());
        ++written;
    }
    return written;
}

} // namespace openswmmvis::inlet
