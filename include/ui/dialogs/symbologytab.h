/**
 * @file      symbologytab.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QGIS-style Symbology tab content (Slice X.2).
 *
 *         Top row: a renderer-class dropdown populated from
 *         RendererPanelRegistry (Single / Graduated / Categorized / …).
 *         Body: a QStackedWidget whose current page is the renderer panel
 *         matching the dropdown selection.  Switching the dropdown
 *         installs the new renderer class on the host layer and mounts
 *         the matching panel.
 *
 *         Used by LayerStyleDialog's `buildSymbologyTab` for simple layers
 *         (vector / raster / mesh).  For SWMM multi-kind layers the
 *         dialog uses KindTreeSymbologyPanel (X.5) which composes a
 *         SymbologyTab per kind in the editor pane.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYTAB_H
#define OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYTAB_H

#include "ui/dialogs/irendererpanel.h"

#include <QWidget>

class QComboBox;
class QStackedWidget;
class QVBoxLayout;

namespace openswmmvis::ui
{

class SymbologyTab : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Build a renderer-class picker and mount its registered panel for the copied context.
     * @param ctx Borrowed value copied into storage; raw host/rule targets must outlive this
     * tab, queued callbacks and child panels. Empty fields trigger full context resolve,
     * replacing the capability snapshot; otherwise supplied snapshot is retained.
     * @param parent Optional Qt owner; caller owns parentless tab.
     * Copies registry entries, disabling inapplicable rows for UI guidance. Initially uses
     * live renderer id, registered single fallback, then first registry entry; mounting does
     * not itself install that fallback. Subscribes queued renderer changes from supported
     * hosts to avoid destroying an emitting panel mid-signal. No undo/snapshot; GUI thread.
     */
    explicit SymbologyTab(const RendererPanelContext &ctx, QWidget *parent = nullptr);

private slots:
    /**
     * @brief Request the selected renderer class and remount its panel immediately.
     * @param comboIndex Qt notification index, deliberately ignored; reads currentData from
     * the combo instead. No applicability recheck or success verification here.
     * Installation can refuse/no-op while the selected editor still mounts; old widgets
     * are removed then deleteLater, with ownership managed by this tab/scroll wrappers.
     */
    void onRendererChanged(int comboIndex);
    /**
     * @brief Remount when the live renderer's nonempty id differs from the picker selection.
     * Queued host notifications call this after external swaps. A registered combo id is
     * selected under blocker; an unknown id still mounts a fallback panel while the combo
     * can retain its prior selection. Empty/same ids do nothing, including same-class
     * renderer replacement. Raw context targets must remain alive.
     */
    void syncToInstalledRenderer();

private:
    /**
     * @brief Remove existing pages and construct a registry panel inside a vertically scrolling
     * wrapper.
     * @param rendererId Borrowed exact registry id consumed synchronously.
     * Old pages use deleteLater, so their callbacks/borrowed targets can remain live until
     * deferred deletion. Missing entry/factory shows an explanatory label; a factory returning
     * null leaves the stack empty. Successful panel is scroll-owned, refreshed immediately,
     * and must honor its context lifetime contract. No renderer installation or capability
     * revalidation here; factory callbacks may have their own live side effects.
     */
    void mountPanelForId(const QString &rendererId);
    /**
     * @brief Read the selected combo's stored renderer id rather than inspecting installed model
     * state.
     * @return Owned current-data string or empty without combo/selection; may differ from
     * live renderer when installation fails or an unknown external id is encountered.
     */
    [[nodiscard]] QString currentRendererId() const;

    RendererPanelContext m_ctx;

    QComboBox *m_rendererCombo = nullptr;
    QStackedWidget *m_stack = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYTAB_H
