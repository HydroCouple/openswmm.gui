/*!
 * \file   test_timeseries_table_model.cpp
 * \brief  Slice BQ Phase 6.7.3.4 — table model layout + edit + signal forwarding.
 */
#include <gtest/gtest.h>

#include "timeseries/timeseriesprovider.h"
#include "ui/panels/timeseriestablemodel.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QUndoStack>

using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::ui::TimeseriesTableModel;

namespace {
QDateTime t(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), Qt::UTC);
}

QVector<TimeseriesPoint> fixture()
{
    return {
        {t(2026, 1, 1, 0),  1.0},
        {t(2026, 1, 1, 6),  2.0},
        {t(2026, 1, 1, 12), 3.0},
    };
}
} // namespace

// ── Layout detection ────────────────────────────────────────────────────────

TEST(TimeseriesTableModel, EmptyBindIsEmpty)
{
    TimeseriesTableModel m;
    EXPECT_EQ(m.rowCount(), 0);
    EXPECT_EQ(m.columnCount(), 0);
}

TEST(TimeseriesTableModel, SingleProviderShowsTwoColumns)
{
    TimeseriesProvider a(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(a.setAllPoints(fixture()));

    TimeseriesTableModel m;
    m.setProviders({&a});
    EXPECT_EQ(m.rowCount(), 3);
    EXPECT_EQ(m.columnCount(), 2);
    EXPECT_EQ(m.layoutMode(), TimeseriesTableModel::LayoutMode::SharedGrid);
}

TEST(TimeseriesTableModel, SharedGridDetected)
{
    TimeseriesProvider a(QStringLiteral("A")), b(QStringLiteral("B"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    ASSERT_TRUE(b.setAllPoints(fixture()));

    TimeseriesTableModel m;
    m.setProviders({&a, &b});
    EXPECT_EQ(m.layoutMode(), TimeseriesTableModel::LayoutMode::SharedGrid);
    EXPECT_EQ(m.columnCount(), 3);   // [Time | A | B]
    EXPECT_EQ(m.rowCount(), 3);
}

TEST(TimeseriesTableModel, DivergentGridFallsBackToProviderZero)
{
    TimeseriesProvider a(QStringLiteral("A")), b(QStringLiteral("B"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    ASSERT_TRUE(b.setAllPoints({{t(2026, 2, 1, 0), 9.0}}));   // different grid

    TimeseriesTableModel m;
    m.setProviders({&a, &b});
    EXPECT_EQ(m.layoutMode(), TimeseriesTableModel::LayoutMode::Divergent);
    EXPECT_EQ(m.columnCount(), 2);   // [Time | Value (of provider 0)]
    EXPECT_EQ(m.rowCount(), 3);
}

// ── data() and headers ──────────────────────────────────────────────────────

TEST(TimeseriesTableModel, DataReturnsTimeAndValuesAcrossProviders)
{
    TimeseriesProvider a(QStringLiteral("A")), b(QStringLiteral("B"));
    ASSERT_TRUE(a.setAllPoints({{t(2026, 1, 1, 0), 1.0}, {t(2026, 1, 1, 6), 2.0}}));
    ASSERT_TRUE(b.setAllPoints({{t(2026, 1, 1, 0), 10.0}, {t(2026, 1, 1, 6), 20.0}}));

    TimeseriesTableModel m;
    m.setProviders({&a, &b});

    EXPECT_EQ(m.data(m.index(0, 0)).toDateTime(), t(2026, 1, 1, 0));
    EXPECT_DOUBLE_EQ(m.data(m.index(0, 1)).toDouble(), 1.0);
    EXPECT_DOUBLE_EQ(m.data(m.index(0, 2)).toDouble(), 10.0);
    EXPECT_DOUBLE_EQ(m.data(m.index(1, 1)).toDouble(), 2.0);
    EXPECT_DOUBLE_EQ(m.data(m.index(1, 2)).toDouble(), 20.0);
}

TEST(TimeseriesTableModel, HeaderUsesProviderName)
{
    TimeseriesProvider a(QStringLiteral("RAIN_A")), b(QStringLiteral("RAIN_B"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    ASSERT_TRUE(b.setAllPoints(fixture()));

    TimeseriesTableModel m;
    m.setProviders({&a, &b});
    EXPECT_EQ(m.headerData(0, Qt::Horizontal).toString(), QStringLiteral("Time"));
    EXPECT_EQ(m.headerData(1, Qt::Horizontal).toString(), QStringLiteral("RAIN_A"));
    EXPECT_EQ(m.headerData(2, Qt::Horizontal).toString(), QStringLiteral("RAIN_B"));
}

// ── Edits push undo commands ────────────────────────────────────────────────

TEST(TimeseriesTableModel, SetDataValueGoesThroughUndoStack)
{
    TimeseriesProvider a(QStringLiteral("A"));
    ASSERT_TRUE(a.setAllPoints(fixture()));

    TimeseriesTableModel m;
    QUndoStack stack;
    m.setUndoStack(&stack);
    m.setProviders({&a});

    EXPECT_TRUE(m.setData(m.index(1, 1), 99.0));
    EXPECT_DOUBLE_EQ(a.pointAt(1).value, 99.0);
    EXPECT_EQ(stack.count(), 1);

    stack.undo();
    EXPECT_DOUBLE_EQ(a.pointAt(1).value, 2.0);
}

TEST(TimeseriesTableModel, SetHeaderDataRenamesProvider)
{
    TimeseriesProvider a(QStringLiteral("OLD"));
    ASSERT_TRUE(a.setAllPoints(fixture()));

    TimeseriesTableModel m;
    QUndoStack stack;
    m.setUndoStack(&stack);
    m.setProviders({&a});

    EXPECT_TRUE(m.setHeaderData(1, Qt::Horizontal, QStringLiteral("NEW"), Qt::EditRole));
    EXPECT_EQ(a.name(), QStringLiteral("NEW"));
    EXPECT_EQ(stack.count(), 1);

    stack.undo();
    EXPECT_EQ(a.name(), QStringLiteral("OLD"));
}

TEST(TimeseriesTableModel, TimeColumnHeaderRenameIgnored)
{
    TimeseriesProvider a(QStringLiteral("A"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    TimeseriesTableModel m;
    m.setProviders({&a});

    EXPECT_FALSE(m.setHeaderData(0, Qt::Horizontal, QStringLiteral("X"), Qt::EditRole));
}

// ── External-file mode → read-only ──────────────────────────────────────────

TEST(TimeseriesTableModel, ExternalSourceModeFlagsExcludeEdit)
{
    TimeseriesProvider a(QStringLiteral("A"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    a.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);

    TimeseriesTableModel m;
    m.setProviders({&a});
    EXPECT_FALSE(m.flags(m.index(0, 1)) & Qt::ItemIsEditable);
}

TEST(TimeseriesTableModel, ExternalSourceModeRejectsSetData)
{
    TimeseriesProvider a(QStringLiteral("A"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    a.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
    TimeseriesTableModel m;
    m.setProviders({&a});
    EXPECT_FALSE(m.setData(m.index(0, 1), 99.0));
    EXPECT_DOUBLE_EQ(a.pointAt(0).value, 1.0);   // unchanged
}

// ── Provider signals forwarded to view ──────────────────────────────────────

TEST(TimeseriesTableModel, ProviderPointsChangedEmitsDataChanged)
{
    TimeseriesProvider a(QStringLiteral("A"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    TimeseriesTableModel m;
    m.setProviders({&a});

    QSignalSpy spy(&m, &QAbstractItemModel::dataChanged);
    a.setValueAt(1, 42.0);
    ASSERT_EQ(spy.count(), 1);
    const auto args = spy.first();
    EXPECT_EQ(args.at(0).value<QModelIndex>().row(), 1);
    EXPECT_EQ(args.at(0).value<QModelIndex>().column(), 1);
}

TEST(TimeseriesTableModel, ProviderRenameEmitsHeaderDataChanged)
{
    TimeseriesProvider a(QStringLiteral("OLD"));
    ASSERT_TRUE(a.setAllPoints(fixture()));
    TimeseriesTableModel m;
    m.setProviders({&a});

    QSignalSpy spy(&m, &QAbstractItemModel::headerDataChanged);
    a.setName(QStringLiteral("NEW"));
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(1).toInt(), 1);   // section
}
