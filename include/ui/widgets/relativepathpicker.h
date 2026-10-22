/**
 * @file      relativepathpicker.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Path-picker that displays paths relative to a project anchor.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_RELATIVEPATHPICKER_H
#define OPENSWMMVIS_UI_WIDGETS_RELATIVEPATHPICKER_H

#include <QFileDialog>
#include <QString>
#include <QWidget>

class QLineEdit;
class QToolButton;

namespace openswmmvis::ui
{

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
    /**
     * @brief Create an empty path edit and browse control with open-file acceptance mode.
     * @param[in] parent Optional QWidget owner; use on the GUI thread.
     */
    explicit RelativePathPicker(QWidget *parent = nullptr);

    /*! Directory that displayed paths are made relative to.
     *  Typically the parent directory of the project's `.inp` file. */
    [[nodiscard]] QString projectAnchor() const { return m_anchor; }
    /**
     * @brief Replace the cleaned display anchor and refresh text/tooltip without changing the stored path.
     * @param[in] dir Directory token, optionally empty; trailing separators are removed. It is not checked or resolved through symlinks.
     * @note Changing the anchor neither re-resolves the stored path nor emits pathChanged().
     */
    void setProjectAnchor(const QString &dir);

    /*! Acceptance mode for the browse dialog.
     *  Defaults to AcceptOpen ("pick an existing file"). */
    [[nodiscard]] QFileDialog::AcceptMode acceptMode() const { return m_mode; }
    /**
     * @brief Select whether browsing opens an existing file or chooses a save destination.
     * @param[in] m QFileDialog acceptance mode; no dialog opens until the browse button is clicked.
     */
    void setAcceptMode(QFileDialog::AcceptMode m) { m_mode = m; }

    /*! Browse-dialog file filter, e.g. "Time-series files (*.dat *.csv)". */
    [[nodiscard]] QString fileFilter() const { return m_filter; }
    /**
     * @brief Store the browser's filename filter.
     * @param[in] filter Qt filename filter text, optionally empty.
     */
    void setFileFilter(const QString &filter) { m_filter = filter; }

    /*! Browse-dialog window title. */
    [[nodiscard]] QString dialogCaption() const { return m_caption; }
    /**
     * @brief Store the title used for the next browse dialog.
     * @param[in] caption Window caption; empty allows default dialog behavior.
     */
    void setDialogCaption(const QString &caption) { m_caption = caption; }

    /*! Current absolute path. Empty when no file is selected. */
    [[nodiscard]] QString absolutePath() const { return m_absolute; }

    /*! Path string as currently displayed in the line edit — relative
     *  to the anchor when reachable, absolute otherwise.
     * @return Current line-edit text, including an uncommitted user edit.
     */
    [[nodiscard]] QString displayPath() const;

    /*! Whether the displayed path is currently expressed relatively
     *  (informational; the GUI status panel uses this).
     * @return True if both the stored path and current display text are nonempty and QFileInfo classifies the display as relative.
     */
    [[nodiscard]] bool isDisplayedRelatively() const;

public slots:
    /*! Accepts either an absolute or a relative path. Relative inputs are
     *  resolved against `projectAnchor()` when set; otherwise stored
     *  verbatim. Emits pathChanged() on a real change.
     * @param[in] p Filename token, optionally empty. Absolute paths are cleaned; relative tokens are cleaned against a nonempty anchor or stored verbatim without one.
     * @note The stored value may remain relative when no absolute anchor was provided. No existence/readability check is made.
     */
    void setPath(const QString &p);

signals:
    /*! Emitted whenever the absolute path changes via setPath() or browse.
     * @param[in] absolutePath Newly stored resolved token; despite the name it may be relative when no absolute anchor is set.
     */
    void pathChanged(const QString &absolutePath);

private slots:
    /**
     * @brief Open the configured file dialog from the stored path directory, anchor or home directory and accept a nonempty selection through setPath().
     * @note Canceling leaves the path unchanged; this widget does not read/write the selected file.
     */
    void onBrowseClicked();
    /**
     * @brief Resolve committed line-edit text through setPath(); unchanged stored values do not emit a signal.
     */
    void onLineEditFinished();

private:
    /*! Recompute display + tooltip from m_absolute and m_anchor.
     * @note Updates only the child text/tooltip; relative formatting uses QDir and preserves cross-volume absolute fallback.
     */
    void refreshDisplay();

    QLineEdit *m_edit = nullptr;
    QToolButton *m_browse = nullptr;

    QString m_absolute;
    QString m_anchor;
    QString m_filter;
    QString m_caption;
    QFileDialog::AcceptMode m_mode = QFileDialog::AcceptOpen;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_RELATIVEPATHPICKER_H
