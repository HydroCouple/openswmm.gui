/*!
 * \file   symbologytab.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QGIS-style Symbology tab content (Slice X.2).
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
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYTAB_H
#define OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYTAB_H

#include "ui/dialogs/irendererpanel.h"

#include <QWidget>

class QComboBox;
class QStackedWidget;
class QVBoxLayout;

namespace openswmmvis::ui {

class SymbologyTab : public QWidget
{
    Q_OBJECT
public:
    /*! Construct against the (host, optional category) pair.  The
     *  dropdown is populated from RendererPanelRegistry; switching it
     *  installs the matching renderer class on the host and mounts the
     *  registered panel. */
    explicit SymbologyTab(const RendererPanelContext &ctx,
                          QWidget *parent = nullptr);

private slots:
    void onRendererChanged(int comboIndex);
    /*! Re-read the installed renderer and, when its class differs from the
     *  combo selection, resync the combo + remount the matching panel.
     *  Deferred (queued) from the host layer's rendererChanged so renderer
     *  swaps made outside the combo — the embedded Mode combo in a mounted
     *  editor, undo, style import — keep this tab truthful. */
    void syncToInstalledRenderer();

private:
    void mountPanelForId(const QString &rendererId);
    [[nodiscard]] QString currentRendererId() const;

    RendererPanelContext m_ctx;

    QComboBox      *m_rendererCombo = nullptr;
    QStackedWidget *m_stack         = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SYMBOLOGYTAB_H
