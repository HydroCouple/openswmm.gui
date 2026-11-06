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
#include "results/runspeciescatalog.h"

#include <QPoint>
#include <QString>
#include <QStringList>
#include <QVector>

class QMenu;
class QWidget;
class QAction;
class SWMM2DResultsLayer;
class SWMMResultsLayer;

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

    /*! \brief V0 / R4: the same menu with the species taken from the run's
     *  catalog — every entry labelled with its kind badge and units
     *  (`RunSpeciesCatalog::pickerLabel`); species the `.out` lacks (2D-only
     *  MSX species) are listed disabled with the reason. On nodes and links
     *  each pollutant carrying a load is followed by its load and cumulative
     *  load (plot/pollutantload.h) when \p flowUnitsCode is known.
     * @param[in] catalog The run's species (SWMMResultsLayer::speciesCatalog).
     * @param[in] flowUnitsCode `.out` flow-units code; negative omits loads.
     * @return As the name-list overload. Species actions carry the NAME;
     *   load actions a {key, label, units} QStringList (\ref descriptorFrom).
     */
    static QMenu *createForObjectKind(openswmmvis::plot::ObjectRef::Kind kind,
                                      openswmmvis::plot::UnitSystem unitSystem, QWidget *parent,
                                      const openswmmvis::results::RunSpeciesCatalog &catalog,
                                      int flowUnitsCode = -1);

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

    /*! \brief V0: the species catalog of one loaded run — \p results' `.out`
     *  species with the model's declarations, plus \p mesh's 2D species
     *  slices when a 2D run is active (so 2D-only species show as such).
     * @param[in] results Borrowed 1D results; null gives no 1D species.
     * @param[in] mesh Borrowed active 2D results; null for 1D only.
     * @return Owned catalog; empty when both are null.
     */
    static openswmmvis::results::RunSpeciesCatalog runCatalog(const SWMMResultsLayer *results,
                                                              const SWMM2DResultsLayer *mesh);

    /*! \brief Build the system-attribute menu (14 entries). Each action's
     *  data() carries the `PlotAttribute` value.
     * @param[in] unitSystem Unit system used to label the system attribute catalog.
     * @param[in] parent Optional QWidget menu owner.
     * @return Newly allocated menu without an All sentinel; caller manages lifetime when parent is null.
     */
    static QMenu *createForSystem(openswmmvis::plot::UnitSystem unitSystem,
                                  QWidget *parent = nullptr);

    /*! \brief V3: append the saved 2D run's groundwater-exchange entries for
     *  node \p node — a "Groundwater exchange (saved 2D results)" section
     *  before "All attributes" with the report-interval rate and the
     *  cumulative volume (`GwNetworkExchangeSource`). Nothing is added when
     *  \p results is null, live, or its file has no
     *  `groundwater_node_exchange_cum`. A node without an aquifer bed gets
     *  the entries disabled with the reason (missing, never zero). The
     *  actions carry mesh-variable descriptors; \ref descriptorFrom returns
     *  them and the caller routes them to the 2D run, not the `.out`.
     * @param[in,out] menu Menu built by createForObjectKind(Node); null is ignored.
     * @param[in] results Borrowed 2D results layer whose saved file is read once here.
     * @param[in] node Exact node ID.
     */
    static void appendGroundwaterExchange(QMenu *menu, const SWMM2DResultsLayer *results,
                                          const QString &node);

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
     * @param[in] action Optional picked menu action. QString payloads become species names before integer conversion is considered; a {key, label, units} QStringList (appendGroundwaterExchange) becomes a mesh-variable descriptor.
     * @return Species/fixed/mesh-variable descriptor, or invalid for null/nonconvertible/Unknown payload. No catalog-membership check is performed here.
     */
    static openswmmvis::plot::ResultDescriptor descriptorFrom(const QAction *action);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_ATTRIBUTEPICKERMENU_H
