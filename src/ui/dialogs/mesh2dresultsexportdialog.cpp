/*!
 * \file   mesh2dresultsexportdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/mesh2dresultsexportdialog.h"

#include "io/gdaldrivers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <cmath>

namespace openswmmvis::ui {

using openswmmvis::io::Mesh2DExportFormat;
using openswmmvis::io::Mesh2DExportOptions;
using openswmmvis::io::Mesh2DInterp;

namespace {

/*! A Shapefile's DBF caps a table at 255 fields; the export spends one on
 *  cell_id and one on `max`, so this is the most time steps it can carry. */
constexpr int kShapefileFieldCap = 255;

/*! Refuse to build an absurd raster rather than exhausting memory. */
constexpr double kMaxRasterPixels = 2e8;

} // namespace

Mesh2DResultsExportDialog::Mesh2DResultsExportDialog(Mesh2DExportDialogInputs inputs,
                                                     QWidget *parent)
    : QDialog(parent), m_in(std::move(inputs))
{
    setObjectName(QStringLiteral("Mesh2DResultsExportDialog"));   // layout persistence
    setWindowTitle(tr("Export 2D Results"));
    setModal(true);
    buildUi();
    revalidate();
}

Mesh2DResultsExportDialog::~Mesh2DResultsExportDialog() = default;

void Mesh2DResultsExportDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // ── Output ──────────────────────────────────────────────────────────
    auto *outputGroup = new QGroupBox(tr("Output"), this);
    auto *outputForm = new QFormLayout(outputGroup);

    m_format = new QComboBox(outputGroup);
    m_format->setObjectName(QStringLiteral("formatCombo"));
    openswmmvis::io::gdalcaps::ensureRegistered();
    if (openswmmvis::io::gdalcaps::driverAvailable("GTiff"))
        m_format->addItem(tr("GeoTIFF raster"), int(Mesh2DExportFormat::GeoTiff));
    if (openswmmvis::io::gdalcaps::driverAvailable("ESRI Shapefile"))
        m_format->addItem(tr("ESRI Shapefile"), int(Mesh2DExportFormat::Shapefile));
    if (openswmmvis::io::gdalcaps::driverAvailable("GPKG"))
        m_format->addItem(tr("GeoPackage"), int(Mesh2DExportFormat::GeoPackage));
    outputForm->addRow(tr("&Format"), m_format);

    auto *pathRow = new QHBoxLayout;
    m_path = new QLineEdit(outputGroup);
    m_path->setObjectName(QStringLiteral("outputPathEdit"));
    m_path->setPlaceholderText(tr("Folder and base name, without an extension"));
    if (!m_in.defaultDir.isEmpty() && !m_in.defaultBaseName.isEmpty())
        m_path->setText(QDir(m_in.defaultDir).filePath(m_in.defaultBaseName));
    m_browse = new QPushButton(tr("&Browse…"), outputGroup);
    m_browse->setObjectName(QStringLiteral("browseButton"));
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(m_browse);
    outputForm->addRow(tr("Save &to"), pathRow);
    root->addWidget(outputGroup);

    // ── Variables ───────────────────────────────────────────────────────
    auto *varGroup = new QGroupBox(tr("Variables"), this);
    auto *varGrid = new QGridLayout(varGroup);
    const auto mkCheck = [&](const QString &text, const char *name, bool on) {
        auto *c = new QCheckBox(text, varGroup);
        c->setObjectName(QLatin1String(name));
        c->setChecked(on);
        return c;
    };
    m_depth = mkCheck(tr("&Depth"), "depthCheck", true);
    m_head  = mkCheck(tr("&Water surface elevation"), "headCheck", true);
    m_vmag  = mkCheck(tr("&Speed"), "vmagCheck", m_in.hasVelocity);
    m_vx    = mkCheck(tr("Velocity &X"), "vxCheck", false);
    m_vy    = mkCheck(tr("Velocity &Y"), "vyCheck", false);
    varGrid->addWidget(m_depth, 0, 0);
    varGrid->addWidget(m_head,  0, 1);
    varGrid->addWidget(m_vmag,  1, 0);
    varGrid->addWidget(m_vx,    1, 1);
    varGrid->addWidget(m_vy,    1, 2);

    if (!m_in.hasVelocity) {
        const QString why = tr("This run carries no velocity field. Re-run with "
                               "the VELOCITY 2D output group enabled.");
        for (QCheckBox *c : {m_vx, m_vy, m_vmag}) {
            c->setChecked(false);
            c->setEnabled(false);
            c->setToolTip(why);
        }
    }

    m_includeMax = new QCheckBox(tr("Include the run &maxima (depth, water surface, speed)"),
                                 varGroup);
    m_includeMax->setObjectName(QStringLiteral("includeMaxCheck"));
    m_includeMax->setChecked(true);
    m_includeMax->setToolTip(tr("Adds a \"max\" field to each vector layer, and a "
                                "separate single-band _max raster."));
    varGrid->addWidget(m_includeMax, 2, 0, 1, 3);
    root->addWidget(varGroup);

    // ── Time steps ──────────────────────────────────────────────────────
    auto *stepGroup = new QGroupBox(tr("Time steps"), this);
    auto *stepLayout = new QVBoxLayout(stepGroup);
    m_steps = new QListWidget(stepGroup);
    m_steps->setObjectName(QStringLiteral("timeStepList"));
    m_steps->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (int i = 0; i < m_in.times.size(); ++i) {
        const QDateTime &dt = m_in.times.at(i);
        m_steps->addItem(dt.isValid()
                             ? QStringLiteral("%1  %2")
                                   .arg(i + 1, 4, 10, QLatin1Char('0'))
                                   .arg(dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
                             : QStringLiteral("%1").arg(i + 1, 4, 10, QLatin1Char('0')));
    }
    stepLayout->addWidget(m_steps, 1);

    auto *stepButtons = new QHBoxLayout;
    m_all = new QPushButton(tr("&All"), stepGroup);
    m_all->setObjectName(QStringLiteral("selectAllButton"));
    m_none = new QPushButton(tr("N&one"), stepGroup);
    m_none->setObjectName(QStringLiteral("selectNoneButton"));
    m_current = new QPushButton(tr("&Current"), stepGroup);
    m_current->setObjectName(QStringLiteral("selectCurrentButton"));
    m_current->setEnabled(m_in.currentIndex >= 0 && m_in.currentIndex < m_in.times.size());
    stepButtons->addWidget(m_all);
    stepButtons->addWidget(m_none);
    stepButtons->addWidget(m_current);
    stepButtons->addSpacing(12);
    auto *strideLabel = new QLabel(tr("E&very Nth"), stepGroup);
    m_stride = new QSpinBox(stepGroup);
    m_stride->setObjectName(QStringLiteral("strideSpin"));
    m_stride->setRange(1, std::max(1, int(m_in.times.size())));
    m_stride->setValue(1);
    strideLabel->setBuddy(m_stride);
    m_applyStride = new QPushButton(tr("A&pply"), stepGroup);
    m_applyStride->setObjectName(QStringLiteral("applyStrideButton"));
    stepButtons->addWidget(strideLabel);
    stepButtons->addWidget(m_stride);
    stepButtons->addWidget(m_applyStride);
    stepButtons->addStretch(1);
    stepLayout->addLayout(stepButtons);

    m_selection = new QLabel(stepGroup);
    m_selection->setObjectName(QStringLiteral("selectionLabel"));
    stepLayout->addWidget(m_selection);
    root->addWidget(stepGroup, 1);

    // ── Raster ──────────────────────────────────────────────────────────
    m_rasterGroup = new QGroupBox(tr("Raster grid"), this);
    m_rasterGroup->setObjectName(QStringLiteral("rasterGroup"));
    auto *rasterForm = new QFormLayout(m_rasterGroup);

    m_cellSize = new QDoubleSpinBox(m_rasterGroup);
    m_cellSize->setObjectName(QStringLiteral("cellSizeSpin"));
    m_cellSize->setDecimals(4);
    m_cellSize->setRange(1e-4, 1e7);
    m_cellSize->setValue(m_in.suggestedCellSize > 0.0 ? m_in.suggestedCellSize : 1.0);
    m_cellSize->setSuffix(QStringLiteral(" ") + m_in.lengthUnit);
    rasterForm->addRow(tr("Cell si&ze"), m_cellSize);

    m_gridInfo = new QLabel(m_rasterGroup);
    m_gridInfo->setObjectName(QStringLiteral("gridInfoLabel"));
    rasterForm->addRow(QString(), m_gridInfo);

    m_interp = new QComboBox(m_rasterGroup);
    m_interp->setObjectName(QStringLiteral("interpCombo"));
    m_interp->addItem(tr("Natural neighbour"), int(Mesh2DInterp::NaturalNeighbour));
    m_interp->addItem(tr("Inverse distance (k nearest)"), int(Mesh2DInterp::Idw));
    m_interp->addItem(tr("Green–Gauss gradient"), int(Mesh2DInterp::GreenGauss));
    m_interp->setToolTip(tr(
        "Natural neighbour is the smoothest and the slowest. Inverse distance is "
        "a robust middle ground. Green–Gauss evaluates each cell's own gradient "
        "and is the fastest; it never overshoots the neighbouring cells."));
    rasterForm->addRow(tr("&Interpolation"), m_interp);

    m_neighbours = new QSpinBox(m_rasterGroup);
    m_neighbours->setObjectName(QStringLiteral("idwNeighboursSpin"));
    m_neighbours->setRange(1, 64);
    m_neighbours->setValue(8);
    rasterForm->addRow(tr("Nei&ghbours"), m_neighbours);

    m_maskDry = new QCheckBox(tr("Write &NoData where the surface is dry"), m_rasterGroup);
    m_maskDry->setObjectName(QStringLiteral("maskDryCheck"));
    m_maskDry->setChecked(true);
    rasterForm->addRow(QString(), m_maskDry);
    root->addWidget(m_rasterGroup);

    // ── Validation + buttons ────────────────────────────────────────────
    m_validation = new QLabel(this);
    m_validation->setObjectName(QStringLiteral("validationLabel"));
    m_validation->setWordWrap(true);
    m_validation->setStyleSheet(QStringLiteral("color: palette(link-visited);"));
    root->addWidget(m_validation);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->setObjectName(QStringLiteral("buttonBox"));
    m_buttons->button(QDialogButtonBox::Ok)->setText(tr("&Export"));
    root->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_browse, &QPushButton::clicked, this, &Mesh2DResultsExportDialog::onBrowse);
    connect(m_format, &QComboBox::currentIndexChanged,
            this, &Mesh2DResultsExportDialog::onFormatChanged);
    connect(m_all, &QPushButton::clicked, this, &Mesh2DResultsExportDialog::onSelectAll);
    connect(m_none, &QPushButton::clicked, this, &Mesh2DResultsExportDialog::onSelectNone);
    connect(m_current, &QPushButton::clicked, this, &Mesh2DResultsExportDialog::onSelectCurrent);
    connect(m_applyStride, &QPushButton::clicked,
            this, &Mesh2DResultsExportDialog::onApplyStride);
    connect(m_steps, &QListWidget::itemSelectionChanged,
            this, &Mesh2DResultsExportDialog::revalidate);
    connect(m_path, &QLineEdit::textChanged, this, &Mesh2DResultsExportDialog::revalidate);
    connect(m_cellSize, &QDoubleSpinBox::valueChanged,
            this, &Mesh2DResultsExportDialog::revalidate);
    connect(m_interp, &QComboBox::currentIndexChanged,
            this, &Mesh2DResultsExportDialog::revalidate);
    connect(m_includeMax, &QCheckBox::toggled, this, &Mesh2DResultsExportDialog::revalidate);
    for (QCheckBox *c : {m_depth, m_head, m_vx, m_vy, m_vmag})
        connect(c, &QCheckBox::toggled, this, &Mesh2DResultsExportDialog::revalidate);

    // Start on the frame the animation is showing, so the common "export what
    // I am looking at" case is one click.
    onSelectCurrent();
    onFormatChanged();
}

Mesh2DExportFormat Mesh2DResultsExportDialog::currentFormat() const
{
    if (!m_format || m_format->count() == 0) return Mesh2DExportFormat::GeoTiff;
    return Mesh2DExportFormat(m_format->currentData().toInt());
}

std::vector<int> Mesh2DResultsExportDialog::selectedSteps() const
{
    std::vector<int> out;
    if (!m_steps) return out;
    const auto rows = m_steps->selectionModel()->selectedRows();
    for (const QModelIndex &idx : rows) out.push_back(idx.row());
    std::sort(out.begin(), out.end());
    return out;
}

unsigned Mesh2DResultsExportDialog::selectedVariables() const
{
    unsigned mask = 0;
    if (m_depth && m_depth->isChecked()) mask |= openswmmvis::io::Mesh2DDepth;
    if (m_head  && m_head->isChecked())  mask |= openswmmvis::io::Mesh2DHead;
    if (m_vx    && m_vx->isChecked())    mask |= openswmmvis::io::Mesh2DVx;
    if (m_vy    && m_vy->isChecked())    mask |= openswmmvis::io::Mesh2DVy;
    if (m_vmag  && m_vmag->isChecked())  mask |= openswmmvis::io::Mesh2DVmag;
    return mask;
}

Mesh2DExportOptions Mesh2DResultsExportDialog::options() const
{
    Mesh2DExportOptions o;
    // The writers append their own suffix and extension, so hand them a stem.
    const QFileInfo fi(m_path ? m_path->text().trimmed() : QString());
    o.basePath = fi.filePath().isEmpty()
                     ? QString()
                     : (fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName());
    o.format        = currentFormat();
    o.variables     = selectedVariables();
    o.timeSteps     = selectedSteps();
    o.includeMax    = m_includeMax && m_includeMax->isChecked();
    o.cellSize      = m_cellSize ? m_cellSize->value() : 0.0;
    o.interp        = m_interp ? Mesh2DInterp(m_interp->currentData().toInt())
                               : Mesh2DInterp::NaturalNeighbour;
    o.idwNeighbours = m_neighbours ? m_neighbours->value() : 8;
    o.maskDry       = m_maskDry && m_maskDry->isChecked();
    return o;
}

void Mesh2DResultsExportDialog::onBrowse()
{
    QString filter;
    QString suffix;
    switch (currentFormat()) {
    case Mesh2DExportFormat::GeoTiff:
        filter = tr("GeoTIFF (*.tif)");   suffix = QStringLiteral("tif");  break;
    case Mesh2DExportFormat::Shapefile:
        filter = tr("ESRI Shapefile (*.shp)"); suffix = QStringLiteral("shp"); break;
    case Mesh2DExportFormat::GeoPackage:
        filter = tr("GeoPackage (*.gpkg)"); suffix = QStringLiteral("gpkg"); break;
    }
    const QString start = m_path->text().isEmpty()
                              ? (m_in.defaultDir.isEmpty() ? QDir::homePath() : m_in.defaultDir)
                              : m_path->text();
    const QString chosen = QFileDialog::getSaveFileName(this, tr("Export 2D Results"),
                                                        start, filter);
    if (chosen.isEmpty()) return;
    // Store the stem: one export writes several files off the same base.
    const QFileInfo fi(chosen);
    m_path->setText(fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName());
}

void Mesh2DResultsExportDialog::onFormatChanged()
{
    const bool raster = currentFormat() == Mesh2DExportFormat::GeoTiff;
    if (m_rasterGroup) m_rasterGroup->setEnabled(raster);
    revalidate();
}

void Mesh2DResultsExportDialog::onSelectAll()
{
    if (m_steps) m_steps->selectAll();
}

void Mesh2DResultsExportDialog::onSelectNone()
{
    if (m_steps) m_steps->clearSelection();
}

void Mesh2DResultsExportDialog::onSelectCurrent()
{
    if (!m_steps) return;
    m_steps->clearSelection();
    if (m_in.currentIndex < 0 || m_in.currentIndex >= m_steps->count()) return;
    m_steps->setCurrentRow(m_in.currentIndex, QItemSelectionModel::ClearAndSelect);
    m_steps->scrollToItem(m_steps->item(m_in.currentIndex));
}

void Mesh2DResultsExportDialog::onApplyStride()
{
    if (!m_steps) return;
    const int n = m_stride ? m_stride->value() : 1;
    QItemSelection sel;
    m_steps->clearSelection();
    for (int i = 0; i < m_steps->count(); i += std::max(1, n))
        m_steps->item(i)->setSelected(true);
    revalidate();
}

QString Mesh2DResultsExportDialog::validationMessage() const
{
    if (!m_format || m_format->count() == 0)
        return tr("This build of GDAL has no driver that can write any of the "
                  "supported formats.");
    if (!m_path || m_path->text().trimmed().isEmpty())
        return tr("Choose where to save the export.");
    if (selectedVariables() == 0)
        return tr("Select at least one variable.");

    const int steps = int(selectedSteps().size());
    const bool wantsMax = m_includeMax && m_includeMax->isChecked();
    if (steps == 0 && !wantsMax)
        return tr("Select at least one time step, or include the run maxima.");

    if (currentFormat() == Mesh2DExportFormat::Shapefile) {
        const int fields = steps + (wantsMax ? 1 : 0) + 1;   // + cell_id
        if (fields > kShapefileFieldCap)
            return tr("A Shapefile holds at most %1 fields and this would need %2. "
                      "Select fewer time steps, use \"Every Nth\", or choose "
                      "GeoPackage.")
                .arg(kShapefileFieldCap).arg(fields);
    }
    return QString();
}

void Mesh2DResultsExportDialog::revalidate()
{
    if (!m_buttons) return;

    const int steps = int(selectedSteps().size());
    if (m_selection)
        m_selection->setText(tr("%1 of %2 time steps selected")
                                 .arg(steps).arg(m_steps ? m_steps->count() : 0));

    // Grid feedback: the actual raster dimensions and what one band costs, so
    // an unworkable cell size is obvious before anything is written.
    QString gridProblem;
    if (m_gridInfo && m_rasterGroup && m_rasterGroup->isEnabled()) {
        const double cs = m_cellSize ? m_cellSize->value() : 0.0;
        if (cs > 0.0 && m_in.extentWidth > 0.0 && m_in.extentHeight > 0.0) {
            const double w = std::ceil(m_in.extentWidth / cs);
            const double h = std::ceil(m_in.extentHeight / cs);
            const double pixels = w * h;
            m_gridInfo->setText(tr("%1 x %2 pixels, %3 MB per band")
                                    .arg(w, 0, 'f', 0).arg(h, 0, 'f', 0)
                                    .arg(pixels * 4.0 / (1024.0 * 1024.0), 0, 'f', 1));
            if (pixels > kMaxRasterPixels)
                gridProblem = tr("That cell size would make a %1 x %2 raster. "
                                 "Increase it.")
                                  .arg(w, 0, 'f', 0).arg(h, 0, 'f', 0);
        } else {
            m_gridInfo->setText(QString());
        }
    }
    if (m_neighbours && m_interp)
        m_neighbours->setEnabled(m_rasterGroup && m_rasterGroup->isEnabled()
                                 && Mesh2DInterp(m_interp->currentData().toInt())
                                        == Mesh2DInterp::Idw);

    QString problem = validationMessage();
    if (problem.isEmpty()) problem = gridProblem;
    if (m_validation) m_validation->setText(problem);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problem.isEmpty());
}

} // namespace openswmmvis::ui
