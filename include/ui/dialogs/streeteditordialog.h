/*!
 * \file   streeteditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Three-pane CRUD editor for SWMM street cross-sections ([STREETS]).
 *
 * Mirrors TransectEditorDialog's role and entry points (createNew /
 * pickStreet) but a street is parametric, so the middle pane is a field
 * form (ten legacy TStreet parameters + sides) rather than a station table,
 * and the right pane is a schematic section preview rather than a point
 * chart.
 *
 * Layout (left → right):
 *
 *   ┌──────────────┬───────────────────────────┬────────────────────────┐
 *   │  Streets     │  Name (QLineEdit)         │  Section preview       │
 *   │  list view   │  Road Width / Curb Height │  (gutter + crown +     │
 *   │              │  Cross Slope / Roughness  │   backing schematic)   │
 *   │  [+ New]     │  Gutter Depression / Width│                        │
 *   │  [- Delete]  │  Sides (1 / 2)            │                        │
 *   │              │  Backing W / Slope / n    │                        │
 *   └──────────────┴───────────────────────────┴────────────────────────┘
 *
 * MVC contract — every mutation goes through StreetProvider; subscribed
 * registry signals keep the list, field form, and preview in lock-step.
 *
 * Legacy parity: EPA SWMM-GUI Dstreet.pas (TStreetEditorForm) + the
 * combo/edit-button launch path in Dxsect.pas (TsectBtnClick).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_STREETEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STREETEDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QRadioButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::street {
class StreetProvider;
class StreetRegistry;
}

namespace openswmmvis::ui {

class StreetListModel;

/*! \brief Lightweight schematic preview of a street cross-section. Draws
 *  the road crown, curb, depressed gutter and (optionally) backing from a
 *  StreetProvider's parameters. No Q_OBJECT — plain paintEvent widget. */
class StreetSectionPreview : public QWidget
{
public:
    explicit StreetSectionPreview(QWidget *parent = nullptr);
    void setProvider(openswmmvis::street::StreetProvider *p);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QPointer<openswmmvis::street::StreetProvider> m_provider;
};

class StreetEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    StreetEditorDialog(openswmmvis::street::StreetRegistry *registry,
                        SWMMModelLayer *layer,
                        QWidget *parent = nullptr);
    ~StreetEditorDialog() override;

    static StreetEditorDialog *createNew(
        openswmmvis::street::StreetRegistry *registry,
        SWMMModelLayer *layer,
        QWidget *parent = nullptr);

    /*! \brief Modal pick / create / edit entry point — mirrors
     *  TransectEditorDialog::pickTransect. Empty \p initialName →
     *  CreateNew mode; otherwise → Edit mode pre-selecting that street.
     *  Returns the name of the street bound to the dialog on close, or
     *  empty if none. After close the registry is flushed to the engine. */
    static QString pickStreet(
        openswmmvis::street::StreetRegistry *registry,
        SWMMModelLayer *layer,
        const QString  &initialName,
        QWidget        *parent = nullptr);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::street::StreetProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────
    QListView       *listView()  const noexcept { return m_listView; }
    StreetListModel *listModel()  const noexcept { return m_listModel; }
    QLineEdit       *nameEdit()   const noexcept { return m_nameEdit; }

    void invokeNew();
    void deleteCurrentSilently();
    bool renameCurrent(const QString &newName);

private slots:
    void onListSelectionChanged_();
    void onAddStreetClicked_();
    void onDeleteStreetClicked_();
    void onNameEdited_();
    void onFieldEdited_();
    void onSidesToggled_();
    void onProviderAdded_(openswmmvis::street::StreetProvider *p);
    void onProviderRenamed_(openswmmvis::street::StreetProvider *p,
                              const QString &prev, const QString &now);

private:
    void buildUi_();
    void bindProvider_(openswmmvis::street::StreetProvider *p);
    void selectProviderInList_(openswmmvis::street::StreetProvider *p);
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::street::StreetRegistry> m_registry;
    QPointer<SWMMModelLayer>                       m_layer;
    QPointer<openswmmvis::street::StreetProvider>  m_current;
    Mode                                           m_mode = Mode::Edit;

    QSplitter   *m_splitter = nullptr;

    // Left pane.
    QListView       *m_listView  = nullptr;
    StreetListModel *m_listModel = nullptr;
    QPushButton     *m_addBtn    = nullptr;
    QPushButton     *m_delBtn    = nullptr;

    // Middle pane (field form).
    QLineEdit      *m_nameEdit          = nullptr;
    QDoubleSpinBox *m_crownWidthSpin    = nullptr;
    QDoubleSpinBox *m_curbHeightSpin    = nullptr;
    QDoubleSpinBox *m_crossSlopeSpin    = nullptr;
    QDoubleSpinBox *m_roadRoughSpin     = nullptr;
    QDoubleSpinBox *m_gutterDepSpin     = nullptr;
    QDoubleSpinBox *m_gutterWidthSpin   = nullptr;
    QRadioButton   *m_oneSidedRadio     = nullptr;
    QRadioButton   *m_twoSidedRadio     = nullptr;
    QDoubleSpinBox *m_backWidthSpin     = nullptr;
    QDoubleSpinBox *m_backSlopeSpin     = nullptr;
    QDoubleSpinBox *m_backRoughSpin     = nullptr;

    // Right pane.
    StreetSectionPreview *m_preview = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STREETEDITORDIALOG_H
