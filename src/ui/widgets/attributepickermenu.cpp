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
                    std::initializer_list<PlotAttribute> attrs,
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
        addAttrActions(menu, {
            PlotAttribute::NodeDepth,
            PlotAttribute::NodeHead,
            PlotAttribute::NodeVolume,
            PlotAttribute::NodeLateralInflow,
            PlotAttribute::NodeTotalInflow,
            PlotAttribute::NodeOverflow,
        }, u);
        break;

    case ObjectRef::Kind::Link:
        addAttrActions(menu, {
            PlotAttribute::LinkFlow,
            PlotAttribute::LinkDepth,
            PlotAttribute::LinkVelocity,
            PlotAttribute::LinkVolume,
            PlotAttribute::LinkCapacity,
        }, u);
        break;

    case ObjectRef::Kind::Subcatch:
        addAttrActions(menu, {
            PlotAttribute::SubcatchRainfall,
            PlotAttribute::SubcatchSnowDepth,
            PlotAttribute::SubcatchEvap,
            PlotAttribute::SubcatchInfil,
            PlotAttribute::SubcatchRunoff,
        }, u);
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
    addAttrActions(menu, {
        PlotAttribute::SystemRainfall,
        PlotAttribute::SystemRunoff,
        PlotAttribute::SystemDwInflow,
        PlotAttribute::SystemGwInflow,
        PlotAttribute::SystemLatInflow,
        PlotAttribute::SystemFlooding,
        PlotAttribute::SystemOutflow,
        PlotAttribute::SystemStorage,
        PlotAttribute::SystemEvap,
        PlotAttribute::SystemEvapTotal,
        PlotAttribute::SystemPET,
        PlotAttribute::SystemInfil,
        PlotAttribute::SystemSnowDepth,
        PlotAttribute::SystemTemperature,
    }, u);
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
