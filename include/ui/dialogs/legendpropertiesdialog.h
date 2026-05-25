/*!
 * \file   legendpropertiesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BB Phase 8.6.16 — modeless QPropertyModel-backed editor
 *         for a shared LegendOverlayStyle.
 *
 *         Opened from LegendOverlay's right-click context menu. The dialog
 *         does not own the style — it edits the shared model that the
 *         legend overlay (and, eventually, the LegendDock and any other
 *         legend surface) all subscribe to via Qt signals. Edits are live;
 *         Reset reverts to defaults; Cancel reverts to the snapshot
 *         captured at construction time.
 *
 *         Properties are grouped visually via the model's displayLabelFor()
 *         hook (prefixed labels "General — …", "Frame — …", "Background —
 *         …"). Per-class theming for individual legend items lives on each
 *         layer's IFeatureRenderer and is edited from the context menu
 *         directly — not in this dialog.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_LEGENDPROPERTIESDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LEGENDPROPERTIESDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QPointer>

class QTreeView;

namespace OpenSWMM::Render { class LegendOverlayStyle; }

namespace openswmmvis::ui {

class LegendPropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \brief Construct against a shared style. \p style is NOT owned by
     *  the dialog; closing leaves it intact (any edits already applied
     *  via live preview remain in effect unless the user clicks Cancel). */
    explicit LegendPropertiesDialog(OpenSWMM::Render::LegendOverlayStyle *style,
                                    QWidget *parent = nullptr);
    ~LegendPropertiesDialog() override = default;

protected:
    // Cancel reverts edits to the snapshot taken at construction time.
    void reject() override;

private slots:
    void onResetClicked();

private:
    QPointer<OpenSWMM::Render::LegendOverlayStyle> m_style;
    QJsonObject  m_snapshot;   // captured at construction for Cancel.
    QTreeView   *m_tree = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LEGENDPROPERTIESDIALOG_H
