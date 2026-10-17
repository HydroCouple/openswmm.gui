/*!
 * \file   test_resultssizechannel.cpp
 * \brief  Results-layer size channel: per-frame widths under a Single Symbol
 *         renderer, Cancel rollback, migration of the Graduated output axes,
 *         and the tabbed per-kind symbology page.
 *
 *         Model / run files are written to RESULTS_SIZE_CHANNEL_OUTPUT
 *         (tests/output/results_size_channel_2026-10-17).
 */
#include "layers/swmmresultslayer.h"
#include "project/openswmmvisworkspace.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/stylefileio.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/kindtreesymbologypanel.h"
#include "ui/dialogs/layerstyledialog.h"

#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTabWidget>
#include <QUndoStack>
#include <QtTest>

#include <memory>
#include <openswmm/engine/openswmm_engine.h>

using OpenSWMM::Render::LineFeatureSublayerStyle;
using OpenSWMM::Render::RangeMode;
using OpenSWMM::Render::SizeChannel;
using OpenSWMM::Render::StyleFileIO;
using openswmmvis::ui::LayerStyleDialog;

class TestResultsSizeChannel : public QObject
{
    Q_OBJECT

    QString folder;
    std::unique_ptr<OpenSWMMVisWorkspace> workspace;
    std::unique_ptr<SWMMVisProjectWindow> window;
    SWMMResultsLayer *results = nullptr;

    QString path(const QString &name) const { return QDir(folder).filePath(name); }
    LineFeatureSublayerStyle *conduitStyle() const
    {
        return results->featureSublayer(SWMMModelLayer::CatConduits)->lineStyle();
    }
    int conduitRow(const QString &name) const
    {
        auto *model = window->modelLayer();
        for (int r = 0; r < model->categoryCount(SWMMModelLayer::CatConduits); ++r)
            if (model->objectNameAt(SWMMModelLayer::CatConduits, r) == name)
                return r;
        return -1;
    }

private slots:
    void initTestCase()
    {
        folder = QStringLiteral(RESULTS_SIZE_CHANNEL_OUTPUT);
        QVERIFY(QDir().mkpath(folder));
        // Two parallel branches: A carries 4× the inflow of B, and both ramp
        // up over the hour, so widths differ between links and frames.
        const QString inp = path("size_channel.inp");
        QFile file(inp);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(
            "[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\n"
            "START_DATE 10/01/2026\nEND_DATE 10/01/2026\nEND_TIME 01:00:00\n"
            "REPORT_STEP 00:05:00\nROUTING_STEP 00:00:05\n"
            "[JUNCTIONS]\nA 10 5\nB 10 5\nJ 5 5\n[OUTFALLS]\nO 0 FREE NO\n"
            "[CONDUITS]\nAO A J 100 0.013 0 0\nBO B J 100 0.013 0 0\nJO J O 100 0.013 0 0\n"
            "[XSECTIONS]\nAO CIRCULAR 2 0 0 0 1\nBO CIRCULAR 2 0 0 0 1\nJO CIRCULAR 2 0 0 0 1\n"
            "[INFLOWS]\nA FLOW Ramp FLOW 1 4\nB FLOW Ramp FLOW 1 1\n"
            "[TIMESERIES]\nRamp 0:00 0.05\nRamp 1:00 1.0\n"
            "[COORDINATES]\nA 0 50\nB 0 -50\nJ 100 0\nO 200 0\n"
            "[REPORT]\nNODES ALL\nLINKS ALL\n");
        file.close();
        const QString out = path("size_channel.out");
        QCOMPARE(swmm_engine_run(inp.toUtf8().constData(),
                                 path("size_channel.rpt").toUtf8().constData(),
                                 out.toUtf8().constData(), nullptr), 0);

        workspace.reset(OpenSWMMVisWorkspace::newInstance({}, nullptr));
        window = std::make_unique<SWMMVisProjectWindow>(workspace.get(), inp);
        QList<QString> warnings, errors;
        QVERIFY2(window->loadModel(warnings, errors), qPrintable(errors.join('\n')));
        results = new SWMMResultsLayer(out, window->modelLayer(), workspace.get());
        QVERIFY(results->openResults(warnings, errors));
        QVERIFY(results->totalTimeSteps() >= 10);
    }

    void widthFollowsFlowUnderSingleSymbol()
    {
        results->setKindRenderer(SWMMModelLayer::CatConduits,
                                 std::make_unique<OpenSWMM::Render::SingleSymbolRenderer>());
        SizeChannel ch;
        ch.enabled = true;
        ch.attribute = QStringLiteral("LinkFlow");
        ch.rangeMode = RangeMode::FixedUser;
        ch.userMin = 0.0; ch.userMax = 5.0;
        ch.minPx = 1.0; ch.maxPx = 11.0;
        conduitStyle()->setWidthChannel(ch);

        const int a = conduitRow("AO"), b = conduitRow("BO");
        QVERIFY(a >= 0 && b >= 0);
        results->setCurrentTimeStep(1);
        const double aEarly = results->featureWidthOverride(SWMMModelLayer::CatConduits, a);
        results->setCurrentTimeStep(results->totalTimeSteps() - 1);
        const double aLate = results->featureWidthOverride(SWMMModelLayer::CatConduits, a);
        const double bLate = results->featureWidthOverride(SWMMModelLayer::CatConduits, b);
        for (double w : {aEarly, aLate, bLate})
            QVERIFY2(w >= ch.minPx && w <= ch.maxPx, qPrintable(QString::number(w)));
        QVERIFY2(aLate > aEarly + 1.0, qPrintable(QStringLiteral("%1 -> %2").arg(aEarly).arg(aLate)));
        QVERIFY(aLate > bLate + 1.0);

        // Each frame: this frame's largest flow (the trunk JO) is at max px,
        // its smallest (BO) at min px.
        ch.rangeMode = RangeMode::PerFrameAutoStretch;
        ch.zeroBased = false;
        conduitStyle()->setWidthChannel(ch);
        QCOMPARE(results->featureWidthOverride(SWMMModelLayer::CatConduits, conduitRow("JO")), ch.maxPx);
        QCOMPARE(results->featureWidthOverride(SWMMModelLayer::CatConduits, b), ch.minPx);

        // Whole run: resolves through the async range scan.
        ch.rangeMode = RangeMode::FixedOverRun;
        conduitStyle()->setWidthChannel(ch);
        results->setCurrentTimeStep(1);
        QTRY_VERIFY(results->featureWidthOverride(SWMMModelLayer::CatConduits, a) < ch.maxPx);

        ch.enabled = false;
        conduitStyle()->setWidthChannel(ch);
        QCOMPARE(results->featureWidthOverride(SWMMModelLayer::CatConduits, a), -1.0);
    }

    void cancelRestoresChannel()
    {
        conduitStyle()->setWidthChannel(SizeChannel{});
        const QJsonObject before = StyleFileIO::styleToJson(results);
        QUndoStack undo;
        {
            LayerStyleDialog dialog(results, {}, nullptr, &undo);
            SizeChannel ch;
            ch.enabled = true;
            ch.attribute = QStringLiteral("LinkDepth");
            conduitStyle()->setWidthChannel(ch);
            dialog.reject();
        }
        QVERIFY(!conduitStyle()->widthChannel().enabled);
        QCOMPARE(StyleFileIO::styleToJson(results), before);
    }

    void graduatedWidthAxisMigrates()
    {
        conduitStyle()->setWidthChannel(SizeChannel{});
        QFile fixture(QStringLiteral(RESULTS_SIZE_CHANNEL_FIXTURE));
        QVERIFY(fixture.open(QIODevice::ReadOnly));
        const QJsonObject legacy = QJsonDocument::fromJson(fixture.readAll()).object();
        QVERIFY(!legacy.isEmpty());
        QVERIFY(StyleFileIO::applyStyleJson(results, legacy).ok);

        const SizeChannel &ch = conduitStyle()->widthChannel();
        QVERIFY(ch.enabled);
        QCOMPARE(ch.attribute, QStringLiteral("LinkFlow"));
        QCOMPARE(ch.minPx, 1.5);
        QCOMPARE(ch.maxPx, 9.0);
        QCOMPARE(ch.rangeMode, RangeMode::FixedOverRun);
        auto *g = dynamic_cast<OpenSWMM::Render::GraduatedRenderer *>(
            results->kindRenderer(SWMMModelLayer::CatConduits));
        QVERIFY(g);
        QVERIFY(!g->outputWidthEnabled());
        QVERIFY(!g->outputSizeEnabled());
        // Re-saving writes the channel and no axis, so a reload is stable.
        const QJsonObject resaved = StyleFileIO::styleToJson(results);
        QVERIFY(StyleFileIO::applyStyleJson(results, resaved).ok);
        QCOMPARE(StyleFileIO::styleToJson(results), resaved);
    }

    void kindPageTabs()
    {
        conduitStyle()->setWidthChannel(SizeChannel{});
        QUndoStack undo;
        LayerStyleDialog dialog(results, {}, nullptr, &undo);
        auto *panel = dialog.findChild<openswmmvis::ui::KindTreeSymbologyPanel *>();
        QVERIFY(panel);
        auto tabsFor = [&](const QString &routing) -> QTabWidget * {
            panel->focusKind(routing);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return panel->findChild<QTabWidget *>(QStringLiteral("kindStyleTabs"));
        };
        auto titles = [](QTabWidget *t) {
            QStringList out;
            for (int i = 0; t && i < t->count(); ++i) out << t->tabText(i);
            return out;
        };

        QTabWidget *tabs = tabsFor(QStringLiteral("results.conduits"));
        QCOMPARE(titles(tabs), (QStringList{"Color", "Size", "Arrows", "Labels"}));
        // Results kinds size through the Size tab, not the Graduated rows.
        for (auto *check : tabs->widget(0)->findChildren<QCheckBox *>())
            QVERIFY(check->text() != QLatin1String("Width by value") || !check->isVisibleTo(tabs));

        // The Size tab writes the sublayer style.
        tabs->setCurrentIndex(1);
        auto *enable = tabs->widget(1)->findChild<QCheckBox *>(QStringLiteral("sizeChannelEnabled"));
        QVERIFY(enable);
        enable->setChecked(true);
        QVERIFY(conduitStyle()->widthChannel().enabled);
        QCOMPARE(conduitStyle()->widthChannel().attribute, QStringLiteral("flow"));
        // ... and re-seeds from it (another view / Cancel rollback).
        conduitStyle()->setWidthChannel(SizeChannel{});
        QVERIFY(!enable->isChecked());

        tabs = tabsFor(QStringLiteral("results.junctions"));
        QCOMPARE(titles(tabs), (QStringList{"Color", "Size", "Labels"}));
        QCOMPARE(tabs->tabText(tabs->currentIndex()), QStringLiteral("Size"));   // remembered

        tabs = tabsFor(QStringLiteral("results.subcatchments"));
        QCOMPARE(titles(tabs), (QStringList{"Color", "Labels"}));
        QCOMPARE(tabs->currentIndex(), 0);
        dialog.reject();
    }

    void cleanupTestCase()
    {
        delete results;
        results = nullptr;
        window.reset();
        workspace.reset();
    }
};

QTEST_MAIN(TestResultsSizeChannel)
#include "test_resultssizechannel.moc"
