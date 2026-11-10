/**
 * @file      typeconversionflow.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared confirm → convert → summary flow for node/link type
 *         conversion. See typeconversionflow.h.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/typeconversionflow.h"
namespace openswmmvis::ui
{

QString TypeConversionFlow::nodeTypeLabel(int swmmNodeType)
{
    switch (swmmNodeType) {
    case 0:
        return tr("Junction");
    case 1:
        return tr("Outfall");
    case 2:
        return tr("Storage");
    case 3:
        return tr("Divider");
    case kVirtualNodeType:
        return tr("Virtual Junction");
    case kInletNodeType:
        return tr("Inlet Junction");
    default:
        return {};
    }
}

QString TypeConversionFlow::linkTypeLabel(int swmmLinkType)
{
    switch (swmmLinkType) {
    case 0:
        return tr("Conduit");
    case 1:
        return tr("Pump");
    case 2:
        return tr("Orifice");
    case 3:
        return tr("Weir");
    case 4:
        return tr("Outlet");
    default:
        return {};
    }
}

QString TypeConversionFlow::confirmText(bool isNode, const QString &name, int currentType,
                                        int newType)
{
    const QString from = isNode ? nodeTypeLabel(currentType) : linkTypeLabel(currentType);
    const QString to = isNode ? nodeTypeLabel(newType) : linkTypeLabel(newType);
    return tr("Convert \"%1\" from %2 to %3?\n\n"
              "All %2-specific attributes will be cleared and %3 defaults "
              "applied. Attributes incompatible with the new type will be "
              "lost. This cannot be undone.")
        .arg(name, from, to);
}

QString TypeConversionFlow::summaryHtml(const QStringList &cleared, const QStringList &warnings)
{
    QString details;
    if (!cleared.isEmpty()) {
        details +=
            tr("<b>Cleared fields:</b><br>%1<br><br>").arg(cleared.join(QStringLiteral(", ")));
    }
    if (!warnings.isEmpty()) {
        QStringList bullets;
        for (const QString &w : warnings)
            bullets << QStringLiteral("• ") + w;
        details += tr("<b>Topology warnings:</b><br>%1").arg(bullets.join(QStringLiteral("<br>")));
    }
    if (details.isEmpty())
        details = tr("(no side effects)");
    return details;
}

} // namespace openswmmvis::ui
