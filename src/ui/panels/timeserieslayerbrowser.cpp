/**
 * @file timeserieslayerbrowser.cpp
 * @brief Native collection tables, linked selection, bounded observations and lossless CSV export.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "ui/panels/timeserieslayerbrowser.h"
#include "layers/timeserieslayer.h"
#include "timeseries/timeseriesstore.h"
#include <QAction>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>

using namespace OpenSWMM::TimeSeries;
namespace
{
/** @brief One generation-fenced bounded read result; values own all memory. */
struct PageResult {
    ObservationPage page;
    QString error;
};
/** @brief Quote one CSV field while preserving literal newlines/quotes and leading zeros. */
QByteArray csv(const QString &text)
{
    auto bytes = text.toUtf8();
    bytes.replace("\"", "\"\"");
    return '"' + bytes + '"';
}
/** @brief Stream complete selected series to an atomic CSV, independently of visible page/filter. */
QString exportRows(std::shared_ptr<const TimeSeriesDataset> data, QStringList ids, QString path,
                   std::shared_ptr<std::atomic_bool> cancel)
{
    const QFileInfo destination(path), source(data->path);
    const auto destinationCanonical = destination.canonicalFilePath();
    const auto sourceCanonical = source.canonicalFilePath();
    if (destination.absoluteFilePath() == source.absoluteFilePath() ||
        (!destinationCanonical.isEmpty() && destinationCanonical == sourceCanonical))
        return QObject::tr(
            "Choose a different export file; the source dataset cannot be overwritten.");
    QSaveFile output(path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly))
        return output.errorString();
    if (output.write(
            "series_id,time,value,unit,approval,qualifier,properties_json,geometry_json\r\n") < 0)
        return output.errorString();
    for (const auto &id : ids) {
        QString unit;
        for (const auto &series : data->series)
            if (series.id == id)
                unit = series.unit;
        ObservationQuery query;
        query.expectedDatasetId = data->id;
        query.expectedFingerprint = data->fingerprint;
        query.limit = 1000;
        for (;;) {
            if (cancel->load())
                return QObject::tr("Export cancelled.");
            QString error;
            const auto page =
                TimeSeriesStore::readPage(data->path, id, query, &error, cancel.get());
            if (!error.isEmpty())
                return error;
            for (const auto &row : page.observations) {
                QByteArray line =
                    csv(id) + ',' + csv(row.sourceTime) + ',' +
                    (row.hasValue ? QByteArray::number(row.value, 'g', 17) : QByteArray{}) + ',' +
                    csv(unit) + ',' + csv(row.approval) + ',' + csv(row.qualifier) + ',' +
                    csv(QString::fromUtf8(
                        QJsonDocument(row.properties).toJson(QJsonDocument::Compact))) +
                    ',' +
                    csv(QString::fromUtf8(
                        QJsonDocument(row.geometry).toJson(QJsonDocument::Compact))) +
                    "\r\n";
                if (output.write(line) != line.size())
                    return output.errorString();
            }
            if (!page.hasMore)
                break;
            if (page.nextKey <= query.afterKey)
                return QObject::tr("The source did not advance the observation cursor.");
            query.afterKey = page.nextKey;
        }
    }
    if (cancel->load())
        return QObject::tr("Export cancelled.");
    return output.commit() ? QString{} : output.errorString();
}
}
/** @brief Browser state; GUI data models contain metadata and one bounded page only. */
class TimeSeriesLayerBrowser::Impl
{
public:
    TimeSeriesLayerBrowser *owner; ///< Borrowed owning dialog; all widget callbacks use its QObject lifetime.
    QPointer<TimeSeriesLayer> layer; ///< Borrowed collection, guarded against project or layer destruction.
    QStandardItemModel *series = nullptr; ///< Dialog-owned series metadata model, never observation rows.
    QStandardItemModel *samples = nullptr; ///< Dialog-owned model for the current bounded observation page.
    QSortFilterProxyModel *filter = nullptr; ///< Dialog-owned metadata sort/filter proxy preserving UUID item roles.
    QTableView *seriesView = nullptr; ///< Dialog-owned table sharing qualified series selection with the layer.
    QTableView *sampleView = nullptr; ///< Dialog-owned read-only observation page view.
    QComboBox *sampleSeries = nullptr; ///< Chooses the single series whose observations are paged below.
    QPlainTextEdit *details = nullptr; ///< Optional source metadata/quality details without lossy formatting.
    QLabel *status = nullptr; ///< Displays page position, export progress, and reader diagnostics.
    QPushButton *previous = nullptr; ///< Pops one keyset cursor to revisit the preceding page.
    QPushButton *next = nullptr; ///< Pushes the current page's final time key before reading the next page.
    QAction *use = nullptr; ///< Requests an explicit model-owned copy of one usable selected series.
    QAction *plot = nullptr; ///< Sends selected stable identities to the native comparison plot controller.
    QAction *zoom = nullptr; ///< Frames selected source stations or resolved nonspatial-series links.
    QAction *attach = nullptr; ///< Creates an undoable association to the selected persistent feature.
    QAction *detach = nullptr; ///< Removes selected series' feature relationships without changing samples.
    QAction *exportAction = nullptr; ///< Starts complete original-sample export independently of paging.
    QVector<qint64> cursors{std::numeric_limits<qint64>::min()}; ///< Exclusive time-key history; first sentinel starts before all observations.
    ObservationPage page; ///< Owned current page used for Next availability and observation details.
    quint64 generation = 0; ///< Monotonic page request token rejecting obsolete worker completions.
    bool syncing = false; ///< Prevents table and layer selection callbacks from recursively mutating each other.
    std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false); ///< Shared page cancellation flag, readable after the browser closes.
    std::shared_ptr<std::atomic_bool> exportCancel; ///< Active export cancellation flag; null when no export is running.
    /** @brief Copy visible selected series IDs without depending on sorted row ordinals.
     * @return Stable IDs held in the selected proxy rows' item roles. */
    QStringList selection() const
    {
        QStringList ids;
        for (const auto &index : seriesView->selectionModel()->selectedRows())
            ids.append(index.data(Qt::UserRole).toString());
        return ids;
    }
    /** @brief Refresh metadata and restore qualified selection; samples are read separately. */
    void refresh()
    {
        if (!layer)
            return;
        QScopedValueRollback<bool> lock(syncing, true);
        series->clear();
        series->setHorizontalHeaderLabels({QObject::tr("Site"), QObject::tr("Measurement"),
                                           QObject::tr("Statistic"), QObject::tr("Units"),
                                           QObject::tr("Samples"), QObject::tr("Usable"),
                                           QObject::tr("Location / link")});
        if (auto data = layer->dataset())
            for (const auto &s : data->series) {
                QList<QStandardItem *> row;
                const QStringList values{
                    s.siteId,
                    s.title,
                    s.statistic,
                    s.unit,
                    QString::number(s.count),
                    QString::number(s.validCount),
                    s.locationId.isEmpty()
                        ? layer->associationStatus(s.id)
                        : QObject::tr("Located; %1").arg(layer->associationStatus(s.id))};
                for (const auto &value : values) {
                    auto *item = new QStandardItem(value);
                    item->setEditable(false);
                    row.append(item);
                }
                row[0]->setData(s.id, Qt::UserRole);
                row[0]->setToolTip(
                    QString::fromUtf8(QJsonDocument(s.attributes).toJson(QJsonDocument::Indented)));
                series->appendRow(row);
            }
        sampleSeries->clear();
        if (layer->dataset())
            for (const auto &s : layer->dataset()->series)
                sampleSeries->addItem(layer->seriesCaption(s.id), s.id);
        status->setText(layer->dataset()
                            ? QObject::tr("%1 series. Select a series to inspect or plot.")
                                  .arg(series->rowCount())
                            : layer->sourceError());
        syncSelection();
    }
    /** @brief Reflect layer selection in sorted table and sample selector without signal loops. */
    void syncSelection()
    {
        if (!layer)
            return;
        QScopedValueRollback<bool> lock(syncing, true);
        const auto selected = layer->selectedSeriesIds();
        seriesView->selectionModel()->clearSelection();
        for (int i = 0; i < filter->rowCount(); ++i)
            if (selected.contains(filter->index(i, 0).data(Qt::UserRole).toString()))
                seriesView->selectionModel()->select(
                    filter->index(i, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        if (!selected.isEmpty())
            sampleSeries->setCurrentIndex(sampleSeries->findData(selected.first()));
        use->setEnabled(selected.size() == 1 && layer->seriesInfo(selected.first()).validCount > 0);
        plot->setEnabled(!selected.isEmpty());
        zoom->setEnabled(layer->canZoomToLocations(selected));
        attach->setEnabled(!selected.isEmpty());
        detach->setEnabled(!selected.isEmpty());
        exportAction->setEnabled(!selected.isEmpty() && !exportCancel);
    }
    /** @brief Start a worker page query and discard any obsolete receiver/source generation. */
    void loadPage()
    {
        cancel->store(true);
        cancel = std::make_shared<std::atomic_bool>(false);
        const auto ticket = ++generation;
        samples->clear();
        samples->setHorizontalHeaderLabels({QObject::tr("Source time / date"), QObject::tr("Value"),
                                            QObject::tr("Approval"), QObject::tr("Qualifier")});
        previous->setEnabled(false);
        next->setEnabled(false);
        if (!layer || !layer->dataset() || sampleSeries->currentIndex() < 0)
            return;
        const auto data = layer->dataset();
        const auto id = sampleSeries->currentData().toString();
        ObservationQuery query;
        query.expectedDatasetId = data->id;
        query.expectedFingerprint = data->fingerprint;
        query.afterKey = cursors.last();
        query.limit = 500;
        auto *watcher = new QFutureWatcher<PageResult>(owner);
        QObject::connect(
            watcher, &QFutureWatcher<PageResult>::finished, owner,
            [this, watcher, ticket, data, id] {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (ticket != generation || !layer || layer->dataset() != data ||
                    sampleSeries->currentData().toString() != id)
                    return;
                page = result.page;
                if (!result.error.isEmpty()) {
                    status->setText(result.error);
                    return;
                }
                for (const auto &sample : page.observations) {
                    QList<QStandardItem *> row;
                    for (const auto &value :
                         QStringList{sample.sourceTime,
                                     sample.hasValue ? QString::number(sample.value, 'g', 16)
                                                     : QObject::tr("Missing"),
                                     sample.approval, sample.qualifier}) {
                        auto *item = new QStandardItem(value);
                        item->setEditable(false);
                        row.append(item);
                    }
                    samples->appendRow(row);
                }
                status->setText(
                    QObject::tr(
                        "Page %1 — %2 observations. Export uses all observations of the selected series.")
                        .arg(cursors.size())
                        .arg(page.observations.size()));
                previous->setEnabled(cursors.size() > 1);
                next->setEnabled(page.hasMore);
            });
        const auto flag = cancel;
        watcher->setFuture(QtConcurrent::run([data, id, query, flag] {
            PageResult result;
            result.page =
                TimeSeriesStore::readPage(data->path, id, query, &result.error, flag.get());
            return result;
        }));
    }
    /** @brief Export selected immutable series; no GUI/table data is transferred to the worker. */
    void exportSelection()
    {
        if (!layer || !layer->dataset() || exportCancel)
            return;
        const auto ids = selection();
        if (ids.isEmpty())
            return;
        const auto data = layer->dataset();
        const QPointer<TimeSeriesLayerBrowser> guard(owner);
        QFileDialog chooser(nullptr, QObject::tr("Export time-series observations"), {},
                            QObject::tr("CSV (*.csv)"));
        chooser.setAcceptMode(QFileDialog::AcceptSave);
        chooser.setDefaultSuffix("csv");
        chooser.setWindowModality(Qt::ApplicationModal);
        QObject::connect(owner, &QObject::destroyed, &chooser, &QDialog::reject);
        QObject::connect(owner, &QDialog::finished, &chooser, &QDialog::reject);
        if (chooser.exec() != QDialog::Accepted || !guard || chooser.selectedFiles().isEmpty())
            return;
        const auto path = chooser.selectedFiles().first();
        if (!guard || !layer || layer->dataset() != data || path.isEmpty())
            return;
        exportCancel = std::make_shared<std::atomic_bool>(false);
        exportAction->setEnabled(false);
        auto *watcher = new QFutureWatcher<QString>(owner);
        QObject::connect(watcher, &QFutureWatcher<QString>::finished, owner, [this, watcher, path] {
            const auto error = watcher->result();
            watcher->deleteLater();
            exportCancel.reset();
            status->setText(error.isEmpty() ? QObject::tr("Saved %1").arg(path) : error);
            syncSelection();
        });
        status->setText(
            QObject::tr("Exporting complete observations… Close this window to cancel."));
        watcher->setFuture(
            QtConcurrent::run(exportRows, data, ids, path, exportCancel));
    }
};
TimeSeriesLayerBrowser::TimeSeriesLayerBrowser(TimeSeriesLayer *layer, QWidget *parent)
    : QDialog(parent), d(std::make_unique<Impl>())
{
    d->owner = this;
    d->layer = layer;
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Time series — %1").arg(layer->name()));
    resize(1000, 680);
    auto *layout = new QVBoxLayout(this);
    auto *bar = new QToolBar(this);
    d->plot = bar->addAction(tr("Plot"));
    d->use = bar->addAction(tr("Use in model…"));
    d->zoom = bar->addAction(tr("Zoom to location"));
    d->zoom->setObjectName(QStringLiteral("timeSeriesZoomToLocation"));
    bar->addSeparator();
    d->attach = bar->addAction(tr("Attach to selected feature"));
    d->detach = bar->addAction(tr("Detach"));
    bar->addSeparator();
    d->exportAction = bar->addAction(tr("Export…"));
    auto *showDetails = bar->addAction(tr("Details"));
    showDetails->setCheckable(true);
    layout->addWidget(bar);
    auto *search = new QLineEdit(this);
    search->setPlaceholderText(tr("Filter series…"));
    search->setClearButtonEnabled(true);
    layout->addWidget(search);
    auto *split = new QSplitter(Qt::Vertical, this);
    layout->addWidget(split, 1);
    d->series = new QStandardItemModel(this);
    d->filter = new QSortFilterProxyModel(this);
    d->filter->setSourceModel(d->series);
    d->filter->setFilterKeyColumn(-1);
    d->filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    d->seriesView = new QTableView(split);
    d->seriesView->setObjectName("timeSeriesMetadataTable");
    d->seriesView->setModel(d->filter);
    d->seriesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->seriesView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    d->seriesView->setSortingEnabled(true);
    d->seriesView->horizontalHeader()->setStretchLastSection(true);
    d->seriesView->setContextMenuPolicy(Qt::CustomContextMenu);
    auto *bottom = new QWidget(split);
    auto *bottomLayout = new QVBoxLayout(bottom);
    auto *paging = new QHBoxLayout;
    d->sampleSeries = new QComboBox(bottom);
    d->sampleSeries->setObjectName("timeSeriesSampleSelector");
    paging->addWidget(d->sampleSeries, 1);
    d->previous = new QPushButton(tr("Previous"), bottom);
    d->next = new QPushButton(tr("Next"), bottom);
    paging->addWidget(d->previous);
    paging->addWidget(d->next);
    bottomLayout->addLayout(paging);
    d->samples = new QStandardItemModel(this);
    d->sampleView = new QTableView(bottom);
    d->sampleView->setObjectName("timeSeriesObservationsTable");
    d->sampleView->setModel(d->samples);
    d->sampleView->setSelectionBehavior(QAbstractItemView::SelectRows);
    d->sampleView->horizontalHeader()->setStretchLastSection(true);
    bottomLayout->addWidget(d->sampleView);
    d->details = new QPlainTextEdit(this);
    d->details->setReadOnly(true);
    d->details->setVisible(false);
    d->details->setMaximumHeight(150);
    layout->addWidget(d->details);
    d->status = new QLabel(this);
    d->status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    d->status->setWordWrap(true);
    layout->addWidget(d->status);
    connect(search, &QLineEdit::textChanged, d->filter,
            &QSortFilterProxyModel::setFilterFixedString);
    connect(showDetails, &QAction::toggled, d->details, &QWidget::setVisible);
    connect(d->plot, &QAction::triggered, this, [this] {
        if (d->layer)
            emit d->layer->plotRequested(d->selection());
    });
    connect(d->use, &QAction::triggered, this, [this] {
        const auto ids = d->selection();
        if (d->layer && ids.size() == 1)
            emit d->layer->useInModelRequested(ids.first());
    });
    connect(d->zoom, &QAction::triggered, this, [this] {
        if (d->layer)
            d->layer->zoomToLocations(d->selection());
    });
    connect(d->attach, &QAction::triggered, this, [this] {
        QString error;
        if (d->layer && !d->layer->attachToSelectedFeature(d->selection(), &error))
            QMessageBox::information(this, tr("Attach time series"), error);
    });
    connect(d->detach, &QAction::triggered, this, [this] {
        if (d->layer)
            d->layer->detachFeatures(d->selection());
    });
    connect(d->exportAction, &QAction::triggered, this, [this] { d->exportSelection(); });
    connect(d->seriesView, &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        const QPointer<TimeSeriesLayerBrowser> guard(this);
        QMenu menu;
        connect(this, &QObject::destroyed, &menu, &QWidget::close);
        connect(this, &QDialog::finished, &menu, &QWidget::close);
        for (auto *action : {d->plot, d->use, d->zoom, d->attach, d->detach, d->exportAction})
            menu.addAction(action);
        menu.exec(d->seriesView->viewport()->mapToGlobal(point));
        if (!guard)
            return;
    });
    connect(d->seriesView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (!d->syncing && d->layer) {
            d->layer->setSelectedSeriesIds(d->selection());
            d->syncSelection();
        }
    });
    connect(d->sampleSeries, &QComboBox::currentIndexChanged, this, [this] {
        d->cursors = {std::numeric_limits<qint64>::min()};
        d->loadPage();
        if (d->layer) {
            const auto info = d->layer->seriesInfo(d->sampleSeries->currentData().toString());
            QJsonObject detail = info.attributes;
            detail["series_id"] = info.id;
            detail["site_id"] = info.siteId;
            detail["source_series_id"] = info.sourceSeriesId;
            detail["unit"] = info.unit;
            detail["statistic"] = info.statistic;
            detail["time_basis"] = info.timeBasis == TimeBasis::CalendarDate
                                       ? "Calendar date statistic"
                                       : "Timestamp with source offset";
            if (d->layer->dataset())
                for (const auto &location : d->layer->dataset()->locations)
                    if (location.id == info.locationId)
                        detail["site_attributes"] = location.attributes;
            d->details->setPlainText(
                QString::fromUtf8(QJsonDocument(detail).toJson(QJsonDocument::Indented)));
        }
    });
    connect(d->sampleView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &row) {
                if (row.row() >= 0 && row.row() < d->page.observations.size()) {
                    const auto &sample = d->page.observations[row.row()];
                    QJsonObject info = sample.properties;
                    info["source_geometry"] = sample.geometry;
                    d->details->setPlainText(
                        QString::fromUtf8(QJsonDocument(info).toJson(QJsonDocument::Indented)));
                }
            });
    connect(d->previous, &QPushButton::clicked, this, [this] {
        if (d->cursors.size() > 1) {
            d->cursors.removeLast();
            d->loadPage();
        }
    });
    connect(d->next, &QPushButton::clicked, this, [this] {
        if (d->page.hasMore) {
            d->cursors.append(d->page.nextKey);
            d->loadPage();
        }
    });
    connect(layer, &TimeSeriesLayer::datasetChanged, this, [this] { d->refresh(); });
    connect(layer, &TimeSeriesLayer::associationsChanged, this, [this] { d->refresh(); });
    connect(layer, &TimeSeriesLayer::associationResolutionChanged, this, [this] { d->refresh(); });
    connect(layer, &TimeSeriesLayer::seriesSelectionChanged, this, [this] { d->syncSelection(); });
    connect(layer, &QObject::destroyed, this, &QWidget::close);
    d->refresh();
}
TimeSeriesLayerBrowser::~TimeSeriesLayerBrowser()
{
    d->cancel->store(true);
    if (d->exportCancel)
        d->exportCancel->store(true);
}
