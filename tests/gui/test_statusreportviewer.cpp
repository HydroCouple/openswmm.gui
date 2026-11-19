/**
 * @file      test_statusreportviewer.cpp
 * @brief     R8 (Phase 4A, 2026-10-30): the report viewer's bookmarks tell the
 *            per-species "2D Aquifer Quality Continuity" blocks apart, and its
 *            continuity banner names the blocks that exceed 10 %.
 *
 * The report fixture is written to tests/output/gw_phase4a_2026-10-30/report_viewer.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/statusreportdialog.h"
#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListView>
#include <QtTest>

namespace
{
const QString kRoot = QStringLiteral(SWMMVIS_PHASE4A_OUTPUT "/report_viewer");

// Two species blocks of the engine's per-species aquifer continuity (one above
// 10 %), and a routing block whose second-species column is fine.
const char *kReport = R"RPT(
  **************************        Volume         Depth
  Runoff Quantity Continuity     hectare-m            mm
  **************************     ---------       -------
  Total Precipitation ......       154.561       571.430
  Continuity Error (%) .....        -0.437


  **************************      SurfPoll
  2D Aquifer Quality Continuity       mass
  **************************    ----------
  Initial Stored Mass ......        0.0000
  Continuity Error (%) .....         0.000


  **************************      GWTracer
  2D Aquifer Quality Continuity       mass
  **************************    ----------
  Initial Stored Mass ......        0.0000
  Continuity Error (%) .....        15.200


  **************************      SurfPoll      GWTracer
  Quality Routing Continuity            kg            kg
  **************************    ----------    ----------
  External Inflow ..........        21.853         0.000
  Continuity Error (%) .....        12.463         0.000
)RPT";
} // namespace

class TestStatusReportViewer : public QObject
{
    Q_OBJECT
private slots:
    void bookmarksAndBannerNameTheBlocks()
    {
        QVERIFY(QDir().mkpath(kRoot));
        const QString path = kRoot + "/species_blocks_dialog.rpt";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(file.write(kReport) == qint64(qstrlen(kReport)));
        file.close();

        openswmmvis::ui::StatusReportDialog dialog(path);
        QStringList bookmarks;
        for (auto *view : dialog.findChildren<QListView *>())
            if (auto *model = view->model())
                for (int row = 0; row < model->rowCount(); ++row)
                    bookmarks << model->index(row, 0).data().toString();
        QVERIFY2(bookmarks.contains("2D Aquifer Quality Continuity — SurfPoll"),
                 qPrintable(bookmarks.join(" | ")));
        QVERIFY(bookmarks.contains("2D Aquifer Quality Continuity — GWTracer"));

        QString banner;
        for (auto *label : dialog.findChildren<QLabel *>())
            if (label->text().startsWith(QStringLiteral("⚠")))
                banner = label->text();
        QVERIFY2(banner.contains("2D Aquifer Quality Continuity (GWTracer)"), qPrintable(banner));
        QVERIFY(banner.contains("Quality Routing Continuity (SurfPoll)"));
        QVERIFY(!banner.contains("Runoff"));
        QFile out(kRoot + "/dialog_bookmarks_and_banner.txt");
        QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write((bookmarks.join('\n') + "\n\nBanner: " + banner + '\n').toUtf8());
    }
};

QTEST_MAIN(TestStatusReportViewer)
#include "test_statusreportviewer.moc"
