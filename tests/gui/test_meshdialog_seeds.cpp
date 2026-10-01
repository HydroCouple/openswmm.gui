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
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/meshgenerationdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QSpinBox>
#include <QTest>

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

        QVERIFY(nodes->isChecked());
        QVERIFY(rim->isChecked());
        QVERIFY(rim->isEnabled());          // gated on the nodes box
        QVERIFY(sepB->isChecked());
        QVERIFY(sepS->isEnabled());
        QVERIFY(sepS->value() > 0.0);
        QCOMPARE(cell->value(), 0.0);       // (from extent)
        QCOMPARE(ratio->value(), 1.5);
        QCOMPARE(tol->value(), 0.0);        // terrain roughness off until asked
        // Triangle engine: a 30° bound, quads in streets, no conduit strips.
        QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshMinAngleSpin")->value(), 30.0);
        QVERIFY(seam<QCheckBox>(&dlg, "meshStreetQuadsBox")->isChecked());
        QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshConduitStripSpin")->value(), 0.0);
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

private:
    OpenSWMMVisWorkspace *m_workspace = nullptr;
    SWMMVisProjectWindow *m_window    = nullptr;
};

QTEST_MAIN(TestMeshDialogSeeds)
#include "test_meshdialog_seeds.moc"
