/**
 * @file      tabularresultsdialog.cpp
 * @brief     Two view modes: - **By object** — pick an object kind + a variable; one row per
 *            object, one column per report time. - **By variable** — pick an object; one row per
 *            report time, one column per variable of that kind. V6: every value is an IRunLayer
 *            series of a ResultDescriptor (species, loads, 2D variables and 2D regions included).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/tabularresultsdialog.h"

#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "plot/mesh2drunlayer.h"
#include "plot/pollutantload.h"
#include "plot/swmmoutrunlayer.h"
#include "ui/theme/iconfactory.h"
#include "ui/util/tableclipboard.h"

#include <openswmm/engine/openswmm_output.h>

#include <QComboBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QStandardItemModel>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace openswmmvis::ui
{

using plot::ObjectRef;
using plot::ResultDescriptor;
using plot::SeriesData;
using plot::SeriesRequest;

namespace
{
ObjectRef::Kind objectKind(int kind)
{
    switch (kind) {
    case TabularResultsDialog::Nodes:
        return ObjectRef::Kind::Node;
    case TabularResultsDialog::Links:
        return ObjectRef::Kind::Link;
    case TabularResultsDialog::Subcatchments:
        return ObjectRef::Kind::Subcatch;
    case TabularResultsDialog::System:
        return ObjectRef::Kind::System;
    case TabularResultsDialog::Cells2D:
    case TabularResultsDialog::Regions2D:
        return ObjectRef::Kind::Mesh2DCell;
    default:
        return ObjectRef::Kind::Unknown;
    }
}

bool is1D(int kind)
{
    return kind >= TabularResultsDialog::Nodes && kind <= TabularResultsDialog::System;
}
} // namespace

TabularResultsDialog::TabularResultsDialog(SWMMResultsLayer *layer, QWidget *parent)
    : TabularResultsDialog(layer, nullptr, parent)
{
}

TabularResultsDialog::TabularResultsDialog(SWMMResultsLayer *layer, SWMM2DResultsLayer *mesh,
                                           QWidget *parent)
    : QDialog(parent), m_layer(layer), m_mesh(mesh)
{
    setWindowTitle(tr("Tabular Results"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("TabularResultsDialog"));
    resize(1080, 640);
    if (layer)
        m_run1D = std::make_unique<plot::SwmmOutRunLayer>(layer);
    if (mesh)
        m_run2D = std::make_unique<plot::Mesh2DRunLayer>(mesh);
    buildUi();
    onModeChanged();
}

TabularResultsDialog::~TabularResultsDialog() = default;

void TabularResultsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Mode + selectors row.
    auto *modeGrp = new QGroupBox(tr("View"), this);
    auto *modeRow = new QHBoxLayout(modeGrp);
    m_byObjectRadio = new QRadioButton(tr("By object"), modeGrp);
    m_byVariableRadio = new QRadioButton(tr("By variable"), modeGrp);
    m_byObjectRadio->setChecked(true);

    m_kindCombo = new QComboBox(modeGrp);
    m_kindCombo->setObjectName(QStringLiteral("tabularKind"));
    if (m_layer) {
        m_kindCombo->addItem(tr("Nodes"), int(Nodes));
        m_kindCombo->addItem(tr("Links"), int(Links));
        m_kindCombo->addItem(tr("Subcatchments"), int(Subcatchments));
        m_kindCombo->addItem(tr("System"), int(System));
    }
    if (m_mesh) {
        m_kindCombo->addItem(tr("2D cells (map selection)"), int(Cells2D));
        m_kindCombo->addItem(tr("2D regions (selection, tags, domain)"), int(Regions2D));
    }

    m_varOrObjCombo = new QComboBox(modeGrp);
    m_varOrObjCombo->setObjectName(QStringLiteral("tabularVariableOrObject"));
    m_varOrObjCombo->setMinimumWidth(260);

    m_aggregateLabel = new QLabel(tr("Aggregate:"), modeGrp);
    m_aggregateCombo = new QComboBox(modeGrp);
    m_aggregateCombo->setObjectName(QStringLiteral("tabularAggregate"));
    for (auto a : {results::MeshAggregate::AreaWeightedMean, results::MeshAggregate::Mean,
                   results::MeshAggregate::Sum, results::MeshAggregate::Mass})
        m_aggregateCombo->addItem(results::aggregateLabel(a), int(a));
    m_aggregateCombo->setToolTip(
        tr("How the cells of a region combine at each report time. Mass = concentration x "
           "water volume (depth x cell area); offered for surface species concentrations. "
           "Missing cells are left out, never counted as zero."));

    modeRow->addWidget(m_byObjectRadio);
    modeRow->addWidget(m_byVariableRadio);
    modeRow->addSpacing(20);
    modeRow->addWidget(new QLabel(tr("Kind:"), modeGrp));
    modeRow->addWidget(m_kindCombo);
    modeRow->addWidget(new QLabel(tr("Variable / Object:"), modeGrp));
    modeRow->addWidget(m_varOrObjCombo, 1);
    modeRow->addWidget(m_aggregateLabel);
    modeRow->addWidget(m_aggregateCombo);
    root->addWidget(modeGrp);

    // Table.
    m_model = new QStandardItemModel(this);
    m_tableView = new QTableView(this);
    openswmmvis::ui::TableClipboard::install(m_tableView, {.copyOnly = true});
    m_tableView->setModel(m_model);
    m_tableView->setSortingEnabled(true);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    root->addWidget(m_tableView, 1);

    // Buttons.
    auto *btnRow = new QHBoxLayout;
    auto *csvBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportCsv")),
                                   tr("Export CSV…"), this);
    auto *tsvBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportCsv")),
                                   tr("Export TSV…"), this);
    csvBtn->setToolTip(tr("Header, a Units row, then the rows; missing values are blank (not "
                          "zero) and the Status column says what is missing."));
    tsvBtn->setToolTip(csvBtn->toolTip());
    auto *closeBtn = new QPushButton(tr("Close"), this);
    btnRow->addWidget(csvBtn);
    btnRow->addWidget(tsvBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);

    connect(m_byObjectRadio, &QRadioButton::toggled, this, &TabularResultsDialog::onModeChanged);
    connect(m_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TabularResultsDialog::onModeChanged);
    connect(m_aggregateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TabularResultsDialog::onModeChanged);
    connect(m_varOrObjCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &TabularResultsDialog::onSelectionChanged);
    connect(csvBtn, &QPushButton::clicked, this, &TabularResultsDialog::onExportCsvClicked);
    connect(tsvBtn, &QPushButton::clicked, this, &TabularResultsDialog::onExportTsvClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void TabularResultsDialog::setCellTags(const QStringList &tags)
{
    m_cellTags = tags;
    onModeChanged();
}

results::MeshAggregate TabularResultsDialog::currentAggregate() const
{
    return static_cast<results::MeshAggregate>(m_aggregateCombo->currentData().toInt());
}

std::vector<int> TabularResultsDialog::selectedCells() const
{
    // The map's selection, mapped exactly as the 2D style panel's
    // selected-cell table maps it (display triangle -> engine cell).
    std::set<int> cells;
    if (m_mesh) {
        const auto &triCell = m_mesh->triCellMap();
        for (int triangle : m_mesh->highlightedCells())
            if (triangle >= 0 && std::size_t(triangle) < triCell.size() &&
                triCell[std::size_t(triangle)] >= 0)
                cells.insert(triCell[std::size_t(triangle)]);
    }
    return {cells.begin(), cells.end()};
}

QVector<results::MeshRegion> TabularResultsDialog::regions() const
{
    QVector<results::MeshRegion> out;
    const auto *src = m_mesh ? m_mesh->source() : nullptr;
    const int cells = src ? src->triangleCount() : 0;
    if (cells <= 0)
        return out;
    const auto selection = selectedCells();
    if (!selection.empty())
        out.append({tr("Selection"), selection});
    if (m_cellTags.size() == cells)
        out += results::tagRegions(m_cellTags);
    out.append(results::domainRegion(cells));
    for (auto &r : out)
        r.label = tr("%1 (%n cells)", nullptr, int(r.cells.size())).arg(r.label);
    return out;
}

QVector<ResultDescriptor> TabularResultsDialog::descriptorsFor(int kind) const
{
    QVector<ResultDescriptor> out;
    if (is1D(kind)) {
        if (m_run1D)
            out = m_run1D->resultDescriptorsForKind(objectKind(kind));
        return out;
    }
    if (!m_run2D)
        return out;
    const auto cellDescriptors = m_run2D->resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell);
    for (const ResultDescriptor &d : cellDescriptors) {
        if (!d.isMeshVariable() && !m_run2D->supportsAttribute(d.attr))
            continue;
        if (kind == Regions2D) {
            // Regions aggregate the depth and the catalog variables.
            if (!d.isMeshVariable() && d.attr != plot::PlotAttribute::Mesh2DDepth)
                continue;
            if (currentAggregate() == results::MeshAggregate::Mass && !m_run2D->massAvailable(d))
                continue;
        }
        out.append(d);
    }
    return out;
}

QVector<TabularResultsDialog::Row> TabularResultsDialog::rowsFor(int kind) const
{
    QVector<Row> rows;
    switch (kind) {
    case Nodes:
    case Links:
    case Subcatchments: {
        SWMM_Output h = m_layer ? m_layer->outputHandle() : nullptr;
        if (!h)
            break;
        const int n = kind == Nodes   ? swmm_output_get_node_count(h)
                      : kind == Links ? swmm_output_get_link_count(h)
                                      : swmm_output_get_subcatch_count(h);
        for (int i = 0; i < n; ++i) {
            const char *id = kind == Nodes   ? swmm_output_get_node_id(h, i)
                             : kind == Links ? swmm_output_get_link_id(h, i)
                                             : swmm_output_get_subcatch_id(h, i);
            const QString name = QString::fromUtf8(id);
            rows.append({name, ObjectRef(objectKind(kind), name), -1});
        }
        break;
    }
    case System:
        if (m_layer && m_layer->outputHandle())
            rows.append({tr("System"), ObjectRef::forSystem(), -1});
        break;
    case Cells2D:
        for (int c : selectedCells())
            rows.append({tr("Cell %1").arg(c), ObjectRef::forMesh2DCell(c), -1});
        break;
    case Regions2D: {
        const auto all = regions();
        for (int r = 0; r < all.size(); ++r)
            rows.append({all[r].label, ObjectRef{}, r});
        break;
    }
    default:
        break;
    }
    return rows;
}

QString TabularResultsDialog::headerFor(const ResultDescriptor &d) const
{
    return d.label();
}

QString TabularResultsDialog::unitsFor(const ResultDescriptor &d, int kind) const
{
    if (kind == Regions2D)
        return m_run2D ? m_run2D->regionUnits(d, currentAggregate()) : QString();
    if (is1D(kind)) {
        if (!m_run1D)
            return {};
        // .out flow columns are in the run's flow units (LPS, MGD, ...).
        using PA = plot::PlotAttribute;
        static const QSet<PA> flows{PA::NodeLateralInflow, PA::NodeTotalInflow,
                                    PA::NodeOverflow,      PA::LinkFlow,
                                    PA::SubcatchRunoff,    PA::SubcatchGwFlow,
                                    PA::SystemRunoff,      PA::SystemDwInflow,
                                    PA::SystemGwInflow,    PA::SystemRdiiInflow,
                                    PA::SystemExtInflow,   PA::SystemLatInflow,
                                    PA::SystemFlooding,    PA::SystemOutflow};
        const QString flow =
            plot::pollutantload::flowUnitsName(m_layer ? m_layer->flowUnits() : -1);
        if (!d.isSpecies() && !d.isMeshVariable() && flows.contains(d.attr) && !flow.isEmpty())
            return flow;
        return d.unitLabel(m_run1D->unitSystem());
    }
    return d.unitLabel(plot::UnitSystem::SI);
}

QVector<SeriesData> TabularResultsDialog::fetch(int kind, const QVector<Row> &rows,
                                                const ResultDescriptor &d) const
{
    QVector<SeriesData> out;
    if (kind == Regions2D) {
        if (!m_run2D)
            return QVector<SeriesData>(rows.size());
        const auto all = regions();
        QVector<results::MeshRegion> wanted;
        for (const Row &r : rows)
            wanted.append(r.region >= 0 && r.region < all.size() ? all[r.region]
                                                                 : results::MeshRegion{});
        m_run2D->getRegionSeries(wanted, d, currentAggregate(), out);
        return out;
    }
    const plot::IRunLayer *run = is1D(kind) ? static_cast<const plot::IRunLayer *>(m_run1D.get())
                                            : static_cast<const plot::IRunLayer *>(m_run2D.get());
    if (!run)
        return QVector<SeriesData>(rows.size());
    QVector<SeriesRequest> requests;
    for (const Row &r : rows)
        requests.append({r.ref, d, 0});
    run->getSeriesBatch(requests, out);
    return out;
}

void TabularResultsDialog::onModeChanged()
{
    const int kind = m_kindCombo->currentData().toInt();
    const bool regions2D = kind == Regions2D;
    m_aggregateLabel->setVisible(regions2D);
    m_aggregateCombo->setVisible(regions2D);

    m_varOrObjCombo->blockSignals(true);
    m_varOrObjCombo->clear();
    m_comboDescriptors.clear();
    m_comboRows.clear();
    if (m_kindCombo->count() > 0) {
        if (m_byObjectRadio->isChecked()) {
            m_comboDescriptors = descriptorsFor(kind);
            for (const ResultDescriptor &d : std::as_const(m_comboDescriptors)) {
                const QString units = unitsFor(d, kind);
                m_varOrObjCombo->addItem(units.isEmpty()
                                             ? headerFor(d)
                                             : QStringLiteral("%1 (%2)").arg(headerFor(d), units));
                if (!d.variableNote.isEmpty()) // e.g. loads: report-step, not the .rpt total
                    m_varOrObjCombo->setItemData(m_varOrObjCombo->count() - 1, d.variableNote,
                                                 Qt::ToolTipRole);
            }
        } else {
            m_comboRows = rowsFor(kind);
            for (const Row &r : std::as_const(m_comboRows))
                m_varOrObjCombo->addItem(r.label);
        }
    }
    m_varOrObjCombo->blockSignals(false);
    onSelectionChanged();
}

void TabularResultsDialog::onSelectionChanged()
{
    if (m_byObjectRadio->isChecked())
        rebuildByObject();
    else
        rebuildByVariable();
}

void TabularResultsDialog::rebuildByObject()
{
    const int kind = m_kindCombo->currentData().toInt();
    const int index = m_varOrObjCombo->currentIndex();
    m_table = {};
    if (index >= 0 && index < m_comboDescriptors.size()) {
        const ResultDescriptor &d = m_comboDescriptors[index];
        const QVector<Row> rows = rowsFor(kind);
        QStringList labels;
        for (const Row &r : rows)
            labels.append(r.label);
        m_table = results::tableByObject(labels, fetch(kind, rows, d), unitsFor(d, kind),
                                         d.variableNote);
    }
    results::fillItemModel(m_table, *m_model);
}

void TabularResultsDialog::rebuildByVariable()
{
    const int kind = m_kindCombo->currentData().toInt();
    const int index = m_varOrObjCombo->currentIndex();
    m_table = {};
    if (index >= 0 && index < m_comboRows.size()) {
        const Row &row = m_comboRows[index];
        const QVector<ResultDescriptor> descriptors = descriptorsFor(kind);
        QStringList headers, units, notes;
        QVector<SeriesData> series;
        if (kind == Regions2D) {
            for (const ResultDescriptor &d : descriptors) {
                headers.append(headerFor(d));
                units.append(unitsFor(d, kind));
                notes.append(d.variableNote);
                series.append(fetch(kind, {row}, d).value(0));
            }
        } else if (const plot::IRunLayer *run =
                       is1D(kind) ? static_cast<const plot::IRunLayer *>(m_run1D.get())
                                  : static_cast<const plot::IRunLayer *>(m_run2D.get())) {
            QVector<SeriesRequest> requests;
            for (const ResultDescriptor &d : descriptors) {
                headers.append(headerFor(d));
                units.append(unitsFor(d, kind));
                notes.append(d.variableNote);
                requests.append({row.ref, d, 0});
            }
            run->getSeriesBatch(requests, series);
        }
        m_table = results::tableByVariable(headers, units, series, notes);
    }
    results::fillItemModel(m_table, *m_model);
}

bool TabularResultsDialog::exportTo(const QString &path, QChar delimiter, QString *error) const
{
    return results::writeDelimited(m_table, path, delimiter, error);
}

void TabularResultsDialog::exportDelimited(const QString &path, QChar delim)
{
    QString error;
    if (!exportTo(path, delim, &error))
        QMessageBox::warning(this, tr("Export failed"), tr("Cannot export %1: %2").arg(path, error));
}

void TabularResultsDialog::onExportCsvClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export CSV"), QStringLiteral("results.csv"), tr("CSV (*.csv)"));
    if (path.isEmpty())
        return;
    exportDelimited(path, QLatin1Char(','));
}

void TabularResultsDialog::onExportTsvClicked()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export TSV"), QStringLiteral("results.tsv"), tr("TSV (*.tsv)"));
    if (path.isEmpty())
        return;
    exportDelimited(path, QLatin1Char('\t'));
}

} // namespace openswmmvis::ui
