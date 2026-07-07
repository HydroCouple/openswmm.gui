/*!
 * \file   landuseeditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Two-pane CRUD editor for SWMM land uses ([LANDUSES]).
 *
 * Mirrors PollutantEditorDialog (list pane + scalar field form). Scalars are
 * the street-sweeping interval (days) and removal fraction. Buildup and
 * washoff functions are per-(landuse × pollutant) sub-tables and are NOT yet
 * edited here (see docs/HANDOFF_compile_verify_agent.md — add a
 * Buildup/Washoff grid page next).
 *
 * NOTE (first cut): build-verify with the compiler in the loop.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_LANDUSEEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LANDUSEEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::landuse {
class LandUseProvider;
class LandUseRegistry;
}

namespace openswmmvis::ui {

class LandUseListModel;

class LandUseEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    LandUseEditorDialog(openswmmvis::landuse::LandUseRegistry *registry,
                        SWMMModelLayer *layer,
                        QWidget *parent = nullptr);
    ~LandUseEditorDialog() override;

    static LandUseEditorDialog *createNew(
        openswmmvis::landuse::LandUseRegistry *registry,
        SWMMModelLayer *layer,
        QWidget *parent = nullptr);

    static QString pickLandUse(
        openswmmvis::landuse::LandUseRegistry *registry,
        SWMMModelLayer *layer,
        const QString  &initialName,
        QWidget        *parent = nullptr);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::landuse::LandUseProvider *currentProvider() const noexcept;

    QListView         *listView() const noexcept { return m_listView; }
    LandUseListModel  *listModel() const noexcept { return m_listModel; }
    QLineEdit         *nameEdit()  const noexcept { return m_nameEdit; }

    void invokeNew();

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onProviderRenamed_(openswmmvis::landuse::LandUseProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::landuse::LandUseProvider *p);
    void selectProviderInList_(openswmmvis::landuse::LandUseProvider *p);
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::landuse::LandUseRegistry> m_registry;
    QPointer<SWMMModelLayer>                        m_layer;
    QPointer<openswmmvis::landuse::LandUseProvider> m_current;
    Mode                                            m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView        *m_listView  = nullptr;
    LandUseListModel *m_listModel = nullptr;
    QPushButton      *m_addBtn    = nullptr;
    QPushButton      *m_delBtn    = nullptr;

    QLineEdit      *m_nameEdit      = nullptr;
    QDoubleSpinBox *m_intervalSpin  = nullptr;
    QDoubleSpinBox *m_removalSpin   = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LANDUSEEDITORDIALOG_H
