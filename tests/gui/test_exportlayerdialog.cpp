/**
 * @file      test_exportlayerdialog.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     The one export dialog
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8.3).
 *
 * The view only collects options: GeoPackage by default (Q8), a file for a
 * multi-layer format and a folder otherwise, WGS 84 stated for GeoJSON, the
 * checklist and options wired through, and nothing to export — or a
 * destination that is the source or the project's feature store — refused.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "io/gdaldrivers.h"
#include "ui/dialogs/exportlayerdialog.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QTest>

using openswmmvis::ui::ExportDialogSetup;
using openswmmvis::ui::ExportItem;
using openswmmvis::ui::ExportLayerDialog;

namespace
{

/**
 *
 * @brief Build detached export options for junctions, conduits and an initially unchecked empty
 * subcatchment category.
 * @return Owned setup with results enabled, source label EPSG25832, project directory
 * /data/project and model_objects basename. Does not create/open files or validate the
 * assumed directory; dialog tests only collect options.
 *
 */
ExportDialogSetup swmmSetup()
{
    ExportDialogSetup s;
    s.mode = ExportDialogSetup::Mode::SwmmObjects;
    s.title = QStringLiteral("Export SWMM objects");
    s.items << ExportItem{0, QStringLiteral("Junctions (12)"), QStringLiteral("junctions"), true}
            << ExportItem{4, QStringLiteral("Conduits (11)"), QStringLiteral("conduits"), true}
            << ExportItem{9, QStringLiteral("Subcatchments (0)"), QStringLiteral("subcatchments"),
                          false};
    s.canIncludeResults = true;
    s.includeResults = true;
    s.sourceCrsLabel = QStringLiteral("EPSG:25832");
    s.defaultDir = QStringLiteral("/data/project");
    s.defaultBaseName = QStringLiteral("model_objects");
    return s;
}

} // namespace

class TestExportLayerDialog : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Require default GPKG setup to resolve one destination file, checked category IDs0/4
     * and expected option defaults.
     * Checks results enabled, add-to-map false, empty target CRS override and no validation
     * error. No actual GeoPackage is written.
     *
     */
    void geoPackageIsTheDefaultAndOneFile()
    {
        ExportLayerDialog dlg(swmmSetup());
        QCOMPARE(dlg.driver(), QStringLiteral("GPKG"));
        QCOMPARE(dlg.destination(), QStringLiteral("/data/project/model_objects.gpkg"));
        QCOMPARE(dlg.targetPaths(),
                 QStringList{QStringLiteral("/data/project/model_objects.gpkg")});
        QCOMPARE(dlg.checkedItems(), (QList<int>{0, 4}));
        QVERIFY(dlg.includeResults());
        QVERIFY(!dlg.addToMap()); // off by default
        QVERIFY(dlg.targetSrsWkt().isEmpty());
        QVERIFY(dlg.validationError().isEmpty());
    }

    /**
     *
     * @brief Require Shapefile selection to build a folder plus per-category paths and include a
     * newly checked third item.
     * Skips when the driver is unavailable; checks planned paths/count only, without writing
     * files or testing empty-source export results.
     *
     */
    void singleLayerFormatsWriteAFolder()
    {
        if (!openswmmvis::io::gdalcaps::driverAvailable("ESRI Shapefile"))
            QSKIP("no Shapefile driver in this GDAL build");
        ExportLayerDialog dlg(swmmSetup());
        dlg.setFormat(QStringLiteral("ESRI Shapefile"));
        QCOMPARE(dlg.destination(), QStringLiteral("/data/project/model_objects_shp"));
        QCOMPARE(dlg.targetPaths(),
                 (QStringList{QStringLiteral("/data/project/model_objects_shp/junctions.shp"),
                              QStringLiteral("/data/project/model_objects_shp/conduits.shp")}));
        dlg.setItemChecked(9, true);
        QCOMPARE(dlg.targetPaths().size(), 3);
    }

    /**
     *
     * @brief Require the CRS label to show the source initially and WGS84 after GeoJSON
     * selection.
     * Skips if GeoJSON unavailable; no coordinate transformation, export or target WKT check.
     *
     */
    void geoJsonSaysWgs84()
    {
        if (!openswmmvis::io::gdalcaps::driverAvailable("GeoJSON"))
            QSKIP("no GeoJSON driver in this GDAL build");
        ExportLayerDialog dlg(swmmSetup());
        auto *label = dlg.findChild<QLabel *>(QStringLiteral("exportCrsLabel"));
        QVERIFY(label);
        QVERIFY(label->text().contains(QStringLiteral("EPSG:25832")));
        dlg.setFormat(QStringLiteral("GeoJSON"));
        QVERIFY(label->text().contains(QStringLiteral("WGS 84")));
    }

    /**
     *
     * @brief Require an explicitly chosen destination string to remain unchanged across a format
     * switch.
     * Keeps the authored .gpkg suffix even after GeoJSON choice; does not validate resulting
     * file extension, driver availability or export content.
     *
     */
    void aUserDestinationSurvivesAFormatChange()
    {
        ExportLayerDialog dlg(swmmSetup());
        dlg.setDestination(QStringLiteral("/elsewhere/out.gpkg"));
        dlg.setFormat(QStringLiteral("GeoJSON"));
        QCOMPARE(dlg.destination(), QStringLiteral("/elsewhere/out.gpkg"));
    }

    /**
     *
     * @brief Require no checked categories and later an empty destination to produce validation
     * errors.
     * Checks nonempty diagnostics only, without trying to accept or write the dialog.
     *
     */
    void nothingTickedIsRefused()
    {
        ExportLayerDialog dlg(swmmSetup());
        dlg.setItemChecked(0, false);
        dlg.setItemChecked(4, false);
        QVERIFY(!dlg.validationError().isEmpty());
        dlg.setItemChecked(4, true);
        dlg.setDestination(QString());
        QVERIFY(!dlg.validationError().isEmpty());
    }

    /**
     *
     * @brief Require protected destination aliases and file-mode directory destinations to be
     * refused before export.
     * Checks cleaned-path equivalence, folder diagnostic, and when Shapefile exists a
     * protected per-layer output inside its destination folder. A different folder is valid;
     * no file replacement is attempted and symlink aliases are not tested.
     *
     */
    void theSourceAndTheFeatureStoreAreNeverReplaced()
    {
        ExportDialogSetup s = swmmSetup();
        s.protectedPaths << QStringLiteral("/data/project/model.features.gpkg");
        ExportLayerDialog dlg(s);
        QVERIFY(dlg.validationError().isEmpty()); // default name is elsewhere
        dlg.setDestination(QStringLiteral("/data/project/model.features.gpkg"));
        QVERIFY(dlg.validationError().contains(QStringLiteral("model.features.gpkg")));
        dlg.setDestination(QStringLiteral("/data/project/../project/./model.features.gpkg"));
        QVERIFY(!dlg.validationError().isEmpty()); // same file, spelt differently

        // A folder is never taken for a file: GeoPackage would replace it whole.
        const QString folder = QCoreApplication::applicationDirPath();
        dlg.setDestination(folder);
        QVERIFY(dlg.validationError().contains(QStringLiteral("folder")));

        // A folder format refuses a protected file inside the folder.
        if (openswmmvis::io::gdalcaps::driverAvailable("ESRI Shapefile")) {
            ExportDialogSetup v;
            v.mode = ExportDialogSetup::Mode::VectorLayer;
            v.items << ExportItem{0, QStringLiteral("Roads"), QStringLiteral("roads"), true};
            v.defaultDir = QStringLiteral("/gis");
            v.defaultBaseName = QStringLiteral("roads");
            v.protectedPaths << QStringLiteral("/gis/src/roads.shp");
            ExportLayerDialog shp(v);
            shp.setFormat(QStringLiteral("ESRI Shapefile"));
            shp.setDestination(QStringLiteral("/gis/src"));
            QVERIFY(!shp.validationError().isEmpty());
            shp.setDestination(QStringLiteral("/gis/out"));
            QVERIFY(shp.validationError().isEmpty());
        }
    }

    /**
     *
     * @brief Require vector selected-only to disable without selection and become usable when
     * selection metadata is present.
     * Checks absence of results checkbox, selectedOnly getter and sole checked item0;
     * no real feature selection or file export occurs.
     *
     */
    void selectedOnlyNeedsASelection()
    {
        ExportDialogSetup s;
        s.mode = ExportDialogSetup::Mode::VectorLayer;
        s.items << ExportItem{0, QStringLiteral("Regions"), QStringLiteral("regions"), true};
        ExportLayerDialog without(s);
        auto *box = without.findChild<QCheckBox *>(QStringLiteral("exportSelectedOnly"));
        QVERIFY(box);
        QVERIFY(!box->isEnabled());
        QVERIFY(!without.findChild<QCheckBox *>(QStringLiteral("exportIncludeResults")));
        s.hasSelection = true;
        ExportLayerDialog with(s);
        box = with.findChild<QCheckBox *>(QStringLiteral("exportSelectedOnly"));
        QVERIFY(box->isEnabled());
        box->setChecked(true);
        QVERIFY(with.selectedOnly());
        QCOMPARE(with.checkedItems(), QList<int>{0}); // no checklist: the one item
    }

    /**
     *
     * @brief Require raster setup to default to GTiff/.tif with no selected-only or add-to-map
     * controls.
     * Checks collected destination/options only, without creating a raster or sampling pixels.
     *
     */
    void rasterExportIsOneGeoTiff()
    {
        ExportDialogSetup s;
        s.mode = ExportDialogSetup::Mode::Raster;
        s.items << ExportItem{0, QStringLiteral("DEM"), QStringLiteral("dem"), true};
        s.defaultDir = QStringLiteral("/data");
        s.defaultBaseName = QStringLiteral("dem_export");
        ExportLayerDialog dlg(s);
        QCOMPARE(dlg.driver(), QStringLiteral("GTiff"));
        QCOMPARE(dlg.destination(), QStringLiteral("/data/dem_export.tif"));
        QVERIFY(!dlg.findChild<QCheckBox *>(QStringLiteral("exportSelectedOnly")));
        QVERIFY(!dlg.findChild<QCheckBox *>(QStringLiteral("exportAddToMap")));
    }
};

QTEST_MAIN(TestExportLayerDialog)
#include "test_exportlayerdialog.moc"
