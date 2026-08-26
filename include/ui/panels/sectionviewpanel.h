/*!
 * \file   sectionviewpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Dockable section / profile view of the current selection.
 *
 * Slices SP.4 / SP.5 (workplans/SECTION_PREVIEW_WORKPLAN.md).
 *
 * A standalone dock rather than a pane inside the Properties panel: the
 * drawing wants far more room than a property grid can spare, users want it
 * docked where it suits them (or floating on a second monitor), and keeping it
 * separate leaves PropertiesPanel untouched.
 *
 * Follows the active selection through the same `SelectionManager` dispatch
 * that drives the property browser, and repaints on the model layer's
 * `attributeChanged` so an edit made anywhere — property grid, attribute
 * table, cross-section dialog — is reflected immediately.
 */

#ifndef OPENSWMMVIS_UI_PANELS_SECTIONVIEWPANEL_H
#define OPENSWMMVIS_UI_PANELS_SECTIONVIEWPANEL_H

#include <QDockWidget>
#include <QPointer>
#include <QString>

class QComboBox;
class QLabel;
class QToolButton;
class SWMMModelLayer;

namespace openswmmvis::sectionview { class SectionPreviewWidget; }

namespace openswmmvis::ui {

/*!
 * \class SectionViewPanel
 * \brief Right-dock vector section / profile view for the selected object.
 */
class SectionViewPanel : public QDockWidget
{
    Q_OBJECT

public:
    /*! Which drawing is shown for a link. Nodes always draw a profile. */
    enum class Mode { Section, Profile };

    explicit SectionViewPanel(QWidget *parent = nullptr);
    ~SectionViewPanel() override;

    /*! Bind to the active project's model layer (nullptr to detach).
     *  Idempotent; re-wires the attributeChanged connection safely. */
    void setProject(SWMMModelLayer *layer);

    /*! Show the object identified by \p objectType (a
     *  `SWMMObjectRef::ObjectType`, passed as int to keep the header free of
     *  the selection include) and \p name. Non-drawable kinds clear the view
     *  with an explanatory message. */
    void showObject(int objectType, const QString &name);

    /*! Clear to the "nothing selected" state. */
    void clearSelection();

    [[nodiscard]] Mode mode() const noexcept { return m_mode; }
    void setMode(Mode mode);

    /*! Scale (V:H) applied to LINK drawings — section and profile alike:
     *  0 = automatic (fill the pane, ratio stated on the drawing), >0 = an
     *  explicit V:H ratio, 1.0 being true shape / true scale. Node drawings
     *  always fill the pane and ignore this (SVX: fill-canvas default with
     *  a link-only scale override). */
    [[nodiscard]] double verticalExaggeration() const noexcept
    { return m_verticalExaggeration; }
    void setVerticalExaggeration(double ve);

public slots:
    /*! Rebuild the current drawing from the engine. Cheap enough to call on
     *  every edit: one engine read plus one section sampling. */
    void refresh();

    /*! Refresh only when \p name is the object on display. Wired to
     *  `SWMMModelLayer::attributeChanged`. */
    void onObjectEditedExternally(const QString &name);

private:
    void buildUi();
    void updateModeButtons();

    QPointer<SWMMModelLayer> m_layer;

    sectionview::SectionPreviewWidget *m_preview     = nullptr;
    QToolButton                       *m_sectionBtn  = nullptr;
    QToolButton                       *m_profileBtn  = nullptr;
    QLabel                            *m_veLabel     = nullptr;
    QComboBox                         *m_veCombo     = nullptr;

    /*! 0 = automatic. Persisted across selections so a user who works at 1:1
     *  is not put back on the exaggerated view by every click. */
    double m_verticalExaggeration = 0.0;

    /*! `SWMMObjectRef::ObjectType` of what is displayed; 0 = Unknown. */
    int     m_objectType = 0;
    QString m_objectName;
    Mode    m_mode = Mode::Section;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_SECTIONVIEWPANEL_H
