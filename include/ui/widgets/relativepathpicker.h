/*!
 * \file   relativepathpicker.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Path-picker that displays paths relative to a project anchor.
 *
 *         Slice IO-10. The GUI counterpart to the engine's
 *         openswmm::io::PathResolver. The widget stores paths internally
 *         in their absolute form so the engine's `fopen` paths keep
 *         working, but displays them relative to a project-supplied
 *         anchor directory whenever that's possible. A tooltip surfaces
 *         the resolved absolute form for power users.
 *
 *         Used by every editor that today exposes an external-file slot:
 *         SimulationOptionsDialog (Files / Climate / Hot-Start tabs),
 *         TimeSeriesEditorDialog (external panel), and any property
 *         adapter that needs to capture a file reference. See
 *         IO_PORTABILITY_PLAN.md §3.7.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_RELATIVEPATHPICKER_H
#define OPENSWMMVIS_UI_WIDGETS_RELATIVEPATHPICKER_H

#include <QFileDialog>
#include <QString>
#include <QWidget>

class QLineEdit;
class QToolButton;

namespace openswmmvis::ui {

/*!
 * \class RelativePathPicker
 * \brief A QLineEdit + browse-button widget that normalises display
 *        paths against a project anchor.
 *
 * \details The widget exposes two views of the same value:
 *   - absolutePath() — the resolved absolute path (or empty); this is the
 *     authoritative value, persisted by callers.
 *   - displayPath()  — what the line edit shows: relative to the anchor
 *     when reachable, otherwise absolute (with a warning tooltip).
 *
 *   Setting the path via setPath() accepts either form; the widget
 *   re-derives the absolute and display strings internally.
 *
 *   Acceptance mode (open / save) and file filter are configurable.
 */
class RelativePathPicker : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(QString absolutePath READ absolutePath WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString projectAnchor READ projectAnchor WRITE setProjectAnchor)
    Q_PROPERTY(QString fileFilter READ fileFilter WRITE setFileFilter)

public:
    explicit RelativePathPicker(QWidget *parent = nullptr);

    /*! Directory that displayed paths are made relative to.
     *  Typically the parent directory of the project's `.inp` file. */
    [[nodiscard]] QString projectAnchor() const { return m_anchor; }
    void setProjectAnchor(const QString &dir);

    /*! Acceptance mode for the browse dialog.
     *  Defaults to AcceptOpen ("pick an existing file"). */
    [[nodiscard]] QFileDialog::AcceptMode acceptMode() const { return m_mode; }
    void setAcceptMode(QFileDialog::AcceptMode m) { m_mode = m; }

    /*! Browse-dialog file filter, e.g. "Time-series files (*.dat *.csv)". */
    [[nodiscard]] QString fileFilter() const { return m_filter; }
    void setFileFilter(const QString &filter) { m_filter = filter; }

    /*! Browse-dialog window title. */
    [[nodiscard]] QString dialogCaption() const { return m_caption; }
    void setDialogCaption(const QString &caption) { m_caption = caption; }

    /*! Current absolute path. Empty when no file is selected. */
    [[nodiscard]] QString absolutePath() const { return m_absolute; }

    /*! Path string as currently displayed in the line edit — relative
     *  to the anchor when reachable, absolute otherwise. */
    [[nodiscard]] QString displayPath() const;

    /*! Whether the displayed path is currently expressed relatively
     *  (informational; the GUI status panel uses this). */
    [[nodiscard]] bool isDisplayedRelatively() const;

public slots:
    /*! Accepts either an absolute or a relative path. Relative inputs are
     *  resolved against `projectAnchor()` when set; otherwise stored
     *  verbatim. Emits pathChanged() on a real change. */
    void setPath(const QString &p);

signals:
    /*! Emitted whenever the absolute path changes via setPath() or browse. */
    void pathChanged(const QString &absolutePath);

private slots:
    void onBrowseClicked();
    void onLineEditFinished();

private:
    /*! Recompute display + tooltip from m_absolute and m_anchor. */
    void refreshDisplay();

    QLineEdit   *m_edit    = nullptr;
    QToolButton *m_browse  = nullptr;

    QString m_absolute;
    QString m_anchor;
    QString m_filter;
    QString m_caption;
    QFileDialog::AcceptMode m_mode = QFileDialog::AcceptOpen;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_RELATIVEPATHPICKER_H
