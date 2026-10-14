/*!
 * \file   test_meshdialog_seeds.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Mesh-generation dialog seeds (2026-09-11 defaults):
 *           - nodes → Steiner vertices ON, at rim elevation, with the
 *             minimum node separation enforced
 *           - cell size derived from the extent, size ratio 1.5, terrain
 *             tolerance off, mixed cell shape (MESH_OVERHAUL_PLAN_2026-09-29.md
 *             §3, PHASE6B §2.2)
 *           - SI-canonical lengths scaled to the model unit
 *           - a 2D Defaults preference override reaches the dialog
 *
 *         Widgets are found by the object names the dialog sets as test seams.
 *         The full app is needed because the dialog takes a live project
 *         window; the model is loaded so the window's UnitSystem is real and
 *         can be flipped between US and SI for the rounding check.
 */

#include "core/preferencesmanager.h"
#include "core/unitsystem.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshgenerationdialog.h"
#include "ui/widgets/meshregiondefaultswidget.h"
#include "ui/widgets/meshresolutionwidget.h"
#include <QToolButton>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QLabel>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QTabWidget>

#include <cmath>

namespace {

QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("typed_selection_fixture.inp"));
}

template <class W>
W *seam(QWidget *root, const char *name)
{
    return root->findChild<W *>(QLatin1String(name));
}

}  // namespace

class TestMeshDialogSeeds : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Isolated QSettings (PreferencesManager default-constructs its store).
        QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
        QCoreApplication::setApplicationName(QStringLiteral("meshdialog-seeds-test"));
        m_output=QFileInfo(fixturePath()).absolutePath()+QStringLiteral("/../../verification/mesh_controls_2026-10-13/dialog");
        QVERIFY(QDir().mkpath(m_output));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,m_output);
        QSettings settings;
        settings.remove(QStringLiteral("SWMMVis/Preferences/TwoDDefaults"));
        settings.sync();
        PreferencesManager::instance()->setTwoDDefaults(PreferencesManager::TwoDDefaults{});

        m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(m_workspace);
        m_window = new SWMMVisProjectWindow(m_workspace, fixturePath(), nullptr);
        QList<QString> warnings, errors;
        QVERIFY2(m_window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        QVERIFY(m_window->unitSystem());
        UnitSystem::setActiveProject(m_window->unitSystem());
        QTest::qWait(50);
    }

    void cleanupTestCase()
    {
        PreferencesManager::instance()->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
        UnitSystem::setActiveProject(nullptr);
        delete m_window;
        delete m_workspace;
    }

    void compiledDefaultsSeedTheDialog()
    {
        MeshGenerationDialog dlg(m_window, m_window);

        auto *nodes = seam<QCheckBox>(&dlg, "meshNodesAsVerticesBox");
        auto *rim   = seam<QCheckBox>(&dlg, "meshNodesUseRimBox");
        auto *sepB  = seam<QCheckBox>(&dlg, "meshMinNodeSepBox");
        auto *sepS  = seam<QDoubleSpinBox>(&dlg, "meshMinNodeSepSpin");
        auto *cell  = seam<QDoubleSpinBox>(&dlg, "meshCellSizeSpin");
        auto *ratio = seam<QDoubleSpinBox>(&dlg, "meshSizeRatioSpin");
        auto *tol   = seam<QDoubleSpinBox>(&dlg, "meshTerrainTolSpin");
        QVERIFY(nodes && rim && sepB && sepS && cell && ratio && tol);
        QVERIFY(!seam<QCheckBox>(&dlg,"meshRefineFeaturesBox")->isChecked());
        QVERIFY(seam<QCheckBox>(&dlg,"meshQualityOrderBox")->isChecked());

        QVERIFY(nodes->isChecked());
        QVERIFY(rim->isChecked());
        QVERIFY(rim->isEnabled());          // gated on the nodes box
        QVERIFY(sepB->isChecked());
        QVERIFY(sepS->isEnabled());
        QVERIFY(sepS->value() > 0.0);
        // Independent sizing: 20 ft minimum, no maximum, automatic vertical estimate.
        const double toUnit = UnitSystem::instance()->isSI() ? 1.0 : 1.0 / 0.3048;
        QVERIFY(qAbs(cell->value() - 6.096 * toUnit) < 1e-3);
        QCOMPARE(ratio->value(), 2.0);
        QCOMPARE(seam<QComboBox>(&dlg,"meshMaximumMode")->currentIndex(),0);
        QCOMPARE(seam<QComboBox>(&dlg,"meshFeatureMode")->currentIndex(),0);
        QCOMPARE(seam<QComboBox>(&dlg,"meshToleranceMode")->currentIndex(),0);
        QCOMPARE(seam<QComboBox>(&dlg,"meshTerrainModeCombo")->currentIndex(),0);
        QCOMPARE(seam<QSpinBox>(&dlg,"meshTerrainCacheSpin")->value(),0);   // automatic
        QCOMPARE(seam<QSpinBox>(&dlg,"meshTerrainCacheSpin")->specialValueText(),QStringLiteral("Automatic"));
        QCOMPARE(seam<QSpinBox>(&dlg,"meshMaxCellsSpin")->value(),20'000'000);
        // A 33° bound, quads in streets, 10 ft conduit strips, open-block quads,
        // 4 smoothing passes.
        QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshMinAngleSpin")->value(), 33.0);
        QVERIFY(seam<QCheckBox>(&dlg, "meshStreetQuadsBox")->isChecked());
        QVERIFY(qAbs(seam<QDoubleSpinBox>(&dlg, "meshConduitStripSpin")->value() - 3.048 * toUnit) < 1e-3);
        QCOMPARE(seam<QComboBox>(&dlg, "meshQuadModeCombo")->currentIndex(), 2);
        QCOMPARE(seam<QSpinBox>(&dlg, "meshSmoothingSpin")->value(), 4);
    }

    void lengthsAreScaledToTheModelUnit()
    {
        UnitSystem *us = m_window->unitSystem();
        const swmm_FlowUnitsProperty original = us->flowUnits();
        PreferencesManager::TwoDDefaults d;
        d.meshCellSizeM = 3.048;             // 10 ft exactly
        PreferencesManager::instance()->setTwoDDefaults(d);

        us->setFlowUnits(swmm_CFS);          // US customary
        QVERIFY(!us->isSI());
        {
            MeshGenerationDialog dlg(m_window, m_window);
            QVERIFY(std::abs(seam<QDoubleSpinBox>(&dlg, "meshCellSizeSpin")->value() - 10.0) < 1e-6);
        }
        us->setFlowUnits(swmm_CMS);          // SI
        QVERIFY(us->isSI());
        {
            MeshGenerationDialog dlg(m_window, m_window);
            QVERIFY(std::abs(seam<QDoubleSpinBox>(&dlg, "meshCellSizeSpin")->value() - 3.048) < 1e-6);
        }
        us->setFlowUnits(original);
        PreferencesManager::instance()->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
    }

    void preferenceOverridesSeedTheDialog()
    {
        PreferencesManager::TwoDDefaults d;
        d.meshNodesAsVertices = false;
        d.meshNodesUseRim     = false;
        d.meshSizeRatio       = 1.25;
        d.meshMinAngleDeg     = 26.0;
        d.meshQuadsBetweenBreaklines = false;
        PreferencesManager::instance()->setTwoDDefaults(d);

        MeshGenerationDialog dlg(m_window, m_window);
        QVERIFY(!seam<QCheckBox>(&dlg, "meshNodesAsVerticesBox")->isChecked());
        QVERIFY(!seam<QCheckBox>(&dlg, "meshNodesUseRimBox")->isChecked());
        QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshSizeRatioSpin")->value(), 1.25);
        QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshMinAngleSpin")->value(), 26.0);
        QVERIFY(!seam<QCheckBox>(&dlg, "meshStreetQuadsBox")->isChecked());

        PreferencesManager::instance()->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
    }

    void optionsSurviveRejectAndProjectReload()
    {
        const auto units=m_window->unitSystem()->flowUnits();
        auto burnSettings=m_window->channelBurnSettings();
        burnSettings.options.maxIncision=4.5;
        burnSettings.options.geometryTolerance=.025;
        m_window->setChannelBurnSettings(burnSettings);
        m_window->unitSystem()->setFlowUnits(swmm_CMS);
        {
            MeshGenerationDialog dialog(m_window,m_window);
            QVERIFY(!dialog.findChild<QCheckBox *>("meshBurnEnabledBox"));
            seam<QComboBox>(&dialog,"meshFeatureMode")->setCurrentIndex(1);
            seam<QComboBox>(&dialog,"meshMaximumMode")->setCurrentIndex(1);
            seam<QComboBox>(&dialog,"meshToleranceMode")->setCurrentIndex(1);
            seam<QDoubleSpinBox>(&dialog,"meshCellSizeSpin")->setValue(12.5);
            seam<QDoubleSpinBox>(&dialog,"meshMaxCellSizeSpin")->setValue(40);
            seam<QDoubleSpinBox>(&dialog,"meshTerrainTolSpin")->setValue(.4);
            seam<QDoubleSpinBox>(&dialog,"meshConduitStripSpin")->setValue(2.5);
            seam<QCheckBox>(&dialog,"meshRefineFeaturesBox")->setChecked(false);
            seam<QCheckBox>(&dialog,"meshQualityOrderBox")->setChecked(false);
            seam<QCheckBox>(&dialog,"meshConduitsBox")->setChecked(false);
            seam<QSpinBox>(&dialog,"meshTerrainCacheSpin")->setValue(128);
            seam<QSpinBox>(&dialog,"meshMaxCellsSpin")->setValue(10'000'000);
            auto *regions=dialog.findChild<MeshRegionDefaultsWidget *>(); QVERIFY(regions);
            auto rows=regions->rows(); QVERIFY(!rows.isEmpty());
            rows[0].infil.method=mesh::InfilMethod::Constant; rows[0].infil.p[0]=2.25;
            regions->restoreRows(rows);
            dialog.reject();
        }
        QVERIFY(!m_window->meshGenerationOptions().isEmpty());
        QCOMPARE(m_window->channelBurnSettings().options.maxIncision,4.5);
        QCOMPARE(m_window->channelBurnSettings().options.geometryTolerance,.025);
        auto verify=[](MeshGenerationDialog &dialog) {
            QCOMPARE(seam<QDoubleSpinBox>(&dialog,"meshCellSizeSpin")->value(),12.5);
            QCOMPARE(seam<QDoubleSpinBox>(&dialog,"meshMaxCellSizeSpin")->value(),40.0);
            QCOMPARE(seam<QDoubleSpinBox>(&dialog,"meshTerrainTolSpin")->value(),.4);
            QCOMPARE(seam<QDoubleSpinBox>(&dialog,"meshConduitStripSpin")->value(),2.5);
            QVERIFY(!seam<QCheckBox>(&dialog,"meshRefineFeaturesBox")->isChecked());
            QVERIFY(!seam<QCheckBox>(&dialog,"meshQualityOrderBox")->isChecked());
            QVERIFY(!seam<QCheckBox>(&dialog,"meshConduitsBox")->isChecked());
            QCOMPARE(seam<QSpinBox>(&dialog,"meshTerrainCacheSpin")->value(),128);
            QCOMPARE(seam<QSpinBox>(&dialog,"meshMaxCellsSpin")->value(),10'000'000);
            auto *regions=dialog.findChild<MeshRegionDefaultsWidget *>(); QVERIFY(regions);
            QCOMPARE(regions->rows()[0].infil.method,mesh::InfilMethod::Constant);
            QCOMPARE(regions->rows()[0].infil.p[0],2.25);
        };
        { MeshGenerationDialog dialog(m_window,m_window); verify(dialog); }
        const QJsonObject saved=m_window->meshGenerationOptions();
        const QString path=QDir(m_output).filePath("mesh-options.oswp");
        QString error; QVERIFY2(ProjectSerializer::saveToFile(path,m_window,&error),qPrintable(error));
        m_window->setMeshGenerationOptions({});
        QStringList warnings;
        QVERIFY2(ProjectSerializer::applyFromFile(path,m_window,&error,&warnings),qPrintable(error));
        QCOMPARE(m_window->meshGenerationOptions(),saved);
        { MeshGenerationDialog dialog(m_window,m_window); verify(dialog); }
        m_window->unitSystem()->setFlowUnits(swmm_CFS);
        {
            MeshGenerationDialog dialog(m_window,m_window);
            QVERIFY(std::abs(seam<QDoubleSpinBox>(&dialog,"meshCellSizeSpin")->value()-12.5/.3048)<.001);
            seam<QComboBox>(&dialog,"meshTerrainModeCombo")->setCurrentIndex(2);
            QVERIFY(!seam<QDoubleSpinBox>(&dialog,"meshTerrainTolSpin")->isEnabled());
            dialog.reject();
        }
        { MeshGenerationDialog dialog(m_window,m_window); QCOMPARE(seam<QComboBox>(&dialog,"meshTerrainModeCombo")->currentIndex(),2); }
        m_window->setMeshGenerationOptions({});
        m_window->unitSystem()->setFlowUnits(units);
        m_window->setHasChanges(false);
    }

    void migratesLegacyMultiplierAndSeparatesTolerances()
    {
        m_window->setMeshGenerationOptions({{"version",1},{"controls",QJsonObject{
            {"meshCellSizeSpin",6.096},{"meshMinCellSizeSpin",6.096},{"meshCoarsenSpin",1000.0},
            {"meshTerrainTolSpin",6.096},{"meshTerrainModeCombo",0}}}});
        MeshGenerationDialog dialog(m_window,m_window);
        const double toUnit=UnitSystem::instance()->isSI()?1:1/.3048;
        QCOMPARE(seam<QComboBox>(&dialog,"meshMaximumMode")->currentIndex(),1);
        QVERIFY(std::abs(seam<QDoubleSpinBox>(&dialog,"meshMaxCellSizeSpin")->value()-6096*toUnit)<.001);
        auto *breakline=seam<QDoubleSpinBox>(&dialog,"meshBreaklineTolSpin");
        QVERIFY(std::abs(breakline->value()-6.096*toUnit)<.001);
        seam<QDoubleSpinBox>(&dialog,"meshTerrainTolSpin")->setValue(.5);
        QVERIFY(std::abs(breakline->value()-6.096*toUnit)<.001);
        seam<QComboBox>(&dialog,"meshMaximumMode")->setCurrentIndex(0);
        QVERIFY(!seam<QCheckBox>(&dialog,"meshCoarsenEnabled")->isChecked());
        seam<QCheckBox>(&dialog,"meshCoarsenEnabled")->setChecked(true);
        seam<QSpinBox>(&dialog,"meshCoarsenPassesSpin")->setValue(5);
        dialog.reject();
        QCOMPARE(m_window->meshGenerationOptions().value("version").toInt(),2);
        const auto r=mesh::ResolutionSettings::fromJson(m_window->meshGenerationOptions().value("resolution").toObject());
        QVERIFY(!r.maximumM); QVERIFY(r.breaklineToleranceM);QCOMPARE(r.coarsenPasses,5);
        { MeshGenerationDialog restored(m_window,m_window); QCOMPARE(seam<QComboBox>(&restored,"meshMaximumMode")->currentIndex(),0); }
        m_window->setMeshGenerationOptions({});
    }

    void sharedEditorPreservesPhysicalSizesAcrossUnitChanges()
    {
        MeshResolutionWidget widget;
        mesh::ResolutionSettings r; r.minimumM=3.048; r.maximumM=30.48; r.toleranceM=.1524; r.breaklineToleranceM=.6096;
        widget.setSettings(r); widget.setUnits(.3048,"ft");
        QCOMPARE(widget.minimum->value(),10.0); QCOMPARE(widget.maximum->value(),100.0);
        QCOMPARE(widget.tolerance->value(),.5); QCOMPARE(widget.breaklineTolerance->value(),2.0);
        widget.setUnits(1,"m"); QCOMPARE(widget.minimum->value(),3.048); QCOMPARE(widget.maximum->value(),30.48);
        widget.setUnits(1200.0/3937.0,"US survey ft");
        QVERIFY(std::abs(widget.settings().minimumM-r.minimumM)<1e-6);
        QVERIFY(std::abs(widget.settings().toleranceM.value()-r.toleranceM.value())<1e-6);
        r.maximumM=1; QVERIFY(!r.validate().isEmpty());
    }

    void qualityControlsAreVisible()
    {
        m_window->setMeshGenerationOptions({});
        MeshGenerationDialog dialog(m_window,m_window);
        auto *tabs=dialog.findChild<QTabWidget *>(); QVERIFY(tabs);
        for(int i=0;i<tabs->count();++i) if(tabs->tabText(i).contains("Quality")) tabs->setCurrentIndex(i);
        dialog.show(); QTest::qWait(100);
        QVERIFY(!seam<QComboBox>(&dialog,"meshTerrainModeCombo")->isVisible());
        QVERIFY(seam<QComboBox>(&dialog,"meshMaximumMode")->isVisible());
        QVERIFY(!seam<QSpinBox>(&dialog,"meshMaxCellsSpin")->isVisible());
        QVERIFY(seam<QCheckBox>(&dialog,"meshQualityOrderBox")->isChecked());
        QVERIFY(dialog.grab().save(QDir(m_output).filePath("mesh-quality-options.png")));
        auto *categories=seam<QTreeWidget>(&dialog,"meshSettingsCategories");
        auto *pages=seam<QStackedWidget>(&dialog,"meshSettingsPages");
        const QStringList names={"Cell size","Local refinement","Terrain accuracy","Terrain features",
                                 "Cell shape","Quads and regions","Corridors","Boundaries","Limits"};
        QCOMPARE(categories->topLevelItemCount(),names.size());
        categories->setFocus();
        for (int i=0;i<names.size();++i) {
            if(i) QTest::keyClick(categories,Qt::Key_Down);
            QCoreApplication::processEvents();
            QCOMPARE(categories->currentItem()->text(0),names[i]);
            QCOMPARE(pages->currentIndex(),i);
            auto *scroll=qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
            QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            if(names[i]!="Corridors") QCOMPARE(scroll->verticalScrollBar()->maximum(),0);
            QVERIFY(dialog.grab().save(QDir(m_output).filePath(QString("mesh-quality-%1.png").arg(i))));
        }
        QVERIFY(seam<QSpinBox>(&dialog,"meshMaxCellsSpin")->isVisible());
        // Links must reveal the target page before focusing its control.
        categories->setCurrentItem(categories->topLevelItem(1));
        auto *links=seam<QLabel>(&dialog,"meshLocalTargetsLink");
        QVERIFY(QMetaObject::invokeMethod(links,"linkActivated",Qt::DirectConnection,Q_ARG(QString,"regions")));
        QCOMPARE(categories->currentItem()->text(0),QString("Quads and regions"));
        QVERIFY(seam<QComboBox>(&dialog,"meshQuadRegionLayerCombo")->isVisible());
        categories->setCurrentItem(categories->topLevelItem(1));
        QVERIFY(QMetaObject::invokeMethod(links,"linkActivated",Qt::DirectConnection,Q_ARG(QString,"corridors")));
        QCOMPARE(categories->currentItem()->text(0),QString("Corridors"));
        // Native themes and enlarged UI fonts must not clip the category text.
        categories->setCurrentItem(categories->topLevelItem(5));
        for (int points : {12,24}) {
            QFont font=categories->font(); font.setPointSize(points); categories->setFont(font);
            QCoreApplication::processEvents();
            const int padding=2*categories->style()->pixelMetric(QStyle::PM_FocusFrameHMargin);
            for (const auto &label : names)
                QTRY_VERIFY(categories->columnWidth(0)>=categories->fontMetrics().horizontalAdvance(label)+padding);
            QTRY_VERIFY(categories->viewport()->width()>=categories->columnWidth(0));
            QCOMPARE(categories->horizontalScrollBar()->maximum(),0);
        }
        QVERIFY(dialog.grab().save(QDir(m_output).filePath("mesh-category-large-font.png")));
    }

private:
    QString m_output;
    OpenSWMMVisWorkspace *m_workspace = nullptr;
    SWMMVisProjectWindow *m_window    = nullptr;
};

QTEST_MAIN(TestMeshDialogSeeds)
#include "test_meshdialog_seeds.moc"
