/*!
 * \file   attributepickermenu.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/attributepickermenu.h"

#include <QAction>
#include <QHash>
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

    // 2D mesh kinds: fixed list, no species. Callers grey out entries the
    // layer can't serve (IRunLayer::supportsAttribute) after construction.
    case ObjectRef::Kind::Mesh2DCell:
    case ObjectRef::Kind::Mesh2DEdge:
    case ObjectRef::Kind::Mesh2DVertex:
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

QVector<ResultDescriptor> AttributePickerMenu::execForMeshKind(
    ObjectRef::Kind kind,
    const QPoint &globalPos,
    const IRunLayer *availability,
    const QString &unavailableTip)
{
    // The 2D mesh stores SI internally (Mesh2DRunLayer::unitSystem).
    QMenu *menu = createForObjectKind(kind, UnitSystem::SI);
    if (!menu) return {};

    QHash<QAction *, ResultDescriptor> picks;
    QAction *allAction = nullptr;
    for (QAction *act : menu->actions()) {
        if (act->isSeparator()) continue;
        const PlotAttribute a = attributeFrom(act);
        if (a == PlotAttribute::Unknown) { allAction = act; continue; }   // "All"
        const bool ok = !availability || availability->supportsAttribute(a);
        act->setEnabled(ok);
        if (ok) picks.insert(act, ResultDescriptor::forAttribute(a));
        else if (!unavailableTip.isEmpty()) act->setToolTip(unavailableTip);
    }

    // The source's own catalog: groundwater terms, infiltration, 2D species.
    if (availability) {
        QVector<ResultDescriptor> surface, groundwater;
        for (const ResultDescriptor &d : availability->resultDescriptorsForKind(kind)) {
            if (!d.isMeshVariable()) continue;
            (d.variableKey.startsWith(QStringLiteral("groundwater:")) ? groundwater : surface).append(d);
        }
        const auto addSection = [&](const QString &title, const QVector<ResultDescriptor> &list) {
            if (list.isEmpty()) return;
            QAction *header = menu->insertSection(allAction, title);
            Q_UNUSED(header);
            for (const ResultDescriptor &d : list) {
                const QString units = d.unitLabel(UnitSystem::SI);
                auto *act = new QAction(units.isEmpty() ? d.label()
                                                        : QStringLiteral("%1 (%2)").arg(d.label(), units),
                                        menu);
                menu->insertAction(allAction, act);
                picks.insert(act, d);
            }
        };
        addSection(QObject::tr("Surface results"), surface);
        addSection(QObject::tr("Groundwater results"), groundwater);
        if (allAction && (!surface.isEmpty() || !groundwater.isEmpty()))
            menu->insertSeparator(allAction);
    }
    menu->setToolTipsVisible(true);

    QVector<ResultDescriptor> chosen;
    if (QAction *picked = menu->exec(globalPos)) {
        if (picked == allAction) {
            // Menu order, so "All" plots read top to bottom like the menu.
            for (QAction *act : menu->actions())
                if (picks.contains(act)) chosen.push_back(picks.value(act));
        } else if (picks.contains(picked)) {
            chosen.push_back(picks.value(picked));
        }
    }
    delete menu;
    return chosen;
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
