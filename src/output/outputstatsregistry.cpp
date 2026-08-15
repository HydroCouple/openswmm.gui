/*!
 * \file   outputstatsregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice QA.1 — implementation. See outputstatsregistry.h for design notes.
 */

#include "output/outputstatsregistry.h"

// Slice QA.2 — registerLayer now stores QPointer<SWMMResultsLayer>
// and dereferences it via .data() / operator=, both of which need
// the complete type. (Earlier QA.1 cut held the raw pointer only.)
#include "layers/swmmresultslayer.h"

#include <QFileInfo>
#include <QHash>

namespace openswmmvis {

OutputStatsRegistry::OutputStatsRegistry(QObject *parent)
    : QObject(parent)
{
}

OutputStatsRegistry::~OutputStatsRegistry() = default;

void OutputStatsRegistry::registerLayer(SWMMResultsLayer *layer,
                                        const QString    &resultsFilePath)
{
    if (!layer) return;

    // Idempotent by pointer identity — re-registering the same layer is
    // a no-op so callers can safely wire registerLayer to both
    // resultsOpened and a one-shot construction hook without thinking
    // about ordering.
    for (const auto &s : m_slots) {
        if (s.layer.data() == layer) return;
    }

    Slot s;
    s.stableId    = QUuid::createUuid();
    s.tooltipPath = QFileInfo(resultsFilePath).absoluteFilePath();
    s.layer       = layer;
    // shortLabel will be filled by recomputeLabels() below; leaving it
    // blank here means a midflight observer would never see a partial
    // state. recomputeLabels reads from s.tooltipPath, not from the
    // layer pointer.
    m_slots.append(s);

    recomputeLabels();
    emit identitiesChanged();
}

void OutputStatsRegistry::unregisterLayer(SWMMResultsLayer *layer)
{
    if (!layer) return;

    bool removed = false;
    for (int i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].layer.data() == layer) {
            m_slots.removeAt(i);
            removed = true;
            break;
        }
    }
    if (!removed) return;

    recomputeLabels();
    emit identitiesChanged();
}

QList<OutputIdentity> OutputStatsRegistry::identities() const
{
    QList<OutputIdentity> out;
    out.reserve(m_slots.size());
    for (const auto &s : m_slots) {
        OutputIdentity id;
        id.stableId    = s.stableId;
        id.shortLabel  = s.shortLabel;
        id.tooltipPath = s.tooltipPath;
        // QPointer::data() yields nullptr after the layer is destroyed;
        // consumers null-check before dispatch.
        id.layer       = s.layer.data();
        out.append(id);
    }
    return out;
}

OutputIdentity OutputStatsRegistry::identityFor(const QUuid &id) const
{
    if (id.isNull()) return {};
    for (const auto &s : m_slots) {
        if (s.stableId == id) {
            OutputIdentity out;
            out.stableId    = s.stableId;
            out.shortLabel  = s.shortLabel;
            out.tooltipPath = s.tooltipPath;
            out.layer       = s.layer.data();
            return out;
        }
    }
    return {};
}

void OutputStatsRegistry::recomputeLabels()
{
    // Two-pass: (1) compute the basename for every slot from the cached
    // tooltipPath (captured at register-time), (2) walk in registration
    // order and append "(N)" when an earlier slot already claimed the
    // same basename. The first occurrence stays unlabelled so single-
    // output projects (the common case) read naturally.
    QHash<QString, int> seen;
    for (auto &s : m_slots) {
        const QString base = QFileInfo(s.tooltipPath).completeBaseName();
        const int count = seen.value(base, 0);
        if (count == 0) {
            s.shortLabel = base;
        } else {
            s.shortLabel = QStringLiteral("%1 (%2)").arg(base).arg(count + 1);
        }
        seen[base] = count + 1;
    }
}

} // namespace openswmmvis
