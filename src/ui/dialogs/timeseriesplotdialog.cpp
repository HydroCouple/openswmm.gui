/*!
 * \file   timeseriesplotdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/timeseriesplotdialog.h"

#include "plot/seriesstyleobject.h"
#include "ui/widgets/seriesstyleeditor.h"

#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QListView>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QValueAxis>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_output.h>

using openswmmvis::plot::SeriesStyle;
using openswmmvis::plot::SeriesStyleObject;
using openswmmvis::ui::SeriesStyleEditor;

// Qt 6 puts charts classes in the global namespace.

namespace {

struct VarOption {
    QString label;
    int     code;   // SWMM_OUT_*_* enum value
};

QList<VarOption> variablesFor(SWMMObjectRef::ObjectType t)
{
    using L = SWMMObjectRef;
    switch (t)
    {
    case L::Node:
        return {
            {QStringLiteral("Depth"),          0},
            {QStringLiteral("Head"),           1},
            {QStringLiteral("Volume"),         2},
            {QStringLiteral("Lateral inflow"), 3},
            {QStringLiteral("Total inflow"),   4},
            {QStringLiteral("Overflow"),       5},
        };
    case L::Link:
        return {
            {QStringLiteral("Flow"),     0},
            {QStringLiteral("Depth"),    1},
            {QStringLiteral("Velocity"), 2},
            {QStringLiteral("Volume"),   3},
            {QStringLiteral("Capacity"), 4},
        };
    case L::Subcatchment:
        return {
            {QStringLiteral("Rainfall"),     0},
            {QStringLiteral("Snow depth"),   1},
            {QStringLiteral("Evaporation"),  2},
            {QStringLiteral("Infiltration"), 3},
            {QStringLiteral("Runoff"),       4},
        };
    default:
        return {};
    }
}

QString labelForCode(SWMMObjectRef::ObjectType t, int code)
{
    for (const auto& v : variablesFor(t))
        if (v.code == code) return v.label;
    return QStringLiteral("var %1").arg(code);
}

int objectIndexInOut(SWMM_Output out, const SWMMObjectRef& ref)
{
    const QByteArray want = ref.name.toUtf8();
    auto findBy = [&](int n, auto getId) {
        for (int i = 0; i < n; ++i)
            if (qstrcmp(getId(i), want.constData()) == 0) return i;
        return -1;
    };
    switch (ref.objectType) {
    case SWMMObjectRef::Node:
        return findBy(swmm_output_get_node_count(out),
                      [&](int i){ return swmm_output_get_node_id(out, i); });
    case SWMMObjectRef::Link:
        return findBy(swmm_output_get_link_count(out),
                      [&](int i){ return swmm_output_get_link_id(out, i); });
    case SWMMObjectRef::Subcatchment:
        return findBy(swmm_output_get_subcatch_count(out),
                      [&](int i){ return swmm_output_get_subcatch_id(out, i); });
    default:
        return -1;
    }
}

bool readSeriesFromOut(SWMM_Output out, const SWMMObjectRef& ref,
                      int variableCode, int periods,
                      QVector<float>& outValues)
{
    const int idx = objectIndexInOut(out, ref);
    if (idx < 0) return false;
    outValues.resize(periods);
    int rc = -1;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:
        rc = swmm_output_get_node_series(out, idx, variableCode,
                                         0, periods - 1, outValues.data());
        break;
    case SWMMObjectRef::Link:
        rc = swmm_output_get_link_series(out, idx, variableCode,
                                         0, periods - 1, outValues.data());
        break;
    case SWMMObjectRef::Subcatchment:
        rc = swmm_output_get_subcatch_series(out, idx, variableCode,
                                             0, periods - 1, outValues.data());
        break;
    default: break;
    }
    return rc == 0;
}

QPixmap swatchPixmap(const QColor& c)
{
    QPixmap pm(16, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(0, 0, 0, 80), 1));
    p.setBrush(c.isValid() ? c : QColor(160, 160, 160));
    p.drawRoundedRect(QRectF(1.5, 1.5, 13, 13), 3, 3);
    return pm;
}

} // anonymous

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TimeSeriesPlotDialog::TimeSeriesPlotDialog(const QString &outPath,
                                           const SWMMObjectRef &obj,
                                           QWidget *parent)
    : QDialog(parent),
      m_outPath(outPath)
{
    setWindowTitle(tr("Time Series — %1").arg(obj.name));
    resize(1100, 600);
    buildUi();

    // Seed with one series using the first variable for the object's class.
    const auto vars = variablesFor(obj.objectType);
    if (!vars.isEmpty())
        addSeries(obj, vars.first().code);
}

TimeSeriesPlotDialog::~TimeSeriesPlotDialog() = default;

void TimeSeriesPlotDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Header --------------------------------------------------------------
    m_titleLabel = new QLabel(this);
    m_titleLabel->setText(tr("<b>Time Series Plot</b>  (from %1)")
                              .arg(m_outPath.toHtmlEscaped()));
    m_titleLabel->setTextFormat(Qt::RichText);
    m_titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_titleLabel->setWordWrap(true);
    root->addWidget(m_titleLabel);

    // Splitter: series list | chart | style editor ------------------------
    m_splitter = new QSplitter(Qt::Horizontal, this);

    // ---- Left pane: series list + Add/Remove ---------------------------
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLayout = new QVBoxLayout(leftPane);
    leftLayout->setContentsMargins(0, 0, 0, 0);

    m_seriesList = new QListView(leftPane);
    m_seriesModel = new QStandardItemModel(m_seriesList);
    m_seriesList->setModel(m_seriesModel);
    m_seriesList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_seriesList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    leftLayout->addWidget(new QLabel(tr("Series"), leftPane));
    leftLayout->addWidget(m_seriesList, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn    = new QPushButton(tr("Add…"),    leftPane);
    m_removeBtn = new QPushButton(tr("Remove"),  leftPane);
    m_removeBtn->setEnabled(false);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    leftLayout->addLayout(btnRow);

    // ---- Centre pane: chart --------------------------------------------
    m_chart = new QChart;
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chartView = new QChartView(m_chart, m_splitter);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    m_xAxis = new QValueAxis;
    m_xAxis->setTitleText(tr("Hours since simulation start"));
    m_chart->addAxis(m_xAxis, Qt::AlignBottom);
    m_yAxis = new QValueAxis;
    m_yAxis->setTitleText(tr("Value"));
    m_chart->addAxis(m_yAxis, Qt::AlignLeft);

    // ---- Right pane: style editor --------------------------------------
    auto *rightPane = new QWidget(m_splitter);
    auto *rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("Series Style"), rightPane));
    m_styleEditor = new SeriesStyleEditor(rightPane);
    rightLayout->addWidget(m_styleEditor, 1);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(m_chartView);
    m_splitter->addWidget(rightPane);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);
    m_splitter->setSizes({220, 600, 320});

    root->addWidget(m_splitter, 1);

    // Footer --------------------------------------------------------------
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    root->addWidget(bb);

    // Wire signals --------------------------------------------------------
    connect(m_addBtn,    &QPushButton::clicked,
            this, &TimeSeriesPlotDialog::onAddSeriesClicked);
    connect(m_removeBtn, &QPushButton::clicked,
            this, &TimeSeriesPlotDialog::onRemoveSeriesClicked);
    connect(m_seriesList->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TimeSeriesPlotDialog::onSeriesSelectionChanged);
    connect(m_styleEditor, &SeriesStyleEditor::styleChanged,
            this, &TimeSeriesPlotDialog::onSeriesStyleChanged);
}

// ---------------------------------------------------------------------------
// Series mutation
// ---------------------------------------------------------------------------

QString TimeSeriesPlotDialog::defaultLegendFor(const Entry& e) const
{
    return QStringLiteral("%1 — %2").arg(e.object.name, e.variableLabel);
}

int TimeSeriesPlotDialog::addSeries(const SWMMObjectRef &obj, int variableCode)
{
    const auto vars = variablesFor(obj.objectType);
    if (vars.isEmpty()) return -1;

    Entry e;
    e.object        = obj;
    e.variableCode  = variableCode;
    e.variableLabel = labelForCode(obj.objectType, variableCode);

    // Seed style off the colour cycle so successive series differ.
    const SeriesStyle defaultStyle = openswmmvis::plot::defaultStyleForCycle(m_entries.size());
    e.styleObject = new SeriesStyleObject(defaultStyle, this);

    m_entries.push_back(e);
    const int newIdx = m_entries.size() - 1;

    rebuildChart();
    rebuildSeriesListModel();

    // Auto-select the newly added series so the editor binds to it.
    const QModelIndex mi = m_seriesModel->index(newIdx, 0);
    m_seriesList->setCurrentIndex(mi);

    return newIdx;
}

void TimeSeriesPlotDialog::removeSeries(int index)
{
    if (index < 0 || index >= m_entries.size()) return;
    Entry e = m_entries.takeAt(index);
    if (e.line) {
        m_chart->removeSeries(e.line);
        delete e.line;
    }
    if (e.styleObject) e.styleObject->deleteLater();

    rebuildSeriesListModel();
    if (m_entries.isEmpty()) {
        m_selectedIndex = -1;
        bindEditorToSelection();
    } else {
        const int nextSel = std::min<int>(index, m_entries.size() - 1);
        m_seriesList->setCurrentIndex(m_seriesModel->index(nextSel, 0));
    }
}

// ---------------------------------------------------------------------------
// Chart rebuild
// ---------------------------------------------------------------------------

void TimeSeriesPlotDialog::rebuildChart()
{
    // Wipe existing chart series (keep axes).
    for (auto& e : m_entries) {
        if (e.line) {
            m_chart->removeSeries(e.line);
            delete e.line;
            e.line = nullptr;
        }
    }

    SWMM_Output out = swmm_output_open(m_outPath.toUtf8().constData());
    if (!out) {
        m_chart->setTitle(tr("Could not open %1").arg(m_outPath));
        return;
    }

    const int periods = swmm_output_get_period_count(out);
    if (periods <= 0) {
        m_chart->setTitle(tr("Output file has no reporting periods"));
        swmm_output_close(out);
        return;
    }
    const int reportStepSec = swmm_output_get_report_step(out);

    double yMin =  std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    double xMax = 0.0;

    for (auto& e : m_entries) {
        QVector<float> values;
        if (!readSeriesFromOut(out, e.object, e.variableCode, periods, values))
            continue;

        auto *line = new QLineSeries;
        const QString legend = e.styleObject->legendName().isEmpty()
                                   ? defaultLegendFor(e)
                                   : e.styleObject->legendName();
        line->setName(legend);

        for (int p = 0; p < periods; ++p) {
            const double tHours = double(p) * double(reportStepSec) / 3600.0;
            const double v      = double(values[p]);
            line->append(tHours, v);
            if (tHours > xMax) xMax = tHours;
            if (v < yMin) yMin = v;
            if (v > yMax) yMax = v;
        }

        m_chart->addSeries(line);
        line->attachAxis(m_xAxis);
        line->attachAxis(m_yAxis);

        openswmmvis::plot::applySeriesStyle(e.styleObject->style(), line);
        e.line = line;
    }
    swmm_output_close(out);

    if (m_entries.isEmpty() || !std::isfinite(yMin) || !std::isfinite(yMax)) {
        m_chart->setTitle(tr("No data to plot"));
        return;
    }

    const double pad = (yMax > yMin) ? 0.05 * (yMax - yMin) : 1.0;
    m_xAxis->setRange(0.0, std::max(1.0, xMax));
    m_yAxis->setRange(yMin - pad, yMax + pad);

    if (m_entries.size() == 1)
        m_chart->setTitle(defaultLegendFor(m_entries.first()));
    else
        m_chart->setTitle(tr("%1 series").arg(m_entries.size()));
}

// ---------------------------------------------------------------------------
// Series list (left pane) + selection
// ---------------------------------------------------------------------------

void TimeSeriesPlotDialog::rebuildSeriesListModel()
{
    if (!m_seriesModel) return;
    const int previousRow = m_seriesList->currentIndex().row();
    m_seriesModel->clear();
    for (const auto& e : m_entries) {
        const QString legend = e.styleObject->legendName().isEmpty()
                                   ? defaultLegendFor(e)
                                   : e.styleObject->legendName();
        auto *item = new QStandardItem(swatchPixmap(e.styleObject->color()), legend);
        item->setEditable(false);
        m_seriesModel->appendRow(item);
    }
    if (previousRow >= 0 && previousRow < m_entries.size())
        m_seriesList->setCurrentIndex(m_seriesModel->index(previousRow, 0));
}

void TimeSeriesPlotDialog::onSeriesSelectionChanged()
{
    const QModelIndex idx = m_seriesList->currentIndex();
    m_selectedIndex = idx.isValid() ? idx.row() : -1;
    m_removeBtn->setEnabled(m_selectedIndex >= 0);
    bindEditorToSelection();
}

void TimeSeriesPlotDialog::bindEditorToSelection()
{
    if (!m_styleEditor) return;
    if (m_selectedIndex < 0 || m_selectedIndex >= m_entries.size()) {
        m_styleEditor->setStyle(SeriesStyle{});
        m_styleEditor->setEnabled(false);
        return;
    }
    m_styleEditor->setEnabled(true);
    m_styleEditor->setStyle(m_entries[m_selectedIndex].styleObject->style());
}

void TimeSeriesPlotDialog::onSeriesStyleChanged(const SeriesStyle& style)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_entries.size()) return;
    Entry& e = m_entries[m_selectedIndex];

    e.styleObject->setStyle(style);
    if (e.line) {
        // If the legend name changed, push it to the chart series too.
        const QString legend = style.legendName.isEmpty()
                                   ? defaultLegendFor(e)
                                   : style.legendName;
        e.line->setName(legend);
        openswmmvis::plot::applySeriesStyle(style, e.line);
    }

    // Refresh the row label + colour swatch in the left pane.
    if (auto *item = m_seriesModel->item(m_selectedIndex)) {
        const QString legend = style.legendName.isEmpty()
                                   ? defaultLegendFor(e)
                                   : style.legendName;
        item->setText(legend);
        item->setIcon(swatchPixmap(style.color));
    }
}

// ---------------------------------------------------------------------------
// Add / Remove dialog
// ---------------------------------------------------------------------------

void TimeSeriesPlotDialog::onAddSeriesClicked()
{
    // Modal sub-dialog: pick an object class, an object id (from the .out),
    // and a variable. Keeps the multi-series creation flow self-contained
    // so the host doesn't need a separate "Add Series" panel.
    SWMM_Output out = swmm_output_open(m_outPath.toUtf8().constData());
    if (!out) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Add Series"));
    auto *form = new QFormLayout(&dlg);

    auto *classCombo = new QComboBox(&dlg);
    classCombo->addItem(tr("Node"),         int(SWMMObjectRef::Node));
    classCombo->addItem(tr("Link"),         int(SWMMObjectRef::Link));
    classCombo->addItem(tr("Subcatchment"), int(SWMMObjectRef::Subcatchment));
    form->addRow(tr("Type:"), classCombo);

    auto *idCombo  = new QComboBox(&dlg);
    auto *varCombo = new QComboBox(&dlg);
    form->addRow(tr("Object:"),   idCombo);
    form->addRow(tr("Variable:"), varCombo);

    auto refreshObjects = [&]() {
        idCombo->clear();
        const auto t = static_cast<SWMMObjectRef::ObjectType>(classCombo->currentData().toInt());
        switch (t) {
        case SWMMObjectRef::Node: {
            const int n = swmm_output_get_node_count(out);
            for (int i = 0; i < n; ++i)
                idCombo->addItem(QString::fromUtf8(swmm_output_get_node_id(out, i)));
            break;
        }
        case SWMMObjectRef::Link: {
            const int n = swmm_output_get_link_count(out);
            for (int i = 0; i < n; ++i)
                idCombo->addItem(QString::fromUtf8(swmm_output_get_link_id(out, i)));
            break;
        }
        case SWMMObjectRef::Subcatchment: {
            const int n = swmm_output_get_subcatch_count(out);
            for (int i = 0; i < n; ++i)
                idCombo->addItem(QString::fromUtf8(swmm_output_get_subcatch_id(out, i)));
            break;
        }
        default: break;
        }
    };
    auto refreshVariables = [&]() {
        varCombo->clear();
        const auto t = static_cast<SWMMObjectRef::ObjectType>(classCombo->currentData().toInt());
        for (const auto& v : variablesFor(t))
            varCombo->addItem(v.label, v.code);
    };

    QObject::connect(classCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                     &dlg, [&](int){ refreshObjects(); refreshVariables(); });
    refreshObjects();
    refreshVariables();

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);

    if (dlg.exec() == QDialog::Accepted &&
        !idCombo->currentText().isEmpty() && varCombo->currentData().isValid())
    {
        SWMMObjectRef ref;
        ref.objectType = static_cast<SWMMObjectRef::ObjectType>(classCombo->currentData().toInt());
        ref.name       = idCombo->currentText();
        addSeries(ref, varCombo->currentData().toInt());
    }

    swmm_output_close(out);
}

void TimeSeriesPlotDialog::onRemoveSeriesClicked()
{
    if (m_selectedIndex >= 0) removeSeries(m_selectedIndex);
}
