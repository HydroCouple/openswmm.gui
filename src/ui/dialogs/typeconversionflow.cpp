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

#include <QMessageBox>

namespace openswmmvis::ui {

QString TypeConversionFlow::nodeTypeLabel(int swmmNodeType)
{
    switch (swmmNodeType) {
    case 0: return tr("Junction");
    case 1: return tr("Outfall");
    case 2: return tr("Storage");
    case 3: return tr("Divider");
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

    const auto choice = QMessageBox::question(parent, tr("Convert Type"),
        confirmText(isNode, name, currentType, newType),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) return false;

    QStringList cleared, warnings;
    QString error;
    const bool ok = isNode
        ? layer->applyNodeConvert(name, newType, &cleared, &warnings, &error)
        : layer->applyLinkConvert(name, newType, &cleared, &warnings, &error);
    if (!ok) {
        QMessageBox::warning(parent, tr("Convert Type"), error);
        return false;
    }

    const QString to = isNode ? nodeTypeLabel(newType)
                              : linkTypeLabel(newType);
    QMessageBox::information(parent, tr("Conversion Complete"),
        tr("Converted <b>%1</b> to %2.<br><br>%3")
            .arg(name, to, summaryHtml(cleared, warnings)));
    return true;
}

} // namespace openswmmvis::ui
