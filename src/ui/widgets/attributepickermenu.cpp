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
                                                QWidget *parent,
                                                const QStringList &speciesNames)
{
    auto *menu = new QMenu(parent);

    switch (kind) {
    case ObjectRef::Kind::Node:
    case ObjectRef::Kind::Link:
    case ObjectRef::Kind::Subcatch:
        addAttrActions(menu, attributesForKind(kind), u);
        // Y2b-2 (amendment D-Y4): the run's species, labelled/united by
        // the descriptor authorities (age in hours). Data carries the
        // NAME as a QString — descriptorFrom() tells it apart from the
        // int-typed fixed attributes.
        if (!speciesNames.isEmpty()) {
            menu->addSeparator();
            for (const QString &sp : speciesNames) {
                if (sp.isEmpty()) continue;
                const auto d = openswmmvis::plot::ResultDescriptor::forSpecies(sp);
                QAction *act = menu->addAction(
                    QStringLiteral("%1 (%2)").arg(d.label(), d.unitLabel(u)));
                act->setData(sp);
            }
        }
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


openswmmvis::plot::ResultDescriptor AttributePickerMenu::descriptorFrom(
    const QAction *action)
{
    using openswmmvis::plot::ResultDescriptor;
    if (!action)
        return ResultDescriptor{};
    const QVariant v = action->data();
    if (v.typeId() == QMetaType::QString)
        return ResultDescriptor::forSpecies(v.toString());
    bool ok = false;
    const int a = v.toInt(&ok);
    if (!ok)
        return ResultDescriptor{};
    // PlotAttribute::Unknown (the "All attributes" sentinel) maps to an
    // invalid descriptor on purpose — same sentinel, richer type.
    return ResultDescriptor::forAttribute(
        static_cast<openswmmvis::plot::PlotAttribute>(a));
}

} // namespace openswmmvis::ui
