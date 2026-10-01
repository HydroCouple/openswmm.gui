// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/traceanalysisdialog.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "layers/traceanalysislayer.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "output/outputstatsregistry.h"
#include "output/traceanalysisstore.h"
#include "output/tracecontroller.h"
#include "swmmvisprojectwindow.h"
#include "ui/widgets/colorrampcombobox.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimeZone>
#include <QVBoxLayout>
#include <cmath>
#include <ogr_spatialref.h>

namespace openswmmvis::trace
{
namespace
{
QString number(double v)
{
    return std::isfinite(v) ? QString::number(v, 'g', 6) : QStringLiteral("Unavailable");
}
class NumberItem : public QTableWidgetItem
{
  public:
    explicit NumberItem(double n) : QTableWidgetItem(number(n)), value(n) {}
    bool operator<(const QTableWidgetItem &o) const override
    {
        auto *p = dynamic_cast<const NumberItem *>(&o);
        return p ? (!std::isfinite(value) ? false : !std::isfinite(p->value) || value < p->value)
                 : QTableWidgetItem::operator<(o);
    }
    double value;
};
QWidget *summaryPage(QLabel *&label)
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    label = new QLabel;
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(label);
    l->addStretch();
    return w;
}
QWidget *tablePage(QTableWidget *&table)
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *filter = new QLineEdit;
    filter->setPlaceholderText(QObject::tr("Filter ID or status…"));
    table = new QTableWidget;
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setStretchLastSection(true);
    l->addWidget(filter);
    l->addWidget(table);
    QObject::connect(filter, &QLineEdit::textChanged, table,
                     [table](const QString &s)
                     {
                         for (int row = 0; row < table->rowCount(); ++row)
                         {
                             bool matches = s.isEmpty();
                             for (int col = 0; col < table->columnCount(); ++col)
                                 if (auto *i = table->item(row, col))
                                     matches |= i->text().contains(s, Qt::CaseInsensitive);
                             table->setRowHidden(row, !matches);
                         }
                     });
    auto *exportButton = new QPushButton(QObject::tr("Export table CSV…"));
    l->addWidget(exportButton);
    QObject::connect(
        exportButton, &QPushButton::clicked, table,
        [table]
        {
            auto path = QFileDialog::getSaveFileName(table, QObject::tr("Export analysis table"),
                                                     {}, "CSV (*.csv)");
            if (path.isEmpty())
                return;
            QByteArray bytes;
            auto quote = [](QString s)
            {
                s.replace('"', "\"\"");
                return '"' + s + '"';
            };
            for (int row = -1; row < table->rowCount(); ++row)
            {
                QStringList values;
                const QStringList provenance =
                    row < 0 ? QStringList{"Run ID", "Analysis ID", "Source output", "Seed node",
                                          "Direction"}
                            : table->property("traceProvenance").toStringList();
                for (const auto &value : provenance)
                    values << quote(value);
                for (int col = 0; col < table->columnCount(); ++col)
                {
                    auto *i = row < 0 ? table->horizontalHeaderItem(col) : table->item(row, col);
                    values << quote(i ? i->text() : QString());
                }
                bytes += values.join(',').toUtf8() + '\n';
            }
            QSaveFile f(path);
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
                QMessageBox::warning(table, QObject::tr("Export failed"), f.errorString());
        });
    return w;
}
} // namespace
TraceAnalysisDialog *TraceAnalysisDialog::showFor(SWMMVisProjectWindow *p, bool upstream,
                                                  bool travel, const QString &seed)
{
    auto *d = p->findChild<TraceAnalysisDialog *>("flowTraceDialog", Qt::FindDirectChildrenOnly);
    if (!d)
        d = new TraceAnalysisDialog(p);
    d->m_direction->setCurrentIndex(upstream ? 1 : 0);
    d->m_tabs->setCurrentIndex(travel ? 1 : 0);
    if (seed.isEmpty())
        d->useSelection();
    else
        d->setSeedNodes({seed});
    d->show();
    d->raise();
    d->activateWindow();
    return d;
}
TraceAnalysisDialog::TraceAnalysisDialog(SWMMVisProjectWindow *p)
    : QDialog(p), m_project(p), m_controller(TraceController::forProject(p))
{
    setObjectName("flowTraceDialog");
    setWindowTitle(tr("Flow Balance and Travel Time"));
    resize(990, 760);
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);
    m_sources = new QComboBox;
    m_sources->setObjectName("traceResults");
    m_sources->setEditable(true);
    m_sources->setInsertPolicy(QComboBox::NoInsert);
    m_sources->completer()->setFilterMode(Qt::MatchContains);
    m_sources->setMinimumContentsLength(35);
    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_sources, 1);
    auto *load = new QPushButton(tr("Load output…"));
    sourceRow->addWidget(load);
    auto *open = new QPushButton(tr("Open analysis…"));
    sourceRow->addWidget(open);
    form->addRow(tr("Results"), sourceRow);
    m_follow = new QCheckBox(tr("Follow active results"));
    m_follow->setChecked(true);
    m_keep = new QCheckBox(tr("Keep previous raw results when rerunning"));
    m_keep->setChecked(!p->property("traceKeepPreviousRaw").isValid() ||
                       p->property("traceKeepPreviousRaw").toBool());
    auto *options = new QHBoxLayout;
    options->addWidget(m_follow);
    options->addWidget(m_keep);
    form->addRow(options);
    m_saved = new QComboBox;
    m_saved->setObjectName("traceSavedAnalysis");
    form->addRow(tr("Saved analysis"), m_saved);
    m_direction = new QComboBox;
    m_direction->addItems({tr("Downstream from node"), tr("Upstream to node")});
    form->addRow(tr("Direction"), m_direction);
    auto *seedRow = new QHBoxLayout;
    m_seeds = new QLineEdit;
    m_seeds->setObjectName("traceNodeIds");
    m_seeds->setPlaceholderText(tr("Node IDs, separated by commas; one estimate per node"));
    seedRow->addWidget(m_seeds, 1);
    auto *selection = new QPushButton(tr("Use selection"));
    seedRow->addWidget(selection);
    m_pick = new QPushButton(tr("Pick node on map"));
    m_pick->setCheckable(true);
    seedRow->addWidget(m_pick);
    form->addRow(tr("Nodes"), seedRow);
    auto *inspect = new QPushButton(tr("Inspect on map"));
    inspect->setCheckable(true);
    seedRow->addWidget(inspect);
    connect(inspect, &QPushButton::toggled, this,
            [this](bool b)
            {
                m_inspecting = b;
                if (b)
                    m_pick->setChecked(false);
            });
    auto *dest = new QHBoxLayout;
    m_destination = new QLineEdit;
    m_destination->setObjectName("traceDestination");
    dest->addWidget(m_destination, 1);
    auto *browse = new QPushButton(tr("Browse…"));
    dest->addWidget(browse);
    form->addRow(tr("Analysis package"), dest);
    auto *buttons = new QHBoxLayout;
    m_prepare = new QPushButton(tr("Prepare averages"));
    m_estimate = new QPushButton(tr("Estimate and save"));
    m_update = new QPushButton(tr("Update saved estimate"));
    m_cancel = new QPushButton(tr("Cancel analysis"));
    m_cancel->setEnabled(false);
    auto *retry = new QPushButton(tr("Retry save"));
    auto *recalculate = new QPushButton(tr("Use recipe on active run"));
    for (auto *b : {m_prepare, m_estimate, m_update, recalculate, retry, m_cancel})
        buttons->addWidget(b);
    layout->addLayout(buttons);
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->hide();
    layout->addWidget(m_progress);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);
    m_tabs = new QTabWidget;
    m_tabs->setObjectName("traceTabs");
    m_tabs->addTab(summaryPage(m_flow), tr("Flow Balance"));
    m_tabs->addTab(summaryPage(m_time), tr("Travel Time"));
    m_tabs->addTab(tablePage(m_nodes), tr("Nodes"));
    m_tabs->addTab(tablePage(m_links), tr("Links"));
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    m_appearance = new QWidget;
    m_appearanceLayout = new QFormLayout(m_appearance);
    scroll->setWidget(m_appearance);
    m_tabs->addTab(scroll, tr("Appearance"));
    layout->addWidget(m_tabs, 1);
    connect(load, &QPushButton::clicked, this, &TraceAnalysisDialog::loadOutput);
    connect(open, &QPushButton::clicked, this, &TraceAnalysisDialog::loadPackage);
    connect(browse, &QPushButton::clicked, this,
            [this]
            {
                auto path = QFileDialog::getSaveFileName(
                    this, tr("Analysis package"), m_destination->text(), "GeoPackage (*.gpkg)");
                if (!path.isEmpty())
                    m_destination->setText(path);
            });
    connect(m_sources, &QComboBox::activated, this,
            [this]
            {
                m_follow->setChecked(false);
                const QString runId = m_sources->currentData().toString();
                for (const auto &identity : m_project->statsRegistry()->identities())
                    if (identity.runId == runId)
                    {
                        m_project->setActiveResultsLayer(identity.layer);
                        break;
                    }
                sourceChanged();
            });
    connect(m_follow, &QCheckBox::toggled, this,
            [this](bool v)
            {
                if (v)
                    followActive();
                persistState();
            });
    connect(m_keep, &QCheckBox::toggled, this,
            [this](bool v)
            {
                m_project->setProperty("traceKeepPreviousRaw", v);
                m_project->setHasChanges(true);
            });
    connect(m_saved, &QComboBox::activated, this,
            [this]
            {
                QString id = m_saved->currentData().toString();
                if (id.isEmpty())
                    return;
                QString error;
                auto r = AnalysisStore::read(m_destination->text(), id, &error);
                if (!r)
                {
                    showError(error);
                    return;
                }
                m_follow->setChecked(false);
                display(r);
            });
    connect(m_prepare, &QPushButton::clicked, this, [this] { start(true); });
    connect(m_estimate, &QPushButton::clicked, this, [this] { start(); });
    connect(m_update, &QPushButton::clicked, this, [this] { start(false, true); });
    connect(m_cancel, &QPushButton::clicked, m_controller, &TraceController::cancel);
    connect(retry, &QPushButton::clicked, this,
            [this]
            {
                if (!m_result || m_controller->busy())
                    return;
                QString error;
                if (!AnalysisStore::save(m_result->dataset->packagePath, *m_result, &error))
                {
                    showError(error);
                    return;
                }
                m_result->saved = true;
                m_status->setText(tr("Analysis saved."));
                refreshSaved();
            });
    connect(recalculate, &QPushButton::clicked, this,
            [this]
            {
                if (!m_result)
                    return;
                QString seed = m_result->dataset->snapshot.nodes[m_result->seed].id;
                int direction = m_result->direction;
                m_follow->setChecked(true);
                followActive();
                m_follow->setChecked(false);
                m_seeds->setText(seed);
                m_direction->setCurrentIndex(direction);
                m_status->setText(tr("Recipe copied. Select Estimate and save to create a "
                                     "new analysis for this run."));
            });
    connect(selection, &QPushButton::clicked, this, &TraceAnalysisDialog::useSelection);
    connect(m_pick, &QPushButton::toggled, this,
            [this](bool v)
            {
                m_picking = v;
                if (v)
                    m_status->setText(
                        tr("Click a node on the map. Hold Shift to add another node."));
            });
    m_project->canvas()->installEventFilter(this);
    for (auto *w : m_project->canvas()->findChildren<QWidget *>())
        w->installEventFilter(this);
    connect(m_project->statsRegistry(), &OutputStatsRegistry::runsChanged, this,
            &TraceAnalysisDialog::refreshSources);
    connect(m_project, &SWMMVisProjectWindow::activeResultsLayerChanged, this,
            [this]
            {
                if (m_follow->isChecked())
                    followActive();
            });
    connect(m_controller, &TraceController::busyChanged, this,
            [this](bool busy)
            {
                m_sources->setEnabled(!busy);
                m_saved->setEnabled(!busy);
                m_prepare->setEnabled(!busy);
                m_estimate->setEnabled(!busy);
                m_update->setEnabled(!busy && bool(m_result));
                m_cancel->setEnabled(busy);
                m_progress->setVisible(busy);
            });
    connect(m_controller, &TraceController::progress, this,
            [this](int n, const QString &s)
            {
                m_progress->setValue(n);
                m_status->setText(s);
            });
    connect(m_controller, &TraceController::failed, this, &TraceAnalysisDialog::showError);
    connect(m_controller, &TraceController::prepared, this,
            [this](std::shared_ptr<Dataset> d)
            {
                if (m_sources->currentData().toString() != d->runId)
                    return;
                m_status->setText(tr("Averages prepared: %1 report periods over %2 hours. %3")
                                      .arg(d->info.periods)
                                      .arg(d->info.duration_s / 3600., 0, 'g', 6)
                                      .arg(d->provenance));
                refreshSaved();
            });
    connect(m_controller, &TraceController::resultReady, this,
            [this](std::shared_ptr<Result> r)
            {
                if (m_sources->currentData().toString() == r->dataset->runId)
                    display(r);
            });
    connect(m_controller, &TraceController::finished, this,
            [this](const QString &, bool cancelled)
            {
                if (cancelled)
                    m_status->setText(tr("Cancelled. Previously saved analyses remain available."));
                refreshSaved();
            });
    for (auto *table : {m_nodes, m_links})
        connect(table, &QTableWidget::itemSelectionChanged, this,
                [this, table]
                {
                    if (!m_layer || table->currentRow() < 0)
                        return;
                    auto *cell = table->item(table->currentRow(), 0);
                    if (!cell)
                        return;
                    bool node = table == m_nodes;
                    int i = cell->data(Qt::UserRole).toInt();
                    m_layer->highlight(node, i);
                    // Historical results select their own map geometry, never edit the
                    // current model.
                });
    const auto initialState = p->property("tracePanelState").toJsonObject();
    refreshSources();
    restore(initialState);
    if (m_follow->isChecked())
        followActive();
    sourceChanged();
    const QString analysisId = initialState.value("analysisId").toString();
    if (!analysisId.isEmpty())
    {
        for (auto *mapLayer : p->canvas()->layers())
            if (auto *trace = qobject_cast<TraceAnalysisLayer *>(mapLayer))
                if (trace->result()->id == analysisId &&
                    trace->result()->dataset->runId == m_sources->currentData().toString())
                {
                    display(trace->result());
                    break;
                }
    }
}
void TraceAnalysisDialog::showError(const QString &s)
{
    m_status->setText(tr("Analysis: %1").arg(s));
}
void TraceAnalysisDialog::refreshSources()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    QString id = m_sources->currentData().toString();
    QSignalBlocker b(m_sources);
    m_sources->clear();
    auto runs = m_project->statsRegistry()->runs();
    for (auto it = runs.crbegin(); it != runs.crend(); ++it)
    {
        const auto &r = *it;
        bool current = m_project->statsRegistry()->latestRun(r.path).id == r.id;
        bool exists = (current && QFileInfo::exists(r.path)) || QFileInfo::exists(r.packagePath) ||
                      QFileInfo::exists(r.retainedPath);
        m_sources->addItem(
            tr("%1 · Run %2 · %3 · %4%5")
                .arg(r.label)
                .arg(r.number)
                .arg(r.completedAt, r.state, exists ? QString() : tr(" · Missing files")),
            r.id);
        m_sources->setItemData(m_sources->count() - 1,
                               r.path + "\n" + r.packagePath + "\n" + r.retainedPath,
                               Qt::ToolTipRole);
        if (auto *model = qobject_cast<QStandardItemModel *>(m_sources->model()))
            if (auto *item = model->item(m_sources->count() - 1))
                item->setEnabled(r.canAnalyze() && exists);
    }
    int index = m_sources->findData(id);
    m_sources->setCurrentIndex(index >= 0 ? index : (id.isEmpty() && m_sources->count() ? 0 : -1));
    m_refreshing = false;
    if (m_follow->isChecked())
        followActive();
}
void TraceAnalysisDialog::followActive()
{
    for (const auto &i : m_project->statsRegistry()->identities())
        if (i.layer == m_project->activeResultsLayer())
        {
            int index = m_sources->findData(i.runId);
            if (index >= 0 && index != m_sources->currentIndex())
            {
                m_sources->setCurrentIndex(index);
                sourceChanged();
            }
            return;
        }
}
void TraceAnalysisDialog::sourceChanged()
{
    m_result.reset();
    m_layer = nullptr;
    m_nodes->setRowCount(0);
    m_links->setRowCount(0);
    m_flow->setText(tr("Prepare averages, then select any node to estimate its "
                       "upstream contributors or downstream destinations."));
    m_time->setText(tr("Travel times use the same saved average hydraulics as flow balance. "
                       "They are estimates over the report window."));
    m_destination->setText(m_controller->defaultPackagePath(m_sources->currentData().toString()));
    refreshSaved();
    updateAppearance();
    m_update->setEnabled(false);
    const auto run = m_project->statsRegistry()->run(m_sources->currentData().toString());
    m_status->setText(run.id.isEmpty()
                          ? tr("Load a completed result or open a saved analysis.")
                          : tr("%1 · %2 · %3")
                                .arg(run.label, run.state,
                                     run.packagePath.isEmpty() ? run.path : run.packagePath));
    persistState();
}
void TraceAnalysisDialog::refreshSaved()
{
    QSignalBlocker b(m_saved);
    QString id = m_result ? m_result->id : m_saved->currentData().toString();
    m_saved->clear();
    m_saved->addItem(tr("New analysis"), QString());
    if (QFileInfo::exists(m_destination->text()))
    {
        QString error;
        for (auto j : AnalysisStore::analyses(m_destination->text(), &error))
            m_saved->addItem(j.value("title").toString(), j.value("id").toString());
        if (!error.isEmpty())
            showError(error);
    }
    int row = m_saved->findData(id);
    m_saved->setCurrentIndex(std::max(0, row));
}
void TraceAnalysisDialog::start(bool prepareOnly, bool replace)
{
    QString error;
    Snapshot snapshot = Snapshot::capture(m_project->modelLayer(), &error);
    auto source = m_project->statsRegistry()->run(m_sources->currentData().toString());
    if (source.id.isEmpty())
    {
        showError(tr("Load a completed output first."));
        return;
    }
    QStringList seeds = m_seeds->text().split(QRegularExpression("[,;\\s]+"), Qt::SkipEmptyParts);
    if (!prepareOnly && seeds.isEmpty())
    {
        showError(tr("Enter or select at least one node."));
        return;
    }
    if (replace && (!m_result || m_result->dataset->runId != source.id))
    {
        showError(tr("Open the saved estimate to update first."));
        return;
    }
    m_follow->setChecked(false);
    persistState();
    m_controller->run(source.id, prepareOnly ? QStringList() : seeds, m_direction->currentIndex(),
                      m_destination->text(), snapshot, replace ? m_result->id : QString());
}
void TraceAnalysisDialog::useSelection()
{
    auto *model = m_project->modelLayer();
    if (!model)
        return;
    QStringList ids;
    for (const auto &s : model->selectedElements())
    {
        if (s.kinds & SWMMModelLayer::kKindNode)
            ids << s.name;
        else if (s.kinds & SWMMModelLayer::kKindLink)
        {
            auto snapshot = Snapshot::capture(model, nullptr);
            for (const auto &l : snapshot.links)
                if (l.id == s.name)
                {
                    QStringList endpoints{snapshot.nodes[l.from].id, snapshot.nodes[l.to].id};
                    bool ok = false;
                    auto node = QInputDialog::getItem(
                        this, tr("Trace selected link"),
                        tr("Choose the seed endpoint for %1").arg(l.id), endpoints, 0, false, &ok);
                    if (ok)
                        ids << node;
                    break;
                }
        }
    }
    ids.removeDuplicates();
    if (!ids.isEmpty())
        m_seeds->setText(ids.join(", "));
}
void TraceAnalysisDialog::display(std::shared_ptr<Result> r)
{
    m_result = r;
    m_follow->setChecked(false);
    m_seeds->setText(r->dataset->snapshot.nodes[r->seed].id);
    m_direction->setCurrentIndex(r->direction);
    m_destination->setText(r->dataset->packagePath);
    m_layer = nullptr;
    for (auto *l : m_project->canvas()->layers())
        if (auto *t = qobject_cast<TraceAnalysisLayer *>(l))
            if (t->result()->id == r->id && t->result()->dataset->runId == r->dataset->runId)
            {
                if (t->result() == r)
                {
                    r = t->result();
                    m_result = r;
                    m_layer = t;
                    break;
                }
                r->style = t->savedStyle();
                int index = m_project->canvas()->layers().indexOf(t);
                delete m_project->canvas()->takeLayer(index, false);
                break;
            }
    if (!m_layer)
    {
        m_layer = new TraceAnalysisLayer(r, m_project->modelLayer()->workspace());
        if (r->style.isEmpty() && m_tabs->currentIndex() == 1)
        {
            m_layer->traceStyle()->linkColor.field = "time";
            m_layer->traceStyle()->nodeColor.field = "time";
            m_layer->traceStyle()->changed();
        }
        m_project->canvas()->addLayer(m_layer);
        connect(m_layer->traceStyle(), &TraceStyle::styleChanged, m_project,
                [p = m_project] { p->setHasChanges(true); });
    }
    const auto &s = r->summary;
    QStringList terminals;
    const QStringList names{tr("Outfall / external boundary"),
                            tr("Known loss / withdrawal"),
                            tr("Local source"),
                            tr("Dry / stagnant"),
                            tr("Unresolved terminal"),
                            tr("Trapped in closed circulation")};
    for (int i = 0; i < 6; ++i)
        terminals << tr("%1: %2% ").arg(names[i]).arg(100 * s.terminal[i], 0, 'g', 6);
    m_flow->setText(tr("<b>%1</b><br>%2<br><br>Terminal allocation<br>%3<br><br>Allocation "
                       "residual: %4; solver residual: %5.<br>Boundary inflow: %6 m³; "
                       "boundary outflow: %7 m³.<br>Lateral inflow: %8 m³; withdrawals: %9 "
                       "m³; known losses: %10 m³.<br>Storage change: %11 m³; partial sampled "
                       "balance residual: %12 m³.<br><br>%13<br>Passage ratios can exceed 1 "
                       "in circulating networks. Terminal allocation and the partial "
                       "hydraulic balance are different quantities. Unreported exchanges "
                       "prevent an exact continuity budget.")
                        .arg(r->title().toHtmlEscaped(), r->dataset->provenance.toHtmlEscaped(),
                             terminals.join("<br>"), number(s.accounting_error),
                             number(s.solver_residual), number(s.boundary_in_m3),
                             number(s.boundary_out_m3), number(s.lateral_in_m3),
                             number(s.withdrawal_m3), number(s.known_loss_m3),
                             number(s.storage_change_m3), number(s.partial_balance_residual_m3),
                             s.cyclic ? tr("Circulation detected; ratios are expected "
                                           "passage counts.")
                                      : tr("No reachable circulation detected.")));
    auto reportDate = [](double value)
    {
        return QDateTime(QDate(1899, 12, 30), QTime(0, 0), QTimeZone::UTC)
            .addMSecs(qRound64(value * 86400000.))
            .toString("yyyy-MM-dd HH:mm:ss");
    };
    m_flow->setText(m_flow->text() + tr("<br><br>Reported coverage: %1 to %2 (simulation clock).")
                                         .arg(reportDate(r->dataset->info.first_report_date),
                                              reportDate(r->dataset->info.last_report_date)));
    QStringList limitations;
    if (r->dataset->snapshot.wkt.isEmpty())
        limitations << tr("CRS unavailable: geometry is shown without reprojection.");
    int uncertain = 0, reversing = 0, excluded = 0;
    for (const auto &node : r->dataset->snapshot.nodes)
        if (node.flags)
            ++uncertain;
    for (const auto &link : r->dataset->links)
    {
        if (link.flags & SWMM_TRACE_REVERSAL)
            ++reversing;
        if (link.flags & SWMM_TRACE_NO_DIRECTION)
            ++excluded;
    }
    if (uncertain)
        limitations << tr("%1 nodes have ponding, routed boundaries, coupling, or unreported "
                          "losses; interpret the balance as partial.")
                           .arg(uncertain);
    if (reversing || excluded)
        limitations << tr("%1 reversing links; %2 links excluded for negligible mean net "
                          "direction. Their average magnitudes and flags remain in the package.")
                           .arg(reversing)
                           .arg(excluded);
    if (!limitations.isEmpty())
        m_flow->setText(m_flow->text() + "<br><br>" + limitations.join("<br>"));
    m_time->setText(tr("<b>Expected travel times over average hydraulics</b><br><br>Report "
                       "window: %1 hours; %2 saved periods.<br>Conduit delay = length / "
                       "average absolute velocity.<br>Storage delay = average volume / "
                       "outgoing rate including known withdrawals.<br>Non-conduit controls have zero modeled delay. "
                       "Unresolved storage exchanges may have unavailable residence time.<br><br>Nodes and "
                       "Links show expected time and its coverage. Coverage below 100% means "
                       "only the known-time paths contribute to that displayed time. Missing "
                       "times remain unavailable. Closed circulation is shown as trapped; "
                       "travel time there is undefined.<br><br>Link taper and arrows follow "
                       "average hydraulic direction, including for upstream analysis. "
                       "Appearance controls color and size independently.")
                        .arg(number(r->dataset->info.duration_s / 3600.))
                        .arg(r->dataset->info.periods));
    for (bool node : {true, false})
    {
        auto *t = node ? m_nodes : m_links;
        t->setProperty("traceProvenance",
                       QStringList{r->dataset->runId, r->id, r->dataset->outputPath,
                                   r->dataset->snapshot.nodes[r->seed].id,
                                   r->direction == 0 ? "Downstream" : "Upstream"});
        QSignalBlocker blocker(t);
        t->setSortingEnabled(false);
        t->clear();
        t->setColumnCount(9);
        t->setHorizontalHeaderLabels(
            {tr("ID"), tr("Passage ratio"),
             node ? tr("Mean outgoing flow m³/s") : tr("Mean net magnitude m³/s"),
             tr("Expected time min"), tr("Coverage %"), tr("Local delay min"),
             tr("Terminal fraction"), tr("Status"), tr("Index")});
        const auto &values = node ? r->nodes : r->links;
        int count = 0;
        for (const auto &v : values)
            if (v.ratio != 0)
                ++count;
        t->setRowCount(count);
        int row = 0;
        for (int i = 0; i < values.size(); ++i)
        {
            const auto &v = values[i];
            if (v.ratio == 0)
                continue;
            auto *id = new QTableWidgetItem(node ? r->dataset->snapshot.nodes[i].id
                                                 : r->dataset->snapshot.links[i].id);
            id->setData(Qt::UserRole, i);
            t->setItem(row, 0, id);
            t->setItem(row, 1, new NumberItem(v.ratio));
            t->setItem(row, 2, new NumberItem(m_layer->value(node, i, "flow")));
            t->setItem(row, 3, new NumberItem(v.time_s / 60.));
            t->setItem(row, 4, new NumberItem(100 * v.time_coverage));
            t->setItem(row, 5, new NumberItem(m_layer->value(node, i, "local")));
            t->setItem(row, 6, new NumberItem(v.terminal_fraction));
            t->setItem(row, 7, new QTableWidgetItem(statusText(v.flags)));
            t->setItem(row, 8, new NumberItem(i));
            ++row;
        }
        t->setColumnHidden(8, true);
        t->setSortingEnabled(true);
        t->resizeColumnsToContents();
    }
    m_status->setText(r->saved
                          ? tr("Saved · %1 · %2").arg(r->dataset->label, r->dataset->packagePath)
                          : tr("Computed but not saved. Use Retry save after "
                               "resolving the storage error."));
    m_update->setEnabled(!m_controller->busy() && r->saved);
    refreshSaved();
    updateAppearance();
    persistState();
    m_project->setHasChanges(true);
}
void TraceAnalysisDialog::updateAppearance()
{
    while (m_appearanceLayout->rowCount())
        m_appearanceLayout->removeRow(0);
    if (!m_layer)
    {
        m_appearanceLayout->addRow(
            new QLabel(tr("Open or estimate an analysis to change its appearance.")));
        return;
    }
    auto *style = m_layer->traceStyle();
    const QStringList fields{"ratio", "flow", "gross", "time", "local", "coverage", "uniform"};
    auto channel = [&](const QString &title, Channel *value, bool size, bool node = false)
    {
        auto *box = new QWidget;
        auto *form = new QFormLayout(box);
        auto *field = new QComboBox;
        for (auto f : fields)
            field->addItem(fieldLabel(f, node), f);
        if (node)
            field->addItem(fieldLabel("volume", true), "volume");
        field->setCurrentIndex(field->findData(value->field));
        form->addRow(tr("Value"), field);
        auto *transform = new QComboBox;
        transform->addItem(tr("Linear"), "linear");
        transform->addItem(tr("Square root"), "sqrt");
        transform->addItem(tr("Logarithmic (log1p)"), "log");
        transform->setCurrentIndex(transform->findData(value->transform));
        form->addRow(tr("Scale"), transform);
        auto *automatic = new QCheckBox(tr("Automatic range"));
        automatic->setChecked(value->automatic);
        form->addRow(automatic);
        auto *minimum = new QDoubleSpinBox;
        auto *maximum = new QDoubleSpinBox;
        for (auto *w : {minimum, maximum})
        {
            w->setDecimals(6);
            w->setRange(-1e12, 1e12);
            w->setEnabled(!value->automatic);
        }
        minimum->setValue(value->minimum);
        maximum->setValue(value->maximum);
        form->addRow(tr("Range minimum"), minimum);
        form->addRow(tr("Range maximum"), maximum);
        connect(field, &QComboBox::currentIndexChanged, style,
                [style, value, field]
                {
                    value->field = field->currentData().toString();
                    style->changed();
                });
        connect(transform, &QComboBox::currentIndexChanged, style,
                [style, value, transform]
                {
                    value->transform = transform->currentData().toString();
                    style->changed();
                });
        connect(automatic, &QCheckBox::toggled, style,
                [=](bool b)
                {
                    value->automatic = b;
                    minimum->setEnabled(!b);
                    maximum->setEnabled(!b);
                    style->changed();
                });
        connect(minimum, &QDoubleSpinBox::valueChanged, style,
                [=](double v)
                {
                    value->minimum = v;
                    maximum->setMinimum(v);
                    style->changed();
                });
        connect(maximum, &QDoubleSpinBox::valueChanged, style,
                [=](double v)
                {
                    value->maximum = v;
                    minimum->setMaximum(v);
                    style->changed();
                });
        if (size)
        {
            for (bool low : {true, false})
            {
                auto *spin = new QDoubleSpinBox;
                spin->setRange(.5, 80);
                spin->setValue(low ? value->minimumSize : value->maximumSize);
                form->addRow(low ? tr("Minimum pixels") : tr("Maximum pixels"), spin);
                connect(spin, &QDoubleSpinBox::valueChanged, style,
                        [=](double v)
                        {
                            if (low)
                                value->minimumSize = std::min(v, value->maximumSize);
                            else
                                value->maximumSize = std::max(v, value->minimumSize);
                            style->changed();
                        });
            }
        }
        else
        {
            auto *ramp = new ColorRampComboBox;
            ramp->ensureRampSelected(tr("Analysis ramp"), value->ramp);
            form->addRow(tr("Color ramp"), ramp);
            connect(ramp, &ColorRampComboBox::rampChanged, style,
                    [style, value](const RasterColorRamp &r)
                    {
                        value->ramp = r;
                        style->changed();
                    });
            auto *classes = new QComboBox;
            classes->addItem(tr("Continuous"), 0);
            for (int i = 2; i <= 10; ++i)
                classes->addItem(tr("%1 equal intervals").arg(i), i);
            classes->setCurrentIndex(std::max(0, classes->findData(value->classes)));
            form->addRow(tr("Classification"), classes);
            connect(classes, &QComboBox::currentIndexChanged, style,
                    [style, value, classes]
                    {
                        value->classes = classes->currentData().toInt();
                        style->changed();
                    });
        }
        m_appearanceLayout->addRow(title, box);
    };
    channel(tr("Link color"), &style->linkColor, false);
    channel(tr("Link width"), &style->linkWidth, true);
    channel(tr("Node color"), &style->nodeColor, false, true);
    channel(tr("Node area"), &style->nodeSize, true, true);
    auto *taper = new QDoubleSpinBox;
    taper->setRange(.05, 1);
    taper->setSingleStep(.05);
    taper->setValue(style->taper());
    m_appearanceLayout->addRow(tr("Downstream / upstream width"), taper);
    connect(taper, &QDoubleSpinBox::valueChanged, style, &TraceStyle::setTaper);
    for (auto key : {QString("arrows"), QString("labels")})
    {
        auto *check = new QCheckBox(key == "arrows" ? tr("Flow arrows") : tr("Node labels"));
        check->setChecked(key == "arrows" ? style->arrows() : style->labels());
        m_appearanceLayout->addRow(check);
        connect(check, &QCheckBox::toggled, style,
                [=](bool b)
                {
                    if (key == "arrows")
                        style->setArrows(b);
                    else
                        style->setLabels(b);
                });
    }
    auto *save = new QPushButton(tr("Save as analysis default style"));
    m_appearanceLayout->addRow(save);
    connect(save, &QPushButton::clicked, this,
            [this]
            {
                if (!m_layer || m_controller->busy())
                    return;
                QString error;
                if (!AnalysisStore::saveStyle(m_result->dataset->packagePath, m_result->id,
                                              m_layer->savedStyle(), &error))
                    showError(error);
                else
                    m_status->setText(tr("Default style saved in the analysis package."));
            });
}
void TraceAnalysisDialog::loadOutput()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Load completed SWMM output"), {},
                                                "SWMM output (*.out)");
    if (path.isEmpty())
        return;
    auto *l =
        new SWMMResultsLayer(path, m_project->modelLayer(), m_project->modelLayer()->workspace());
    QList<QString> warnings, errors;
    if (!l->openResults(warnings, errors))
    {
        delete l;
        showError(tr("Cannot open the output file."));
        return;
    }
    m_project->canvas()->addLayer(l);
    m_project->setActiveResultsLayer(l);
    m_follow->setChecked(true);
    followActive();
}
void TraceAnalysisDialog::loadPackage()
{
    QString path =
        QFileDialog::getOpenFileName(this, tr("Open saved analysis"), {}, "GeoPackage (*.gpkg)");
    if (path.isEmpty())
        return;
    QString error;
    auto d = AnalysisStore::readDataset(path, &error);
    if (!d)
    {
        showError(error);
        return;
    }
    m_project->statsRegistry()->adoptAnalysis(d->runId, d->sourceId, d->outputPath, d->label,
                                              d->fingerprint, path, d->snapshot.toJson());
    m_follow->setChecked(false);
    refreshSources();
    m_sources->setCurrentIndex(m_sources->findData(d->runId));
    sourceChanged();
    m_destination->setText(path);
    refreshSaved();
    if (m_saved->count() > 1)
    {
        m_saved->setCurrentIndex(1);
        auto r = AnalysisStore::read(path, m_saved->currentData().toString(), &error);
        if (r)
            display(r);
        else
            showError(error);
    }
    else
        m_status->setText(tr("Averages opened. Enter any node ID to estimate "
                             "without the original output."));
}
void TraceAnalysisDialog::setSeedNodes(const QStringList &nodes)
{
    m_seeds->setText(nodes.join(", "));
    persistState();
}
QJsonObject TraceAnalysisDialog::state() const
{
    return {{"runId", m_sources->currentData().toString()},
            {"analysisId", m_result ? m_result->id : QString()},
            {"follow", m_follow->isChecked()},
            {"direction", m_direction->currentIndex()},
            {"nodes", m_seeds->text()},
            {"tab", m_tabs->currentIndex()}};
}
void TraceAnalysisDialog::persistState() { m_project->setProperty("tracePanelState", state()); }
void TraceAnalysisDialog::restore(const QJsonObject &j)
{
    if (j.isEmpty())
        return;
    m_follow->setChecked(j.value("follow").toBool(true));
    m_sources->setCurrentIndex(m_sources->findData(j.value("runId").toString()));
    m_direction->setCurrentIndex(j.value("direction").toInt());
    m_seeds->setText(j.value("nodes").toString());
    m_tabs->setCurrentIndex(std::clamp(j.value("tab").toInt(), 0, 4));
}
bool TraceAnalysisDialog::eventFilter(QObject *obj, QEvent *e)
{
    if (!isVisible() || (!m_picking && !m_inspecting) || e->type() != QEvent::MouseButtonPress)
        return QDialog::eventFilter(obj, e);
    auto *mouse = static_cast<QMouseEvent *>(e);
    if (mouse->button() != Qt::LeftButton)
        return false;
    auto *w = qobject_cast<QWidget *>(obj);
    if (!w)
        return false;
    QPoint click = m_project->canvas()->mapFromGlobal(w->mapToGlobal(mouse->position().toPoint()));
    if (m_inspecting && m_layer)
    {
        auto *canvas = m_project->canvas();
        auto extent = canvas->extent();
        if (!extent.isValid())
            return true;
        double sx = canvas->width() / extent.width(), sy = canvas->height() / extent.height();
        QTransform transform(sx, 0, 0, -sy, -extent.xMin() * sx, extent.yMax() * sy);
        bool node;
        int index;
        if (m_layer->hitTest(click, transform, canvas->canvasSRS(), &node, &index))
        {
            auto *table = node ? m_nodes : m_links;
            for (int row = 0; row < table->rowCount(); ++row)
                if (table->item(row, 0)->data(Qt::UserRole).toInt() == index)
                {
                    table->setRowHidden(row, false);
                    table->setCurrentCell(row, 0);
                    table->scrollToItem(table->item(row, 0));
                    m_tabs->setCurrentIndex(node ? 2 : 3);
                    break;
                }
        }
        return true;
    }
    auto run = m_project->statsRegistry()->run(m_sources->currentData().toString());
    Snapshot snapshot = run.snapshot.isEmpty() ? Snapshot::capture(m_project->modelLayer(), nullptr)
                                               : Snapshot::fromJson(run.snapshot);
    std::unique_ptr<SpatialReferenceSystem> crs(
        SpatialReferenceSystem::fromWktOrProj(snapshot.wkt));
    auto *canvas = m_project->canvas();
    OGRCoordinateTransformation *ct =
        crs && canvas->canvasSRS() ? crs->createTransformationTo(*canvas->canvasSRS()) : nullptr;
    if (crs && canvas->canvasSRS() && !crs->equals(*canvas->canvasSRS()) && !ct)
    {
        showError(tr("Cannot transform this analysis into the map CRS."));
        return true;
    }
    double best = 14;
    QString found;
    for (const auto &n : snapshot.nodes)
        if (n.hasGeometry)
        {
            double x = n.point.x(), y = n.point.y();
            if (ct && !ct->Transform(1, &x, &y))
                continue;
            int px, py;
            canvas->toPixelCoords(x, y, px, py);
            double distance = QLineF(click, QPointF(px, py)).length();
            if (distance < best)
            {
                best = distance;
                found = n.id;
            }
        }
    if (ct)
        OCTDestroyCoordinateTransformation(ct);
    if (!found.isEmpty())
    {
        bool add = mouse->modifiers().testFlag(Qt::ShiftModifier);
        m_seeds->setText(add && !m_seeds->text().isEmpty() ? m_seeds->text() + ", " + found
                                                           : found);
        if (!add)
            m_pick->setChecked(false);
        m_status->setText(tr("Selected node %1 from this run's geometry.").arg(found));
    }
    return true;
}
} // namespace openswmmvis::trace
