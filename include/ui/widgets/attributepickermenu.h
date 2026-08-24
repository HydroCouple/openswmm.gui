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

#include <QString>
#include <QStringList>

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
     *  Returns `nullptr` for `Kind::Unknown`, `Kind::Mesh2DCell`, or
     *  `Kind::Observed` (those don't use this picker). */
    static QMenu *createForObjectKind(openswmmvis::plot::ObjectRef::Kind kind,
                                      openswmmvis::plot::UnitSystem unitSystem,
                                      QWidget *parent = nullptr,
                                      const QStringList &speciesNames =
                                          QStringList());

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
