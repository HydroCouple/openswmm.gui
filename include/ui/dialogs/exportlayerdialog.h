/*!
 * \file   exportlayerdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * One export dialog for every source the layer tree exports
 * (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.3, R8–R10).
 *
 * A VIEW only: it collects the format, destination, CRS and options, asks
 * before overwriting, and hands them back. SWMMVis builds the tables, runs
 * io/vectorexport on a worker and logs the report.
 *
 *   - Format: the vector formats this build can write
 *     (gdalcaps::vectorWriteFormats(), GeoPackage first — Q8); a raster
 *     export is always GeoTIFF (Q10).
 *   - Destination: a file for a format that holds several layers, otherwise
 *     a folder receiving one file per table.
 *   - CRS: the source's by default, or another picked with the CRS dialog;
 *     GeoJSON and KML say they are written in WGS 84.
 *   - Options: selected only; the run's statistics (SWMM); add to the map.
 */

#ifndef EXPORTLAYERDIALOG_H
#define EXPORTLAYERDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace openswmmvis::ui {

/*! One output the dialog lists: an object type or a mesh table. */
struct ExportItem
{
    int     id = 0;        ///< SWMMModelLayer::Category, or a MeshExportPart bit.
    QString label;         ///< "Conduits".
    QString stem;          ///< Output file / layer stem, "conduits".
    bool    checked = true;
};

/*! What the dialog is exporting. */
struct ExportDialogSetup
{
    enum class Mode { SwmmObjects, VectorLayer, Mesh, Raster };
    Mode    mode = Mode::VectorLayer;
    QString title;
    QString sourceName;           ///< One line naming the source.
    /*! The outputs; a checklist is shown for SwmmObjects and Mesh. */
    QVector<ExportItem> items;
    bool    hasSelection = false; ///< Enables "Selected features only".
    bool    canIncludeResults = false;
    bool    includeResults = false;
    QString sourceSrsWkt;
    QString sourceCrsLabel;       ///< e.g. "EPSG:25832"; empty = "none".
    QString defaultDir;
    QString defaultBaseName;
    /*! Files the export must never replace: every file open in the project
     *  (the model, its results, each layer's source) and the project's
     *  features GeoPackage. */
    QStringList protectedPaths;
};

class ExportLayerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportLayerDialog(const ExportDialogSetup &setup, QWidget *parent = nullptr);

    /*! GDAL driver short name; "GTiff" in raster mode. */
    [[nodiscard]] QString driver() const;
    [[nodiscard]] QString destination() const;
    /*! The CRS to write in; empty keeps the source CRS. */
    [[nodiscard]] QString targetSrsWkt() const { return m_targetWkt; }
    /*! The ids of the ticked outputs (all items when no checklist is shown). */
    [[nodiscard]] QList<int> checkedItems() const;
    [[nodiscard]] bool selectedOnly() const;
    [[nodiscard]] bool includeResults() const;
    [[nodiscard]] bool addToMap() const;

    // ----- Test seams ----------------------------------------------------
    void setFormat(const QString &driver);
    void setDestination(const QString &path);
    void setItemChecked(int id, bool on);
    /*! Why Export would be refused (no destination, nothing ticked, a folder
     *  where a file is written, a protected file in the way), or empty. */
    [[nodiscard]] QString validationError() const;
    /*! The files this export would create or replace. */
    [[nodiscard]] QStringList targetPaths() const;

public slots:
    void accept() override;

private slots:
    void onFormatChanged();
    void onBrowse();
    void onChooseCrs();
    void onUseSourceCrs();

private:
    [[nodiscard]] bool multiLayer() const;
    [[nodiscard]] QString extension() const;
    [[nodiscard]] QString defaultDestination() const;
    void refreshCrsLabel();

    ExportDialogSetup m_setup;
    QString      m_targetWkt;
    QString      m_targetLabel;
    bool         m_destinationEdited = false;

    QListWidget *m_items      = nullptr;
    QComboBox   *m_format     = nullptr;
    QLabel      *m_formatNote = nullptr;
    QLineEdit   *m_dest       = nullptr;
    QLabel      *m_crsLabel   = nullptr;
    QPushButton *m_crsButton  = nullptr;
    QPushButton *m_crsReset   = nullptr;
    QCheckBox   *m_selected   = nullptr;
    QCheckBox   *m_results    = nullptr;
    QCheckBox   *m_addToMap   = nullptr;
};

}   // namespace openswmmvis::ui

#endif // EXPORTLAYERDIALOG_H
