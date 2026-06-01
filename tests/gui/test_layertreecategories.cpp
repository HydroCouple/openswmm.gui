/*!
 * \file   test_layertreecategories.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice LTR-2026-05-30 — verifies the layer-tree category bucketing
 *         (categoryForLayerType + categoryInfo) extracted from
 *         layertreepanel.cpp. Headless: no MapCanvas / no Qt widgets.
 *
 *         Asserts that:
 *           1. Every layer-type ordinal maps to the expected CategoryId.
 *           2. The 1D / 2D output split is correct
 *              (SWMMResultsLayer → CatSwmm1DOutputs,
 *               SWMM2DResultsLayer → CatSwmm2DOutputs).
 *           3. The mesh layer is broken out into CatMeshes
 *              (was incorrectly falling into CatFeatureLayers pre-LTR).
 *           4. categoryInfo() returns the expected display labels and
 *              icon aliases.
 *           5. Unknown ordinals fall back to CatFeatureLayers without
 *              throwing.
 */

#include <QString>
#include <QTest>

#include "ui/panels/layertreecategories.h"

using openswmmvis::ui::CategoryId;
using openswmmvis::ui::CategoryInfo;
using openswmmvis::ui::LayerTypeOrdinal;
using openswmmvis::ui::categoryForLayerType;
using openswmmvis::ui::categoryInfo;

class TestLayerTreeCategories : public QObject
{
    Q_OBJECT

private slots:
    // §1 + §2 + §3 — per-ordinal bucketing.
    void swmmModelGoesToSwmmGroup();
    void swmm1DResultsGoesTo1DOutputsGroup();
    void swmm2DResultsGoesTo2DOutputsGroup();
    void swmm2DMeshGoesToMeshesGroup();      // regression: was Feature Layers
    void vectorGisSubprojectGoToFeatureLayers();
    void annotationGoesToFeatureLayers();    // explicit, no warn
    void rasterGoesToRasterLayers();
    void imageryWmsWmtsGoToBasemaps();
    void tabularGoesToTables();

    // §5 — fall-back.
    void unknownOrdinalFallsBackToFeatureLayers();

    // §4 — labels + icons.
    void categoryInfoLabelsMatchPlan();
    void categoryInfoMeshAnd2DReuseCreateMeshIcon();
};

// ── §1 + §2 + §3 ────────────────────────────────────────────────────────────

void TestLayerTreeCategories::swmmModelGoesToSwmmGroup()
{
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMModelLayer),
             openswmmvis::ui::CatSwmm);
}

void TestLayerTreeCategories::swmm1DResultsGoesTo1DOutputsGroup()
{
    // Renamed from CatSwmmOutputs in Slice LTR-2026-05-30.
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMResultsLayer),
             openswmmvis::ui::CatSwmm1DOutputs);
}

void TestLayerTreeCategories::swmm2DResultsGoesTo2DOutputsGroup()
{
    // Pre-LTR this hit the default-warn branch and went to CatFeatureLayers.
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMM2DResultsLayer),
             openswmmvis::ui::CatSwmm2DOutputs);
}

void TestLayerTreeCategories::swmm2DMeshGoesToMeshesGroup()
{
    // Pre-LTR this hit the default-warn branch and went to CatFeatureLayers.
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMM2DMeshLayer),
             openswmmvis::ui::CatMeshes);
}

void TestLayerTreeCategories::vectorGisSubprojectGoToFeatureLayers()
{
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMVectorLayer),
             openswmmvis::ui::CatFeatureLayers);
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMGISLayer),
             openswmmvis::ui::CatFeatureLayers);
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMSubProjectLayer),
             openswmmvis::ui::CatFeatureLayers);
}

void TestLayerTreeCategories::annotationGoesToFeatureLayers()
{
    // Slice LTR-2026-05-30 — explicit case (was default-warn pre-LTR).
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMAnnotationLayer),
             openswmmvis::ui::CatFeatureLayers);
}

void TestLayerTreeCategories::rasterGoesToRasterLayers()
{
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMRasterLayer),
             openswmmvis::ui::CatRasterLayers);
}

void TestLayerTreeCategories::imageryWmsWmtsGoToBasemaps()
{
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMImageryLayer),
             openswmmvis::ui::CatBasemaps);
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMWMSLayer),
             openswmmvis::ui::CatBasemaps);
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMWMTSLayer),
             openswmmvis::ui::CatBasemaps);
}

void TestLayerTreeCategories::tabularGoesToTables()
{
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMTabularDataLayer),
             openswmmvis::ui::CatTables);
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMTabularyTimeSeriesLayer),
             openswmmvis::ui::CatTables);
}

// ── §5 — fall-back ──────────────────────────────────────────────────────────

void TestLayerTreeCategories::unknownOrdinalFallsBackToFeatureLayers()
{
    // 999 is not a valid layer-type ordinal; must not throw.
    QCOMPARE(categoryForLayerType(999), openswmmvis::ui::CatFeatureLayers);
    // SWMMDefaultLayer (0) is also a fall-back per the runtime semantics.
    QCOMPARE(categoryForLayerType(LayerTypeOrdinal::SWMMDefaultLayer),
             openswmmvis::ui::CatFeatureLayers);
}

// ── §4 — labels + icons ─────────────────────────────────────────────────────

void TestLayerTreeCategories::categoryInfoLabelsMatchPlan()
{
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatSwmm).name),
             QStringLiteral("SWMM"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatMeshes).name),
             QStringLiteral("Meshes"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatSwmm1DOutputs).name),
             QStringLiteral("SWMM 1D Outputs"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatSwmm2DOutputs).name),
             QStringLiteral("SWMM 2D Outputs"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatFeatureLayers).name),
             QStringLiteral("Feature Layers"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatRasterLayers).name),
             QStringLiteral("Raster Layers"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatBasemaps).name),
             QStringLiteral("Basemaps"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatTables).name),
             QStringLiteral("Tables"));
}

void TestLayerTreeCategories::categoryInfoMeshAnd2DReuseCreateMeshIcon()
{
    // Core LTR requirement: Meshes and 2D outputs both reuse the
    // Generate-Mesh toolbar icon (:/swmmvis/CreateMesh).
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatMeshes).iconAlias),
             QStringLiteral(":/swmmvis/CreateMesh"));
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatSwmm2DOutputs).iconAlias),
             QStringLiteral(":/swmmvis/CreateMesh"));
    // 1D outputs keep the Chart icon.
    QCOMPARE(QString(categoryInfo(openswmmvis::ui::CatSwmm1DOutputs).iconAlias),
             QStringLiteral(":/swmmvis/Chart"));
}

QTEST_APPLESS_MAIN(TestLayerTreeCategories)
#include "test_layertreecategories.moc"
