/*!
 * \file   statusreportdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice GUI-2026-05-30 §6 — two-panel Report Viewer.
 *
 *         Left:  section list (QListView, filterable).
 *         Right: monospace rich-text viewer of the full report with a
 *                searchable line bar above (regex toggle).
 *         Top:   continuity error banner (only when continuity > threshold).
 *
 *         The class name remains `StatusReportDialog` for build-stability;
 *         a `using ReportViewerDialog = StatusReportDialog;` alias documents
 *         the intent.
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
class QTextEdit;
class QSyntaxHighlighter;

namespace openswmmvis::ui {

/*! One selectable report in the viewer's run combo — typically the
 *  scenario name of a run plus the `.rpt` path it wrote. */
struct ReportSource {
    QString label;   ///< user-visible run / scenario name.
    QString path;    ///< absolute `.rpt` path.
};

class StatusReportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StatusReportDialog(const QString &rptPath, QWidget *parent = nullptr);

    /*! Multi-run overload — shows a run selector combo when more than one
     *  source is given.  \p initialIndex selects the initially shown run. */
    explicit StatusReportDialog(const QVector<ReportSource> &sources,
                                int initialIndex = 0,
                                QWidget *parent = nullptr);
    ~StatusReportDialog() override;

private slots:
    void onSearchChanged();
    void onSearchNext();
    void onSearchPrev();
    void onSectionFilterChanged(const QString &text);
    void onSectionActivated(const QModelIndex &proxyIdx);

private:
    void buildUi();
    void loadReport(int sourceIndex);
    void populateText(const QVector<openswmmvis::io::RptSection> &sections);
    void runFind(bool backwards);
    void updateMatchCounter();

    QVector<ReportSource>  m_sources;
    QString                m_path;

    QComboBox             *m_runCombo         = nullptr;

    QSplitter             *m_splitter         = nullptr;

    QListView             *m_sectionList      = nullptr;
    QLineEdit             *m_sectionFilter    = nullptr;
    QStandardItemModel    *m_sectionsModel    = nullptr;
    QSortFilterProxyModel *m_sectionsProxy    = nullptr;

    QTextEdit             *m_viewer           = nullptr;
    QSyntaxHighlighter    *m_highlighter      = nullptr;

    QLineEdit             *m_searchEdit       = nullptr;
    QCheckBox             *m_regexToggle      = nullptr;
    QLabel                *m_searchStatus     = nullptr;

    QLabel                *m_continuityBanner = nullptr;

    // Per-section anchor positions inside m_viewer (start of section title in
    // the concatenated text).  Index matches m_sectionsModel rows.
    QVector<int>           m_sectionAnchors;
};

// Slice GUI-2026-05-30 §6 — alias documents the intended name.
using ReportViewerDialog = StatusReportDialog;

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STATUSREPORTDIALOG_H
