/*!
 * \file   aquifereditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Three-pane CRUD editor for SWMM aquifers ([AQUIFERS]).
 *
 * Phase G2 (workplans/AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN_2026-09-05.md).
 *
 * Mirrors LidControlEditorDialog: list pane (add / delete), a grouped form
 * over the twelve AquiferProvider::Param values plus the upper-zone
 * evaporation pattern, and a SectionPreviewWidget showing the two-zone
 * groundwater illustration (buildAquiferDiagram) whose callouts track the
 * form and emphasise the focused field. Soft validation (plan D6) is shown in
 * a status label and forwarded to the diagram; the engine bounds stay the
 * hard limits via the spin-box ranges.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::aquifer {
class AquiferProvider;
class AquiferRegistry;
}

namespace openswmmvis::sectionview { class SectionPreviewWidget; }

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

    // ── Test hooks ──────────────────────────────────────────────────────────
    QListView        *listView() const noexcept { return m_listView; }
    AquiferListModel *listModel() const noexcept { return m_listModel; }
    QLineEdit        *nameEdit()  const noexcept { return m_nameEdit; }
    /*! Spin box for AquiferProvider::Param \p param, or nullptr. */
    QDoubleSpinBox   *spinBox(int param) const noexcept;
    QComboBox        *evapPatternCombo() const noexcept { return m_patternCombo; }
    QLabel           *warningLabel() const noexcept { return m_statusLabel; }
    openswmmvis::sectionview::SectionPreviewWidget *diagram() const noexcept
    { return m_diagram; }
    /*! Plan D6 soft-validation texts for the values currently in the form. */
    QStringList validationWarnings() const;

    void invokeNew();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onPatternPickClicked_();
    void onProviderRenamed_(openswmmvis::aquifer::AquiferProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::aquifer::AquiferProvider *p);
    void selectProviderInList_(openswmmvis::aquifer::AquiferProvider *p);
    QString suggestUniqueName_() const;
    void populatePatternCombo_(const QString &select);
    void refreshDiagram_();
    int  activeParam_() const;

    QPointer<openswmmvis::aquifer::AquiferRegistry> m_registry;
    QPointer<SWMMModelLayer>                        m_layer;
    QPointer<openswmmvis::aquifer::AquiferProvider> m_current;
    Mode                                            m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView        *m_listView  = nullptr;
    AquiferListModel *m_listModel = nullptr;
    QPushButton      *m_addBtn    = nullptr;
    QPushButton      *m_delBtn    = nullptr;

    QLineEdit               *m_nameEdit     = nullptr;
    QVector<QDoubleSpinBox*> m_spins;   ///< one per AquiferProvider::Param
    QComboBox               *m_patternCombo = nullptr;
    QPushButton             *m_patternBtn   = nullptr;
    QLabel                  *m_statusLabel  = nullptr;

    openswmmvis::sectionview::SectionPreviewWidget *m_diagram = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H
