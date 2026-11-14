/**
 * @file      paramsdraftedit.h
 * @brief     One object's Apply for a data object family whose provider holds
 *            a value-type parameter set (`params()` / `setParams()`).
 *
 * Shared by the draft-based editors (pollutant, aquifer, street, inlet): the
 * write puts the values into the provider and its engine object, reads the
 * engine back, and reports whether the model now holds them. It is replayed
 * by Undo / Redo after the editor may have closed, so it captures only
 * guarded pointers, names and values
 * (workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_UI_EDITORS_PARAMSDRAFTEDIT_H
#define OPENSWMMVIS_UI_EDITORS_PARAMSDRAFTEDIT_H

#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"

#include <QPointer>
#include <QSignalBlocker>

namespace openswmmvis::ui
{

/*! Put \p target into \p live and its engine object, read the engine back
 *  and report whether the model now holds it (\p same). Also restores a
 *  deleted object's settings when Undo brings it back
 *  (dataobjectstructureedit.h). \p Registry needs `engineHandle()`,
 *  `saveToEngine(engine, name)` and `loadFromEngine(engine, name)`; \p Provider
 *  needs `name()`, `params()`, `setParams()` and the `paramsChanged()` signal.
 *  \p same(now, target) says whether what the engine kept is the target
 *  (tolerance, stored form). */
template <class Registry, class Provider, class Params, class Same>
bool writeParams(Registry *registry, Provider *live, const Params &target, const Same &same)
{
    {
        const QSignalBlocker quietProvider(live), quietRegistry(registry);
        live->setParams(target);
        if (void *engine = registry->engineHandle()) {
            registry->saveToEngine(engine, live->name());
            registry->loadFromEngine(engine, live->name()); // what the model holds now
        }
    }
    emit live->paramsChanged(); // other views, and the project's edited mark
    return same(live->params(), target);
}

/*! One object's Apply (see writeParams). The object is the one Apply saw,
 *  or, once that one was deleted and brought back by Undo, the object of the
 *  same name. A write refuses when the registry, the object or (if given)
 *  the layer is gone, or the registry now serves another engine. */
template <class Registry, class Provider, class Layer, class Params, class Same>
DataObjectDraftSession::Edit paramsDraftEdit(Registry *registry, Layer *layer, Provider *live,
                                             Params before, Params after, Same same)
{
    return {[registry = QPointer<Registry>(registry), layer = QPointer<Layer>(layer),
             hadLayer = layer != nullptr, engine = registry ? registry->engineHandle() : nullptr,
             live = QPointer<Provider>(live), name = live ? live->name() : QString(),
             before = std::move(before), after = std::move(after), same](bool forward) {
        if (!registry || (hadLayer && !layer) || registry->engineHandle() != engine)
            return false;
        Provider *p = stepTarget(registry.data(), live, name);
        return p && writeParams(registry.data(), p, forward ? after : before, same);
    }};
}

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_EDITORS_PARAMSDRAFTEDIT_H
