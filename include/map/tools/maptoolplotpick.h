/**
 * @file      maptoolplotpick.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice AT.2 — transient map tool that lets the Comparison Plot
 *         Dialog harvest object picks from the canvas without leaving
 *         the dialog open.
 *
 * Pushed onto the active canvas when the user toggles the dialog's
 * "Add from Map…" toolbar action. Each left-click hit-tests the visible
 * SWMM model layers, pops the `AttributePickerMenu` at the cursor, and
 * emits descriptor-based objectPicked signals for the chosen
 * attribute. Stays active across clicks; Escape, or un-checking the
 * dialog's toolbar action, pops the tool and restores the previous one.
 *
 * Background-hit clicks (no object under cursor) emit
 * `plotSystemRequested(PlotAttribute)` via the system-attribute submenu —
 * same as `OpenSWMMVisMapToolSelect::showContextMenu` does on right-click.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLPLOTPICK_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLPLOTPICK_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"

#include <QStringList>
#include "selection/selectionmanager.h" // SWMMObjectRef

class OpenSWMMVisMapToolPlotPick : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /**
     * @param[in] canvas Optional borrowed raw canvas; must outlive tool and event/paint use.
     *   QObject parent ownership is separate.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Sets identity PlotPick, inactive. Mouse press opens synchronous menus;
     *   owner must preserve tool/canvas/hit layers through nested menu loop and signal handling.
     */
    explicit OpenSWMMVisMapToolPlotPick(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @brief Destroy base QObject state; no cancellation signal, tool restoration or model edit.
     */
    ~OpenSWMMVisMapToolPlotPick() override = default;

    /**
     * @return Crosshair cursor, independent of available plot attributes.
     */
    QCursor cursor() const override;

    /*! \brief Y2b-2: the active run's species names — shown in the
     *  attribute submenu beneath the fixed set. Snapshotted by the owner
     *  at tool creation (the tool lives for one pick session).
     * @param[in] names Fallback active-run species labels copied/moved; used for model hits, while
     *   results hits use their own layer species.
     * @note No menu rebuild/notification until next click; no validation or source lookup.
     */
    void setSpeciesNames(QStringList names) { m_speciesNames = std::move(names); }

    /**
     * @param[in] event Optional borrowed event; null/no canvas no-op, nonleft passed to base.
     * @brief Hit-test six-pixel canvas tolerance on left press and synchronously offer
     *   object/system plot attribute menu.
     * @note Shared pickNetworkForPlot prioritizes results over visible model layers. Unknown/empty
     *   hit offers system menu; node/link/subcatch offer matching kinds, other types accepted
     *   without menu. Menus currently use US unit-label convention. Results species come from hit
     *   output, model fallback from stored names.
     * @note Menu cancellation emits nothing; valid system attribute emits plotSystemRequested, any
     *   chosen object entry emits layer-qualified or ordinary objectPicked with descriptor (invalid
     *   for All attributes sentinel). Menu deleted before emission; left handled path accepted.
     *   Tool remains active and changes no model selection/plot itself.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Optional borrowed event, Escape accepted; others/base no-op.
     * @brief Emit cancelled on Escape; owner restores tool/session, no self-deactivation or
     *   deletion.
     */
    void keyPressEvent(QKeyEvent *event) override;

signals:
    /** @brief A native observation marker/linked feature was picked for the invoking plot.
     * @param[in] layer Borrowed native collection; receiver must guard retained pointers.
     * @param[in] ids Stable dataset series identities. */
    void timeSeriesPicked(class TimeSeriesLayer *layer, const QStringList &ids);
    /*! \brief A map object was hit and the user picked an attribute from
     *  the submenu. Carries an invalid ResultDescriptor for the "All
     *  attributes" sentinel — handler should fan out across all valid
     *  attributes for the ref's kind.
     * @param[in] ref Copied-by-consumer object identity derived from current hit; no lifetime
     *   guarantee for underlying model object.
     * @param[in] descriptor Result attribute/species descriptor; invalid means All attributes
     *   sentinel, consumer fans out.
     * @note Emitted only after user selects object menu action; ordinary signal for model hit,
     *   layer-qualified for results hit, never both for same choice. No data extraction/plot
     *   mutation here.
     */
    void objectPicked(const SWMMObjectRef &ref,
                      const openswmmvis::plot::ResultDescriptor &descriptor);
    /**
     * @param[in] ref Copied-by-consumer object identity derived from current hit; no lifetime
     *   guarantee for underlying model object.
     * @param[in] descriptor Result attribute/species descriptor; invalid means All attributes
     *   sentinel, consumer fans out.
     * @param[in] layer Borrowed output layer that supplied pick; consumer must guard if retaining
     *   beyond synchronous signal.
     * @note Emitted only after user selects object menu action; ordinary signal for model hit,
     *   layer-qualified for results hit, never both for same choice. No data extraction/plot
     *   mutation here.
     */
    void objectPickedFromLayer(const SWMMObjectRef &ref,
                               const openswmmvis::plot::ResultDescriptor &descriptor,
                               class SWMMResultsLayer *layer);

    /*! \brief A background click (no object under cursor) with a system
     *  attribute selected from the submenu.
     * @param[in] attribute Chosen non-Unknown system attribute, no object target.
     * @note Background menu selection emits; All/invalid/cancel ignored. Owner performs plotting.
     */
    void plotSystemRequested(openswmmvis::plot::PlotAttribute attribute);

    /*! \brief User pressed Escape or otherwise cancelled the pick tool.
     *  The owner should pop the tool off the canvas and uncheck the
     *  toolbar action.
     * @note Escape emits; tool does not automatically pop itself or restore previous tool. Owner
     *   handles button/session cleanup.
     */
    void cancelled();

private:
    QStringList m_speciesNames;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLPLOTPICK_H
