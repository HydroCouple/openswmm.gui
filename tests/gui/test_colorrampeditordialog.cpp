/**
 * @file      test_colorrampeditordialog.cpp
 * @brief     QtTest: ColorRampEditorDialog's "Remove Stop" removes the stop in
 *            the selected row, also after a position edit has reordered the
 *            ramp under a table that keeps its row order.
 *
 * @see ui/dialogs/colorrampeditordialog.h
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/colorrampeditordialog.h"

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>

using openswmmvis::ui::ColorRampEditorDialog;

namespace {

QPushButton *removeButton(ColorRampEditorDialog &dlg)
{
    const auto buttons = dlg.findChildren<QPushButton *>();
    for (QPushButton *b : buttons)
        if (b->text() == QStringLiteral("&Remove Stop"))
            return b;
    return nullptr;
}

} // namespace

class TestColorRampEditorDialog : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keep any preference reads away from the user's own settings.
        QCoreApplication::setOrganizationName(QStringLiteral("OpenSWMMVisTests"));
        QCoreApplication::setApplicationName(QStringLiteral("test_colorrampeditordialog"));
    }

    void removeStopRemovesSelectedRowAfterReorder()
    {
        RasterColorRamp ramp;
        ramp.stops = {{0.0, QColor(Qt::red)}, {0.5, QColor(Qt::green)}, {1.0, QColor(Qt::blue)}};
        ColorRampEditorDialog dlg(ramp);
        auto *table = dlg.findChild<QTableWidget *>();
        QVERIFY(table);
        QPushButton *remove = removeButton(dlg);
        QVERIFY(remove);

        // Move the red stop past the green one. The table keeps its rows
        // (red, green, blue) while the ramp is now ordered green, red, blue.
        auto *redPos = qobject_cast<QDoubleSpinBox *>(table->cellWidget(0, 0));
        QVERIFY(redPos);
        redPos->setValue(0.75);
        QCOMPARE(dlg.ramp().stops.at(0).second, QColor(Qt::green));

        // Remove the row the user selected: red.
        table->setCurrentCell(0, 0);
        remove->click();

        const QGradientStops left = dlg.ramp().stops;
        QCOMPARE(left.size(), 2);
        QCOMPARE(left.at(0).first, 0.5);
        QCOMPARE(left.at(0).second, QColor(Qt::green));
        QCOMPARE(left.at(1).first, 1.0);
        QCOMPARE(left.at(1).second, QColor(Qt::blue));
    }

    void removeStopWithoutReorderRemovesSelectedRow()
    {
        RasterColorRamp ramp;
        ramp.stops = {{0.0, QColor(Qt::red)}, {0.5, QColor(Qt::green)}, {1.0, QColor(Qt::blue)}};
        ColorRampEditorDialog dlg(ramp);
        auto *table = dlg.findChild<QTableWidget *>();
        QVERIFY(table);
        table->setCurrentCell(1, 0);
        removeButton(dlg)->click();
        const QGradientStops left = dlg.ramp().stops;
        QCOMPARE(left.size(), 2);
        QCOMPARE(left.at(0).second, QColor(Qt::red));
        QCOMPARE(left.at(1).second, QColor(Qt::blue));
    }

    void removeStopRemovesEverySelectedRow()
    {
        RasterColorRamp ramp;
        ramp.stops = {{0.0, QColor(Qt::red)},
                      {0.25, QColor(Qt::yellow)},
                      {0.5, QColor(Qt::green)},
                      {1.0, QColor(Qt::blue)}};
        ColorRampEditorDialog dlg(ramp);
        auto *table = dlg.findChild<QTableWidget *>();
        QVERIFY(table);
        QCOMPARE(table->selectionMode(), QAbstractItemView::ExtendedSelection);
        table->setCurrentCell(1, 0);
        table->setRangeSelected(QTableWidgetSelectionRange(2, 0, 2, 1), true);
        removeButton(dlg)->click();
        const QGradientStops left = dlg.ramp().stops;
        QCOMPARE(left.size(), 2);
        QCOMPARE(left.at(0).second, QColor(Qt::red));
        QCOMPARE(left.at(1).second, QColor(Qt::blue));
    }

    void removeStopKeepsAtLeastTwoStops()
    {
        RasterColorRamp ramp;
        ramp.stops = {{0.0, QColor(Qt::red)}, {0.5, QColor(Qt::green)}, {1.0, QColor(Qt::blue)}};
        ColorRampEditorDialog dlg(ramp);
        auto *table = dlg.findChild<QTableWidget *>();
        QVERIFY(table);
        table->setCurrentCell(0, 0);
        table->setRangeSelected(QTableWidgetSelectionRange(1, 0, 1, 1), true);
        removeButton(dlg)->click();
        QCOMPARE(dlg.ramp().stops.size(), 3); // two of three would leave one stop
    }
};

QTEST_MAIN(TestColorRampEditorDialog)
#include "test_colorrampeditordialog.moc"
