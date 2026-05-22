/*!
 * \file   profileoptionsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Display Options dialog for the profile plot — a tabbed dialog with
 *         a QPropertyModel-backed tree view that edits a `ProfilePlotOptions`
 *         instance (Display tab) plus a Sources tab that lets the user
 *         toggle, recolour, rename and add comparison-output sources.
 */

#ifndef PROFILE_OPTIONS_DIALOG_H
#define PROFILE_OPTIONS_DIALOG_H

#include <QDialog>
#include <QPointer>

class AnimationController;
class ProfilePlotOptions;
class ProfileSourceStyleAdapter;
class SWMMVisProjectWindow;
class QAbstractItemDelegate;
class QAbstractItemModel;
class QStandardItemModel;
class QTabWidget;
class QTreeView;

class ProfileOptionsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProfileOptionsDialog(ProfilePlotOptions   *options,
                                  AnimationController  *anim          = nullptr,
                                  SWMMVisProjectWindow *projectWindow = nullptr,
                                  QWidget              *parent        = nullptr);

public slots:
    /*! Refresh the Sources tab to mirror the AnimationController's current
        layer list.  Call after the host adds/removes a layer. */
    void refreshSources();

signals:
    /*!
     * \brief Emitted when the user toggles visibility, edits a colour, or
     *        renames a source.  The host dialog should call rebindSources()
     *        + populateSourcesPanel() to update the plot + toolbar.
     */
    void sourcesChanged();

    /*!
     * \brief Emitted when the user picks an external .out file.  The host
     *        should construct an SWMMResultsLayer for the file and register
     *        it on the AnimationController as a secondary layer.
     */
    void addOutputFileRequested(const QString &path);

private:
    void buildDisplayTab();
    void buildSourcesTab();
    void refreshSourcesModel();
    void persistSourcesState() const;
    void restoreSourcesState();

    [[nodiscard]] QString persistenceKey() const;

    QPointer<ProfilePlotOptions>   m_options;
    QPointer<AnimationController>  m_anim;
    QPointer<SWMMVisProjectWindow> m_projectWindow;

    QTabWidget                    *m_tabs      = nullptr;
    QTreeView                     *m_tree      = nullptr;
    QAbstractItemModel            *m_model     = nullptr;
    QAbstractItemDelegate         *m_delegate  = nullptr;

    QTreeView                     *m_sourcesView   = nullptr;
    QStandardItemModel            *m_sourcesModel  = nullptr;
    // Right-pane editor on the Sources tab — a QPropertyModel-backed
    // QTreeView bound to a ProfileSourceStyleAdapter that proxies the
    // selected layer's profile-style Q_PROPERTYs.  Empty (disabled) when
    // no source row is selected.
    QTreeView                     *m_styleView     = nullptr;
    QAbstractItemModel            *m_styleModel    = nullptr;
    QAbstractItemDelegate         *m_styleDelegate = nullptr;
    ProfileSourceStyleAdapter     *m_styleAdapter  = nullptr;
};

#endif // PROFILE_OPTIONS_DIALOG_H
