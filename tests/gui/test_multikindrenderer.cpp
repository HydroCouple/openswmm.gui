/*!
 * \file   test_multikindrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BI-MK.1 Phase 8.13.40 — unit tests for MultiKindRenderer.
 * Self-contained: pulls in only the render/*.cpp sources via CMakeLists.
 */

#include "render/multikindrenderer.h"

#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"

#include "render/featureref.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include <QJsonObject>
#include <QObject>
#include <QTest>

#include <memory>

using namespace OpenSWMM::Render;

namespace {

// Build a SingleSymbolRenderer with a single SimpleMarker symbol layer
// whose colour key carries the supplied hex string. Tests use this to
// produce visually-distinct per-kind renderers they can later identify
// by colour.
std::unique_ptr<SingleSymbolRenderer> makeColored(const QString &hexColor,
                                                  const QString &label = {})
{
    SymbolStyle style;
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props.insert(QStringLiteral("color"), hexColor);
    style.layers.append(layer);
    return std::make_unique<SingleSymbolRenderer>(std::move(style), label);
}

QString firstColor(const SymbolStyle &style)
{
    for (const SymbolLayer &sl : style.layers) {
        const auto it = sl.props.constFind(QStringLiteral("color"));
        if (it != sl.props.constEnd()) return it.value().toString();
    }
    return {};
}

FeatureRef featureWithKind(const QString &kind, int index = 0)
{
    FeatureRef f;
    f.layerId      = QStringLiteral("test-layer");
    f.featureIndex = index;
    f.categoryHint = kind;
    return f;
}

} // namespace

class TestMultiKindRenderer : public QObject
{
    Q_OBJECT
private slots:

    // ---- Construction + defaults -----------------------------------------

    void defaultCtor_hasFallback_noKinds()
    {
        MultiKindRenderer r;
        QCOMPARE(r.kindCount(), 0);
        QVERIFY(r.fallback() != nullptr);
        QCOMPARE(r.fallback()->rendererId(), QStringLiteral("single"));
        QCOMPARE(r.rendererId(), QStringLiteral("multikind"));
    }

    // ---- Per-kind set / get / clear --------------------------------------

    void setRendererFor_addsKind()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        QCOMPARE(r.kindCount(), 1);
        QVERIFY(r.rendererFor(QStringLiteral("Junctions")) != nullptr);
        QVERIFY(r.rendererFor(QStringLiteral("Conduits")) == nullptr);
    }

    void setRendererFor_nullRemoves()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        r.setRendererFor(QStringLiteral("Junctions"), nullptr);
        QCOMPARE(r.kindCount(), 0);
        QVERIFY(r.rendererFor(QStringLiteral("Junctions")) == nullptr);
    }

    void clearRendererFor_removes()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        r.setRendererFor(QStringLiteral("Conduits"), makeColored(QStringLiteral("#ddeeff")));
        r.clearRendererFor(QStringLiteral("Junctions"));
        QCOMPARE(r.kindCount(), 1);
        QVERIFY(r.rendererFor(QStringLiteral("Conduits")) != nullptr);
    }

    // ---- Dispatch --------------------------------------------------------

    void symbolFor_dispatchesByCategoryHint()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        r.setRendererFor(QStringLiteral("Conduits"),  makeColored(QStringLiteral("#ddeeff")));

        const SymbolStyle j = r.symbolFor(featureWithKind(QStringLiteral("Junctions")), {});
        const SymbolStyle c = r.symbolFor(featureWithKind(QStringLiteral("Conduits")), {});

        QCOMPARE(firstColor(j), QStringLiteral("#aabbcc"));
        QCOMPARE(firstColor(c), QStringLiteral("#ddeeff"));
    }

    void symbolFor_unknownKind_fallsBackToFallback()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        // Replace fallback with a distinctive colour so we can prove dispatch took the fallback branch.
        r.setFallback(makeColored(QStringLiteral("#112233")));

        const SymbolStyle out = r.symbolFor(featureWithKind(QStringLiteral("NoSuchKind")), {});
        QCOMPARE(firstColor(out), QStringLiteral("#112233"));
    }

    void symbolFor_emptyCategoryHint_usesFallback()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        r.setFallback(makeColored(QStringLiteral("#445566")));

        FeatureRef f = featureWithKind(QString());
        const SymbolStyle out = r.symbolFor(f, {});
        QCOMPARE(firstColor(out), QStringLiteral("#445566"));
    }

    void setFallback_nullKeepsCurrent()
    {
        MultiKindRenderer r;
        r.setFallback(makeColored(QStringLiteral("#aaaaaa")));
        r.setFallback(nullptr);  // contract: null is silently ignored
        QVERIFY(r.fallback() != nullptr);
        QCOMPARE(firstColor(r.symbolFor(featureWithKind(QStringLiteral("foo")), {})),
                 QStringLiteral("#aaaaaa"));
    }

    // ---- Kinds ordering --------------------------------------------------

    void kinds_returnsSortedKeys()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Conduits"),     makeColored(QStringLiteral("#000001")));
        r.setRendererFor(QStringLiteral("Junctions"),    makeColored(QStringLiteral("#000002")));
        r.setRendererFor(QStringLiteral("Subcatchments"),makeColored(QStringLiteral("#000003")));

        const auto k = r.kinds();
        QCOMPARE(k.size(), size_t(3));
        QCOMPARE(k[0], QStringLiteral("Conduits"));
        QCOMPARE(k[1], QStringLiteral("Junctions"));
        QCOMPARE(k[2], QStringLiteral("Subcatchments"));
    }

    // ---- Legend aggregation ----------------------------------------------

    void legendSymbolItems_aggregatesAcrossKinds_withKindPrefix()
    {
        MultiKindRenderer r;
        r.setRendererFor(QStringLiteral("Junctions"),
                         makeColored(QStringLiteral("#aabbcc"), QStringLiteral("Inflow Pt")));
        r.setRendererFor(QStringLiteral("Conduits"),
                         makeColored(QStringLiteral("#ddeeff")));

        const auto items = r.legendSymbolItems();
        QCOMPARE(items.size(), 2);

        // Sorted: "Conduits" first, "Junctions" second.
        QCOMPARE(items[0].userLabel, QStringLiteral("Conduits"));   // inner label was empty
        QCOMPARE(items[1].userLabel, QStringLiteral("Junctions / Inflow Pt"));
    }

    // ---- JSON round-trip -------------------------------------------------

    void toJson_fromJson_roundTripsPerKindRenderers()
    {
        MultiKindRenderer a;
        a.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        a.setRendererFor(QStringLiteral("Conduits"),  makeColored(QStringLiteral("#ddeeff")));
        a.setFallback(makeColored(QStringLiteral("#112233")));

        const QJsonObject j = a.toJson();
        QCOMPARE(j.value(QStringLiteral("id")).toString(), QStringLiteral("multikind"));
        QVERIFY(j.contains(QStringLiteral("kinds")));
        QVERIFY(j.contains(QStringLiteral("fallback")));

        MultiKindRenderer b;
        b.fromJson(j);
        QCOMPARE(b.kindCount(), 2);
        QCOMPARE(firstColor(b.symbolFor(featureWithKind(QStringLiteral("Junctions")), {})),
                 QStringLiteral("#aabbcc"));
        QCOMPARE(firstColor(b.symbolFor(featureWithKind(QStringLiteral("Conduits")), {})),
                 QStringLiteral("#ddeeff"));
        QCOMPARE(firstColor(b.symbolFor(featureWithKind(QStringLiteral("NoSuchKind")), {})),
                 QStringLiteral("#112233"));
    }

    void fromJson_emptyOrMalformed_silentDefaults()
    {
        MultiKindRenderer r;
        r.fromJson(QJsonObject{});            // empty JSON → no kinds, default fallback
        QCOMPARE(r.kindCount(), 0);
        QVERIFY(r.fallback() != nullptr);
        QCOMPARE(r.fallback()->rendererId(), QStringLiteral("single"));
    }

    void fromJson_unknownInnerIdSkipped()
    {
        MultiKindRenderer r;
        QJsonObject kindsObj;
        QJsonObject bad;
        bad.insert(QStringLiteral("id"), QStringLiteral("xyz-unknown-renderer"));
        kindsObj.insert(QStringLiteral("Junctions"), bad);
        QJsonObject root;
        root.insert(QStringLiteral("id"), QStringLiteral("multikind"));
        root.insert(QStringLiteral("kinds"), kindsObj);
        r.fromJson(root);
        QCOMPARE(r.kindCount(), 0);   // unknown id factories to nullptr, entry skipped
    }

    // ---- Clone independence ----------------------------------------------

    void clone_deepCopiesPerKindAndFallback()
    {
        MultiKindRenderer a;
        a.setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#aabbcc")));
        a.setFallback(makeColored(QStringLiteral("#112233")));

        auto bUp = a.clone();
        auto *b  = dynamic_cast<MultiKindRenderer *>(bUp.get());
        QVERIFY(b);
        QCOMPARE(b->kindCount(), 1);

        // Mutating B's Junctions renderer must not affect A's.
        b->setRendererFor(QStringLiteral("Junctions"), makeColored(QStringLiteral("#ffffff")));
        QCOMPARE(firstColor(a.symbolFor(featureWithKind(QStringLiteral("Junctions")), {})),
                 QStringLiteral("#aabbcc"));
        QCOMPARE(firstColor(b->symbolFor(featureWithKind(QStringLiteral("Junctions")), {})),
                 QStringLiteral("#ffffff"));
    }
};

QTEST_MAIN(TestMultiKindRenderer)
#include "test_multikindrenderer.moc"
