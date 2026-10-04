/*!
 * \file   test_meshqualitydialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The mesh-generation dialog's Quality tab after the overhaul
 * (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md §3): three groups —
 * Resolution, Shape, Boundaries — carrying the eleven controls, every one in
 * a physical unit, seeded from the 2D Defaults preference page.
 *
 * The widgets carry no object names, so they are located structurally — the
 * group box by title, then its children in layout order. The structure IS
 * the thing under test.
 */
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

    void threeGroupsCarryElevenControls()
    {
        QGroupBox *res = groupTitled(m_dlg, QStringLiteral("Resolution"));
        QGroupBox *shape = groupTitled(m_dlg, QStringLiteral("Shape"));
        QGroupBox *bnd = groupTitled(m_dlg, QStringLiteral("Boundaries"));
        QVERIFY2(res && shape && bnd, "Resolution / Shape / Boundaries groups are missing");
        QCOMPARE(res->findChildren<QDoubleSpinBox *>().size(), 5);   // size, coarsen, ratio, floor, terrain
        QCOMPARE(shape->findChildren<QComboBox *>().size(), 1);      // region layer
        QCOMPARE(shape->findChildren<QDoubleSpinBox *>().size(), 2); // minimum angle, conduit strip width
        QCOMPARE(shape->findChildren<QCheckBox *>().size(), 3);      // worst angles first, lattice seeding, quads between facing break lines
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
        QGroupBox *res = groupTitled(m_dlg, QStringLiteral("Resolution"));
        const auto spins = res->findChildren<QDoubleSpinBox *>();
        // Cell size: 0 = derived from the extent; ratio and coarsening as seeded.
        QCOMPARE(spins[0]->specialValueText(), QStringLiteral("(from extent)"));
        QCOMPARE(spins[1]->value(), t.meshCoarsenFactor);
        QCOMPARE(spins[2]->value(), t.meshSizeRatio);
        QCOMPARE(spins[3]->specialValueText(), QStringLiteral("(cell size / 4)"));
        QCOMPARE(spins[4]->specialValueText(), QStringLiteral("(automatic from DEM)"));   // adaptive terrain is the default mode
        QVERIFY(spins[2]->minimum() >= 1.0 && spins[2]->maximum() <= 2.0);
        QVERIFY(spins[0]->suffix().trimmed().size() >= 1);   // a length unit
        QGroupBox *bnd = groupTitled(m_dlg, QStringLiteral("Boundaries"));
        const auto bspins = bnd->findChildren<QDoubleSpinBox *>();
        QCOMPARE(bspins[0]->value(), t.meshTrimTurnDeg);
        QCOMPARE(bspins[0]->suffix(), QStringLiteral("°"));
        // Triangle engine (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §5).
        QCOMPARE(m_dlg->findChild<QDoubleSpinBox *>(QStringLiteral("meshMinAngleSpin"))->value(), t.meshMinAngleDeg);
        QCOMPARE(m_dlg->findChild<QCheckBox *>(QStringLiteral("meshStreetQuadsBox"))->isChecked(), t.meshQuadsBetweenBreaklines);
        QCOMPARE(m_dlg->findChild<QDoubleSpinBox *>(QStringLiteral("meshConduitStripSpin"))->value(), 0.0);
    }

private:
    OpenSWMMVisWorkspace *m_workspace = nullptr;
    SWMMVisProjectWindow *m_window = nullptr;
    MeshGenerationDialog *m_dlg = nullptr;
};

QTEST_MAIN(TestMeshQualityDialog)
#include "test_meshqualitydialog.moc"
