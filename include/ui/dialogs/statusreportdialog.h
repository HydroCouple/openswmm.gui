/*!
 * \file   statusreportdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BE — Status Report Viewer dialog.
 *
 * Loads a .rpt file via openswmmvis::io::RptParser and presents one
 * tab per section in a QTabWidget. Each tab shows a monospace QTextEdit
 * with the section body. Continuity errors above the configured
 * threshold light a red banner at the top.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_STATUSREPORTDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STATUSREPORTDIALOG_H

#include "io/rptparser.h"

#include <QDialog>
#include <QString>

class QTabWidget;
class QLabel;
class QLineEdit;

namespace openswmmvis::ui {

class StatusReportDialog : public QDialog
{
    Q_OBJECT
public:
    explicit StatusReportDialog(const QString &rptPath, QWidget *parent = nullptr);
    ~StatusReportDialog() override;

private slots:
    void onFindNext();

private:
    void buildUi(const QVector<openswmmvis::io::RptSection> &sections);

    QString      m_path;
    QTabWidget  *m_tabs = nullptr;
    QLabel      *m_continuityBanner = nullptr;
    QLineEdit   *m_findEdit = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STATUSREPORTDIALOG_H
