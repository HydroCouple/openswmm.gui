/*!
 * \file   sublayerstyledialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice S3 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md §4.2) — modeless
 *         QPropertyModel-backed editor for a single ISublayer's style bag.
 *
 *         Opened from the layer-tree context menu (right-click on a
 *         sublayer row → "Edit Style…") and, when wired in S4, from the
 *         legend dock right-click on a sublayer-tagged swatch. Same
 *         Cancel-rollback pattern as LegendPropertiesDialog: snapshot
 *         the style's JSON at ctor; restore on reject().
 *
 *         The dialog does NOT own the sublayer or its style — both are
 *         owned by the parent layer via QObject parent-child. When the
 *         sublayer or its style is destroyed (typically because the
 *         layer was removed) the dialog auto-closes via QPointer +
 *         destroyed-signal wiring.
 *
 *         The QPropertyModel walks the style bag's Q_PROPERTY metadata
 *         and produces editors via the project's QPropertyItemDelegate
 *         (color picker, spin box, combo for Q_ENUM, etc.) — no
 *         per-style-bag UI code is needed.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SUBLAYERSTYLEDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_SUBLAYERSTYLEDIALOG_H

#include <QDialog>
#include <QJsonObject>
#include <QPointer>

class QTreeView;

namespace OpenSWMM::Render { class ISublayer; }

namespace openswmmvis::ui {

class SublayerStyleDialog : public QDialog
{
    Q_OBJECT
public:
    /*! Construct against a non-owning sublayer pointer. Closing leaves
     *  the sublayer / its style intact; Cancel reverts edits to the
     *  snapshot captured at construction time. */
    explicit SublayerStyleDialog(OpenSWMM::Render::ISublayer *sublayer,
                                 QWidget *parent = nullptr);
    ~SublayerStyleDialog() override = default;

protected:
    void reject() override;

private:
    QPointer<OpenSWMM::Render::ISublayer> m_sublayer;
    QJsonObject  m_snapshot;
    // P4 — tabbed view; m_tree is kept as a back-compat fallback when the
    // style bag has no Q_CLASSINFO groups (degenerate case → a single tab).
    QTreeView   *m_tree = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SUBLAYERSTYLEDIALOG_H
