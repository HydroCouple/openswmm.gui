/**
 * @file timeseriesassignmentdialog.h
 * @brief Review and commit a model-owned copy of one native observation series.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @license GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */
#pragma once
#include "timeseries/timeseriesassignment.h"
#include <QDialog>
#include <QPointer>
#include <QTimer>
#include <memory>
#include <thread>
class SWMMVisProjectWindow;
class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
namespace openswmmvis::ui
{
/** @brief Apply a previously validated copy through one checked native transaction.
 * @param[in] project Exact live project, never whichever project became active later.
 * @param[in] prepared Immutable prepared values and explicit binding choices.
 * @param[out] error Failure detail. False leaves engine and provenance unchanged.
 * @return True after one undoable model mutation. Caller verifies its preparation revision.
 * @note GUI thread only; no nested UI/event loop is entered during the transaction. */
bool applyPreparedTimeSeries(SWMMVisProjectWindow *project,
                            const OpenSWMM::TimeSeries::PreparedAssignment &prepared,
                            QString *error = nullptr);
/** @brief Compact native assignment review; workers never capture the model or dialog.
 * @details Preparation uses copied options and immutable source metadata. Every option/model
 * change invalidates the prepared values. Applying copies samples; it does not bind the model
 * to the lifetime of the observation layer or plugin. */
class TimeSeriesAssignmentDialog final : public QDialog
{
    Q_OBJECT
public:
    /** @param[in] project Borrowed exact destination, guarded through closing/destruction.
     * @param[in] dataset Immutable collection metadata retained by shared ownership.
     * @param[in] seriesId Stable source series identity.
     * @param[in] parent Optional Qt owner. Call open() for modeless use or exec() for modal use. */
    TimeSeriesAssignmentDialog(SWMMVisProjectWindow *project,
        std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset> dataset,
        QString seriesId, QWidget *parent = nullptr);
    /** @brief Request worker cancellation and join its bounded read loop before destruction. */
    ~TimeSeriesAssignmentDialog() override;
private:
    struct Work; ///< Detached worker state; no model or widget pointers cross the thread boundary.
    /** @brief Cancel pending preparation and revoke Apply after any input/model edit. */
    void invalidate();
    /** @brief Show only controls needed by the chosen binding and derive its project units. */
    void updateTarget();
    /** @brief Copy explicit choices and start worker preparation without borrowing an engine. */
    void prepare();
    /** @brief Publish a completed preview only when its input revision still matches. */
    void poll();
    /** @brief Commit the prepared values to the original surviving model and close on success. */
    void apply();
    QPointer<SWMMVisProjectWindow> m_project; ///< Exact destination; becomes null when the project closes.
    std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset> m_dataset; ///< Shared immutable source metadata.
    OpenSWMM::TimeSeries::SeriesInfo m_series; ///< Copied source descriptor used to present choices.
    OpenSWMM::TimeSeries::PreparedAssignment m_prepared; ///< Reviewed copied samples for the current revision.
    quint64 m_revision = 0; ///< Incremented on any option or model change to invalidate stale previews.
    void *m_engine = nullptr; ///< Borrowed engine identity only; compared on Apply, never used by workers.
    std::shared_ptr<Work> m_work; ///< Current detached worker state, also retained by its thread.
    std::thread m_thread; ///< Preparation thread joined before replacement or destruction.
    QTimer m_timer; ///< GUI-thread polling for worker completion without worker-to-widget callbacks.
    QLineEdit *m_name = nullptr; ///< Dialog-owned unique destination series name input.
    QLineEdit *m_object = nullptr; ///< Dialog-owned destination object identifier input.
    QLineEdit *m_unit = nullptr; ///< Dialog-owned target units; derived and locked for physical bindings.
    QComboBox *m_target = nullptr; ///< Dialog-owned create-only/flow/stage/gage choice.
    QComboBox *m_clock = nullptr; ///< Dialog-owned explicit UTC or fixed-offset interpretation choice.
    QComboBox *m_rainFormat = nullptr; ///< Dialog-owned rainfall intensity/volume/cumulative format choice.
    QDateTimeEdit *m_from = nullptr; ///< Dialog-owned inclusive source-basis start bound.
    QDateTimeEdit *m_to = nullptr; ///< Dialog-owned inclusive source-basis end bound.
    QSpinBox *m_interval = nullptr; ///< Dialog-owned declared source cadence used to detect missing intervals.
    QSpinBox *m_rainInterval = nullptr; ///< Dialog-owned target rain-gage recording interval in seconds.
    QDoubleSpinBox *m_offset = nullptr; ///< Dialog-owned fixed model-clock offset from UTC, in hours.
    QDoubleSpinBox *m_datum = nullptr; ///< Dialog-owned vertical-datum adjustment in target stage units.
    QCheckBox *m_unitsConfirmed = nullptr; ///< Dialog-owned explicit review of source and target units.
    QCheckBox *m_daily = nullptr; ///< Dialog-owned acceptance of holding a daily statistic through each day.
    QCheckBox *m_quality = nullptr; ///< Dialog-owned explicit acceptance of provisional or qualified samples.
    QCheckBox *m_irregular = nullptr; ///< Dialog-owned explicit irregular-sample interpolation choice.
    QCheckBox *m_datumConfirmed = nullptr; ///< Dialog-owned confirmation of absolute stage and vertical datum.
    QLabel *m_status = nullptr; ///< Dialog-owned preparation/validation and operation feedback.
    QPushButton *m_prepare = nullptr; ///< Dialog-owned preparation action; runs no model mutations.
    QPushButton *m_apply = nullptr; ///< Dialog-owned commit action enabled only for the current valid preview.
    QTableWidget *m_preview = nullptr; ///< Dialog-owned bounded first/last sample preview, never the source store.
};
}
