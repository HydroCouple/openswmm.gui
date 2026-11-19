// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/widgets/meshresolutionwidget.h"
#include <QDockWidget>
#include <QHeaderView>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTest>
#include <QTreeWidget>
#include <QDir>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <limits>

class TestMeshSettingsDock : public QObject
{
    Q_OBJECT
private slots:
    void weightedPoliciesPreservePhysicalUnitsAndSelectVerifiedSizing()
    {
        MeshResolutionWidget widget;mesh::ResolutionSettings settings;
        settings.sizing=mesh::ResolutionSettings::Sizing::Verified;settings.toleranceM=.1;
        settings.coarsenPasses=3;settings.coarseningOrder=mesh::CoarseningOrder::ResolutionAware;
        settings.terrainTolerancePolicy={mesh::TerrainToleranceMode::TargetSize,3.048,.03048,.3048};
        widget.setSettings(settings);widget.setUnits(.3048,"ft");
        QCOMPARE(widget.terrainReferenceSize->value(),10.);QCOMPARE(widget.terrainToleranceFloor->value(),.1);QCOMPARE(widget.terrainToleranceCap->value(),1.);
        auto restored=mesh::ResolutionSettings::fromJson(widget.settings().toJson());QVERIFY(restored.validate().isEmpty());
        QCOMPARE(restored.terrainTolerancePolicy.referenceSize,3.048);QCOMPARE(restored.coarseningOrder,settings.coarseningOrder);
        auto *terrain=widget.findChild<QCheckBox *>("meshRefineTerrainBox");QVERIFY(terrain);
        terrain->setChecked(false);QVERIFY(widget.settings().validate().isEmpty());
        QCOMPARE(widget.settings().terrainTolerancePolicy.mode,mesh::TerrainToleranceMode::Constant);
        terrain->setChecked(true);QCOMPARE(widget.settings().terrainTolerancePolicy.mode,mesh::TerrainToleranceMode::TargetSize);
        widget.setTerrainAvailable(false);QVERIFY(widget.terrainToleranceScaling->isEnabled());
        widget.terrainToleranceScaling->setCurrentIndex(0);QVERIFY(widget.settings().validate().isEmpty());
        widget.sizingMode->setCurrentIndex(0);QCOMPARE(widget.settings().terrainTolerancePolicy.mode,mesh::TerrainToleranceMode::Constant);
        QCOMPARE(widget.settings().coarseningOrder,mesh::CoarseningOrder::LegacyFlatness);
        widget.coarseningOrder->setCurrentIndex(1);QCOMPARE(widget.sizingMode->currentIndex(),1);
    }
    void unifiedPlanningIsVersionedAndRequiresVerifiedSizing()
    {
        MeshResolutionWidget widget;
        widget.cellPlanning->setCurrentIndex(int(mesh::ResolutionSettings::CellPlanning::Unified));
        auto settings=widget.settings();QCOMPARE(settings.cellPlanning,mesh::ResolutionSettings::CellPlanning::Unified);
        QCOMPARE(settings.sizing,mesh::ResolutionSettings::Sizing::Verified);
        auto json=settings.toJson();QCOMPARE(mesh::ResolutionSettings::fromJson(json).cellPlanning,settings.cellPlanning);
        json["cellPlanningVersion"]=99;QVERIFY(!mesh::ResolutionSettings::fromJson(json).validate().isEmpty());
        widget.sizingMode->setCurrentIndex(0);QCOMPARE(widget.settings().cellPlanning,mesh::ResolutionSettings::CellPlanning::Legacy);
        QCOMPARE(mesh::ResolutionSettings::fromJson({}).cellPlanning,mesh::ResolutionSettings::CellPlanning::Legacy);
    }
    void verifiedSizingModesAndSeparateEdgeFloorRoundTrip()
    {
        MeshResolutionWidget widget;
        mesh::ResolutionSettings settings;settings.sizing=mesh::ResolutionSettings::Sizing::Strict;
        settings.minimumEdgeM=.3048;settings.maximumM=12;settings.transition=25;
        widget.setSettings(settings);widget.setUnits(.3048,"ft");
        QCOMPARE(widget.minimumEdge->value(),1.);
        QCOMPARE(widget.settings().sizing,settings.sizing);
        QCOMPARE(widget.settings().minimumEdgeM.value(),.3048);
        const auto restored=mesh::ResolutionSettings::fromJson(widget.settings().toJson());
        QCOMPARE(restored.sizing,settings.sizing);QCOMPARE(restored.minimumEdgeM,settings.minimumEdgeM);
        widget.sizingMode->setCurrentIndex(0);
        QVERIFY(!widget.minimumEdgeMode->isEnabled());
        QVERIFY(!widget.settings().minimumEdgeM);
    }
    void readableDockedAndFloatingNavigation()
    {
        MeshResolutionWidget widget;
        widget.addSettingsPage("Quads and regions", new QWidget);
        widget.addSettingsPage("Boundaries", new QWidget);
        widget.resize(900, 620);
        widget.show();
        auto *dock = widget.findChild<QDockWidget *>("meshSettingsNavigationDock");
        auto *categories = widget.findChild<QTreeWidget *>("meshSettingsCategories");
        auto *pages = widget.findChild<QStackedWidget *>("meshSettingsPages");
        auto *host = widget.findChild<QMainWindow *>("meshSettingsDockHost");
        QVERIFY(dock && categories && pages && host);
        QVERIFY(!host->isWindow());
        QVERIFY(!dock->isFloating());
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetMovable));
        QVERIFY(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
        // Regression: the viewport used to grow while its hidden text column
        // stayed at 100 px. Check the actual column before any font change.
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        const int initialWidth = categories->columnWidth(0);
        host->resizeDocks({dock}, {initialWidth + 180}, Qt::Horizontal);
        QTRY_VERIFY(categories->columnWidth(0) > initialWidth + 100);
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        widget.resize(1200, 620);
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        for (int points : {12, 24}) {
            auto font = categories->font();
            font.setPointSize(points);
            categories->setFont(font);
            QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
            for (int i = 0; i < categories->topLevelItemCount(); ++i)
                QTRY_VERIFY(categories->viewport()->width() >=
                            categories->fontMetrics().horizontalAdvance(
                                categories->topLevelItem(i)->text(0)) +
                                8);
        }
        dock->setFloating(true);
        QTRY_VERIFY(dock->isFloating());
        categories->setCurrentItem(categories->topLevelItem(2));
        QCOMPARE(pages->currentIndex(), 2);
        host->addDockWidget(Qt::RightDockWidgetArea, dock);
        dock->setFloating(false);
        QTRY_VERIFY(!dock->isFloating());
        QCOMPARE(host->dockWidgetArea(dock), Qt::RightDockWidgetArea);
        categories->setCurrentItem(categories->topLevelItem(1));
        QCOMPARE(pages->currentIndex(), 1);
        QTest::qWait(100); // Allow redocking animation and repaint before the preview.
        QDir().mkpath(MESH_SETTINGS_TEST_OUTPUT);
        QVERIFY(widget.grab().save(QString(MESH_SETTINGS_TEST_OUTPUT) + "/quality-docked.png"));
    }

    void transitionAboveTenSurvivesEditingAndSerialization()
    {
        MeshResolutionWidget widget;
        for (double value : {25., 1000., 1e12, std::numeric_limits<double>::max()}) {
            mesh::ResolutionSettings settings;
            settings.transition = value;
            QVERIFY(settings.validate().isEmpty());
            widget.setSettings(settings);
            QCOMPARE(widget.transition->value(), value);
            QCOMPARE(widget.settings().transition, value);
            const auto restored = mesh::ResolutionSettings::fromJson(widget.settings().toJson());
            QCOMPARE(restored.transition, value);
            QVERIFY(restored.validate().isEmpty());
        }
        auto settings = widget.settings();
        settings.transition = std::numeric_limits<double>::infinity();
        QVERIFY(!settings.validate().isEmpty());
        settings.transition = 1;
        QVERIFY(!settings.validate().isEmpty());
    }
};
QTEST_MAIN(TestMeshSettingsDock)
#include "test_meshsettingsdock.moc"
