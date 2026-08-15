/*!
 * \file   pollutanteditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Two-pane CRUD editor for SWMM pollutants ([POLLUTANTS]).
 *
 * Mirrors StreetEditorDialog's role and entry points (createNew /
 * pickPollutant). A pollutant is a flat set of scalar fields, so the layout
 * is a list pane (left) + field form (right); there is no geometry preview.
 *
 *   ┌──────────────┬────────────────────────────────────────┐
 *   │  Pollutants  │  Name                                  │
 *   │  list view   │  Units (combo) / Rain / GW / I&I       │
 *   │  [+ New]     │  Initial conc / Decay / Mol. weight    │
 *   │  [- Delete]  │  Snow only / Co-pollutant / Co-fraction│
 *   └──────────────┴────────────────────────────────────────┘
 *
 * MVC contract — every mutation goes through PollutantProvider; subscribed
 * registry signals keep the list and field form in lock-step. Units are
 * write-once at engine creation (no swmm_pollutant_set_units), so the units
 * combo only takes effect for not-yet-created pollutants.
 *
 * NOTE (first cut): build-verify with the compiler in the loop. See
 * docs/HANDOFF_compile_verify_agent.md.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_POLLUTANTEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_POLLUTANTEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::pollutant {
class PollutantProvider;
class PollutantRegistry;
}

namespace openswmmvis::ui {

class PollutantListModel;

class PollutantEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    PollutantEditorDialog(openswmmvis::pollutant::PollutantRegistry *registry,
                          SWMMModelLayer *layer,
                          QWidget *parent = nullptr);
    ~PollutantEditorDialog() override;

    static PollutantEditorDialog *createNew(
        openswmmvis::pollutant::PollutantRegistry *registry,
        SWMMModelLayer *layer,
        QWidget *parent = nullptr);

    /*! Modal pick / create / edit entry point — mirrors pickStreet. Empty
     *  \p initialName → CreateNew; otherwise → Edit pre-selecting that name.
     *  Returns the bound pollutant name on close. */
    static QString pickPollutant(
        openswmmvis::pollutant::PollutantRegistry *registry,
        SWMMModelLayer *layer,
        const QString  &initialName,
        QWidget        *parent = nullptr);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::pollutant::PollutantProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────
    QListView          *listView() const noexcept { return m_listView; }
    PollutantListModel *listModel() const noexcept { return m_listModel; }
    QLineEdit          *nameEdit()  const noexcept { return m_nameEdit; }

    void invokeNew();

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onProviderRenamed_(openswmmvis::pollutant::PollutantProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::pollutant::PollutantProvider *p);
    void rebuildCoPollutantCombo_(openswmmvis::pollutant::PollutantProvider *current);
    void selectProviderInList_(openswmmvis::pollutant::PollutantProvider *p);
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::pollutant::PollutantRegistry> m_registry;
    QPointer<SWMMModelLayer>                            m_layer;
    QPointer<openswmmvis::pollutant::PollutantProvider> m_current;
    Mode                                                m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    // Left pane.
    QListView          *m_listView  = nullptr;
    PollutantListModel *m_listModel = nullptr;
    QPushButton        *m_addBtn    = nullptr;
    QPushButton        *m_delBtn    = nullptr;

    // Right pane (field form).
    QLineEdit      *m_nameEdit      = nullptr;
    QComboBox      *m_unitsCombo    = nullptr;
    QDoubleSpinBox *m_rainSpin      = nullptr;
    QDoubleSpinBox *m_gwSpin        = nullptr;
    QDoubleSpinBox *m_rdiiSpin      = nullptr;
    QDoubleSpinBox *m_initSpin      = nullptr;
    QDoubleSpinBox *m_decaySpin     = nullptr;
    QDoubleSpinBox *m_mwtSpin       = nullptr;
    QCheckBox      *m_snowOnlyCheck = nullptr;
    QComboBox      *m_coPollCombo   = nullptr;
    QDoubleSpinBox *m_coFracSpin    = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_POLLUTANTEDITORDIALOG_H
