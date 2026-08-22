/*!
 * \file   loadprogress.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "core/loadprogress.h"

#include <algorithm>

namespace {

/*! Per-stage share of the total, summing to 100.
 *
 *  Seeded from the measured baseline in
 *  tests/output/load_perf_2026-08-21/baseline.md — engine parse dominates a
 *  large 1D model, and the mesh stages together dominate a 2D one, so a deck
 *  with both spends roughly half the bar on each half of the work.
 *
 *  Indexed by OpenStage; a static_assert below pins the sum. */
constexpr int kWeights[static_cast<int>(OpenStage::Count_)] = {
    28,   // EngineParse
     8,   // SoaCopy
    12,   // GeomCache
     2,   // CrsFinish
     5,   // Sidecar
     8,   // Results
    15,   // MeshParse
    14,   // MeshSceneA
     8,   // MeshSceneB
};

constexpr int weightSum()
{
    int s = 0;
    for (int w : kWeights) s += w;
    return s;
}
static_assert(weightSum() == 100,
              "OpenProgressModel stage weights must sum to 100 — otherwise the "
              "bar cannot reach (or overshoots) 100.");

} // namespace

OpenProgressModel::OpenProgressModel(QObject *parent)
    : QObject(parent)
{
    m_localPct.fill(0);
}

int OpenProgressModel::stageWeight(OpenStage stage)
{
    const int i = static_cast<int>(stage);
    if (i < 0 || i >= kStageCount)
        return 0;
    return kWeights[i];
}

QString OpenProgressModel::stageLabel(OpenStage stage)
{
    switch (stage) {
    case OpenStage::EngineParse: return tr("Parsing model…");
    case OpenStage::SoaCopy:     return tr("Reading network…");
    case OpenStage::GeomCache:   return tr("Building geometry cache…");
    case OpenStage::CrsFinish:   return tr("Resolving coordinate system…");
    case OpenStage::Sidecar:     return tr("Applying project settings…");
    case OpenStage::Results:     return tr("Opening results…");
    case OpenStage::MeshParse:   return tr("Parsing 2D mesh…");
    case OpenStage::MeshSceneA:  return tr("Building mesh display geometry…");
    case OpenStage::MeshSceneB:  return tr("Finishing mesh index…");
    case OpenStage::Count_:      break;
    }
    return {};
}

void OpenProgressModel::setStage(OpenStage stage, int localPct,
                                 const QString &label)
{
    const int i = static_cast<int>(stage);
    if (i < 0 || i >= kStageCount)
        return;

    // Per-stage local progress is itself clamped monotonic: a producer that
    // reports out of order (or re-enters after a stale-revision retry, as the
    // mesh Phase-B build can) must not walk its own slice backwards.
    const int clamped = std::clamp(localPct, 0, 100);
    if (clamped > m_localPct[i])
        m_localPct[i] = clamped;

    if (!label.isEmpty())
        m_label = label;

    recompute();
}

void OpenProgressModel::finishStage(OpenStage stage)
{
    const int i = static_cast<int>(stage);
    if (i < 0 || i >= kStageCount)
        return;
    if (m_localPct[i] == 100)
        return;                 // idempotent

    m_localPct[i] = 100;
    recompute();
}

void OpenProgressModel::finishAll()
{
    bool changed = false;
    for (int &v : m_localPct) {
        if (v != 100) { v = 100; changed = true; }
    }
    if (changed || !m_finishedEmitted)
        recompute();
}

bool OpenProgressModel::isComplete() const
{
    return std::all_of(m_localPct.begin(), m_localPct.end(),
                       [](int v) { return v == 100; });
}

void OpenProgressModel::recompute()
{
    // Weighted sum in hundredths, rounded once at the end. Integer math
    // throughout so the same input always yields the same percent.
    int acc = 0;
    for (int i = 0; i < kStageCount; ++i)
        acc += kWeights[i] * m_localPct[i];

    int pct = (acc + 50) / 100;                 // round to nearest
    pct = std::clamp(pct, 0, 100);

    // Monotonic clamp (see header): the bar may jump forward when a stage is
    // skipped wholesale, but must never run backwards.
    pct = std::max(pct, m_lastPct);

    const bool pctChanged   = (pct != m_lastPct);
    const bool labelChanged = (m_label != m_emittedLabel);
    m_lastPct = pct;

    // Emit on a label-only change too — the stage text is half the point —
    // but NOT on every tick, or a masked loop reporting the same percent
    // floods the GUI thread's event queue with identical updates.
    if (pctChanged || labelChanged) {
        m_emittedLabel = m_label;
        emit progressChanged(m_lastPct, m_label);
    }

    if (!m_finishedEmitted && isComplete()) {
        m_finishedEmitted = true;
        emit finished();
    }
}
