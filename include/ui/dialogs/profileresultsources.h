/**
 * @file      profileresultsources.h
 * @brief     Shared result-source discovery for profile plotting dialogs.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef PROFILE_RESULT_SOURCES_H
#define PROFILE_RESULT_SOURCES_H

#include "animation/animationcontroller.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmresultslayer.h"
#include "map/mapcanvas.h"
#include "swmmvisprojectwindow.h"

#include <QList>
#include <QSet>

namespace openswmmvis::ui
{

/**
 * @param[in] anim Optional borrowed controller; append allLayers in its supplied order first.
 * @param[in] projectWindow Optional borrowed project; append activeResultsLayer next, use its
 *   canvas only if explicit canvas absent.
 * @param[in] canvas Optional borrowed explicit canvas to scan last; replaces project canvas
 *   fallback, not controller/active-source contributions.
 * @return Owned list of borrowed distinct nonnull SWMMResultsLayer pointers, deduplicated by
 *   pointer in first occurrence order. Includes unchecked/invisible/unready/duplicate-path layers;
 *   no eligibility/file compatibility check or lifetime pinning.
 * @note Synchronous GUI-thread QObject/canvas reads only, no source creation, selection change,
 *   style write or registration. Caller keeps all owners alive while iterating.
 */
inline QList<SWMMResultsLayer *> profileResultSources(AnimationController *anim,
                                                      SWMMVisProjectWindow *projectWindow,
                                                      MapCanvas *canvas = nullptr)
{
    QList<SWMMResultsLayer *> result;
    QSet<SWMMResultsLayer *> seen;

    auto add = [&result, &seen](SWMMResultsLayer *layer) {
        if (!layer || seen.contains(layer))
            return;
        seen.insert(layer);
        result.append(layer);
    };

    if (anim) {
        for (SWMMResultsLayer *layer : anim->allLayers())
            add(layer);
    }

    if (projectWindow)
        add(projectWindow->activeResultsLayer());

    MapCanvas *sourceCanvas = canvas;
    if (!sourceCanvas && projectWindow)
        sourceCanvas = projectWindow->canvas();

    if (sourceCanvas) {
        for (OpenSWMMVisLayer *layer : sourceCanvas->layers())
            add(qobject_cast<SWMMResultsLayer *>(layer));
    }

    return result;
}

} // namespace openswmmvis::ui

#endif // PROFILE_RESULT_SOURCES_H
