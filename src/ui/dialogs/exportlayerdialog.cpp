/*!
 * \file   exportlayerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/exportlayerdialog.h"

#include "io/gdaldrivers.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/crsselectiondialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <memory>

namespace openswmmvis::ui {

namespace {

const QString kGeoTiff = QStringLiteral("GTiff");

/*! \p path made absolute and canonical where it exists (symlinks resolved),
 *  else cleaned. */
QString normalPath(const QString &path)
{
    const QFileInfo fi(path);
    const QString canonical = fi.canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : canonical;
}

/*! Whether writing \p target would replace \p kept: the same file (by
 *  identity when both exist — symlinks, case-insensitive volumes) or a file
 *  inside \p target when that is a folder (a .gdb is replaced whole). */
bool replaces(const QString &target, const QString &kept)
{
    if (target.isEmpty() || kept.isEmpty()) return false;
    const QFileInfo ft(target), fk(kept);
    if (ft.exists() && fk.exists() && ft == fk) return true;
    const QString t = normalPath(target), k = normalPath(kept);
    const Qt::CaseSensitivity cs =
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
        Qt::CaseInsensitive;
#else
        Qt::CaseSensitive;
#endif
    return k.compare(t, cs) == 0 || k.startsWith(t + QLatin1Char('/'), cs);
}

}   // namespace

ExportLayerDialog::ExportLayerDialog(const ExportDialogSetup &setup, QWidget *parent)
    : QDialog(parent)
    , m_setup(setup)
{
    setWindowTitle(setup.title.isEmpty() ? tr("Export") : setup.title);
    setObjectName(QStringLiteral("ExportLayerDialog"));
    const bool raster = setup.mode == ExportDialogSetup::Mode::Raster;

    auto *root = new QVBoxLayout(this);
    if (!setup.sourceName.isEmpty()) {
        auto *src = new QLabel(setup.sourceName, this);
        src->setWordWrap(true);
        root->addWidget(src);
    }

    // ----- What --------------------------------------------------------
    const bool checklist = setup.mode == ExportDialogSetup::Mode::SwmmObjects
                        || setup.mode == ExportDialogSetup::Mode::Mesh;
    if (checklist) {
        root->addWidget(new QLabel(setup.mode == ExportDialogSetup::Mode::Mesh
                                       ? tr("Tables to write:")
                                       : tr("Object types (one layer each):"), this));
        m_items = new QListWidget(this);
        m_items->setObjectName(QStringLiteral("exportItems"));
        for (const ExportItem &it : setup.items) {
            auto *row = new QListWidgetItem(it.label, m_items);
            row->setFlags(row->flags() | Qt::ItemIsUserCheckable);
            row->setCheckState(it.checked ? Qt::Checked : Qt::Unchecked);
            row->setData(Qt::UserRole, it.id);
        }
        m_items->setMaximumHeight(180);
        root->addWidget(m_items);
    }

    // ----- Where and how -----------------------------------------------
    auto *form = new QFormLayout();
    m_format = new QComboBox(this);
    m_format->setObjectName(QStringLiteral("exportFormat"));
    if (raster) {
        m_format->addItem(tr("GeoTIFF"), kGeoTiff);
        m_format->setEnabled(false);
    } else {
        for (const io::gdalcaps::VectorWriteFormat &f : io::gdalcaps::vectorWriteFormats())
            m_format->addItem(f.label, f.driver);
    }
    form->addRow(tr("&Format:"), m_format);
    m_formatNote = new QLabel(this);
    m_formatNote->setWordWrap(true);
    m_formatNote->setEnabled(false);
    form->addRow(QString(), m_formatNote);

    auto *destRow = new QWidget(this);
    auto *destLay = new QHBoxLayout(destRow);
    destLay->setContentsMargins(0, 0, 0, 0);
    m_dest = new QLineEdit(destRow);
    m_dest->setObjectName(QStringLiteral("exportDestination"));
    auto *browse = new QPushButton(tr("Browse…"), destRow);
    destLay->addWidget(m_dest, 1);
    destLay->addWidget(browse);
    form->addRow(tr("&Destination:"), destRow);

    auto *crsRow = new QWidget(this);
    auto *crsLay = new QHBoxLayout(crsRow);
    crsLay->setContentsMargins(0, 0, 0, 0);
    m_crsLabel = new QLabel(crsRow);
    m_crsLabel->setObjectName(QStringLiteral("exportCrsLabel"));
    m_crsLabel->setWordWrap(true);
    m_crsButton = new QPushButton(tr("Change…"), crsRow);
    m_crsReset  = new QPushButton(tr("Use source CRS"), crsRow);
    crsLay->addWidget(m_crsLabel, 1);
    crsLay->addWidget(m_crsButton);
    crsLay->addWidget(m_crsReset);
    form->addRow(tr("CRS:"), crsRow);
    root->addLayout(form);

    // ----- Options -----------------------------------------------------
    if (!raster) {
        m_selected = new QCheckBox(tr("Selected features only"), this);
        m_selected->setObjectName(QStringLiteral("exportSelectedOnly"));
        m_selected->setEnabled(setup.hasSelection);
        if (!setup.hasSelection)
            m_selected->setToolTip(tr("Nothing is selected on the map."));
        root->addWidget(m_selected);
    }
    if (setup.canIncludeResults) {
        m_results = new QCheckBox(tr("Include the run's statistics (max depth, max flow, …)"), this);
        m_results->setObjectName(QStringLiteral("exportIncludeResults"));
        m_results->setChecked(setup.includeResults);
        root->addWidget(m_results);
    }
    if (!raster) {
        m_addToMap = new QCheckBox(tr("Add the exported layers to the map"), this);
        m_addToMap->setObjectName(QStringLiteral("exportAddToMap"));
        m_addToMap->setChecked(false);
        root->addWidget(m_addToMap);
    }

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    box->button(QDialogButtonBox::Ok)->setText(tr("Export"));
    root->addWidget(box);

    connect(box, &QDialogButtonBox::accepted, this, &ExportLayerDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_format, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { onFormatChanged(); });
    connect(m_dest, &QLineEdit::textEdited, this, [this] { m_destinationEdited = true; });
    connect(browse, &QPushButton::clicked, this, &ExportLayerDialog::onBrowse);
    connect(m_crsButton, &QPushButton::clicked, this, &ExportLayerDialog::onChooseCrs);
    connect(m_crsReset, &QPushButton::clicked, this, &ExportLayerDialog::onUseSourceCrs);

    onFormatChanged();
    resize(520, checklist ? 520 : 300);
}

QString ExportLayerDialog::driver() const
{
    return m_format->currentData().toString();
}

bool ExportLayerDialog::multiLayer() const
{
    if (driver() == kGeoTiff) return true;   // one file
    return io::gdalcaps::vectorWriteFormat(driver()).multiLayer;
}

QString ExportLayerDialog::extension() const
{
    if (driver() == kGeoTiff) return QStringLiteral("tif");
    return io::gdalcaps::vectorWriteFormat(driver()).extension;
}

QString ExportLayerDialog::defaultDestination() const
{
    const QString dir = m_setup.defaultDir.isEmpty() ? QDir::homePath() : m_setup.defaultDir;
    const QString base = m_setup.defaultBaseName.isEmpty() ? QStringLiteral("export")
                                                           : m_setup.defaultBaseName;
    if (multiLayer())
        return QDir(dir).filePath(base + QLatin1Char('.') + extension());
    // One file per table: a folder named after the export and the format.
    return QDir(dir).filePath(base + QLatin1Char('_') + extension());
}

void ExportLayerDialog::onFormatChanged()
{
    if (driver() == kGeoTiff) {
        m_formatNote->setText(tr("Written as one GeoTIFF; reprojecting resamples with "
                                 "nearest neighbour."));
    } else {
        m_formatNote->setText(io::gdalcaps::vectorWriteFormat(driver()).note);
    }
    if (!m_destinationEdited) m_dest->setText(QDir::toNativeSeparators(defaultDestination()));
    refreshCrsLabel();
}

void ExportLayerDialog::refreshCrsLabel()
{
    const bool wgs84 = driver() != kGeoTiff
                    && io::gdalcaps::vectorWriteFormat(driver()).forcesWgs84;
    m_crsButton->setEnabled(!wgs84);
    m_crsReset->setEnabled(!wgs84 && !m_targetWkt.isEmpty());
    if (wgs84) {
        m_crsLabel->setText(tr("WGS 84 (EPSG:4326) — required by %1")
                                .arg(m_format->currentText()));
        return;
    }
    if (!m_targetWkt.isEmpty()) {
        m_crsLabel->setText(tr("%1 (reprojected)").arg(m_targetLabel));
        return;
    }
    m_crsLabel->setText(m_setup.sourceCrsLabel.isEmpty()
                            ? tr("As the source (none declared)")
                            : tr("As the source: %1").arg(m_setup.sourceCrsLabel));
}

void ExportLayerDialog::onChooseCrs()
{
    CRSSelectionDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;
    std::unique_ptr<SpatialReferenceSystem> srs(dlg.selectedSRS());
    if (!srs) return;
    m_targetWkt   = srs->toWkt();
    m_targetLabel = dlg.selectedAuthCode();
    refreshCrsLabel();
}

void ExportLayerDialog::onUseSourceCrs()
{
    m_targetWkt.clear();
    m_targetLabel.clear();
    refreshCrsLabel();
}

void ExportLayerDialog::onBrowse()
{
    QString chosen;
    if (multiLayer()) {
        const QString filter = QStringLiteral("%1 (*.%2)").arg(m_format->currentText(), extension());
        chosen = QFileDialog::getSaveFileName(this, tr("Export to"), destination(), filter,
                                              nullptr, QFileDialog::DontConfirmOverwrite);
        if (!chosen.isEmpty() && QFileInfo(chosen).suffix().isEmpty())
            chosen += QLatin1Char('.') + extension();
    } else {
        chosen = QFileDialog::getExistingDirectory(this, tr("Export into folder"), destination());
    }
    if (chosen.isEmpty()) return;
    m_destinationEdited = true;
    m_dest->setText(QDir::toNativeSeparators(chosen));
}

QString ExportLayerDialog::destination() const
{
    return QDir::fromNativeSeparators(m_dest->text().trimmed());
}

QList<int> ExportLayerDialog::checkedItems() const
{
    QList<int> out;
    if (!m_items) {
        for (const ExportItem &it : m_setup.items) out << it.id;
        return out;
    }
    for (int i = 0; i < m_items->count(); ++i)
        if (m_items->item(i)->checkState() == Qt::Checked)
            out << m_items->item(i)->data(Qt::UserRole).toInt();
    return out;
}

bool ExportLayerDialog::selectedOnly() const { return m_selected && m_selected->isChecked(); }
bool ExportLayerDialog::includeResults() const { return m_results && m_results->isChecked(); }
bool ExportLayerDialog::addToMap() const { return m_addToMap && m_addToMap->isChecked(); }

void ExportLayerDialog::setFormat(const QString &drv)
{
    const int i = m_format->findData(drv);
    if (i >= 0) m_format->setCurrentIndex(i);
}

void ExportLayerDialog::setDestination(const QString &path)
{
    m_destinationEdited = true;
    m_dest->setText(path);
}

void ExportLayerDialog::setItemChecked(int id, bool on)
{
    if (!m_items) return;
    for (int i = 0; i < m_items->count(); ++i)
        if (m_items->item(i)->data(Qt::UserRole).toInt() == id)
            m_items->item(i)->setCheckState(on ? Qt::Checked : Qt::Unchecked);
}

QStringList ExportLayerDialog::targetPaths() const
{
    if (multiLayer()) return {destination()};
    QStringList out;
    const QList<int> ids = checkedItems();
    for (const ExportItem &it : m_setup.items)
        if (ids.contains(it.id))
            out << QDir(destination()).filePath(it.stem + QLatin1Char('.') + extension());
    return out;
}

QString ExportLayerDialog::validationError() const
{
    if (m_format->count() == 0) return tr("This build cannot write any vector format.");
    if (destination().isEmpty()) return tr("Choose where to export to.");
    if (checkedItems().isEmpty()) return tr("Tick at least one thing to export.");
    // A file format needs a file name: an existing folder is never replaced
    // (a File Geodatabase is itself a ".gdb" folder).
    if (multiLayer()) {   // one file (GeoTIFF included)
        const QFileInfo fi(destination());
        if (fi.isDir() && !(extension() == QLatin1String("gdb")
                            && fi.suffix().compare(QLatin1String("gdb"), Qt::CaseInsensitive) == 0))
            return tr("%1 is a folder. Choose a file name to export to.")
                .arg(QDir::toNativeSeparators(destination()));
    }
    for (const QString &target : targetPaths())
        for (const QString &kept : m_setup.protectedPaths)
            if (replaces(target, kept))
                return tr("%1 is open in this project and cannot be replaced. "
                          "Choose another file.")
                    .arg(QDir::toNativeSeparators(kept));
    return {};
}

void ExportLayerDialog::accept()
{
    const QString err = validationError();
    if (!err.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    QStringList existing;
    for (const QString &p : targetPaths())
        if (QFileInfo::exists(p)) existing << QDir::toNativeSeparators(p);
    if (!existing.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, windowTitle(),
            tr("These already exist and will be replaced:\n\n%1").arg(existing.join(QLatin1Char('\n'))),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }
    QDialog::accept();
}

}   // namespace openswmmvis::ui
