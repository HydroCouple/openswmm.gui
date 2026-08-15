/*!
 * \file   lidcontroleditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  CRUD editor for SWMM LID controls ([LID_CONTROLS]).
 *
 * List pane + right pane with a type combobox and a QTabWidget of the four
 * LID layers (Surface / Soil / Storage / Drain), matching the engine's
 * swmm_lid_set_surface / _soil / _storage / _drain setters.
 *
 * Engine limitation: no LID getters, so existing layer values cannot be
 * pre-loaded; the registry only writes new or user-edited controls.
 *
 * NOTE (first cut): build-verify with the compiler in the loop.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_LIDCONTROLEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LIDCONTROLEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QTabWidget;

class SWMMModelLayer;

namespace openswmmvis::lid {
class LidControlProvider;
class LidControlRegistry;
}

namespace openswmmvis::sectionview { class SectionPreviewWidget; }

namespace openswmmvis::ui {

class LidControlListModel;

class LidControlEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    LidControlEditorDialog(openswmmvis::lid::LidControlRegistry *registry,
                           SWMMModelLayer *layer,
                           QWidget *parent = nullptr);
    ~LidControlEditorDialog() override;

    static LidControlEditorDialog *createNew(
        openswmmvis::lid::LidControlRegistry *registry,
        SWMMModelLayer *layer,
        QWidget *parent = nullptr);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::lid::LidControlProvider *currentProvider() const noexcept;

    QListView          *listView() const noexcept { return m_listView; }
    LidControlListModel *listModel() const noexcept { return m_listModel; }
    QLineEdit          *nameEdit()  const noexcept { return m_nameEdit; }

    void invokeNew();

private slots:
    void onListSelectionChanged_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onProviderRenamed_(openswmmvis::lid::LidControlProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::lid::LidControlProvider *p);

    /*! Slice SP.6 — rebuild the layer-stack diagram from the CURRENT widget
     *  values and the active tab. Called on every field edit, type change and
     *  tab switch, so the drawing always matches the form. */
    void refreshLayerDiagram_();
    void selectProviderInList_(openswmmvis::lid::LidControlProvider *p);
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::lid::LidControlRegistry> m_registry;
    QPointer<SWMMModelLayer>                       m_layer;
    QPointer<openswmmvis::lid::LidControlProvider> m_current;
    Mode                                           m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    /*! Slice SP.6 — right-hand layer-stack drawing: which layers this LID type
     *  has, their relative thicknesses, and the active tab highlighted. */
    openswmmvis::sectionview::SectionPreviewWidget *m_diagram = nullptr;
    QTabWidget                                     *m_tabs    = nullptr;

    QListView           *m_listView  = nullptr;
    LidControlListModel *m_listModel = nullptr;
    QPushButton         *m_addBtn    = nullptr;
    QPushButton         *m_delBtn    = nullptr;

    QLineEdit *m_nameEdit  = nullptr;
    QComboBox *m_typeCombo = nullptr;

    // Surface.
    QDoubleSpinBox *m_surfStorage = nullptr, *m_surfRough = nullptr, *m_surfSlope = nullptr;
    // Soil.
    QDoubleSpinBox *m_soilThick = nullptr, *m_soilPoro = nullptr, *m_soilFc = nullptr,
                   *m_soilWp = nullptr, *m_soilKsat = nullptr, *m_soilKslope = nullptr;
    // Storage.
    QDoubleSpinBox *m_storThick = nullptr, *m_storVoid = nullptr, *m_storKsat = nullptr;
    // Drain.
    QDoubleSpinBox *m_drainCoeff = nullptr, *m_drainExpon = nullptr, *m_drainOffset = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LIDCONTROLEDITORDIALOG_H
