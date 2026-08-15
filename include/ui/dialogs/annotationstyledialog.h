/*!
 * \file   annotationstyledialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QPropertyModel-backed style editor dialog for a single
 *         AnnotationTextItem.
 *
 * Mirrors the SeriesStyleEditor pattern: a QTreeView fed by a QPropertyModel
 * wrapping the live AnnotationTextItem. Edits route through the data model's
 * setters, which emit `changed()`, which the annotation layer wires to a
 * single graphics-item update — so the map shows the new style on every
 * edit (live preview).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_ANNOTATIONSTYLEDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_ANNOTATIONSTYLEDIALOG_H

#include <QDialog>

class AnnotationTextItem;
class QDialogButtonBox;
class QPropertyModel;
class QTreeView;
class QLineEdit;

class AnnotationStyleDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \param item   The annotation being edited. Must outlive the dialog. */
    explicit AnnotationStyleDialog(AnnotationTextItem *item,
                                   QWidget *parent = nullptr);
    ~AnnotationStyleDialog() override;

    /*! When true, the dialog title reads "Edit Text Annotation" and the OK
     *  button defaults to "Apply". When false (placement mode), the title
     *  reads "Add Text Annotation" and OK reads "Add". */
    void setEditMode(bool edit);

private:
    void buildUi();

    AnnotationTextItem *m_item   = nullptr;
    QLineEdit          *m_text   = nullptr;   ///< Quick-access editor for the most-edited field.
    QTreeView          *m_tree   = nullptr;
    QPropertyModel     *m_model  = nullptr;
    QDialogButtonBox   *m_btns   = nullptr;
};

#endif // OPENSWMMVIS_UI_DIALOGS_ANNOTATIONSTYLEDIALOG_H
