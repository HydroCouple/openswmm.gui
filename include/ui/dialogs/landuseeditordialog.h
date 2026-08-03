/*!
 * \file   landuseeditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Unified editor for SWMM land uses ([LANDUSES] + [BUILDUP] +
 *         [WASHOFF]) — iteration 4.
 *
 * List pane + tabbed detail pane for the selected land use:
 *   - General && Sweeping: name, sweep interval (days), removal fraction.
 *   - Buildup:  per-pollutant [BUILDUP] table (BuildupTableModel).
 *   - Washoff:  per-pollutant [WASHOFF] table incl. the per-pollutant
 *     sweep/BMP efficiencies (WashoffTableModel).
 * Rows in the two tables re-dimension automatically as pollutants are
 * added/removed (refreshPollutants(), wired to the pollutant registry).
 * Delete is impact-aware: the confirmation lists what cascades
 * (LandUseRegistry::impactSummary) before swmm_landuse_delete runs.
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
class QTableView;
class QTabWidget;

class SWMMModelLayer;

namespace openswmmvis::landuse {
class LandUseProvider;
class LandUseRegistry;
}

namespace openswmmvis::pollutant {
class PollutantRegistry;
}

namespace openswmmvis::ui {

class BuildupTableModel;
class LandUseListModel;
class WashoffTableModel;

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
    BuildupTableModel *buildupModel() const noexcept { return m_buildupModel; }
    WashoffTableModel *washoffModel() const noexcept { return m_washoffModel; }

    void invokeNew();

    /*! Follow \a registry so the Buildup/Washoff rows re-dimension live as
     *  pollutants are added/removed/renamed (wired by the launch site). */
    void trackPollutantRegistry(
        openswmmvis::pollutant::PollutantRegistry *registry);

public slots:
    /*! Re-dimension the Buildup/Washoff rows after the pollutant set
     *  changed (add/remove/rename). */
    void refreshPollutants();

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

    QTabWidget        *m_tabs         = nullptr;
    QTableView        *m_buildupView  = nullptr;
    QTableView        *m_washoffView  = nullptr;
    BuildupTableModel *m_buildupModel = nullptr;
    WashoffTableModel *m_washoffModel = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LANDUSEEDITORDIALOG_H
