/*!
 * \file   snowpackeditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  CRUD editor for SWMM snow packs ([SNOWPACKS]).
 *
 * Identity-only: the current engine exposes no per-parameter accessors for
 * snow packs, so this dialog supports create / rename / delete and shows an
 * informational note. Melt-coefficient fields can be added once the engine
 * surfaces them. Mirrors LandUseEditorDialog structure.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

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

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H
