/**
 * @file      statusreportdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice GUI-2026-05-30 §6 — two-panel Report Viewer.
 *
 *         Left:  section list (QListView, filterable).
 *         Right: monospace plain-text viewer of the full report with a
 *                searchable line bar above (regex toggle).
 *         Top:   continuity error banner (only when continuity > threshold).
 *
 *         The class name remains `StatusReportDialog` for build-stability;
 *         a `using ReportViewerDialog = StatusReportDialog;` alias documents
 *         the intent.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_STATUSREPORTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STATUSREPORTDIALOG_H

#include "io/rptparser.h"

#include <QDialog>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QSortFilterProxyModel;
class QStandardItemModel;
class QSplitter;
class QPlainTextEdit;
class QSyntaxHighlighter;

namespace openswmmvis::ui
{

/*! One selectable report in the viewer's run combo — typically the
 *  scenario name of a run plus the `.rpt` path it wrote. */
struct ReportSource {
    QString label; ///< user-visible run / scenario name.
    QString path;  ///< absolute `.rpt` path.
};

class StatusReportDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] rptPath Copied file path, resolved by QFile when read; need not be absolute
     *   despite ReportSource convention.
     * @param[in] parent Optional QWidget owner; parentless lifetime belongs to caller.
     * @brief Build read-only viewer and synchronously load one UTF-8 report, labeled by file name.
     *   No engine/output ownership or file watcher.
     */
    explicit StatusReportDialog(const QString &rptPath, QWidget *parent = nullptr);

    /*! Multi-run overload — shows a run selector combo when more than one
     *  source is given.  \p initialIndex selects the initially shown run.
     * @param[in] sources Copied run labels/paths; empty builds an empty viewer, more than one shows
     *   selector.
     * @param[in] initialIndex Zero-based source index clamped to available range; ignored for empty
     *   sources.
     * @param[in] parent Optional QWidget owner; widgets/models/highlighter are owned by Qt
     *   parent/document.
     * @brief Build navigation/search viewer, synchronously load initial report and retain sources
     *   for later reload on selection. No accepted-result data or source edits. GUI thread only.
 */
    explicit StatusReportDialog(const QVector<ReportSource> &sources, int initialIndex = 0,
                                QWidget *parent = nullptr);
    /**
     * @brief Release Qt-owned viewer controls and highlighter; report files and simulation sources
     *   remain caller-owned.
     */
    ~StatusReportDialog() override;

private slots:
    /**
     * @brief Move cursor to document start, select first current match and recount; empty needle
     *   clears status without explicitly clearing prior selection.
     */
    void onSearchChanged();
    /**
     * @brief Search forward from current cursor/selection and wrap once; update count or
     *   invalid-regex/no-match status.
     */
    void onSearchNext();
    /**
     * @brief Search backwards from current cursor/selection and wrap once; update count or
     *   invalid-regex/no-match status.
     */
    void onSearchPrev();
    /**
     * @param[in] text Literal search tokens split on whitespace or hyphens; empty clears filter.
     * @brief Require every escaped token in section label, case-insensitively; preserves full
     *   report text and anchors. Null proxy is a no-op.
     */
    void onSectionFilterChanged(const QString &text);
    /**
     * @param[in] proxyIdx Borrowed index in section proxy; invalid/unresolvable row is ignored.
     * @brief Move text cursor to clamped bookmark and line-based scrollbar to its block;
     *   bottom-of-file scrollbar limits can prevent exact top alignment. No source selection bus or
     *   report writes.
     */
    void onSectionActivated(const QModelIndex &proxyIdx);

private:
    /**
     * @brief Create owned section proxy, no-wrap read-only numbered text viewer, continuity banner
     *   and search controls; Close accepts dialog.
     * @note Enter searches next; previous button searches backwards. Regex toggle updates count
     *   only, without reselecting a match. Does not install a Shift+Enter shortcut despite the
     *   placeholder text.
     */
    void buildUi();
    /**
     * @param[in] sourceIndex Zero-based copied source index; out of range is a no-op.
     * @brief Read parser sections and normalized whole-file text synchronously, update title,
     *   continuity warning and bookmarks.
     * @note Empty raw text falls back to concatenated parsed bodies; warn only when both are empty,
     *   then still populate. Logs disk/parse/widget counts. Preserves search/filter controls but
     *   does not rerun search count after loading; no file/engine writes.
     */
    void loadReport(int sourceIndex);
    /**
     * @param[in] sections Borrowed parser sections with bodies in report order; lengths determine
     *   bookmark offsets.
     * @param[in] reportText Complete normalized report copied into document; may include content
     *   outside parser sections.
     * @brief Replace section model/anchors and entire viewer text, create highlighter once and move
     *   cursor to top.
     * @note Offsets are cumulative body lengths clamped to text size, not title searches. First
     *   untitled block becomes Header & Notes with optional first WARNING/ERROR bookmark; no
     *   sections gives full-report bookmark when text exists. Does not truncate displayed report to
     *   parsed sections.
     */
    void populateText(const QVector<openswmmvis::io::RptSection> &sections,
                      const QString &reportText);
    /**
     * @param[in] backwards True for backward Qt document search and wrap to end; false searches
     *   forward and wraps to start.
     * @brief Find current literal or case-insensitive regular expression, select match and report
     *   count.
     * @note Invalid regex changes status only; empty clears status. Literal uses Qt default
     *   case-insensitive find. Failed wrapped search can move cursor to boundary. No whole-document
     *   highlight or file mutation.
     */
    void runFind(bool backwards);
    /**
     * @brief Count all nonoverlapping case-insensitive literal/regex matches in current displayed
     *   text and update status without moving cursor.
     * @note Empty clears status; invalid regex reports error. Count is not current match ordinal;
     *   can overwrite prior no-match message with zero matches.
     */
    void updateMatchCounter();

    QVector<ReportSource> m_sources;
    QString m_path;

    QComboBox *m_runCombo = nullptr;

    QSplitter *m_splitter = nullptr;

    QListView *m_sectionList = nullptr;
    QLineEdit *m_sectionFilter = nullptr;
    QStandardItemModel *m_sectionsModel = nullptr;
    QSortFilterProxyModel *m_sectionsProxy = nullptr;

    QPlainTextEdit *m_viewer = nullptr;
    QSyntaxHighlighter *m_highlighter = nullptr;

    QLineEdit *m_searchEdit = nullptr;
    QCheckBox *m_regexToggle = nullptr;
    QLabel *m_searchStatus = nullptr;

    QLabel *m_continuityBanner = nullptr;

    // Per-section anchor positions inside m_viewer (start of section title in
    // the concatenated text).  Index matches m_sectionsModel rows.
    QVector<int> m_sectionAnchors;
};

// Slice GUI-2026-05-30 §6 — alias documents the intended name.
using ReportViewerDialog = StatusReportDialog;

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STATUSREPORTDIALOG_H
