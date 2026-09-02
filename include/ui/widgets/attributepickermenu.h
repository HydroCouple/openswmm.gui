/*!
 * \file   attributepickermenu.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.2 — factory that builds a `QMenu` of PlotAttribute
 *         choices valid for a given ObjectRef::Kind. Reused by:
 *           - `OpenSWMMVisMapToolSelect::showContextMenu` (map right-click)
 *           - `OpenSWMMVisMapToolPlotPick` (dialog "Add from Map" tool)
 *           - `ComparisonPlotDialog`'s "Add System Series" toolbar button
 *
 * Each menu entry carries its `PlotAttribute` value as the action's
 * `data()` (stored as `int` in a `QVariant`) so the caller can route the
 * chosen action to the right signal.
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

namespace openswmmvis::ui {

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
     *  `Kind::Observed` (those don't use this picker). */
    static QMenu *createForObjectKind(openswmmvis::plot::ObjectRef::Kind kind,
                                      openswmmvis::plot::UnitSystem unitSystem,
                                      QWidget *parent = nullptr,
                                      const QStringList &speciesNames =
                                          QStringList());

    /*! \brief Pop a "Plot time series" context menu for a 2D mesh kind at
     *  \p globalPos and return the attributes the user picked — one entry,
     *  or every enabled entry when "All attributes" was chosen. Entries
     *  \p availability can't serve (`supportsAttribute` false) are greyed
     *  out with \p unavailableTip as tooltip; pass nullptr to enable all.
     *  Empty on cancel. Blocks in `QMenu::exec` — call it from a mouse
     *  RELEASE handler, never while a button is still down. */
    static QVector<openswmmvis::plot::PlotAttribute> execForMeshKind(
        openswmmvis::plot::ObjectRef::Kind kind,
        const QPoint &globalPos,
        const openswmmvis::plot::IRunLayer *availability,
        const QString &unavailableTip = QString());

    /*! \brief Build the system-attribute menu (14 entries). Each action's
     *  data() carries the `PlotAttribute` value. */
    static QMenu *createForSystem(openswmmvis::plot::UnitSystem unitSystem,
                                  QWidget *parent = nullptr);

    /*! \brief Extract the chosen PlotAttribute from a picked action.
     *  Returns `PlotAttribute::Unknown` for the "All attributes" sentinel
     *  or when the action is null / unrelated.
     *  \warning Species actions ALSO read as Unknown here (their data is
     *  the species name, not an int) — callers offering species must use
     *  \ref descriptorFrom, which tells the three cases apart. */
    static openswmmvis::plot::PlotAttribute attributeFrom(const QAction *action);

    /*! \brief Y2b-2 (amendment D-Y4): extract what the picked action
     *  plots — a fixed attribute or a species BY NAME. An INVALID
     *  descriptor means the "All attributes" sentinel (or a null
     *  action); callers fan out across the run's descriptor list. */
    static openswmmvis::plot::ResultDescriptor descriptorFrom(
        const QAction *action);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_ATTRIBUTEPICKERMENU_H
