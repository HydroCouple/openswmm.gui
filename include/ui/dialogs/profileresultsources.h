/*!
 * \file   profileresultsources.h
 * \brief  Shared result-source discovery for profile plotting dialogs.
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

namespace openswmmvis::ui {

inline QList<SWMMResultsLayer *> profileResultSources(AnimationController  *anim,
                                                      SWMMVisProjectWindow *projectWindow,
                                                      MapCanvas            *canvas = nullptr)
{
    QList<SWMMResultsLayer *> result;
    QSet<SWMMResultsLayer *> seen;

    auto add = [&result, &seen](SWMMResultsLayer *layer) {
        if (!layer || seen.contains(layer)) return;
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
