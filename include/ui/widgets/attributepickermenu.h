/**
 * @file      attributepickermenu.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice AT.2 — factory that builds a `QMenu` of PlotAttribute
 *         choices valid for a given ObjectRef::Kind. Reused by:
 *           - `OpenSWMMVisMapToolSelect::showContextMenu` (map right-click)
 *           - `OpenSWMMVisMapToolPlotPick` (dialog "Add from Map" tool)
 *           - `ComparisonPlotDialog`'s "Add System Series" toolbar button
 *
 * Each menu entry carries its `PlotAttribute` value as the action's
 * `data()` (stored as `int` in a `QVariant`) so the caller can route the
 * chosen action to the right signal.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_ATTRIBUTEPICKERMENU_H
#define OPENSWMMVIS_UI_WIDGETS_ATTRIBUTEPICKERMENU_H

#include "plot/irunlayer.h"
#include "plot/plotattribute.h"

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

class QMenu;
class QWidget;
class QAction;

namespace openswmmvis::ui
{

class AttributePickerMenu
{
public:
    /*! \brief Build (and return) a menu of attributes valid for `kind`.
     *  Caller owns the returned menu (set its parent on construction or
     *  delete after `exec`).
     *
     *  Includes an `All attributes` trailing entry that, when chosen,
     *  carries `PlotAttribute::Unknown` as the action's data() — callers
     *  treat that as a request to add one series per attribute.
     *
     *  Mesh kinds (`Mesh2DCell` / `Mesh2DEdge` / `Mesh2DVertex`) get their
     *  fixed list from `attributesForKind`; the caller greys out entries the
     *  layer can't serve. Returns `nullptr` for `Kind::Unknown` or
     *  `Kind::Observed` (those don't use this picker).
     * @param[in] kind Supported node/link/subcatchment/mesh/system kind. System dispatches to createForSystem().
     * @param[in] unitSystem Unit labels for fixed attributes/species; no data values are converted here.
     * @param[in] parent Optional QWidget owner of the new menu/actions.
     * @param[in] speciesNames Nonempty species names appended only for node/link/subcatchment; strings become action payloads.
     * @return Newly allocated parent-owned/caller-managed menu, or null for unsupported kinds. System menus do not add the All sentinel.
     */
    static QMenu *createForObjectKind(openswmmvis::plot::ObjectRef::Kind kind,
                                      openswmmvis::plot::UnitSystem unitSystem,
                                      QWidget *parent = nullptr,
                                      const QStringList &speciesNames = QStringList());

    /*! \brief Pop a "Plot time series" context menu for a 2D mesh kind at
     *  \p globalPos and return what the user picked — one entry, or every
     *  enabled entry when "All attributes" was chosen. Entries
     *  \p availability can't serve (`supportsAttribute` false) are greyed
     *  out with \p unavailableTip as tooltip; pass nullptr to enable all.
     *  With an \p availability layer, the 2D catalog variables it lists
     *  (`resultDescriptorsForKind`) follow under Surface / Groundwater
     *  sections. Empty on cancel. Blocks in `QMenu::exec` — call it from a
     *  mouse RELEASE handler, never while a button is still down.
     * @param[in] kind Mesh object kind to enumerate with SI unit labels.
     * @param[in] globalPos Global screen position passed to blocking QMenu::exec().
     * @param[in] availability Optional borrowed run layer used to disable unsupported fixed attributes and append dynamic mesh descriptors.
     * @param[in] unavailableTip Tooltip assigned to unsupported fixed attributes when nonempty.
     * @return Selected descriptor, all enabled catalog descriptors in menu order, or empty on cancel/unsupported kind. Temporary menu is deleted before returning.
     */
    static QVector<openswmmvis::plot::ResultDescriptor>
    execForMeshKind(openswmmvis::plot::ObjectRef::Kind kind, const QPoint &globalPos,
                    const openswmmvis::plot::IRunLayer *availability,
                    const QString &unavailableTip = QString());

    /*! \brief Build the system-attribute menu (14 entries). Each action's
     *  data() carries the `PlotAttribute` value.
     * @param[in] unitSystem Unit system used to label the system attribute catalog.
     * @param[in] parent Optional QWidget menu owner.
     * @return Newly allocated menu without an All sentinel; caller manages lifetime when parent is null.
     */
    static QMenu *createForSystem(openswmmvis::plot::UnitSystem unitSystem,
                                  QWidget *parent = nullptr);

    /*! \brief Extract the chosen PlotAttribute from a picked action.
     *  Returns `PlotAttribute::Unknown` for the "All attributes" sentinel
     *  or when the action is null / nonconvertible.
     *  \warning Nonnumeric species actions also read as Unknown here (their data is
     *  the species name, not an int) — callers offering species must use
     *  \ref descriptorFrom, which tells the three cases apart.
     * @param[in] action Optional picked action; no ownership transfer.
     * @return Integer-converted payload cast to PlotAttribute, or Unknown if null/nonconvertible. Unrelated integer payloads are not range-checked; numeric-looking species strings may also convert.
     */
    static openswmmvis::plot::PlotAttribute attributeFrom(const QAction *action);

    /*! \brief Y2b-2 (amendment D-Y4): extract what the picked action
     *  plots — a fixed attribute or a species BY NAME. An INVALID
     *  descriptor means the "All attributes" sentinel (or a null
     *  action); callers fan out across the run's descriptor list.
     * @param[in] action Optional picked menu action. QString payloads become species names before integer conversion is considered.
     * @return Species/fixed descriptor, or invalid for null/nonconvertible/Unknown payload. No catalog-membership check is performed here.
     */
    static openswmmvis::plot::ResultDescriptor descriptorFrom(const QAction *action);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_ATTRIBUTEPICKERMENU_H
