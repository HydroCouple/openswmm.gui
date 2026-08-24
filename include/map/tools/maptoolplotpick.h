/*!
 * \file   maptoolplotpick.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.2 — transient map tool that lets the Comparison Plot
 *         Dialog harvest object picks from the canvas without leaving
 *         the dialog open.
 *
 * Pushed onto the active canvas when the user toggles the dialog's
 * "Add from Map…" toolbar action. Each left-click hit-tests the visible
 * SWMM model layers, pops the `AttributePickerMenu` at the cursor, and
 * emits `objectPicked(SWMMObjectRef, PlotAttribute)` for the chosen
 * attribute. Stays active across clicks; Escape, or un-checking the
 * dialog's toolbar action, pops the tool and restores the previous one.
 *
 * Background-hit clicks (no object under cursor) emit
 * `plotSystemRequested(PlotAttribute)` via the system-attribute submenu —
 * same as `OpenSWMMVisMapToolSelect::showContextMenu` does on right-click.
 */
#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLPLOTPICK_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLPLOTPICK_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"

#include <QStringList>
#include "selection/selectionmanager.h"   // SWMMObjectRef

class OpenSWMMVisMapToolPlotPick : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    explicit OpenSWMMVisMapToolPlotPick(MapCanvas *canvas, QObject *parent = nullptr);
    ~OpenSWMMVisMapToolPlotPick() override = default;

    QCursor cursor() const override;

    /*! \brief Y2b-2: the active run's species names — shown in the
     *  attribute submenu beneath the fixed set. Snapshotted by the owner
     *  at tool creation (the tool lives for one pick session). */
    void setSpeciesNames(QStringList names) { m_speciesNames = std::move(names); }

    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

signals:
    /*! \brief A map object was hit and the user picked an attribute from
     *  the submenu. Carries `PlotAttribute::Unknown` for the "All
     *  attributes" sentinel — handler should fan out across all valid
     *  attributes for the ref's kind. */
    void objectPicked(const SWMMObjectRef &ref,
                      const openswmmvis::plot::ResultDescriptor &descriptor);

    /*! \brief A background click (no object under cursor) with a system
     *  attribute selected from the submenu. */
    void plotSystemRequested(openswmmvis::plot::PlotAttribute attribute);

    /*! \brief User pressed Escape or otherwise cancelled the pick tool.
     *  The owner should pop the tool off the canvas and uncheck the
     *  toolbar action. */
    void cancelled();

private:
    QStringList m_speciesNames;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLPLOTPICK_H
