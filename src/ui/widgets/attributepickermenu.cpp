/*!
 * \file   attributepickermenu.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/attributepickermenu.h"

#include <QAction>
#include <QMenu>
#include <QObject>
#include <QVariant>

namespace openswmmvis::ui {

using namespace openswmmvis::plot;

namespace {

/*! \brief Add one action per `PlotAttribute` in \p attrs to \p menu. */
void addAttrActions(QMenu *menu,
                    const QVector<PlotAttribute> &attrs,
                    UnitSystem u)
{
    for (PlotAttribute a : attrs) {
        QAction *act = menu->addAction(labelWithUnits(a, u));
        act->setData(static_cast<int>(a));
    }
}

void addAllAttributesEntry(QMenu *menu)
{
    menu->addSeparator();
    QAction *all = menu->addAction(QObject::tr("All attributes"));
    all->setData(static_cast<int>(PlotAttribute::Unknown));   // sentinel
}

} // namespace

QMenu *AttributePickerMenu::createForObjectKind(ObjectRef::Kind kind,
                                                UnitSystem u,
                                                QWidget *parent)
{
    auto *menu = new QMenu(parent);

    switch (kind) {
    case ObjectRef::Kind::Node:
    case ObjectRef::Kind::Link:
    case ObjectRef::Kind::Subcatch:
        addAttrActions(menu, attributesForKind(kind), u);
        break;

    case ObjectRef::Kind::System:
        delete menu;
        return createForSystem(u, parent);

    default:
        delete menu;
        return nullptr;
    }

    addAllAttributesEntry(menu);
    return menu;
}

QMenu *AttributePickerMenu::createForSystem(UnitSystem u, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    addAttrActions(menu, systemPlotAttributes(), u);
    return menu;
}

PlotAttribute AttributePickerMenu::attributeFrom(const QAction *action)
{
    if (!action) return PlotAttribute::Unknown;
    bool ok = false;
    const int v = action->data().toInt(&ok);
    if (!ok) return PlotAttribute::Unknown;
    return static_cast<PlotAttribute>(v);
}

} // namespace openswmmvis::ui
