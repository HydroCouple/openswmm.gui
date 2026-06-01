/*!
 * \file   test_featuresublayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Replaces the legacy per-class tests (NodeMarker / ConduitLine /
 *         ConduitArrow / SubcatchmentFill) with one self-contained suite
 *         that exercises the granular FeatureSublayer for each archetype
 *         (Point / Line / Polygon) and round-trips the style bag JSON.
 *
 *         Slice U-0.
 */

#include "render/sublayers/feature/featuresublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"

#include <QJsonObject>
#include <QtTest/QtTest>

using OpenSWMM::Render::FeatureSublayer;
using OpenSWMM::Render::PointFeatureSublayerStyle;
using OpenSWMM::Render::LineFeatureSublayerStyle;
using OpenSWMM::Render::PolygonFeatureSublayerStyle;

class TestFeatureSublayer : public QObject
{
    Q_OBJECT
private slots:
    void archetypeMapping();
    void pointStyleDefaultsAndJsonRoundtrip();
    void lineStyleDefaultsAndJsonRoundtrip();
    void polygonStyleDefaultsAndJsonRoundtrip();
    void rainGageDefaultsToStatic();
    void styleChangedTriggersInvalidated();
    void visibilityAndOpacityEmitInvalidated();
};

void TestFeatureSublayer::archetypeMapping()
{
    using Cat = OpenSWMMVis::SwmmCategory;
    struct Row { Cat c; FeatureSublayer::Archetype a; };
    const Row rows[] = {
        { Cat::CatJunctions,     FeatureSublayer::Archetype::Point   },
        { Cat::CatOutfalls,      FeatureSublayer::Archetype::Point   },
        { Cat::CatStorage,       FeatureSublayer::Archetype::Point   },
        { Cat::CatDividers,      FeatureSublayer::Archetype::Point   },
        { Cat::CatConduits,      FeatureSublayer::Archetype::Line    },
        { Cat::CatPumps,         FeatureSublayer::Archetype::Line    },
        { Cat::CatOrifices,      FeatureSublayer::Archetype::Line    },
        { Cat::CatWeirs,         FeatureSublayer::Archetype::Line    },
        { Cat::CatOutlets,       FeatureSublayer::Archetype::Line    },
        { Cat::CatSubcatchments, FeatureSublayer::Archetype::Polygon },
        { Cat::CatRainGages,     FeatureSublayer::Archetype::Point   },
    };
    for (const Row &r : rows) {
        FeatureSublayer sub(r.c, QStringLiteral("test.id"), QStringLiteral("test"));
        QCOMPARE(sub.archetype(), r.a);
        QVERIFY(sub.style() != nullptr);
    }
}

void TestFeatureSublayer::pointStyleDefaultsAndJsonRoundtrip()
{
    FeatureSublayer sub(OpenSWMMVis::CatJunctions,
                        QStringLiteral("results.junctions"),
                        QStringLiteral("Junctions"));
    auto *ps = sub.pointStyle();
    QVERIFY(ps);
    QCOMPARE(ps->markerSizePx(), 6.0);
    QCOMPARE(ps->shape(), PointFeatureSublayerStyle::Circle);

    ps->setMarkerSizePx(11.5);
    ps->setShape(PointFeatureSublayerStyle::Diamond);
    ps->setColor(QColor(255, 128, 0));
    ps->setAttribute(QStringLiteral("head"));
    ps->setUseColorRamp(false);

    const QJsonObject j = ps->toJson();

    PointFeatureSublayerStyle reborn;
    reborn.fromJson(j);
    QCOMPARE(reborn.markerSizePx(), 11.5);
    QCOMPARE(reborn.shape(), PointFeatureSublayerStyle::Diamond);
    QCOMPARE(reborn.color(), QColor(255, 128, 0));
    QCOMPARE(reborn.attribute(), QStringLiteral("head"));
    QCOMPARE(reborn.useColorRamp(), false);
}

void TestFeatureSublayer::lineStyleDefaultsAndJsonRoundtrip()
{
    FeatureSublayer sub(OpenSWMMVis::CatConduits,
                        QStringLiteral("results.conduits"),
                        QStringLiteral("Conduits"));
    auto *ls = sub.lineStyle();
    QVERIFY(ls);
    QCOMPARE(ls->lineWidthPx(), 1.5);
    QCOMPARE(ls->dashPattern(), Qt::SolidLine);
    QCOMPARE(ls->renderAsLine(), true);
    QCOMPARE(ls->showFlowArrows(), false);

    ls->setLineWidthPx(3.25);
    ls->setDashPattern(Qt::DashDotLine);
    ls->setShowFlowArrows(true);
    ls->setArrowLengthPx(18.0);
    ls->setArrowWidthPx(9.0);
    ls->setArrowColor(QColor(0, 200, 0, 200));
    ls->setRenderAsLine(false);
    ls->setAttribute(QStringLiteral("velocity"));

    const QJsonObject j = ls->toJson();
    LineFeatureSublayerStyle reborn;
    reborn.fromJson(j);
    QCOMPARE(reborn.lineWidthPx(), 3.25);
    QCOMPARE(reborn.dashPattern(), Qt::DashDotLine);
    QCOMPARE(reborn.showFlowArrows(), true);
    QCOMPARE(reborn.arrowLengthPx(), 18.0);
    QCOMPARE(reborn.arrowWidthPx(), 9.0);
    QCOMPARE(reborn.arrowColor(), QColor(0, 200, 0, 200));
    QCOMPARE(reborn.renderAsLine(), false);
    QCOMPARE(reborn.attribute(), QStringLiteral("velocity"));
}

void TestFeatureSublayer::polygonStyleDefaultsAndJsonRoundtrip()
{
    FeatureSublayer sub(OpenSWMMVis::CatSubcatchments,
                        QStringLiteral("results.subcatchments"),
                        QStringLiteral("Subcatchments"));
    auto *ps = sub.polygonStyle();
    QVERIFY(ps);
    QCOMPARE(ps->outlineWidthPx(), 0.5);
    QCOMPARE(ps->fillOpacity(), 0.55);

    ps->setOutlineColor(QColor(80, 80, 80, 200));
    ps->setOutlineWidthPx(2.25);
    ps->setFillOpacity(0.42);
    ps->setAttribute(QStringLiteral("runoff"));

    const QJsonObject j = ps->toJson();
    PolygonFeatureSublayerStyle reborn;
    reborn.fromJson(j);
    QCOMPARE(reborn.outlineColor(), QColor(80, 80, 80, 200));
    QCOMPARE(reborn.outlineWidthPx(), 2.25);
    QCOMPARE(reborn.fillOpacity(), 0.42);
    QCOMPARE(reborn.attribute(), QStringLiteral("runoff"));
}

void TestFeatureSublayer::rainGageDefaultsToStatic()
{
    FeatureSublayer sub(OpenSWMMVis::CatRainGages,
                        QStringLiteral("results.raingages"),
                        QStringLiteral("Rain gages"));
    QVERIFY(!sub.isDynamic());                             // static sublayer
    QVERIFY(sub.style() != nullptr);
    QVERIFY(sub.featureStyle()->attribute().isEmpty());    // no result attr
    QCOMPARE(sub.featureStyle()->useColorRamp(), false);   // single-symbol
}

void TestFeatureSublayer::styleChangedTriggersInvalidated()
{
    FeatureSublayer sub(OpenSWMMVis::CatJunctions,
                        QStringLiteral("results.junctions"),
                        QStringLiteral("Junctions"));
    QSignalSpy spy(&sub, &OpenSWMM::Render::ISublayer::invalidated);
    sub.pointStyle()->setMarkerSizePx(9.0);
    QCOMPARE(spy.count(), 1);
}

void TestFeatureSublayer::visibilityAndOpacityEmitInvalidated()
{
    FeatureSublayer sub(OpenSWMMVis::CatConduits,
                        QStringLiteral("results.conduits"),
                        QStringLiteral("Conduits"));
    QSignalSpy spy(&sub, &OpenSWMM::Render::ISublayer::invalidated);
    sub.setVisible(false);
    QCOMPARE(spy.count(), 1);
    sub.setOpacity(0.5);
    QCOMPARE(spy.count(), 2);
    // No-op setter should NOT emit again.
    sub.setOpacity(0.5);
    QCOMPARE(spy.count(), 2);
}

QTEST_MAIN(TestFeatureSublayer)
#include "test_featuresublayer.moc"
