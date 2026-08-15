/*!
 * \file   snowpackeditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Two-pane CRUD editor for SWMM snow packs ([SNOWPACKS]).
 *
 * Mirrors AquiferEditorDialog (list pane + scalar field form). The twenty-seven
 * snow-pack parameters are surfaced as labeled spin boxes, indexed by
 * SnowpackProvider::Param and grouped as PLOWABLE / IMPERVIOUS / PERVIOUS /
 * REMOVAL, plus a line edit for the REMOVAL destination subcatchment.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QVector>

class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::snowpack {
class SnowpackProvider;
class SnowpackRegistry;
}

namespace openswmmvis::ui {

class SnowpackListModel;

class SnowpackEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    SnowpackEditorDialog(openswmmvis::snowpack::SnowpackRegistry *registry,
                         SWMMModelLayer *layer,
                         QWidget *parent = nullptr);
    ~SnowpackEditorDialog() override;

    static SnowpackEditorDialog *createNew(
        openswmmvis::snowpack::SnowpackRegistry *registry,
        SWMMModelLayer *layer,
        QWidget *parent = nullptr);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::snowpack::SnowpackProvider *currentProvider() const noexcept;

    QListView         *listView() const noexcept { return m_listView; }
    SnowpackListModel *listModel() const noexcept { return m_listModel; }
    QLineEdit         *nameEdit()  const noexcept { return m_nameEdit; }

    void invokeNew();

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onRemovalSubcatchEdited_();
    void onProviderRenamed_(openswmmvis::snowpack::SnowpackProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::snowpack::SnowpackProvider *p);
    void selectProviderInList_(openswmmvis::snowpack::SnowpackProvider *p);
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::snowpack::SnowpackRegistry> m_registry;
    QPointer<SWMMModelLayer>                          m_layer;
    QPointer<openswmmvis::snowpack::SnowpackProvider> m_current;
    Mode                                              m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView         *m_listView  = nullptr;
    SnowpackListModel *m_listModel = nullptr;
    QPushButton       *m_addBtn    = nullptr;
    QPushButton       *m_delBtn    = nullptr;
    QLineEdit         *m_nameEdit  = nullptr;

    QVector<QDoubleSpinBox*> m_spins;   ///< one per SnowpackProvider::Param
    QLineEdit *m_removalSubcatchEdit = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H
