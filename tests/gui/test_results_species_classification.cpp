/**
 * @file      test_results_species_classification.cpp
 * @brief     A Graduated results renderer on a species ("qual:<name>") colors each junction
 *            by its own saved concentration, as the water-age figure needs.
 *
 *         Model / run files are written to RESULTS_SPECIES_CLASSIFICATION_OUTPUT
 *         (tests/output/results_species_classification_2026-11-21).
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmmresultslayer.h"
#include "project/openswmmvisworkspace.h"
#include "render/attributesource.h"
#include "render/featureref.h"
#include "render/rendererfactory.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/symbolstyle.h"
#include "swmmvisprojectwindow.h"

#include <QDir>
#include <QFile>
#include <QSet>
#include <QtTest>

#include <memory>
#include <openswmm/engine/openswmm_engine.h>

class TestResultsSpeciesClassification : public QObject
{
    Q_OBJECT

    QString folder;
    std::unique_ptr<OpenSWMMVisWorkspace> workspace;
    std::unique_ptr<SWMMVisProjectWindow> window;
    SWMMResultsLayer *results = nullptr;

    /** Inflow TSS concentration of junctions J1..J4 (mg/L), one per 5–45 class. */
    const QList<double> kConcentrations{10.0, 20.0, 30.0, 40.0};

    /**
     * @param name Junction id.
     * @return Model row of the junction, or -1.
     */
    int junctionRow(const QString &name) const
    {
        auto *model = window->modelLayer();
        for (int r = 0; r < model->categoryCount(SWMMModelLayer::CatJunctions); ++r)
            if (model->objectNameAt(SWMMModelLayer::CatJunctions, r) == name)
                return r;
        return -1;
    }

private slots:
    /**
     * @brief Run four junctions, each fed its own constant TSS concentration, and open the
     *        results. Files go to the persistent output folder.
     */
    void initTestCase()
    {
        folder = QStringLiteral(RESULTS_SPECIES_CLASSIFICATION_OUTPUT);
        QVERIFY(QDir().mkpath(folder));
        QString junctions, outfalls, conduits, xsections, inflows, coords;
        for (int i = 0; i < kConcentrations.size(); ++i) {
            const QString j = QStringLiteral("J%1").arg(i + 1);
            junctions += QStringLiteral("%1 10 5\n").arg(j);
            outfalls += QStringLiteral("O%1 0 FREE NO\n").arg(i + 1);
            conduits += QStringLiteral("C%1 %2 O%1 100 0.013 0 0\n").arg(i + 1).arg(j);
            xsections += QStringLiteral("C%1 CIRCULAR 1 0 0 0 1\n").arg(i + 1);
            inflows += QStringLiteral("%1 FLOW \"\" FLOW 1.0 1 0.5\n%1 TSS \"\" CONCEN 1.0 1 %2\n")
                           .arg(j)
                           .arg(kConcentrations[i]);
            coords += QStringLiteral("%1 0 %2\nO%3 100 %2\n").arg(j).arg(i * 50).arg(i + 1);
        }
        const QString inp = QDir(folder).filePath(QStringLiteral("species_classes.inp"));
        QFile file(inp);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QStringLiteral("[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\n"
                                  "START_DATE 10/01/2026\nEND_DATE 10/01/2026\nEND_TIME 01:00:00\n"
                                  "REPORT_STEP 00:05:00\nROUTING_STEP 00:00:05\n"
                                  "[POLLUTANTS]\nTSS MG/L 0 0 0 0 NO * 0 0 0\n"
                                  "[JUNCTIONS]\n%1[OUTFALLS]\n%2[CONDUITS]\n%3"
                                  "[XSECTIONS]\n%4[INFLOWS]\n%5[COORDINATES]\n%6"
                                  "[REPORT]\nNODES ALL\nLINKS ALL\n")
                       .arg(junctions, outfalls, conduits, xsections, inflows, coords)
                       .toUtf8());
        file.close();
        const QString rpt = QDir(folder).filePath(QStringLiteral("species_classes.rpt"));
        const QString out = QDir(folder).filePath(QStringLiteral("species_classes.out"));
        QCOMPARE(swmm_engine_run(inp.toUtf8().constData(), rpt.toUtf8().constData(),
                                 out.toUtf8().constData(), nullptr),
                 0);

        workspace.reset(OpenSWMMVisWorkspace::newInstance({}, nullptr));
        window = std::make_unique<SWMMVisProjectWindow>(workspace.get(), inp);
        QList<QString> warnings, errors;
        QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        results = new SWMMResultsLayer(out, window->modelLayer(), workspace.get());
        QVERIFY(results->openResults(warnings, errors));
        QVERIFY(results->totalTimeSteps() >= 10);
    }

    /**
     * @brief With a fixed 5–45 mg/L range in five classes, each junction paints the color of
     *        its own concentration's class: four distinct colors, not one.
     */
    void graduatedSpeciesColorsFollowEachJunction()
    {
        using namespace OpenSWMM::Render;
        // The factory gives the archetype's marker symbol, as the style dialog does; a bare
        // renderer has no color layer and paint falls back to the layer ramp.
        auto r = RendererFactory::makeRenderer(
            QStringLiteral("graduated"),
            FeatureSublayer::archetypeFor(SWMMModelLayer::CatJunctions));
        auto *g = dynamic_cast<GraduatedRenderer *>(r.get());
        QVERIFY(g);
        g->setClassifyAttribute(QStringLiteral("qual:TSS"));
        g->setSourceKind(AttributeSourceKind::Dynamic);
        g->setRangeMode(RangeMode::FixedUser);
        g->setRange(5.0, 45.0);
        results->setKindRenderer(SWMMModelLayer::CatJunctions, std::move(r));
        // The kind renderer's species field is fetched with the next frame.
        results->setCurrentTimeStep(results->totalTimeSteps() - 1);
        const auto *renderer =
            dynamic_cast<GraduatedRenderer *>(results->kindRenderer(SWMMModelLayer::CatJunctions));
        QVERIFY(renderer);

        QSet<QRgb> distinct;
        for (int i = 0; i < kConcentrations.size(); ++i) {
            const QString name = QStringLiteral("J%1").arg(i + 1);
            const int row = junctionRow(name);
            QVERIFY(row >= 0);
            const QColor painted = results->featureColorOverride(SWMMModelLayer::CatJunctions, row);
            QVERIFY2(painted.isValid(), qPrintable(name));
            FeatureRef ref;
            ref.featureIndex = row;
            ref.categoryHint = SWMMModelLayer::kindKey(SWMMModelLayer::CatJunctions);
            const QColor expected = SymbolProps::firstColor(
                renderer->symbolFor(ref, {{QStringLiteral("qual:TSS"), kConcentrations[i]}}),
                QColor());
            QVERIFY(expected.isValid());
            QCOMPARE(painted.rgba(), expected.rgba());
            distinct.insert(painted.rgba());
        }
        QCOMPARE(distinct.size(), kConcentrations.size());
    }

    /** @brief Release the results layer before the window and workspace. */
    void cleanupTestCase()
    {
        delete results;
        results = nullptr;
        window.reset();
        workspace.reset();
    }
};

QTEST_MAIN(TestResultsSpeciesClassification)
#include "test_results_species_classification.moc"
