/*!
 * \file   basesymbolgroup.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  "Base symbol & labels" — the kind's size / outline / marker /
 *         label controls, mountable under any renderer panel.
 *
 *         Feedback C2: those controls lived only in the Single Symbol
 *         panel, so switching a kind to Graduated or Categorized hid them
 *         although the painter keeps reading size, outline and labels
 *         from the kind's base symbol (only colour comes from the
 *         classes). The group binds the same editor the Single Symbol
 *         panel uses to the same persistent per-kind style object, so
 *         the dialog's Cancel snapshot covers it and nothing is
 *         duplicated.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_BASESYMBOLGROUP_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_BASESYMBOLGROUP_H

#include "layers/swmm_category.h"

#include <QWidget>

class OpenSWMMVisLayer;

namespace openswmmvis::ui {

struct RendererPanelContext;

/*! The per-kind style object the base-symbol editor binds to: the model
 *  layer's persistent SwmmElementSymbolAdapter, or the results layer's
 *  FeatureSublayerStyle. Null when \p host has no such channel. */
[[nodiscard]] QObject *baseSymbolStyleFor(OpenSWMMVisLayer *host,
                                          OpenSWMMVis::SwmmCategory category);

/*! A collapsible "Base symbol & labels" group (collapsed by default) that
 *  holds the kind's base-symbol editor. Its size / line-width spin boxes
 *  carry the object name "baseSymbolSize" so a Graduated panel can grey
 *  them while "Size by value" is on. Returns nullptr when the context has
 *  no kind or the host has no base-symbol channel. */
[[nodiscard]] QWidget *createBaseSymbolGroup(const RendererPanelContext &ctx,
                                             QWidget *parent);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_BASESYMBOLGROUP_H
