/*!
 * \file   classificationschemecelleditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice D1-a (MESH_SUBLAYER_CLASSIFICATION_UI_PLAN) — property-grid
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
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONSCHEMECELLEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONSCHEMECELLEDITOR_H

#include "render/classificationscheme.h"

#include <QWidget>

class QLabel;
class QPushButton;

namespace openswmmvis::ui {

class ClassificationSchemeCellEditor : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(OpenSWMM::Render::ClassificationScheme classificationScheme
                   READ scheme WRITE setScheme NOTIFY schemeChanged USER true)
public:
    /*! \param parent  QStandardItemEditorCreator requires the single-QWidget*
     *                 constructor; the delegate parents the editor to the view
     *                 viewport. */
    explicit ClassificationSchemeCellEditor(QWidget *parent = nullptr);

    [[nodiscard]] const OpenSWMM::Render::ClassificationScheme &scheme() const { return m_scheme; }
    void setScheme(const OpenSWMM::Render::ClassificationScheme &s);

signals:
    /*! Emitted after the popup dialog is accepted with a changed scheme — the
     *  delegate commits the USER property to the model on this. */
    void schemeChanged();

private:
    void openDialog();
    void updateSummary();

    OpenSWMM::Render::ClassificationScheme m_scheme;
    QLabel      *m_summary = nullptr;
    QPushButton *m_editBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CLASSIFICATIONSCHEMECELLEDITOR_H
