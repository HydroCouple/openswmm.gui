/*!
 * \file   test_meshdialog_seeds.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Mesh-generation dialog seeds (2026-09-11 defaults):
 *           - nodes → Steiner vertices ON, at rim elevation, with the
 *             minimum node separation enforced
 *           - terrain thinning normal-dot 0.75, 1 pass
 *           - Poisson-disk minimum point spacing ON at 15 m, seeded as a
 *             whole number of MODEL units (15 m, or 49 ft)
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
        auto *tol   = seam<QDoubleSpinBox>(&dlg, "meshThinningTolSpin");
        auto *pass  = seam<QSpinBox>(&dlg, "meshThinningPassesSpin");
        auto *spB   = seam<QCheckBox>(&dlg, "meshMinSpacingBox");
        auto *spS   = seam<QDoubleSpinBox>(&dlg, "meshMinSpacingSpin");
        QVERIFY(nodes && rim && sepB && sepS && tol && pass && spB && spS);

        QVERIFY(nodes->isChecked());
        QVERIFY(rim->isChecked());
        QVERIFY(rim->isEnabled());          // gated on the nodes box
        QVERIFY(sepB->isChecked());
        QVERIFY(sepS->isEnabled());
        QVERIFY(sepS->value() > 0.0);
        QCOMPARE(tol->value(), 0.75);
        QCOMPARE(pass->value(), 1);
        QVERIFY(spB->isChecked());
        QVERIFY(spS->isEnabled());
        QCOMPARE(spS->value(), expectedSpacing(15.0));
        // Whole model units, whatever the unit system.
        QCOMPARE(spS->value(), std::round(spS->value()));
    }

    void minSpacingRoundsToWholeModelUnits()
    {
        UnitSystem *us = m_window->unitSystem();
        const swmm_FlowUnitsProperty original = us->flowUnits();

        us->setFlowUnits(swmm_CFS);          // US customary: 15 m = 49.21 ft → 49
        QVERIFY(!us->isSI());
        {
            MeshGenerationDialog dlg(m_window, m_window);
            QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshMinSpacingSpin")->value(), 49.0);
        }
        us->setFlowUnits(swmm_CMS);          // SI: 15 m → 15
        QVERIFY(us->isSI());
        {
            MeshGenerationDialog dlg(m_window, m_window);
            QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshMinSpacingSpin")->value(), 15.0);
        }
        us->setFlowUnits(original);
    }

    void preferenceOverridesSeedTheDialog()
    {
        PreferencesManager::TwoDDefaults d;
        d.meshNodesAsVertices = false;
        d.meshNodesUseRim     = false;
        d.meshMinSpacingOn    = false;
        d.meshMinSpacingM     = 10.0;
        d.meshThinningPasses  = 4;
        PreferencesManager::instance()->setTwoDDefaults(d);

        MeshGenerationDialog dlg(m_window, m_window);
        QVERIFY(!seam<QCheckBox>(&dlg, "meshNodesAsVerticesBox")->isChecked());
        QVERIFY(!seam<QCheckBox>(&dlg, "meshNodesUseRimBox")->isChecked());
        QVERIFY(!seam<QCheckBox>(&dlg, "meshMinSpacingBox")->isChecked());
        QCOMPARE(seam<QDoubleSpinBox>(&dlg, "meshMinSpacingSpin")->value(), expectedSpacing(10.0));
        QCOMPARE(seam<QSpinBox>(&dlg, "meshThinningPassesSpin")->value(), 4);

        PreferencesManager::instance()->setTwoDDefaults(PreferencesManager::TwoDDefaults{});
    }

private:
    double expectedSpacing(double metres) const
    {
        const double toUnit = m_window->unitSystem()->isSI() ? 1.0 : 1.0 / 0.3048;
        return std::round(metres * toUnit);
    }

    OpenSWMMVisWorkspace *m_workspace = nullptr;
    SWMMVisProjectWindow *m_window    = nullptr;
};

QTEST_MAIN(TestMeshDialogSeeds)
#include "test_meshdialog_seeds.moc"
