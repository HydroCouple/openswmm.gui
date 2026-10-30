/**
 * @file      test_meshqualitydialog.cpp
 * @brief     Tests for meshqualitydialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Quality controls use independent physical size and accuracy settings,
 * shared with Preferences. Geometry and shape controls remain separate.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/unitsystem.h"
#include "feature/featureroles.h"
#include "core/preferencesmanager.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshgenerationdialog.h"
#include "ui/widgets/meshconstraintsourceswidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>
#include <QToolButton>
#include <algorithm>

namespace
{

/**
 * @brief Resolve the GUI-test fixture root from environment.
 * @return SWMMVIS_GUI_TEST_DATA value, default dot; owning path text, no existence check or
 * mutation.
 */
QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

/**
 * @brief Build the typed-selection INP fixture path under the configured test root.
 * @return Owning QDir-joined typed_selection_fixture.inp path; no file open or existence
 * validation.
 */
QString projectFixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

/**
 * @brief Borrow the first descendant group box with exact title text.
 * @param[in] dlg Nonnull QWidget parent, stable QObject tree through lookup/use.
 * @param[in] title Exact case-sensitive translated title to match.
 * @return Nonowned child QGroupBox pointer or null; dlg/Qt parent owns lifetime. No UI mutation.
 */
QGroupBox *groupTitled(QWidget *dlg, const QString &title)
{
    for (QGroupBox *g : dlg->findChildren<QGroupBox *>())
        if (g->title() == title)
            return g;
    return nullptr;
}

} // namespace

class TestMeshQualityDialog : public QObject
{
    Q_OBJECT
private slots:
    /**
     * @brief Create workspace, load typed project fixture and construct parented mesh-generation
     * dialog.
     * @note Requires fixture file and GUI event loop, waits 50 ms for initial project setup,
     * asserts each pointer/file before use. Suite owns workspace/window/dialog; cleanupTestCase
     * releases in dependency order.
     */
    void initTestCase()
    {
        m_workspace = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        QVERIFY(m_workspace != nullptr);
        QVERIFY(QFile::exists(projectFixturePath()));
        m_window = new SWMMVisProjectWindow(m_workspace, projectFixturePath(), nullptr);
        QVERIFY(m_window != nullptr);
        QTest::qWait(50);
        m_dlg = new MeshGenerationDialog(m_window, m_window);
        QVERIFY(m_dlg != nullptr);
    }

    /**
     * @brief Delete dialog, close/delete project window, then release suite workspace.
     * @note Operates on suite-owned pointers initialized by initTestCase; borrowed child widget
     * references must not escape cleanup. No mesh generation or simulation requested.
     */
    void cleanupTestCase()
    {
        delete m_dlg;
        if (m_window) {
            m_window->close();
            delete m_window;
        }
        delete m_workspace;
    }

    /**
     * @brief Verify coarsening settings round-trip, migration disables them and dialog
     * enablement is explicit.
     * @note Pass count 3 serializes, empty/migrated settings 0, invalid 11 diagnosed; dialog
     * initially disabled with remembered 3 passes, checkbox toggles spin enablement. Requires
     * initialized suite dialog, local edits only.
     */
    void requiredQuadsSelectUnifiedPlanning()
    {
        auto *required=m_dlg->findChild<QCheckBox *>("meshRequireMappedRegions");
        auto *planning=m_dlg->findChild<QComboBox *>("meshCellPlanningMode");
        auto *sizing=m_dlg->findChild<QComboBox *>("meshSizingMode");
        QVERIFY(required && planning && sizing);
        required->setChecked(true);QCOMPARE(planning->currentIndex(),1);QVERIFY(sizing->currentIndex()>0);
        bool known=false;
        QCOMPARE(openswmmvis::feature::regionCellsFromValue(" Required_Quads ",&known),openswmmvis::feature::RegionCells::RequiredQuads);QVERIFY(known);
        QCOMPARE(openswmmvis::feature::regionCellsToken(openswmmvis::feature::RegionCells::RequiredQuads),QString("required_quads"));
        required->setChecked(false);planning->setCurrentIndex(0);sizing->setCurrentIndex(0);
    }
    void coarseningSettingsAreSharedAndMigrateOff()
    {
        mesh::ResolutionSettings settings;
        settings.coarsenPasses = 3;
        QCOMPARE(mesh::ResolutionSettings::fromJson(settings.toJson()).coarsenPasses, 3);
        QCOMPARE(mesh::ResolutionSettings::fromJson({}).coarsenPasses, 0);
        QCOMPARE(mesh::ResolutionSettings::migrate({}, settings, 100).coarsenPasses, 0);
        settings.coarsenPasses = 11;
        QVERIFY(!settings.validate().isEmpty());
        auto *enabled = m_dlg->findChild<QCheckBox *>("meshCoarsenEnabled");
        auto *passes = m_dlg->findChild<QSpinBox *>("meshCoarsenPassesSpin");
        QVERIFY(enabled && passes);
        QVERIFY(!enabled->isChecked());
        QCOMPARE(passes->value(), 3);
        enabled->setChecked(true);
        QVERIFY(passes->isEnabled());
        enabled->setChecked(false);
        QVERIFY(!passes->isEnabled());
    }

    /**
     * @brief Verify mesh-generation control groups expose current
     * physical-size/terrain/shape/boundary layout.
     * @note Checks expected spin/combo/checkbox counts and removal of retired groups/tab widget,
     * including quad controls absent from Shape group. Requires built GUI dialog and current
     * translated titles.
     */
    void groupsSeparateSizeAccuracyAndGeometry()
    {
        QGroupBox *sizes = groupTitled(m_dlg, QStringLiteral("Cell size"));
        QGroupBox *local = groupTitled(m_dlg, QStringLiteral("Local refinement"));
        QGroupBox *terrain = groupTitled(m_dlg, QStringLiteral("Terrain accuracy"));
        QGroupBox *shape = groupTitled(m_dlg, QStringLiteral("Shape"));
        QGroupBox *bnd = groupTitled(m_dlg, QStringLiteral("Boundaries"));
        QVERIFY(sizes && local && terrain && shape && bnd);
        QCOMPARE(sizes->findChildren<QDoubleSpinBox *>().size(), 4);
        QCOMPARE(local->findChildren<QDoubleSpinBox *>().size(), 1);
        QCOMPARE(terrain->findChildren<QDoubleSpinBox *>().size(), 5);
        QVERIFY(!m_dlg->findChild<QDoubleSpinBox *>("meshCoarsenSpin"));
        QCOMPARE(shape->findChildren<QComboBox *>().size(), 0); // quad controls have their own page
        QVERIFY(groupTitled(m_dlg,QStringLiteral("Cell shape"))->findChild<QComboBox *>("meshCoarseningOrder"));
        QCOMPARE(shape->findChildren<QDoubleSpinBox *>().size(), 1); // minimum angle
        QCOMPARE(shape->findChildren<QCheckBox *>().size(),
                 2); // worst angles first, lattice seeding
        QCOMPARE(shape->findChildren<QSpinBox *>().size(), 1);     // smoothing passes
        auto *sources = bnd->findChild<MeshConstraintSourcesWidget *>("meshConstraintSources");
        QVERIFY(sources);
        QCOMPARE(bnd->findChildren<QDoubleSpinBox *>().size() -
                     sources->findChildren<QDoubleSpinBox *>().size(), 4);
        // The retired groups are gone.
        for (const char *gone :
             {"Triangle quality", "Minimum Cell Size", "Terrain-Adaptive Thinning", "Quad quality",
              "Quadrilateral cells", "Structured quad patches", "PSLG Optimisation"})
            QVERIFY2(groupTitled(m_dlg, QLatin1String(gone)) == nullptr, gone);
        QVERIFY(m_dlg->findChild<QTabWidget *>(QStringLiteral("meshQualityTabs")) == nullptr);
    }

    /**
     * @brief Verify mesh dialog defaults mirror shared preferences using current project length
     * units.
     * @note Converts stored metres to ft when non-SI, checks min-size/transition/max/tolerance
     * controls, degree trim suffix/min-angle/quad toggle and 3.048 m conduit strip width within
     * displayed precision. Reads singleton preferences/unit system; no preference mutation,
     * suite dialog owns widgets.
     */
    void defaultsComeFromPreferencesInPhysicalUnits()
    {
        const auto t = PreferencesManager::instance()->twoDDefaults();
        const auto resolution = t.resolutionSettings();
        const double toUnit = UnitSystem::instance()->isSI() ? 1.0 : 1.0 / .3048;
        auto *minimum = m_dlg->findChild<QDoubleSpinBox *>("meshMinCellSizeSpin");
        auto *transition = m_dlg->findChild<QDoubleSpinBox *>("meshSizeRatioSpin");
        QVERIFY(minimum && transition);
        QVERIFY(qAbs(minimum->value() - resolution.minimumM * toUnit) < 1e-6);
        QCOMPARE(transition->value(), resolution.transition);
        QCOMPARE(m_dlg->findChild<QComboBox *>("meshMaximumMode")->currentIndex(),
                 resolution.maximumM ? 1 : 0);
        QCOMPARE(m_dlg->findChild<QComboBox *>("meshToleranceMode")->currentIndex(),
                 resolution.toleranceM ? 1 : 0);
        QCOMPARE(transition->minimum(), 1.05);
        transition->setValue(25.0);
        QCOMPARE(transition->value(), 25.0);
        transition->setValue(resolution.transition);
        QVERIFY(!minimum->suffix().trimmed().isEmpty());
        QGroupBox *bnd = groupTitled(m_dlg, QStringLiteral("Boundaries"));
        const auto bspins = bnd->findChildren<QDoubleSpinBox *>();
        auto turn = std::find_if(bspins.begin(), bspins.end(), [](QDoubleSpinBox *spin) {
            return spin->suffix() == QStringLiteral("°");
        });
        QVERIFY(turn != bspins.end());
        QCOMPARE((*turn)->value(), t.meshTrimTurnDeg);
        // Triangle engine (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §5).
        QCOMPARE(m_dlg->findChild<QDoubleSpinBox *>(QStringLiteral("meshMinAngleSpin"))->value(),
                 t.meshMinAngleDeg);
        QCOMPARE(m_dlg->findChild<QCheckBox *>(QStringLiteral("meshStreetQuadsBox"))->isChecked(),
                 t.meshQuadsBetweenBreaklines);
        QVERIFY(qAbs(m_dlg->findChild<QDoubleSpinBox *>(QStringLiteral("meshConduitStripSpin"))
                         ->value() -
                     3.048 * toUnit) < 1e-3);
    }

    void navigationFillsTheRealDialogAndAcceptsLargeTransitions()
    {
        auto *categories = m_dlg->findChild<QTreeWidget *>("meshSettingsCategories");
        auto *tabs = m_dlg->findChild<QTabWidget *>();
        auto *transition = m_dlg->findChild<QDoubleSpinBox *>("meshSizeRatioSpin");
        QVERIFY(categories && tabs && transition);
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QStringLiteral("Quality"))
                tabs->setCurrentIndex(i);
        categories->setCurrentItem(categories->topLevelItem(0));
        m_dlg->resize(900, 620);
        m_dlg->show();
        QTRY_VERIFY(categories->isVisible());
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        for (int i = 0; i < categories->topLevelItemCount(); ++i)
            QVERIFY(categories->viewport()->width() >=
                    categories->fontMetrics().horizontalAdvance(
                        categories->topLevelItem(i)->text(0)) + 8);
        transition->setFocus();
        transition->selectAll();
        QTest::keyClicks(transition, "25");
        QTest::keyClick(transition, Qt::Key_Tab);
        QCOMPARE(transition->value(), 25.0);
        const QString output = qEnvironmentVariable("SWMMVIS_MESH_UI_REVIEW_OUTPUT");
        if (!output.isEmpty())
            QVERIFY(m_dlg->grab().save(output + "/quality-dialog-native.png"));
        m_dlg->resize(1200, 720);
        QTRY_COMPARE(categories->columnWidth(0), categories->viewport()->width());
        if (!output.isEmpty())
            QVERIFY(m_dlg->grab().save(output + "/quality-dialog-native-wide.png"));
        m_dlg->findChild<QComboBox *>("meshSizingMode")->setCurrentIndex(1);
        m_dlg->findChild<QComboBox *>("meshMinimumEdgeMode")->setCurrentIndex(1);
        QTest::qWait(50);
        if (!output.isEmpty())
            QVERIFY(m_dlg->grab().save(output + "/verified-sizing-dialog-wide.png"));
        m_dlg->findChild<QComboBox *>("meshGeometryPolicy")->setCurrentIndex(2);
        for (int i = 0; i < categories->topLevelItemCount(); ++i)
            if (categories->topLevelItem(i)->text(0) == "Boundaries")
                categories->setCurrentItem(categories->topLevelItem(i));
        QTest::qWait(50);
        if (!output.isEmpty())
            QVERIFY(m_dlg->grab().save(output + "/boundary-dialog-native-wide.png"));
        m_dlg->resize(900, 620);
        QTest::qWait(50);
        if (!output.isEmpty())
            QVERIFY(m_dlg->grab().save(output + "/boundary-dialog-native.png"));
        auto *required=m_dlg->findChild<QCheckBox *>("meshRequireMappedRegions");required->setChecked(true);
        for(int i=0;i<categories->topLevelItemCount();++i)
            if(categories->topLevelItem(i)->text(0)=="Quads and regions")categories->setCurrentItem(categories->topLevelItem(i));
        m_dlg->resize(1200,800);QTest::qWait(50);
        if(!output.isEmpty())QVERIFY(m_dlg->grab().save(output+"/unified-planning-dialog.png"));
        required->setChecked(false);
        auto *scaling=m_dlg->findChild<QComboBox *>("meshTerrainToleranceScaling");scaling->setCurrentIndex(1);
        for(int i=0;i<categories->topLevelItemCount();++i)if(categories->topLevelItem(i)->text(0)=="Terrain accuracy")categories->setCurrentItem(categories->topLevelItem(i));
        for(auto *button:m_dlg->findChildren<QToolButton *>())if(button->text()=="Advanced terrain settings")button->setChecked(true);
        m_dlg->resize(1200,920);QTest::qWait(50);
        if(!output.isEmpty())QVERIFY(m_dlg->grab().save(output+"/weighted-terrain-dialog.png"));
        for(int i=0;i<categories->topLevelItemCount();++i)if(categories->topLevelItem(i)->text(0)=="Cell shape")categories->setCurrentItem(categories->topLevelItem(i));
        for(auto *button:m_dlg->findChildren<QToolButton *>())if(button->text()=="Advanced coarsening settings")button->setChecked(true);
        m_dlg->findChild<QComboBox *>("meshCoarseningOrder")->setCurrentIndex(1);
        QTest::qWait(50);if(!output.isEmpty())QVERIFY(m_dlg->grab().save(output+"/weighted-coarsening-dialog.png"));
        m_dlg->hide();
    }

private:
    OpenSWMMVisWorkspace *m_workspace = nullptr;
    SWMMVisProjectWindow *m_window = nullptr;
    MeshGenerationDialog *m_dlg = nullptr;
};

QTEST_MAIN(TestMeshQualityDialog)
#include "test_meshqualitydialog.moc"
