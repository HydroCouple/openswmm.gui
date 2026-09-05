/*!
 * \file   typeconversionflow.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Shared confirm → convert → summary flow for node/link type
 *         conversion. See typeconversionflow.h.
 */
#include "ui/dialogs/typeconversionflow.h"
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QMessageBox>

namespace openswmmvis::ui {

QString TypeConversionFlow::nodeTypeLabel(int swmmNodeType)
{
    switch (swmmNodeType) {
    case 0: return tr("Junction");
    case 1: return tr("Outfall");
    case 2: return tr("Storage");
    case 3: return tr("Divider");
    case kVirtualNodeType: return tr("Virtual Junction");
    case kInletNodeType:   return tr("Inlet Junction");
    default: return {};
    }
}

QString TypeConversionFlow::linkTypeLabel(int swmmLinkType)
{
    switch (swmmLinkType) {
    case 0: return tr("Conduit");
    case 1: return tr("Pump");
    case 2: return tr("Orifice");
    case 3: return tr("Weir");
    case 4: return tr("Outlet");
    default: return {};
    }
}

QString TypeConversionFlow::confirmText(bool isNode, const QString &name,
                                        int currentType, int newType)
{
    const QString from = isNode ? nodeTypeLabel(currentType)
                                : linkTypeLabel(currentType);
    const QString to   = isNode ? nodeTypeLabel(newType)
                                : linkTypeLabel(newType);
    return tr("Convert \"%1\" from %2 to %3?\n\n"
              "All %2-specific attributes will be cleared and %3 defaults "
              "applied. Attributes incompatible with the new type will be "
              "lost. This cannot be undone.")
        .arg(name, from, to);
}

QString TypeConversionFlow::summaryHtml(const QStringList &cleared,
                                        const QStringList &warnings)
{
    QString details;
    if (!cleared.isEmpty()) {
        details += tr("<b>Cleared fields:</b><br>%1<br><br>")
                       .arg(cleared.join(QStringLiteral(", ")));
    }
    if (!warnings.isEmpty()) {
        QStringList bullets;
        for (const QString &w : warnings)
            bullets << QStringLiteral("• ") + w;
        details += tr("<b>Topology warnings:</b><br>%1")
                       .arg(bullets.join(QStringLiteral("<br>")));
    }
    if (details.isEmpty())
        details = tr("(no side effects)");
    return details;
}

bool TypeConversionFlow::run(QWidget *parent, SWMMModelLayer *layer,
                             bool isNode, const QString &name,
                             int currentType, int newType)
{
    if (!layer || name.isEmpty() || currentType == newType) return false;

    // Virtual-junction targets/sources ride the same confirm → apply →
    // summary shape, but the engine operation differs: the flag is set or
    // cleared via applySetVirtual (VIRTUAL_JUNCTION rules enforced by the
    // engine), with a plain type conversion first when the source node is
    // not already a junction.
    const bool toVirtual   = isNode && newType == kVirtualNodeType;
    // An inlet junction IS a virtual junction, so "Inlet Junction → Virtual
    // Junction" is a demotion (clear is_inlet, keep is_virtual), not a
    // conversion; and demoting it further to a plain Junction has to clear
    // both flags in that order.
    //
    // Read the inlet flag from the ENGINE rather than trusting currentType:
    // callers that predate the inlet kind (the attribute table's Change Type)
    // pass kVirtualNodeType for an inlet junction, and demoting one of those
    // without clearing is_inlet first would strand its usage row.
    bool fromInlet = false;
    if (isNode && layer->engine()) {
        const int idx = swmm_node_index(layer->engine(), name.toUtf8().constData());
        int isInlet = 0;
        if (idx >= 0) swmm_node_is_inlet(layer->engine(), idx, &isInlet);
        fromInlet = (isInlet != 0);
    }
    const bool fromVirtual = isNode && (currentType == kVirtualNodeType || fromInlet);

    const auto choice = QMessageBox::question(parent, tr("Convert Type"),
        confirmText(isNode, name, currentType, newType),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) return false;

    QStringList cleared, warnings;
    QString error;

    if (newType == kVirtualNodeType && fromInlet) {
        // Demote an inlet junction to a plain virtual junction: clear only
        // is_inlet (which also drops the node's usage row, engine-side).
        if (!layer->applySetInlet(name, false, &error)) {
            QMessageBox::warning(parent, tr("Convert Type"), error);
            return false;
        }
    } else if (toVirtual) {
        // Non-junction source: become a junction first (attribute loss was
        // covered by the confirm above), then set the flag.
        if (currentType != 0 &&
            !layer->applyNodeConvert(name, 0, &cleared, &warnings, &error)) {
            QMessageBox::warning(parent, tr("Convert Type"), error);
            return false;
        }
        if (!layer->applySetVirtual(name, true, &error)) {
            QMessageBox::warning(parent, tr("Convert Type"),
                currentType != 0
                    ? tr("\"%1\" was converted to a Junction, but could not "
                         "be made virtual:\n\n%2").arg(name, error)
                    : error);
            return false;
        }
    } else if (fromVirtual && newType == 0) {
        // Demote to a regular junction: clear the flag, nothing else moves.
        // An inlet junction sheds its inlet role first, so the usage row goes
        // with it instead of outliving the node kind that owns it.
        if (fromInlet && !layer->applySetInlet(name, false, &error)) {
            QMessageBox::warning(parent, tr("Convert Type"), error);
            return false;
        }
        if (!layer->applySetVirtual(name, false, &error)) {
            QMessageBox::warning(parent, tr("Convert Type"), error);
            return false;
        }
    } else {
        // Plain conversion (fromVirtual to a non-junction type also lands
        // here: the engine's converter clears the is_virtual flag itself).
        // The inlet role is shed explicitly first so the usage row is removed
        // by the API that owns it rather than relying on the converter's
        // cascade.
        if (fromInlet) layer->applySetInlet(name, false);
        const bool ok = isNode
            ? layer->applyNodeConvert(name, newType, &cleared, &warnings, &error)
            : layer->applyLinkConvert(name, newType, &cleared, &warnings, &error);
        if (!ok) {
            QMessageBox::warning(parent, tr("Convert Type"), error);
            return false;
        }
    }

    const QString to = isNode ? nodeTypeLabel(newType)
                              : linkTypeLabel(newType);
    QMessageBox::information(parent, tr("Conversion Complete"),
        tr("Converted <b>%1</b> to %2.<br><br>%3")
            .arg(name, to, summaryHtml(cleared, warnings)));
    return true;
}

bool TypeConversionFlow::runToInletJunction(QWidget *parent,
                                            SWMMModelLayer *layer,
                                            const QString &name,
                                            int currentType,
                                            const QString &inletDesign,
                                            const QString &captureNode,
                                            int placement)
{
    if (!layer || name.isEmpty()) return false;
    if (inletDesign.isEmpty() || captureNode.isEmpty()) return false;

    const auto choice = QMessageBox::question(parent, tr("Convert Type"),
        confirmText(/*isNode=*/true, name, currentType, kInletNodeType),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) return false;

    QStringList cleared, warnings;
    QString error;

    // Non-junction source: become a junction first (attribute loss was
    // covered by the confirm above). swmm_node_set_inlet then applies the
    // virtual-junction derived-geometry contract AND the inlet flag.
    if (currentType != 0 && currentType != kVirtualNodeType
        && !layer->applyNodeConvert(name, 0, &cleared, &warnings, &error)) {
        QMessageBox::warning(parent, tr("Convert Type"), error);
        return false;
    }
    if (!layer->applySetInlet(name, true, &error)) {
        QMessageBox::warning(parent, tr("Convert Type"), error);
        return false;
    }

    // The flag alone leaves the node failing validation with rule 633 until
    // it owns a usage row, so install one immediately; on failure the node is
    // demoted back rather than left in that state.
    SWMM_Engine eng = layer->engine();
    const int nodeIdx    = eng ? swmm_node_index(eng, name.toUtf8().constData()) : -1;
    const int designIdx  = eng ? swmm_inlet_index(eng, inletDesign.toUtf8().constData()) : -1;
    const int captureIdx = eng ? swmm_node_index(eng, captureNode.toUtf8().constData()) : -1;
    bool ok = (nodeIdx >= 0 && designIdx >= 0 && captureIdx >= 0);
    if (ok) {
        SWMM_InletUsage usage{};
        usage.host_kind        = SWMM_INLET_HOST_NODE;
        usage.host_idx         = nodeIdx;
        usage.design_idx       = designIdx;
        usage.capture_node_idx = captureIdx;
        usage.num_inlets       = 1;
        usage.placement        = placement;
        ok = layer->applySetInletUsage(usage, &error);
    }
    if (!ok) {
        layer->applySetInlet(name, false);
        QMessageBox::warning(parent, tr("Convert Type"),
            error.isEmpty()
                ? tr("\"%1\" could not be given an inlet; it was left "
                     "unchanged.").arg(name)
                : error);
        return false;
    }

    QMessageBox::information(parent, tr("Conversion Complete"),
        tr("Converted <b>%1</b> to %2 (%3 → %4).<br><br>%5")
            .arg(name, nodeTypeLabel(kInletNodeType), inletDesign, captureNode,
                 summaryHtml(cleared, warnings)));
    return true;
}

} // namespace openswmmvis::ui
