/**
 * @file      test_dataobject_reference_follow.cpp
 * @brief     Name references between data objects follow a rename or delete
 *            (workplans/GUI_COMPREHENSIVE_REVIEW_2026-11-12.md, E-05 follow-ups).
 *
 * An aquifer names its ET pattern and a custom inlet names its capture curve.
 * The engine rewrites those references when the pattern or curve is renamed
 * (and clears the pattern reference on delete), but the GUI's cached providers
 * kept the old name, so the next saveToEngine wrote it back.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"
#include "curve/curveprovider.h"
#include "curve/curveregistry.h"
#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"
#include "layers/swmmmodellayer.h"
#include "pattern/patternprovider.h"
#include "pattern/patternregistry.h"

#include <openswmm/engine/openswmm_subcatchments.h>

#include <QDir>
#include <QTest>

#include <memory>

using namespace openswmmvis;

namespace
{
std::unique_ptr<SWMMModelLayer> openLayer()
{
    const QString inp = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
                            .filePath(QStringLiteral("reference_rename_fixture.inp"));
    auto layer = std::make_unique<SWMMModelLayer>(inp, nullptr);
    QList<QString> warnings, errors;
    if (!layer->loadModel(warnings, errors))
        return nullptr;
    return layer;
}

/** @brief The engine's ET pattern name for aquifer @p name. */
QString engineEvapPattern(SWMM_Engine e, const char *name)
{
    const int idx = swmm_aquifer_index(e, name);
    char buf[128]{};
    if (idx < 0 || swmm_aquifer_get_evap_pattern(e, idx, buf, sizeof buf) != SWMM_OK)
        return QStringLiteral("<error>");
    return QString::fromUtf8(buf);
}
} // namespace

class TestDataObjectReferenceFollow : public QObject
{
    Q_OBJECT

private slots:
    /** @brief Pattern rename then delete: the aquifer's cached ET pattern follows, and saving keeps the engine's value. */
    void aquiferFollowsPatternRenameAndDelete()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        SWMM_Engine e = layer->engine();
        auto *aquifers = qobject_cast<aquifer::AquiferRegistry *>(layer->ensureAquiferRegistry());
        auto *patterns = qobject_cast<pattern::PatternRegistry *>(layer->ensurePatternRegistry());
        QVERIFY(aquifers && patterns);
        aquifer::AquiferProvider *aq1 = aquifers->findByName(QStringLiteral("AQ1"));
        QVERIFY(aq1);
        QCOMPARE(aq1->evapPattern(), QStringLiteral("ETPat"));

        QVERIFY(patterns->rename(patterns->findByName(QStringLiteral("ETPat")),
                                 QStringLiteral("ETMonthly")));
        QCOMPARE(aq1->evapPattern(), QStringLiteral("ETMonthly"));
        aquifers->saveToEngine();
        QCOMPARE(engineEvapPattern(e, "AQ1"), QStringLiteral("ETMonthly"));

        patterns->remove(patterns->findByName(QStringLiteral("ETMonthly")));
        QVERIFY(aq1->evapPattern().isEmpty());
        aquifers->saveToEngine();
        QCOMPARE(engineEvapPattern(e, "AQ1"), QString());
    }

    /** @brief Curve rename: the custom inlet's cached capture curve follows, and a save + reload keeps it. */
    void inletFollowsCurveRename()
    {
        auto layer = openLayer();
        QVERIFY(layer);
        auto *inlets = qobject_cast<inlet::InletRegistry *>(layer->ensureInletRegistry());
        auto *curves = qobject_cast<curve::CurveRegistry *>(layer->ensureCurveRegistry());
        QVERIFY(inlets && curves);
        inlet::InletProvider *in1 = inlets->findByName(QStringLiteral("IN1"));
        QVERIFY(in1);
        QCOMPARE(in1->curveId(), QStringLiteral("CapCurve"));

        QVERIFY(curves->rename(curves->findByName(QStringLiteral("CapCurve")),
                               QStringLiteral("Capture")));
        QCOMPARE(in1->curveId(), QStringLiteral("Capture"));
        inlets->saveToEngine();

        inlet::InletRegistry reread;
        QVERIFY(reread.loadFromEngine(layer->engine()) >= 1);
        QVERIFY(reread.findByName(QStringLiteral("IN1")));
        QCOMPARE(reread.findByName(QStringLiteral("IN1"))->curveId(), QStringLiteral("Capture"));
    }
};

QTEST_MAIN(TestDataObjectReferenceFollow)
#include "test_dataobject_reference_follow.moc"
