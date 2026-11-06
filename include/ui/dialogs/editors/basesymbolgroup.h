/**
 * @file      basesymbolgroup.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     "Base symbol & labels" — the kind's size / outline / marker /
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_EDITORS_BASESYMBOLGROUP_H
#define OPENSWMMVIS_UI_DIALOGS_EDITORS_BASESYMBOLGROUP_H

#include "layers/swmm_category.h"

#include <QWidget>

class OpenSWMMVisLayer;

namespace openswmmvis::ui
{

struct RendererPanelContext;

/**
 * @brief Resolve the persistent per-kind base-symbol editing object for supported SWMM
 * model/results hosts.
 * @param host Optional borrowed host; must remain live throughout lookup/use.
 * @param category SWMM category; model mapping recognizes eleven spatial kinds.
 * @return Borrowed results sublayer style or model-owned persistent element-symbol
 * adapter, null for unsupported/missing channel. Model accessor may lazily create its
 * adapter. Caller retains host/style lifetime; no ownership transfer or snapshot.
 */
[[nodiscard]] QObject *baseSymbolStyleFor(OpenSWMMVisLayer *host,
                                          OpenSWMMVis::SwmmCategory category);

/**
 * @brief Create an initially collapsed base-symbol editor group for the context's kind/style.
 * @param ctx Borrowed context consumed synchronously; requires a host and category,
 * then resolves baseSymbolStyleFor(). Does not retain the context struct itself.
 * @param parent Optional Qt owner of a successful group; caller owns a parentless result.
 * @return Parent-owned widget or null for absent context/channel/registered editor;
 * cleans partially created widgets if editor creation fails. Group owns its controls
 * and registry-created editor, with the actual style borrowed according to that editor's
 * binding contract. Hides descendant KindRendererPanel copies to avoid nested
 * classification; toggle callback borrows group-owned controls until their destruction.
 * Edits use the persistent style; the surrounding dialog supplies snapshot/Cancel policy,
 * not this factory. No undo or eager style mutation is performed here.
 */
[[nodiscard]] QWidget *createBaseSymbolGroup(const RendererPanelContext &ctx, QWidget *parent);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_EDITORS_BASESYMBOLGROUP_H
