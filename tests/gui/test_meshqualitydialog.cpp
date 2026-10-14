/*!
 * \file   test_meshqualitydialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quality controls use independent physical size and accuracy settings,
 * shared with Preferences. Geometry and shape controls remain separate.
 */
#include "core/unitsystem.h"
#include "core/preferencesmanager.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshgenerationdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QTest>

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

QString projectFixturePath()
{
    return QDir(dataDir()).filePath(QStringLiteral("typed_selection_fixture.inp"));
}

QGroupBox *groupTitled(QWidget *dlg, const QString &title)
{
    for (QGroupBox *g : dlg->findChildren<QGroupBox *>())
        if (g->title() == title) return g;
    return nullptr;
}

} // namespace

class TestMeshQualityDialog : public QObject
{
    Q_OBJECT
private slots:
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

    void cleanupTestCase()
    {
        delete m_dlg;
        if (m_window) { m_window->close(); delete m_window; }
        delete m_workspace;
    }

    void coarseningSettingsAreSharedAndMigrateOff()
    {
        mesh::ResolutionSettings settings;settings.coarsenPasses=3;
        QCOMPARE(mesh::ResolutionSettings::fromJson(settings.toJson()).coarsenPasses,3);
        QCOMPARE(mesh::ResolutionSettings::fromJson({}).coarsenPasses,0);
        QCOMPARE(mesh::ResolutionSettings::migrate({},settings,100).coarsenPasses,0);
        settings.coarsenPasses=11;QVERIFY(!settings.validate().isEmpty());
        auto *enabled=m_dlg->findChild<QCheckBox *>("meshCoarsenEnabled");
        auto *passes=m_dlg->findChild<QSpinBox *>("meshCoarsenPassesSpin");
        QVERIFY(enabled && passes);QVERIFY(!enabled->isChecked());QCOMPARE(passes->value(),3);
        enabled->setChecked(true);QVERIFY(passes->isEnabled());
        enabled->setChecked(false);QVERIFY(!passes->isEnabled());
    }

    void groupsSeparateSizeAccuracyAndGeometry()
    {
        QGroupBox *sizes = groupTitled(m_dlg, QStringLiteral("Cell size"));
        QGroupBox *local = groupTitled(m_dlg, QStringLiteral("Local refinement"));
        QGroupBox *terrain = groupTitled(m_dlg, QStringLiteral("Terrain accuracy"));
        QGroupBox *shape = groupTitled(m_dlg, QStringLiteral("Shape"));
        QGroupBox *bnd = groupTitled(m_dlg, QStringLiteral("Boundaries"));
        QVERIFY(sizes && local && terrain && shape && bnd);
        QCOMPARE(sizes->findChildren<QDoubleSpinBox *>().size(), 3);
        QCOMPARE(local->findChildren<QDoubleSpinBox *>().size(), 1);
        QCOMPARE(terrain->findChildren<QDoubleSpinBox *>().size(), 2);
        QVERIFY(!m_dlg->findChild<QDoubleSpinBox *>("meshCoarsenSpin"));
        QCOMPARE(shape->findChildren<QComboBox *>().size(), 0);      // quad controls have their own page
        QCOMPARE(shape->findChildren<QDoubleSpinBox *>().size(), 1); // minimum angle
        QCOMPARE(shape->findChildren<QCheckBox *>().size(), 2);      // worst angles first, lattice seeding
        QCOMPARE(shape->findChildren<QSpinBox *>().size(), 1);       // smoothing passes
        QCOMPARE(bnd->findChildren<QDoubleSpinBox *>().size(), 2);   // turn, deviation
        // The retired groups are gone.
        for (const char *gone : {"Triangle quality", "Minimum Cell Size", "Terrain-Adaptive Thinning",
                                 "Quad quality", "Quadrilateral cells", "Structured quad patches", "PSLG Optimisation"})
            QVERIFY2(groupTitled(m_dlg, QLatin1String(gone)) == nullptr, gone);
        QVERIFY(m_dlg->findChild<QTabWidget *>(QStringLiteral("meshQualityTabs")) == nullptr);
    }

    void defaultsComeFromPreferencesInPhysicalUnits()
    {
        const auto t = PreferencesManager::instance()->twoDDefaults();
        const auto resolution=t.resolutionSettings();
        const double toUnit=UnitSystem::instance()->isSI()?1.0:1.0/.3048;
        auto *minimum=m_dlg->findChild<QDoubleSpinBox *>("meshMinCellSizeSpin");
        auto *transition=m_dlg->findChild<QDoubleSpinBox *>("meshSizeRatioSpin");
        QVERIFY(minimum && transition);
        QVERIFY(qAbs(minimum->value()-resolution.minimumM*toUnit)<1e-6);
        QCOMPARE(transition->value(),resolution.transition);
        QCOMPARE(m_dlg->findChild<QComboBox *>("meshMaximumMode")->currentIndex(),resolution.maximumM?1:0);
        QCOMPARE(m_dlg->findChild<QComboBox *>("meshToleranceMode")->currentIndex(),resolution.toleranceM?1:0);
        QVERIFY(transition->minimum() >= 1.0 && transition->maximum() <= 2.0);
        QVERIFY(!minimum->suffix().trimmed().isEmpty());
        QGroupBox *bnd = groupTitled(m_dlg, QStringLiteral("Boundaries"));
        const auto bspins = bnd->findChildren<QDoubleSpinBox *>();
        QCOMPARE(bspins[0]->value(), t.meshTrimTurnDeg);
        QCOMPARE(bspins[0]->suffix(), QStringLiteral("°"));
        // Triangle engine (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §5).
        QCOMPARE(m_dlg->findChild<QDoubleSpinBox *>(QStringLiteral("meshMinAngleSpin"))->value(), t.meshMinAngleDeg);
        QCOMPARE(m_dlg->findChild<QCheckBox *>(QStringLiteral("meshStreetQuadsBox"))->isChecked(), t.meshQuadsBetweenBreaklines);
        QVERIFY(qAbs(m_dlg->findChild<QDoubleSpinBox *>(QStringLiteral("meshConduitStripSpin"))->value() - 3.048 * toUnit) < 1e-3);
    }

private:
    OpenSWMMVisWorkspace *m_workspace = nullptr;
    SWMMVisProjectWindow *m_window = nullptr;
    MeshGenerationDialog *m_dlg = nullptr;
};

QTEST_MAIN(TestMeshQualityDialog)
#include "test_meshqualitydialog.moc"
