/**
 * @file timeserieslayerbrowser.h
 * @brief Shared series metadata selection and asynchronous observation paging.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QDialog>
#include <memory>
class TimeSeriesLayer;
/** @brief Modeless native data browser; no model forcing or downloaded samples are edited.
 * @details Owns view models and bounded copied sample pages. The layer is QPointer-guarded;
 * queries reopen their database on a worker and late results are generation fenced. */
class TimeSeriesLayerBrowser final : public QDialog
{
public:
    /** @brief Bind one collection and its shared selection.
     * @param[in] layer Borrowed native collection, must be nonnull on construction.
     * @param[in] parent Optional window parent; the browser deletes itself on close. */
    explicit TimeSeriesLayerBrowser(TimeSeriesLayer *layer, QWidget *parent = nullptr);
    /** @brief Cancel pending page/export queries; no worker retains a widget pointer. */
    ~TimeSeriesLayerBrowser() override;

private:
    class Impl;
    std::unique_ptr<Impl> d; ///< Owns view state and worker cancellation flags; widgets remain QObject children.
};
