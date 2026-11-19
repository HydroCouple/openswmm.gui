/**
 * @file      test_hotstartsavesmodel.cpp
 * @brief     QtTest: the scheduled hot-start saves table model and its Format
 *         column ([FILES] SAVE HOTSTART ... LEGACY|NATIVE).
 *
 * The model stores the engine's SWMM_HOTSTART_FORMAT_* value (0 Auto,
 * 1 Legacy, 2 Native). These cases pin that EditRole carries the int,
 * DisplayRole the name, that invalid values are refused, that row moves
 * carry the format, and that the combo delegate round-trips it.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/hotstartsavesmodel.h"

#include <QComboBox>
#include <QObject>
#include <QSignalSpy>
#include <QStyleOptionViewItem>
#include <QTest>
#include <QWidget>

#include <memory>

class TestHotstartSavesModel : public QObject
{
    Q_OBJECT
private slots:
    void formatColumnExistsAndDefaultsToAuto();
    void appendRowCarriesFormat();
    void setDataAcceptsOnlyTheThreeFormats();
    void swapRowsMovesTheFormat();
    void formatDelegateRoundTrips();
};

/** @brief Require a new scheduled-save row to expose an editable Auto format column.
 * Checks column count/header, integer EditRole, display text and formatAt default. */
void TestHotstartSavesModel::formatColumnExistsAndDefaultsToAuto()
{
    HotstartSavesModel m;
    QCOMPARE(m.columnCount(), 3);
    QCOMPARE(m.headerData(HotstartSavesModel::ColFormat, Qt::Horizontal).toString(),
             QStringLiteral("Format"));
    QVERIFY(m.insertRows(0, 1));
    const QModelIndex f = m.index(0, HotstartSavesModel::ColFormat);
    QCOMPARE(m.data(f, Qt::EditRole).toInt(), 0);
    QCOMPARE(m.data(f, Qt::DisplayRole).toString(), QStringLiteral("Auto"));
    QCOMPARE(m.formatAt(0), 0);
    QVERIFY(m.flags(f) & Qt::ItemIsEditable);
}

/** @brief Require appendRow to retain Native format/date and normalize invalid format to Auto.
 * Also checks the two-argument append overload defaults to Auto; dates use OADate days. */
void TestHotstartSavesModel::appendRowCarriesFormat()
{
    HotstartSavesModel m;
    m.appendRow(QStringLiteral("a.hsf"), 0.0); // two-argument form: Auto
    m.appendRow(QStringLiteral("b.hsf"), 46036.5, 2);
    m.appendRow(QStringLiteral("c.hsf"), 0.0, 7); // out of range -> Auto
    QCOMPARE(m.formatAt(0), 0);
    QCOMPARE(m.formatAt(1), 2);
    QCOMPARE(m.formatAt(2), 0);
    QCOMPARE(m.data(m.index(1, HotstartSavesModel::ColFormat), Qt::DisplayRole).toString(),
             QStringLiteral("Native"));
    QCOMPARE(m.oaDateAt(1), 46036.5);
}

/** @brief Require only integer Auto/Legacy/Native edits and exactly one signal for a real change.
 * Rejects negative/out-of-range/text format values while retaining the last valid value. */
void TestHotstartSavesModel::setDataAcceptsOnlyTheThreeFormats()
{
    HotstartSavesModel m;
    m.appendRow(QStringLiteral("a.hsf"), 0.0);
    const QModelIndex f = m.index(0, HotstartSavesModel::ColFormat);
    QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
    QVERIFY(m.setData(f, 1, Qt::EditRole));
    QCOMPARE(m.formatAt(0), 1);
    QCOMPARE(changed.count(), 1);
    QVERIFY(m.setData(f, 1, Qt::EditRole)); // unchanged: no signal
    QCOMPARE(changed.count(), 1);
    QVERIFY(!m.setData(f, 3, Qt::EditRole));
    QVERIFY(!m.setData(f, -1, Qt::EditRole));
    QVERIFY(!m.setData(f, QStringLiteral("NATIVE"), Qt::EditRole));
    QCOMPARE(m.formatAt(0), 1);
}

/** @brief Require row swapping to move the format together with its scheduled-save path. */
void TestHotstartSavesModel::swapRowsMovesTheFormat()
{
    HotstartSavesModel m;
    m.appendRow(QStringLiteral("a.hsf"), 0.0, 0);
    m.appendRow(QStringLiteral("b.hsf"), 0.0, 2);
    QVERIFY(m.swapRows(0, 1));
    QCOMPARE(m.pathAt(0), QStringLiteral("b.hsf"));
    QCOMPARE(m.formatAt(0), 2);
    QCOMPARE(m.formatAt(1), 0);
}

/** @brief Require the format delegate editor to load Native and write back selected Legacy.
 * Creates a QWidget-owned combo also held by a unique_ptr whose destruction removes
 * it from its parent; checks three choices and uses real model EditRole values. */
void TestHotstartSavesModel::formatDelegateRoundTrips()
{
    HotstartSavesModel m;
    m.appendRow(QStringLiteral("a.hsf"), 0.0, 2);
    const QModelIndex f = m.index(0, HotstartSavesModel::ColFormat);

    HotstartSavesFormatDelegate d;
    QWidget parent;
    std::unique_ptr<QWidget> editor(d.createEditor(&parent, QStyleOptionViewItem(), f));
    auto *combo = qobject_cast<QComboBox *>(editor.get());
    QVERIFY(combo);
    QCOMPARE(combo->count(), 3);
    d.setEditorData(combo, f);
    QCOMPARE(combo->currentData().toInt(), 2);

    combo->setCurrentIndex(combo->findData(1));
    d.setModelData(combo, &m, f);
    QCOMPARE(m.formatAt(0), 1);
}

QTEST_MAIN(TestHotstartSavesModel)
#include "test_hotstartsavesmodel.moc"
