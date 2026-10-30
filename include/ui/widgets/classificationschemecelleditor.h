/**
 * @file      classificationschemecelleditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice D1-a (MESH_SUBLAYER_CLASSIFICATION_UI_PLAN) — property-grid
 *         cell editor for a ClassificationScheme-typed Q_PROPERTY.
 *
 *         A style bag that exposes
 *         Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classification …)
 *         gets a grid row whose editor is a read-only summary label plus an
 *         "Edit…" button that opens the shared ClassificationEditor in a
 *         modal dialog. Registered on the QPropertyItemDelegate via
 *         makeStyleDelegate() so any future scheme property reuses it for
 *         free — no per-layer UI code.
 *
 *         The USER Q_PROPERTY (classificationScheme) is how
 *         QPropertyItemDelegate reads/writes the value through
 *         QStandardItemEditorCreator; schemeChanged() fires on every accepted
 *         edit so commitData round-trips the new scheme back to the model.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONSCHEMECELLEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONSCHEMECELLEDITOR_H

#include "render/classificationscheme.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace openswmmvis::ui
{

class ClassificationSchemeCellEditor : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classificationScheme READ scheme WRITE
                   setScheme NOTIFY schemeChanged USER true)
public:
    /*! \param parent  QStandardItemEditorCreator requires the single-QWidget*
     *                 constructor; the delegate parents the editor to the view
     *                 viewport. */
    explicit ClassificationSchemeCellEditor(QWidget *parent = nullptr);

    /**
     * @brief Access the editor-held scheme value used by the USER property.
     * @return Borrowed reference valid while the editor survives; popup edits can mutate it before acceptance.
     */
    [[nodiscard]] const OpenSWMM::Render::ClassificationScheme &scheme() const { return m_scheme; }
    /**
     * @brief Replace the held scheme and summary without emitting schemeChanged().
     * @param[in] s Value copied into this editor; no validation occurs here.
     */
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s);

signals:
    /*! Emitted after the popup dialog is accepted — the
     *  delegate commits the USER property to the model on this. */
    void schemeChanged();

private:
    /**
     * @brief Open a modal shared classification editor over the held scheme, rolling live edits back on Cancel.
     * @note Uses no sample provider and the scheme's own range. Accept always emits schemeChanged(), even for unchanged content; this method does not independently gate acceptance on hasValidDraft().
     */
    void openDialog();
    /**
     * @brief Refresh the read-only method/class-count/ramp text, using custom for an unnamed ramp.
     */
    void updateSummary();

    OpenSWMM::Render::ClassificationScheme m_scheme;
    QLabel *m_summary = nullptr;
    QPushButton *m_editBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONSCHEMECELLEDITOR_H
