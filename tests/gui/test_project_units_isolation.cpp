/**
 * @file      test_project_units_isolation.cpp
 * @brief     Each project's flow-units cache follows its own engine when another project is
 *            active: first load, reopen and mediated-edit rebuilds, in both open orders.
 *
 *         Model files are written to PROJECT_UNITS_ISOLATION_OUTPUT
 *         (tests/output/project_units_isolation_2026-11-21).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"
#include "project/openswmmvisworkspace.h"
#include "swmmvisprojectwindow.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QtTest>

#include <memory>
#include <openswmm/engine/openswmm_model.h>

class TestProjectUnitsIsolation : public QObject
{
    Q_OBJECT

    struct Project {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace;
        std::unique_ptr<SWMMVisProjectWindow> window;
    };

    /**
     * @brief Write a one-conduit model in the given flow units.
     * @param units FLOW_UNITS keyword.
     * @return Absolute .inp path under the persistent output folder.
     */
    static QString writeModel(const QString &units)
    {
        const QString folder = QStringLiteral(PROJECT_UNITS_ISOLATION_OUTPUT);
        QDir().mkpath(folder);
        const QString inp = QDir(folder).filePath(units.toLower() + QStringLiteral(".inp"));
        QFile file(inp);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return {};
        file.write(QStringLiteral("[OPTIONS]\nFLOW_UNITS %1\nFLOW_ROUTING KINWAVE\n"
                                  "START_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 01:00:00\n"
                                  "[JUNCTIONS]\nJ 10 5\n[OUTFALLS]\nO 0 FREE NO\n"
                                  "[CONDUITS]\nC J O 100 0.013 0 0\n"
                                  "[XSECTIONS]\nC CIRCULAR 1 0 0 0 1\n"
                                  "[COORDINATES]\nJ 0 0\nO 100 0\n")
                       .arg(units)
                       .toUtf8());
        return inp;
    }

    /**
     * @brief Construct a project window without loading its model.
     * @param inp Model path.
     * @return Owned workspace and window.
     */
    static Project makeProject(const QString &inp)
    {
        Project p;
        p.workspace.reset(OpenSWMMVisWorkspace::newInstance({}, nullptr));
        p.window = std::make_unique<SWMMVisProjectWindow>(p.workspace.get(), inp);
        return p;
    }

    /**
     * @param p Loaded project.
     * @return FLOW_UNITS reported by the project's own engine, upper case.
     */
    static QString engineUnits(const Project &p)
    {
        char buf[32] = {};
        swmm_options_get(p.window->modelLayer()->engine(), "FLOW_UNITS", buf, sizeof(buf));
        return QString::fromUtf8(buf).trimmed().toUpper();
    }

    /**
     * @param p Loaded project.
     * @return The project's GUI flow-units cache label.
     */
    static QString cachedUnits(const Project &p) { return p.window->unitSystem()->flowUnitLabel(); }

private slots:
    /** @brief Supply both open orders: CFS then CMS, and CMS then CFS. */
    void eachCacheFollowsItsOwnEngine_data()
    {
        QTest::addColumn<QString>("first");
        QTest::addColumn<QString>("second");
        QTest::newRow("cfs-then-cms") << QStringLiteral("CFS") << QStringLiteral("CMS");
        QTest::newRow("cms-then-cfs") << QStringLiteral("CMS") << QStringLiteral("CFS");
    }

    /**
     * @brief Load, reopen and rebuild each project while the other is the active one; every
     *        project's cache must keep reporting its own engine's units.
     */
    void eachCacheFollowsItsOwnEngine()
    {
        QFETCH(QString, first);
        QFETCH(QString, second);
        const auto restore = qScopeGuard([] { UnitSystem::setActiveProject(nullptr); });
        QList<QString> warnings, errors;

        Project a = makeProject(writeModel(first));
        UnitSystem::setActiveProject(a.window->unitSystem());
        QVERIFY2(a.window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        QCOMPARE(engineUnits(a), first);

        // Open the second project while the first is still the active one.
        Project b = makeProject(writeModel(second));
        QVERIFY2(b.window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        QCOMPARE(engineUnits(b), second);
        QCOMPARE(cachedUnits(a), engineUnits(a));
        QCOMPARE(cachedUnits(b), engineUnits(b));
        QCOMPARE(UnitSystem::instance()->flowUnitLabel(), first);

        // Switch to the second project and reopen the first behind it.
        UnitSystem::setActiveProject(b.window->unitSystem());
        QVERIFY2(a.window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        QCOMPARE(cachedUnits(a), engineUnits(a));
        QCOMPARE(cachedUnits(b), engineUnits(b));
        QCOMPARE(UnitSystem::instance()->flowUnitLabel(), second);

        // A mediated edit rebuild of the inactive project changes only its own cache.
        QCOMPARE(swmm_options_set(a.window->modelLayer()->engine(), "FLOW_UNITS", "LPS"), 0);
        a.window->modelLayer()->rebuildFromEngine();
        QCOMPARE(cachedUnits(a), QStringLiteral("LPS"));
        QCOMPARE(cachedUnits(b), engineUnits(b));
        QCOMPARE(UnitSystem::instance()->flowUnitLabel(), second);
    }
};

QTEST_MAIN(TestProjectUnitsIsolation)
#include "test_project_units_isolation.moc"
