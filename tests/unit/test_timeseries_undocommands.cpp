/*!
 * \file   test_timeseries_undocommands.cpp
 * \brief  Slice BQ Phase 6.7.3.1 — undo/redo round-trip for each command.
 */
#include <gtest/gtest.h>

#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesundocommands.h"

#include <QDateTime>
#include <QUndoStack>
#include <QVector>

using openswmmvis::timeseries::BulkTransformCommand;
using openswmmvis::timeseries::ChangeSourceModeCommand;
using openswmmvis::timeseries::DeletePointsCommand;
using openswmmvis::timeseries::InsertPointCommand;
using openswmmvis::timeseries::MovePointCommand;
using openswmmvis::timeseries::RenameTimeseriesCommand;
using openswmmvis::timeseries::SetPointValueCommand;
using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;

namespace {
QDateTime t(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), Qt::UTC);
}

QVector<TimeseriesPoint> fixture()
{
    return {
        {t(2026, 1, 1, 0), 1.0},
        {t(2026, 1, 1, 6), 2.0},
        {t(2026, 1, 1, 12), 3.0},
    };
}
} // namespace

TEST(TimeseriesUndoCommands, SetPointValue_RoundTrip)
{
    TimeseriesProvider p(QStringLiteral("X"));
    ASSERT_TRUE(p.setAllPoints(fixture()));
    QUndoStack stack;

    stack.push(new SetPointValueCommand(&p, 1, 99.0));
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 99.0);
    stack.undo();
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 2.0);
    stack.redo();
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 99.0);
}

TEST(TimeseriesUndoCommands, MovePoint_RoundTrip)
{
    TimeseriesProvider p(QStringLiteral("X"));
    ASSERT_TRUE(p.setAllPoints(fixture()));
    QUndoStack stack;

    stack.push(new MovePointCommand(&p, 1, t(2026, 1, 1, 7), 5.0));
    EXPECT_EQ(p.pointAt(1).time, t(2026, 1, 1, 7));
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 5.0);
    stack.undo();
    EXPECT_EQ(p.pointAt(1).time, t(2026, 1, 1, 6));
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 2.0);
}

TEST(TimeseriesUndoCommands, InsertPoint_RoundTrip)
{
    TimeseriesProvider p(QStringLiteral("X"));
    ASSERT_TRUE(p.setAllPoints(fixture()));
    QUndoStack stack;

    auto *cmd = new InsertPointCommand(&p, t(2026, 1, 1, 9), 2.5);
    stack.push(cmd);
    EXPECT_EQ(p.pointCount(), 4);
    EXPECT_EQ(cmd->insertedIndex(), 2);

    stack.undo();
    EXPECT_EQ(p.pointCount(), 3);

    stack.redo();
    EXPECT_EQ(p.pointCount(), 4);
    EXPECT_EQ(p.pointAt(2).time, t(2026, 1, 1, 9));
}

TEST(TimeseriesUndoCommands, DeletePoints_RoundTrip)
{
    TimeseriesProvider p(QStringLiteral("X"));
    ASSERT_TRUE(p.setAllPoints(fixture()));
    QUndoStack stack;

    stack.push(new DeletePointsCommand(&p, {0, 2}));
    ASSERT_EQ(p.pointCount(), 1);
    EXPECT_EQ(p.pointAt(0).time, t(2026, 1, 1, 6));

    stack.undo();
    EXPECT_EQ(p.pointCount(), 3);
    // Order restored.
    EXPECT_EQ(p.pointAt(0).time, t(2026, 1, 1, 0));
    EXPECT_EQ(p.pointAt(2).time, t(2026, 1, 1, 12));
}

TEST(TimeseriesUndoCommands, BulkTransform_RoundTrip)
{
    TimeseriesProvider p(QStringLiteral("X"));
    ASSERT_TRUE(p.setAllPoints(fixture()));
    QUndoStack stack;

    QVector<TimeseriesPoint> shifted = {
        {t(2026, 1, 1, 0),  10.0},
        {t(2026, 1, 1, 6),  20.0},
        {t(2026, 1, 1, 12), 30.0},
    };
    stack.push(new BulkTransformCommand(&p, shifted, QStringLiteral("Scale x10")));
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 20.0);

    stack.undo();
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 2.0);
}

TEST(TimeseriesUndoCommands, Rename_RoundTrip)
{
    TimeseriesProvider p(QStringLiteral("OLD"));
    QUndoStack stack;

    stack.push(new RenameTimeseriesCommand(&p, QStringLiteral("NEW")));
    EXPECT_EQ(p.name(), QStringLiteral("NEW"));
    stack.undo();
    EXPECT_EQ(p.name(), QStringLiteral("OLD"));
}

TEST(TimeseriesUndoCommands, ChangeSourceMode_RoundTrip)
{
    TimeseriesProvider p(QStringLiteral("X"));
    QUndoStack stack;

    stack.push(new ChangeSourceModeCommand(
                   &p, TimeseriesProvider::SourceMode::ExternalFile));
    EXPECT_EQ(p.sourceMode(), TimeseriesProvider::SourceMode::ExternalFile);
    stack.undo();
    EXPECT_EQ(p.sourceMode(), TimeseriesProvider::SourceMode::Inline);
}

TEST(TimeseriesUndoCommands, SetTimeMode_UndoRestoresMixedExactly)
{
    using openswmmvis::timeseries::SetTimeModeCommand;
    TimeseriesProvider p(QStringLiteral("X"));
    const QDateTime anchor(QDate(2026, 1, 1), QTime(6, 0), Qt::UTC);
    ASSERT_TRUE(p.setAllPoints({
        {QDateTime(QDate(2026, 1, 1), QTime(6, 0),  Qt::UTC), 1.0},
        {QDateTime(QDate(2026, 1, 1), QTime(7, 0),  Qt::UTC), 2.0},
        {QDateTime(QDate(2026, 1, 2), QTime(0, 0),  Qt::UTC), 3.0},
        {QDateTime(QDate(2026, 1, 2), QTime(1, 0),  Qt::UTC), 4.0},
        {QDateTime(QDate(2026, 1, 2), QTime(2, 0),  Qt::UTC), 5.0},
    }));
    p.setRelativeInfo(2, anchor);   // loaded Mixed state: 2 of 5 relative
    ASSERT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Mixed);

    QUndoStack stack;
    stack.push(new SetTimeModeCommand(
        &p, TimeseriesProvider::TimeMode::Absolute, QDateTime()));
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
    EXPECT_EQ(p.relativeCount(), 0);

    stack.undo();   // must restore Mixed EXACTLY, not merely Absolute
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Mixed);
    EXPECT_EQ(p.relativeCount(), 2);
    EXPECT_EQ(p.relativeAnchor(), anchor);

    stack.redo();
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);

    // Relative round-trip captures/restores the anchor too.
    stack.push(new SetTimeModeCommand(
        &p, TimeseriesProvider::TimeMode::Relative, anchor));
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
    EXPECT_EQ(p.relativeCount(), 5);
    stack.undo();
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
    EXPECT_EQ(p.relativeCount(), 0);
}
