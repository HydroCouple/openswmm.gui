/**
 * @file      test_simtime_display.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Simulation times must display as authored whatever the machine's
 *         time zone (tutorial bug report, 2026-10-17; GitHub issue #11).
 *
 *         Qt 6 QDateTimeEdit::setDateTime() CONVERTS a UTC-labelled value to
 *         the editor's zone. The Simulation Status panel used a local-zone
 *         editor and showed every time shifted by the UTC offset (+9 h for
 *         the reporter). Runs under TZ=Asia/Tokyo (set in CMake) so the
 *         assertion is not vacuous on a UTC machine. Unix only: Qt honours
 *         $TZ there.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/delegates/datetimedelegate.h"

#include <QDateTime>
#include <QDateTimeEdit>
#include <QStandardItemModel>
#include <QTableView>
#include <QTest>
#include <QTimeZone>

class TestSimTimeDisplay : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require a read-only delegate editor to display authored UTC midnight unchanged in a
     * non-UTC process timezone.
     * Requires the test environment to provide a nonzero offset (normally Asia/Tokyo),
     * creates a local model/view/editor and checks exact text. Does not construct the
     * full simulation status panel, change process TZ itself or run a model.
     *
     */
    void statusPanelEditorShowsTheAuthoredTime()
    {
        QVERIFY2(QDateTime(QDate(2026, 1, 1), QTime(0, 0)).offsetFromUtc() != 0,
                 "test must run in a non-UTC zone (TZ=Asia/Tokyo)");

        QStandardItemModel model(1, 1);
        model.setData(model.index(0, 0), QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC),
                      Qt::DisplayRole);
        QTableView view;
        view.setModel(&model);
        openswmmvis::ui::ReadOnlyDateTimeDelegate delegate;
        view.setItemDelegateForColumn(0, &delegate);
        view.openPersistentEditor(model.index(0, 0));

        auto *edit = view.findChild<QDateTimeEdit *>();
        QVERIFY(edit);
        QCOMPARE(edit->text(), QStringLiteral("2026-01-01 00:00:00"));
        QVERIFY(edit->isReadOnly());
    }
};

QTEST_MAIN(TestSimTimeDisplay)
#include "test_simtime_display.moc"
