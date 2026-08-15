/*!
 * \file   inleteditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Two-pane CRUD editor for SWMM inlet designs ([INLETS]).
 *
 * Mirrors PollutantEditorDialog. The inlet type (GRATE/CURB/SLOTTED/CUSTOM) is
 * a combobox; the remaining fields are the swmm_inlet_set_params arguments.
 *
 * Engine limitation: there are no inlet getters, so existing parameter values
 * cannot be pre-loaded — the form shows defaults for loaded inlets, and the
 * registry only writes inlets the user actually edits (see InletRegistry).
 *
 * NOTE (first cut): build-verify with the compiler in the loop.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::inlet {
class InletProvider;
class InletRegistry;
}

namespace openswmmvis::ui {

class InletListModel;

class InletEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    InletEditorDialog(openswmmvis::inlet::InletRegistry *registry,
                      SWMMModelLayer *layer,
                      QWidget *parent = nullptr);
    ~InletEditorDialog() override;

    static InletEditorDialog *createNew(
        openswmmvis::inlet::InletRegistry *registry,
        SWMMModelLayer *layer,
        QWidget *parent = nullptr);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::inlet::InletProvider *currentProvider() const noexcept;

    QListView      *listView() const noexcept { return m_listView; }
    InletListModel *listModel() const noexcept { return m_listModel; }
    QLineEdit      *nameEdit()  const noexcept { return m_nameEdit; }

    void invokeNew();

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onProviderRenamed_(openswmmvis::inlet::InletProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::inlet::InletProvider *p);
    void selectProviderInList_(openswmmvis::inlet::InletProvider *p);
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::inlet::InletRegistry> m_registry;
    QPointer<SWMMModelLayer>                    m_layer;
    QPointer<openswmmvis::inlet::InletProvider> m_current;
    Mode                                        m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView      *m_listView  = nullptr;
    InletListModel *m_listModel = nullptr;
    QPushButton    *m_addBtn    = nullptr;
    QPushButton    *m_delBtn    = nullptr;

    QLineEdit      *m_nameEdit    = nullptr;
    QComboBox      *m_typeCombo   = nullptr;
    QDoubleSpinBox *m_lengthSpin  = nullptr;
    QDoubleSpinBox *m_widthSpin   = nullptr;
    QLineEdit      *m_grateEdit   = nullptr;
    QDoubleSpinBox *m_openAreaSpin = nullptr;
    QDoubleSpinBox *m_splashSpin  = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H
