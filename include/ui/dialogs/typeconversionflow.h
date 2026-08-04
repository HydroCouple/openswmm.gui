/*!
 * \file   typeconversionflow.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Shared confirm → convert → summary flow for changing a node or
 *         link to a different SWMM type. Reused by:
 *           - `OpenSWMMVisMapToolSelect::showContextMenu` ("Convert To ▸")
 *           - `AttributeTablePanel::onChangeTypeTriggered` ("Change Type…")
 *
 * The user is warned BEFORE the conversion that the old type's specific
 * attributes will be cleared (the conversion is not undoable), and shown
 * the engine-reported cleared fields + topology warnings AFTER it.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_TYPECONVERSIONFLOW_H
#define OPENSWMMVIS_UI_DIALOGS_TYPECONVERSIONFLOW_H

#include <QCoreApplication>
#include <QString>
#include <QStringList>

class QWidget;
class SWMMModelLayer;

namespace openswmmvis::ui {

class TypeConversionFlow
{
    Q_DECLARE_TR_FUNCTIONS(TypeConversionFlow)
public:
    TypeConversionFlow() = delete;

    /*! \brief GUI-only pseudo node type for the Convert To flow: a virtual
     *  junction (engine-side it is a JUNCTION with the is_virtual flag, not
     *  a fifth SWMM_NodeType). Callers pass this as currentType when the
     *  node's flag is set, and as newType to convert into one. */
    static constexpr int kVirtualNodeType = 4;

    /*! \brief User-facing label for a SWMM_NodeType value
     *  (Junction / Outfall / Storage / Divider) or kVirtualNodeType
     *  (Virtual Junction). Empty if out of range. */
    static QString nodeTypeLabel(int swmmNodeType);

    /*! \brief User-facing label for a SWMM_LinkType value
     *  (Conduit / Pump / Orifice / Weir / Outlet). Empty if out of range. */
    static QString linkTypeLabel(int swmmLinkType);

    /*! \brief Pre-conversion confirmation text warning that the old type's
     *  specific attributes will be lost. Pure text builder. */
    static QString confirmText(bool isNode, const QString &name,
                               int currentType, int newType);

    /*! \brief Post-conversion summary (cleared fields + topology warnings,
     *  or "(no side effects)" when both are empty). Pure text builder. */
    static QString summaryHtml(const QStringList &cleared,
                               const QStringList &warnings);

    /*! \brief Confirm with the user, then convert via
     *  `SWMMModelLayer::applyNodeConvert/applyLinkConvert`, then show the
     *  engine-reported summary (or the failure reason). The layer method
     *  emits the repaint/geometry/attribute signals that refresh the map
     *  and panels. Returns true only if the conversion succeeded. */
    static bool run(QWidget *parent, SWMMModelLayer *layer, bool isNode,
                    const QString &name, int currentType, int newType);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TYPECONVERSIONFLOW_H
