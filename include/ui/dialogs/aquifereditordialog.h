/*!
 * \file   aquifereditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Two-pane CRUD editor for SWMM aquifers ([AQUIFERS]).
 *
 * Mirrors PollutantEditorDialog (list pane + scalar field form). The twelve
 * aquifer parameters are surfaced as a labeled spin-box form, indexed by
 * AquiferProvider::Param. The upper-evaporation pattern (a Pattern reference)
 * is NOT yet edited here — see docs/HANDOFF_compile_verify_agent.md.
 *
 * NOTE (first cut): build-verify with the compiler in the loop.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QVector>

class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::aquifer {
class AquiferProvider;
class AquiferRegistry;
}

namespace openswmmvis::ui {

class AquiferListModel;

class AquiferEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    AquiferEditorDialog(openswmmvis::aquifer::AquiferRegistry *registry,
                        SWMMModelLayer *layer,
                        QWidget *parent = nullptr);
    ~AquiferEditorDialog() override;

    static AquiferEditorDialog *createNew(
        openswmmvis::aquifer::AquiferRegistry *registry,
        SWMMModelLayer *layer,
        QWidget *parent = nullptr);

    static QString pickAquifer(
        openswmmvis::aquifer::AquiferRegistry *registry,
        SWMMModelLayer *layer,
        const QString  &initialName,
        QWidget        *parent = nullptr);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::aquifer::AquiferProvider *currentProvider() const noexcept;

    QListView        *listView() const noexcept { return m_listView; }
    AquiferListModel *listModel() const noexcept { return m_listModel; }
    QLineEdit        *nameEdit()  const noexcept { return m_nameEdit; }

    void invokeNew();

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onProviderRenamed_(openswmmvis::aquifer::AquiferProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::aquifer::AquiferProvider *p);
    void selectProviderInList_(openswmmvis::aquifer::AquiferProvider *p);
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::aquifer::AquiferRegistry> m_registry;
    QPointer<SWMMModelLayer>                        m_layer;
    QPointer<openswmmvis::aquifer::AquiferProvider> m_current;
    Mode                                            m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView        *m_listView  = nullptr;
    AquiferListModel *m_listModel = nullptr;
    QPushButton      *m_addBtn    = nullptr;
    QPushButton      *m_delBtn    = nullptr;

    QLineEdit               *m_nameEdit = nullptr;
    QVector<QDoubleSpinBox*> m_spins;   ///< one per AquiferProvider::Param

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H
