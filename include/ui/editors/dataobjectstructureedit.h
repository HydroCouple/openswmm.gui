/**
 * @file      dataobjectstructureedit.h
 * @brief     Create, delete and rename of one data object as an undoable step
 *            for the draft-based editors.
 *
 * Each helper returns a DataObjectDraftSession::Edit, so the action is written
 * with DataObjectDraftSession::commit: it takes effect at once and becomes one
 * step on the project undo stack (plan D5). The object is found by NAME each
 * time the step runs, so it still works after the object was deleted and
 * brought back by Undo. Undo of a delete brings the object back with its
 * settings, in the engine too; references the engine detached on delete are
 * not restored, and the delete question says so (plan D8,
 * workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md §10).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_UI_EDITORS_DATAOBJECTSTRUCTUREEDIT_H
#define OPENSWMMVIS_UI_EDITORS_DATAOBJECTSTRUCTUREEDIT_H

#include "ui/editors/dataobjectdraftsession.h"

#include <QCoreApplication>
#include <QMap>
#include <QPointer>
#include <QStringList>

#include <openswmm/engine/openswmm_edit.h>

namespace openswmmvis::ui
{

/*! The object a replayed step writes: \p seen while the registry still holds
 *  it (under any name: a rename does not matter), otherwise the object now
 *  named \p name (\p seen was deleted and Undo brought it back as a new
 *  object; a removed object lingers until deleteLater runs). */
template <class Registry, class Provider>
Provider *stepTarget(Registry *registry, const QPointer<Provider> &seen, const QString &name)
{
    if (seen && registry->findByName(seen->name()) == seen.data())
        return seen.data();
    return registry->findByName(name);
}

/*! Create \p name with \p make(registry, name) (the registry's create() when
 *  it needs more than the name, e.g. a curve's type). Forward creates it and
 *  runs \p setup(provider) (initial settings; returns false when the model
 *  refused them); backward deletes it. \p Registry needs engineHandle(),
 *  findByName() and remove(). */
template <class Registry, class Make, class Setup>
DataObjectDraftSession::Edit createObjectEditWith(Registry *registry, QString name, Make make,
                                                  Setup setup)
{
    return {[registry = QPointer<Registry>(registry),
             engine = registry ? registry->engineHandle() : nullptr, name = std::move(name), make,
             setup](bool forward) {
        if (!registry || registry->engineHandle() != engine)
            return false;
        if (!forward) {
            auto *p = registry->findByName(name);
            if (p)
                registry->remove(p);
            return p != nullptr;
        }
        auto *p = make(registry.data(), name);
        return p && setup(p);
    }};
}

/*! createObjectEditWith for a registry whose create() takes only the name. */
template <class Registry, class Setup>
DataObjectDraftSession::Edit createObjectEdit(Registry *registry, QString name, Setup setup)
{
    return createObjectEditWith(
        registry, std::move(name), [](Registry *r, const QString &n) { return r->create(n); },
        std::move(setup));
}

/*! Delete \p name. Backward creates it again with \p make(registry, name)
 *  and runs \p restore(provider), which writes the settings it had back to the
 *  provider and the engine and says whether the model kept them. */
template <class Registry, class Make, class Restore>
DataObjectDraftSession::Edit deleteObjectEditWith(Registry *registry, QString name, Make make,
                                                  Restore restore)
{
    return {[registry = QPointer<Registry>(registry),
             engine = registry ? registry->engineHandle() : nullptr, name = std::move(name), make,
             restore](bool forward) {
        if (!registry || registry->engineHandle() != engine)
            return false;
        if (forward) {
            auto *p = registry->findByName(name);
            if (p)
                registry->remove(p);
            return p != nullptr;
        }
        if (registry->hasName(name))
            return false; // another object took the name meanwhile
        auto *p = make(registry.data(), name);
        return p && restore(p);
    }};
}

/*! deleteObjectEditWith for a registry whose create() takes only the name. */
template <class Registry, class Restore>
DataObjectDraftSession::Edit deleteObjectEdit(Registry *registry, QString name, Restore restore)
{
    return deleteObjectEditWith(
        registry, std::move(name), [](Registry *r, const QString &n) { return r->create(n); },
        std::move(restore));
}

/*! Rename \p from to \p to (backward: back again). */
template <class Registry>
DataObjectDraftSession::Edit renameObjectEdit(Registry *registry, QString from, QString to)
{
    return {[registry = QPointer<Registry>(registry),
             engine = registry ? registry->engineHandle() : nullptr, from = std::move(from),
             to = std::move(to)](bool forward) {
        if (!registry || registry->engineHandle() != engine)
            return false;
        auto *p = registry->findByName(forward ? from : to);
        return p && registry->rename(p, forward ? to : from);
    }};
}

/*! What uses an object, from the engine's impact analysis: \p analyze is the
 *  family's swmm_*_analyze_impact and \p idx the object's engine index.
 *  E.g. "Used by 2 link(s) and 3 external inflow(s)." Empty when nothing uses
 *  it, there is no engine, \p idx < 0 or the analysis failed. */
inline QString dataObjectUses(void *engine, int (*analyze)(SWMM_Engine, int, SWMM_ImpactReport *),
                              int idx)
{
    if (!engine || idx < 0 || !analyze)
        return {};
    SWMM_ImpactReport report{};
    if (analyze(static_cast<SWMM_Engine>(engine), idx, &report) != SWMM_OK)
        return {};
    QMap<int, int> counts; // by SWMM_RefType, in enum order
    for (int i = 0; i < report.n_entries; ++i)
        ++counts[report.entries[i].obj_type];
    swmm_impact_report_free(&report);

    const auto label = [](int type, int n) {
        const char *ctx = "DataObjectUses";
        switch (type) {
        case SWMM_REF_NODE: return QCoreApplication::translate(ctx, "%n node(s)", nullptr, n);
        case SWMM_REF_LINK: return QCoreApplication::translate(ctx, "%n link(s)", nullptr, n);
        case SWMM_REF_SUBCATCH:
            return QCoreApplication::translate(ctx, "%n subcatchment(s)", nullptr, n);
        case SWMM_REF_GAGE: return QCoreApplication::translate(ctx, "%n rain gage(s)", nullptr, n);
        case SWMM_REF_INLET_USAGE:
            return QCoreApplication::translate(ctx, "%n inlet placement(s)", nullptr, n);
        case SWMM_REF_EXT_INFLOW:
            return QCoreApplication::translate(ctx, "%n external inflow(s)", nullptr, n);
        case SWMM_REF_DWF_INFLOW:
            return QCoreApplication::translate(ctx, "%n dry-weather inflow(s)", nullptr, n);
        case SWMM_REF_RDII_ASSIGN:
            return QCoreApplication::translate(ctx, "%n RDII assignment(s)", nullptr, n);
        case SWMM_REF_TREATMENT:
            return QCoreApplication::translate(ctx, "%n treatment expression(s)", nullptr, n);
        case SWMM_REF_LID_USAGE:
            return QCoreApplication::translate(ctx, "%n LID placement(s)", nullptr, n);
        case SWMM_REF_POLLUTANT:
            return QCoreApplication::translate(ctx, "%n pollutant(s)", nullptr, n);
        case SWMM_REF_LANDUSE:
            return QCoreApplication::translate(ctx, "%n land use(s)", nullptr, n);
        case SWMM_REF_CONTROL_RULE:
            return QCoreApplication::translate(ctx, "%n control rule(s)", nullptr, n);
        default: return QString(); // counted under "other"
        }
    };
    QStringList parts;
    int other = 0;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
        const QString text = label(it.key(), it.value());
        if (text.isEmpty())
            other += it.value();
        else
            parts << text;
    }
    if (other > 0)
        parts << QCoreApplication::translate("DataObjectUses", "%n other object(s)", nullptr, other);
    if (parts.isEmpty())
        return {};
    return QCoreApplication::translate("DataObjectUses", "Used by %1.")
        .arg(parts.join(QStringLiteral(", ")));
}

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_EDITORS_DATAOBJECTSTRUCTUREEDIT_H
