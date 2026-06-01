/*!
 * \file   test_rule.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.1 — Rule + RuleList data model.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.1):
 *           - JSON round-trip fixtures pass.
 *           - Legacy sublayer-key migration produces a Rule list whose
 *             name + visibility + opacity track the source sublayers.
 *
 *         No paint / pixel tests in this slice — Z.2/Z.3 ship the UI
 *         that consumes the model; Z.6 covers pixel-level migration.
 */

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "ui/dialogs/rulestylesubject.h"

using namespace OpenSWMM::Render;
using openswmmvis::ui::ILayerStyleSubject;
using openswmmvis::ui::RuleStyleSubject;
using openswmmvis::ui::subjectsFromRuleList;

class TestRule : public QObject
{
    Q_OBJECT

private slots:
    // ── Rule ──────────────────────────────────────────────────────────
    void rule_defaultConstructorHasSingleRenderer();
    void rule_namedConstructorKeepsRenderer();
    void rule_namedConstructorWithNullRendererSubstitutes();
    void rule_propertyEditsEmitSignal();
    void rule_propertyEditsAreIdempotent();
    void rule_setRendererEmitsBothSignals();
    void rule_setRendererNullSubstitutes();
    void rule_jsonRoundTripDefault();
    void rule_jsonRoundTripPopulated();
    void rule_jsonRoundTripAllFourRendererKinds();
    void rule_jsonFromMissingRendererReturnsNull();
    void rule_jsonFromUnknownRendererIdReturnsNull();
    void rule_jsonTolerantOfMissingOptionalKeys();
    void rule_cloneIsIndependent();
    void rule_cloneCarriesAllProperties();

    // ── RuleList ──────────────────────────────────────────────────────
    void ruleList_emptyByDefault();
    void ruleList_appendActivatesFirstRule();
    void ruleList_appendDoesNotChangeActiveOnSubsequentAppends();
    void ruleList_insertShiftsActiveIndex();
    void ruleList_removeAdjustsActiveIndex();
    void ruleList_moveTracksActiveRule();
    void ruleList_clearResetsState();
    void ruleList_setActiveIndexClamps();
    void ruleList_indexOfFindsRule();
    void ruleList_jsonArrayRoundTrip();
    void ruleList_jsonSkipsMalformedEntries();
    void ruleList_signalsFireOnStructuralChange();
    void ruleList_signalsFireOnRulePropertyChange();
    void ruleList_movePreservesOrder();
    void ruleList_removeIndexBeforeActiveKeepsRuleSelected();

    // ── Legacy migration ──────────────────────────────────────────────
    void migration_emptySublayersArrayIsNoop();
    void migration_eachSublayerBecomesOneRule();
    void migration_preservesNameVisibilityOpacity();
    void migration_invisibleSublayerProducesInvisibleRule();
    void migration_opacityClampsToUnitInterval();
    void migration_appendsToExistingRules();
    void migration_missingIdGetsPlaceholderName();
    void migration_resultantRulesRoundTripThroughJson();

    // ── Z.7 — Rule.rebinPerFrame ─────────────────────────────────────
    void rebinPerFrame_defaultsFalse();
    void rebinPerFrame_setterEmitsSignal();
    void rebinPerFrame_isIdempotent();
    void rebinPerFrame_omittedFromJsonWhenDefault();
    void rebinPerFrame_jsonRoundTripWhenSet();
    void rebinPerFrame_cloneCarriesValue();

    // ── Z.11 — Rule.symbolLevelsEnabled ──────────────────────────────
    void symbolLevels_defaultsFalse();
    void symbolLevels_setterEmitsSignal();
    void symbolLevels_jsonRoundTripWhenSet();
    void symbolLevels_cloneCarriesValue();

    // ── Z.2 — Rule-as-ILayerStyleSubject adapter ─────────────────────
    void subject_titleTracksRuleName();
    void subject_titleFallsBackWhenRuleNameEmpty();
    void subject_propertyObjectIsTheRule();
    void subject_routingIdIsExposed();
    void subject_sectionDefaultsToRules();
    void subject_sectionCanBeOverridden();
    void subjectsFromRuleList_emptyForNullList();
    void subjectsFromRuleList_emptyForEmptyList();
    void subjectsFromRuleList_oneSubjectPerRule();
    void subjectsFromRuleList_routingIdsAreSequential();
    void subjectsFromRuleList_titlesMatchRuleNames();
};

// ───────────────────────── Rule ─────────────────────────────────────

void TestRule::rule_defaultConstructorHasSingleRenderer()
{
    Rule r;
    QVERIFY(r.renderer() != nullptr);
    QCOMPARE(r.renderer()->rendererId(), QStringLiteral("single"));
    QCOMPARE(r.name(), QString());
    QCOMPARE(r.isVisible(), true);
    QCOMPARE(r.filterExpression(), QString());
    QCOMPARE(r.minScale(), 0.0);
    QCOMPARE(r.maxScale(), 0.0);
    QCOMPARE(r.blendMode(), QStringLiteral("Normal"));
}

void TestRule::rule_namedConstructorKeepsRenderer()
{
    auto g = std::make_unique<GraduatedRenderer>();
    auto *raw = g.get();
    Rule r(QStringLiteral("Junctions by depth"), std::move(g));
    QCOMPARE(r.name(), QStringLiteral("Junctions by depth"));
    QCOMPARE(r.renderer(), raw);
}

void TestRule::rule_namedConstructorWithNullRendererSubstitutes()
{
    Rule r(QStringLiteral("Placeholder"), nullptr);
    QVERIFY(r.renderer() != nullptr);
    QCOMPARE(r.renderer()->rendererId(), QStringLiteral("single"));
}

void TestRule::rule_propertyEditsEmitSignal()
{
    Rule r;
    QSignalSpy spy(&r, &Rule::ruleChanged);

    r.setName(QStringLiteral("X"));
    r.setVisible(false);
    r.setFilterExpression(QStringLiteral("depth > 0"));
    r.setMinScale(100.0);
    r.setMaxScale(5000.0);
    r.setBlendMode(QStringLiteral("Multiply"));

    QCOMPARE(spy.count(), 6);
}

void TestRule::rule_propertyEditsAreIdempotent()
{
    Rule r;
    r.setName(QStringLiteral("X"));
    QSignalSpy spy(&r, &Rule::ruleChanged);
    r.setName(QStringLiteral("X"));      // no-op
    r.setVisible(true);                  // already true
    r.setMinScale(0.0);                  // already 0
    r.setBlendMode(QStringLiteral("Normal"));  // already Normal
    QCOMPARE(spy.count(), 0);
}

void TestRule::rule_setRendererEmitsBothSignals()
{
    Rule r;
    QSignalSpy replaced(&r, &Rule::rendererReplaced);
    QSignalSpy changed(&r, &Rule::ruleChanged);
    r.setRenderer(std::make_unique<GraduatedRenderer>());
    QCOMPARE(replaced.count(), 1);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(r.renderer()->rendererId(), QStringLiteral("graduated"));
}

void TestRule::rule_setRendererNullSubstitutes()
{
    Rule r;
    r.setRenderer(std::unique_ptr<IFeatureRenderer>{});
    QVERIFY(r.renderer() != nullptr);
    QCOMPARE(r.renderer()->rendererId(), QStringLiteral("single"));
}

void TestRule::rule_jsonRoundTripDefault()
{
    Rule r;
    const QJsonObject j = r.toJson();

    auto back = Rule::fromJson(j);
    QVERIFY(back);
    QCOMPARE(back->name(), r.name());
    QCOMPARE(back->isVisible(), r.isVisible());
    QCOMPARE(back->filterExpression(), r.filterExpression());
    QCOMPARE(back->minScale(), r.minScale());
    QCOMPARE(back->maxScale(), r.maxScale());
    QCOMPARE(back->blendMode(), r.blendMode());
    QCOMPARE(back->renderer()->rendererId(), r.renderer()->rendererId());
}

void TestRule::rule_jsonRoundTripPopulated()
{
    Rule r(QStringLiteral("Conduits by flow"), std::make_unique<GraduatedRenderer>());
    r.setVisible(false);
    r.setFilterExpression(QStringLiteral("flow > 0.1"));
    r.setMinScale(250.0);
    r.setMaxScale(25000.0);
    r.setBlendMode(QStringLiteral("Screen"));

    const QJsonObject j = r.toJson();
    auto back = Rule::fromJson(j);

    QVERIFY(back);
    QCOMPARE(back->name(), QStringLiteral("Conduits by flow"));
    QCOMPARE(back->isVisible(), false);
    QCOMPARE(back->filterExpression(), QStringLiteral("flow > 0.1"));
    QCOMPARE(back->minScale(), 250.0);
    QCOMPARE(back->maxScale(), 25000.0);
    QCOMPARE(back->blendMode(), QStringLiteral("Screen"));
    QCOMPARE(back->renderer()->rendererId(), QStringLiteral("graduated"));
}

void TestRule::rule_jsonRoundTripAllFourRendererKinds()
{
    const struct {
        const char *id;
        std::unique_ptr<IFeatureRenderer> (*make)();
    } cases[] = {
        {"single", [] { return std::unique_ptr<IFeatureRenderer>(new SingleSymbolRenderer); }},
        {"graduated", [] { return std::unique_ptr<IFeatureRenderer>(new GraduatedRenderer); }},
        {"categorized", [] { return std::unique_ptr<IFeatureRenderer>(new CategorizedRenderer); }},
        {"rule", [] { return std::unique_ptr<IFeatureRenderer>(new RuleBasedRenderer); }},
    };
    for (const auto &c : cases) {
        Rule r(QStringLiteral("R"), c.make());
        auto back = Rule::fromJson(r.toJson());
        QVERIFY2(back, c.id);
        QCOMPARE(back->renderer()->rendererId(), QString::fromLatin1(c.id));
    }
}

void TestRule::rule_jsonFromMissingRendererReturnsNull()
{
    QJsonObject j;
    j[QStringLiteral("name")] = QStringLiteral("Orphan");
    auto r = Rule::fromJson(j);
    QVERIFY(!r);
}

void TestRule::rule_jsonFromUnknownRendererIdReturnsNull()
{
    QJsonObject j;
    QJsonObject rend;
    rend[QStringLiteral("id")] = QStringLiteral("supercaliflavor");
    j[QStringLiteral("renderer")] = rend;
    auto r = Rule::fromJson(j);
    QVERIFY(!r);
}

void TestRule::rule_jsonTolerantOfMissingOptionalKeys()
{
    QJsonObject j;
    QJsonObject rend;
    rend[QStringLiteral("id")] = QStringLiteral("single");
    j[QStringLiteral("renderer")] = rend;
    // No name, no isVisible, no scale, no blend, no filter.
    auto r = Rule::fromJson(j);
    QVERIFY(r);
    QCOMPARE(r->isVisible(), true);          // default
    QCOMPARE(r->minScale(), 0.0);
    QCOMPARE(r->maxScale(), 0.0);
    QCOMPARE(r->blendMode(), QStringLiteral("Normal"));
}

void TestRule::rule_cloneIsIndependent()
{
    Rule r(QStringLiteral("Original"), std::make_unique<GraduatedRenderer>());
    auto copy = r.clone();
    QVERIFY(copy.get() != &r);
    QVERIFY(copy->renderer() != r.renderer());

    // Mutate the original; copy must not change.
    r.setName(QStringLiteral("Mutated"));
    QCOMPARE(copy->name(), QStringLiteral("Original"));
}

void TestRule::rule_cloneCarriesAllProperties()
{
    Rule r(QStringLiteral("Cloned"), std::make_unique<CategorizedRenderer>());
    r.setVisible(false);
    r.setFilterExpression(QStringLiteral("flow > 1.0"));
    r.setMinScale(1.0);
    r.setMaxScale(2.0);
    r.setBlendMode(QStringLiteral("Multiply"));
    auto copy = r.clone();
    QCOMPARE(copy->name(), QStringLiteral("Cloned"));
    QCOMPARE(copy->isVisible(), false);
    QCOMPARE(copy->filterExpression(), QStringLiteral("flow > 1.0"));
    QCOMPARE(copy->minScale(), 1.0);
    QCOMPARE(copy->maxScale(), 2.0);
    QCOMPARE(copy->blendMode(), QStringLiteral("Multiply"));
    QCOMPARE(copy->renderer()->rendererId(), QStringLiteral("categorized"));
}

// ───────────────────────── RuleList ─────────────────────────────────

void TestRule::ruleList_emptyByDefault()
{
    RuleList rl;
    QCOMPARE(rl.count(), 0);
    QVERIFY(rl.isEmpty());
    QCOMPARE(rl.activeIndex(), -1);
    QVERIFY(rl.activeRule() == nullptr);
}

void TestRule::ruleList_appendActivatesFirstRule()
{
    RuleList rl;
    QSignalSpy active(&rl, &RuleList::activeIndexChanged);
    QSignalSpy structural(&rl, &RuleList::ruleListChanged);

    auto *r = rl.append(std::make_unique<Rule>(QStringLiteral("first"), nullptr));
    QVERIFY(r != nullptr);
    QCOMPARE(rl.count(), 1);
    QCOMPARE(rl.activeIndex(), 0);
    QCOMPARE(active.count(), 1);
    QCOMPARE(structural.count(), 1);
}

void TestRule::ruleList_appendDoesNotChangeActiveOnSubsequentAppends()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>());
    QSignalSpy active(&rl, &RuleList::activeIndexChanged);
    rl.append(std::make_unique<Rule>());
    rl.append(std::make_unique<Rule>());
    QCOMPARE(rl.count(), 3);
    QCOMPARE(rl.activeIndex(), 0);
    QCOMPARE(active.count(), 0);
}

void TestRule::ruleList_insertShiftsActiveIndex()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));  // active=0
    rl.append(std::make_unique<Rule>(QStringLiteral("b"), nullptr));
    rl.setActiveIndex(1);  // active is 'b'

    rl.insert(0, std::make_unique<Rule>(QStringLiteral("z"), nullptr));
    QCOMPARE(rl.count(), 3);
    // 'b' was index 1, now it's index 2.
    QCOMPARE(rl.activeIndex(), 2);
    QCOMPARE(rl.activeRule()->name(), QStringLiteral("b"));
}

void TestRule::ruleList_removeAdjustsActiveIndex()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("c"), nullptr));
    rl.setActiveIndex(2);  // 'c'

    rl.remove(0);  // remove 'a' — 'c' shifts to 1
    QCOMPARE(rl.activeIndex(), 1);
    QCOMPARE(rl.activeRule()->name(), QStringLiteral("c"));
}

void TestRule::ruleList_moveTracksActiveRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("c"), nullptr));
    rl.setActiveIndex(1);  // 'b'

    rl.move(1, 2);  // 'b' to last position
    QCOMPARE(rl.activeIndex(), 2);
    QCOMPARE(rl.activeRule()->name(), QStringLiteral("b"));
}

void TestRule::ruleList_clearResetsState()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>());
    rl.append(std::make_unique<Rule>());
    QSignalSpy active(&rl, &RuleList::activeIndexChanged);
    QSignalSpy structural(&rl, &RuleList::ruleListChanged);

    rl.clear();
    QCOMPARE(rl.count(), 0);
    QCOMPARE(rl.activeIndex(), -1);
    QCOMPARE(active.count(), 1);
    QCOMPARE(structural.count(), 1);
}

void TestRule::ruleList_setActiveIndexClamps()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>());
    rl.append(std::make_unique<Rule>());

    rl.setActiveIndex(99);
    QCOMPARE(rl.activeIndex(), 1);
    rl.setActiveIndex(-7);
    QCOMPARE(rl.activeIndex(), -1);
}

void TestRule::ruleList_indexOfFindsRule()
{
    RuleList rl;
    Rule *a = rl.append(std::make_unique<Rule>());
    Rule *b = rl.append(std::make_unique<Rule>());
    QCOMPARE(rl.indexOf(a), 0);
    QCOMPARE(rl.indexOf(b), 1);
    QCOMPARE(rl.indexOf(nullptr), -1);
}

void TestRule::ruleList_jsonArrayRoundTrip()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"),
                                     std::make_unique<SingleSymbolRenderer>()));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"),
                                     std::make_unique<GraduatedRenderer>()));
    rl.append(std::make_unique<Rule>(QStringLiteral("c"),
                                     std::make_unique<CategorizedRenderer>()));
    rl.at(1)->setVisible(false);

    const QJsonArray arr = rl.toJson();
    QCOMPARE(arr.size(), 3);

    RuleList rl2;
    rl2.fromJson(arr);
    QCOMPARE(rl2.count(), 3);
    QCOMPARE(rl2.at(0)->name(), QStringLiteral("a"));
    QCOMPARE(rl2.at(1)->name(), QStringLiteral("b"));
    QCOMPARE(rl2.at(1)->isVisible(), false);
    QCOMPARE(rl2.at(2)->renderer()->rendererId(), QStringLiteral("categorized"));
}

void TestRule::ruleList_jsonSkipsMalformedEntries()
{
    QJsonArray arr;
    {
        QJsonObject good;
        QJsonObject rend;
        rend[QStringLiteral("id")] = QStringLiteral("single");
        good[QStringLiteral("renderer")] = rend;
        good[QStringLiteral("name")] = QStringLiteral("ok");
        arr.append(good);
    }
    arr.append(QJsonValue("not-an-object"));  // skip
    {
        QJsonObject orphan;  // no renderer → fromJson returns null → skip
        orphan[QStringLiteral("name")] = QStringLiteral("noisy");
        arr.append(orphan);
    }
    {
        QJsonObject good2;
        QJsonObject rend;
        rend[QStringLiteral("id")] = QStringLiteral("graduated");
        good2[QStringLiteral("renderer")] = rend;
        good2[QStringLiteral("name")] = QStringLiteral("ok2");
        arr.append(good2);
    }

    RuleList rl;
    rl.fromJson(arr);
    QCOMPARE(rl.count(), 2);
    QCOMPARE(rl.at(0)->name(), QStringLiteral("ok"));
    QCOMPARE(rl.at(1)->name(), QStringLiteral("ok2"));
}

void TestRule::ruleList_signalsFireOnStructuralChange()
{
    RuleList rl;
    QSignalSpy spy(&rl, &RuleList::ruleListChanged);

    rl.append(std::make_unique<Rule>());          // +1
    rl.append(std::make_unique<Rule>());          // +1
    rl.move(0, 1);                                // +1
    rl.remove(0);                                 // +1
    rl.clear();                                   // +1
    rl.clear();                                   // no-op (empty already)
    QCOMPARE(spy.count(), 5);
}

void TestRule::ruleList_signalsFireOnRulePropertyChange()
{
    RuleList rl;
    Rule *r = rl.append(std::make_unique<Rule>());
    QSignalSpy spy(&rl, &RuleList::ruleChanged);
    r->setName(QStringLiteral("Renamed"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 0);
}

void TestRule::ruleList_movePreservesOrder()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("c"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("d"), nullptr));
    rl.move(0, 3);
    QCOMPARE(rl.at(0)->name(), QStringLiteral("b"));
    QCOMPARE(rl.at(1)->name(), QStringLiteral("c"));
    QCOMPARE(rl.at(2)->name(), QStringLiteral("d"));
    QCOMPARE(rl.at(3)->name(), QStringLiteral("a"));
}

void TestRule::ruleList_removeIndexBeforeActiveKeepsRuleSelected()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("c"), nullptr));
    rl.setActiveIndex(2);
    Rule *active = rl.activeRule();
    QCOMPARE(active->name(), QStringLiteral("c"));

    rl.remove(0);
    QCOMPARE(rl.activeIndex(), 1);
    QCOMPARE(rl.activeRule(), active);   // same Rule, new index
}

// ─────────────── Legacy migration ────────────────────────────────────

void TestRule::migration_emptySublayersArrayIsNoop()
{
    RuleList rl;
    QSignalSpy spy(&rl, &RuleList::ruleListChanged);
    rl.loadLegacySublayersAsRules({});
    QCOMPARE(rl.count(), 0);
    QCOMPARE(spy.count(), 0);
}

void TestRule::migration_eachSublayerBecomesOneRule()
{
    QJsonArray arr;
    for (const char *id : { "results.junctions", "results.conduits", "results.subcatchments" }) {
        QJsonObject sl;
        sl[QStringLiteral("id")]        = QString::fromLatin1(id);
        sl[QStringLiteral("isVisible")] = true;
        sl[QStringLiteral("opacity")]   = 1.0;
        arr.append(sl);
    }
    RuleList rl;
    rl.loadLegacySublayersAsRules(arr);
    QCOMPARE(rl.count(), 3);
    QCOMPARE(rl.at(0)->name(), QStringLiteral("results.junctions"));
    QCOMPARE(rl.at(2)->name(), QStringLiteral("results.subcatchments"));
}

void TestRule::migration_preservesNameVisibilityOpacity()
{
    QJsonObject sl;
    sl[QStringLiteral("id")]        = QStringLiteral("results.conduits.lines");
    sl[QStringLiteral("isVisible")] = false;
    sl[QStringLiteral("opacity")]   = 0.65;
    QJsonArray arr;
    arr.append(sl);

    RuleList rl;
    rl.loadLegacySublayersAsRules(arr);
    QCOMPARE(rl.count(), 1);

    Rule *r = rl.at(0);
    QCOMPARE(r->name(), QStringLiteral("results.conduits.lines"));
    QCOMPARE(r->isVisible(), false);

    // The 0.65 opacity should ride on the SingleSymbolRenderer's symbol.opacity.
    auto *single = dynamic_cast<SingleSymbolRenderer *>(r->renderer());
    QVERIFY(single);
    QCOMPARE(single->symbol().opacity, 0.65);
}

void TestRule::migration_invisibleSublayerProducesInvisibleRule()
{
    QJsonObject sl;
    sl[QStringLiteral("id")]        = QStringLiteral("hidden");
    sl[QStringLiteral("isVisible")] = false;
    QJsonArray arr;
    arr.append(sl);

    RuleList rl;
    rl.loadLegacySublayersAsRules(arr);
    QCOMPARE(rl.at(0)->isVisible(), false);
}

void TestRule::migration_opacityClampsToUnitInterval()
{
    QJsonArray arr;
    QJsonObject high;
    high[QStringLiteral("id")] = QStringLiteral("over");
    high[QStringLiteral("opacity")] = 1.5;
    arr.append(high);
    QJsonObject low;
    low[QStringLiteral("id")] = QStringLiteral("under");
    low[QStringLiteral("opacity")] = -0.2;
    arr.append(low);

    RuleList rl;
    rl.loadLegacySublayersAsRules(arr);
    auto *s0 = dynamic_cast<SingleSymbolRenderer *>(rl.at(0)->renderer());
    auto *s1 = dynamic_cast<SingleSymbolRenderer *>(rl.at(1)->renderer());
    QVERIFY(s0);
    QVERIFY(s1);
    QCOMPARE(s0->symbol().opacity, 1.0);
    QCOMPARE(s1->symbol().opacity, 0.0);
}

void TestRule::migration_appendsToExistingRules()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("native"), nullptr));

    QJsonArray arr;
    QJsonObject sl;
    sl[QStringLiteral("id")] = QStringLiteral("legacy");
    arr.append(sl);

    rl.loadLegacySublayersAsRules(arr);
    QCOMPARE(rl.count(), 2);
    QCOMPARE(rl.at(0)->name(), QStringLiteral("native"));
    QCOMPARE(rl.at(1)->name(), QStringLiteral("legacy"));
}

void TestRule::migration_missingIdGetsPlaceholderName()
{
    QJsonArray arr;
    QJsonObject sl;
    sl[QStringLiteral("isVisible")] = true;
    arr.append(sl);

    RuleList rl;
    rl.loadLegacySublayersAsRules(arr);
    QCOMPARE(rl.count(), 1);
    QCOMPARE(rl.at(0)->name(), QStringLiteral("Sublayer"));
}

void TestRule::migration_resultantRulesRoundTripThroughJson()
{
    QJsonArray legacy;
    QJsonObject a;
    a[QStringLiteral("id")] = QStringLiteral("results.junctions");
    a[QStringLiteral("isVisible")] = true;
    a[QStringLiteral("opacity")] = 0.8;
    legacy.append(a);
    QJsonObject b;
    b[QStringLiteral("id")] = QStringLiteral("results.conduits");
    b[QStringLiteral("isVisible")] = false;
    b[QStringLiteral("opacity")] = 1.0;
    legacy.append(b);

    RuleList rl;
    rl.loadLegacySublayersAsRules(legacy);

    const QJsonArray modern = rl.toJson();
    RuleList rl2;
    rl2.fromJson(modern);
    QCOMPARE(rl2.count(), 2);
    QCOMPARE(rl2.at(0)->name(), QStringLiteral("results.junctions"));
    QCOMPARE(rl2.at(1)->isVisible(), false);
    auto *s0 = dynamic_cast<SingleSymbolRenderer *>(rl2.at(0)->renderer());
    QVERIFY(s0);
    QCOMPARE(s0->symbol().opacity, 0.8);
}

// ─────────────── Z.7 — Rule.rebinPerFrame ─────────────────────────────

void TestRule::rebinPerFrame_defaultsFalse()
{
    Rule r;
    QCOMPARE(r.rebinPerFrame(), false);
}

void TestRule::rebinPerFrame_setterEmitsSignal()
{
    Rule r;
    QSignalSpy spy(&r, &Rule::ruleChanged);
    r.setRebinPerFrame(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(r.rebinPerFrame(), true);
}

void TestRule::rebinPerFrame_isIdempotent()
{
    Rule r;
    r.setRebinPerFrame(true);
    QSignalSpy spy(&r, &Rule::ruleChanged);
    r.setRebinPerFrame(true);
    QCOMPARE(spy.count(), 0);
}

void TestRule::rebinPerFrame_omittedFromJsonWhenDefault()
{
    Rule r;
    QVERIFY(!r.toJson().contains(QStringLiteral("rebinPerFrame")));
}

void TestRule::rebinPerFrame_jsonRoundTripWhenSet()
{
    Rule r(QStringLiteral("dynamic"), nullptr);
    r.setRebinPerFrame(true);
    auto back = Rule::fromJson(r.toJson());
    QVERIFY(back);
    QCOMPARE(back->rebinPerFrame(), true);
}

void TestRule::rebinPerFrame_cloneCarriesValue()
{
    Rule r;
    r.setRebinPerFrame(true);
    auto copy = r.clone();
    QCOMPARE(copy->rebinPerFrame(), true);
}

// ─────────────── Z.11 — symbolLevelsEnabled ────────────────────────

void TestRule::symbolLevels_defaultsFalse()
{
    Rule r;
    QCOMPARE(r.symbolLevelsEnabled(), false);
}

void TestRule::symbolLevels_setterEmitsSignal()
{
    Rule r;
    QSignalSpy spy(&r, &Rule::ruleChanged);
    r.setSymbolLevelsEnabled(true);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(r.symbolLevelsEnabled(), true);
}

void TestRule::symbolLevels_jsonRoundTripWhenSet()
{
    Rule r(QStringLiteral("levels"), nullptr);
    r.setSymbolLevelsEnabled(true);
    auto back = Rule::fromJson(r.toJson());
    QVERIFY(back);
    QCOMPARE(back->symbolLevelsEnabled(), true);
}

void TestRule::symbolLevels_cloneCarriesValue()
{
    Rule r;
    r.setSymbolLevelsEnabled(true);
    auto copy = r.clone();
    QCOMPARE(copy->symbolLevelsEnabled(), true);
}

// ─────────────── Z.2 — Rule-as-ILayerStyleSubject ────────────────────

void TestRule::subject_titleTracksRuleName()
{
    Rule r(QStringLiteral("Junctions by depth"), nullptr);
    RuleStyleSubject s(&r, QStringLiteral("rule.0"));
    QCOMPARE(s.title(), QStringLiteral("Junctions by depth"));
}

void TestRule::subject_titleFallsBackWhenRuleNameEmpty()
{
    Rule r;
    RuleStyleSubject s(&r, QStringLiteral("rule.0"));
    QCOMPARE(s.title(), QStringLiteral("Rule"));
}

void TestRule::subject_propertyObjectIsTheRule()
{
    Rule r;
    RuleStyleSubject s(&r, QStringLiteral("rule.0"));
    QCOMPARE(s.propertyObject(), static_cast<QObject *>(&r));
}

void TestRule::subject_routingIdIsExposed()
{
    Rule r;
    RuleStyleSubject s(&r, QStringLiteral("rule.7"));
    QCOMPARE(s.routingId(), QStringLiteral("rule.7"));
}

void TestRule::subject_sectionDefaultsToRules()
{
    Rule r;
    RuleStyleSubject s(&r, QStringLiteral("rule.0"));
    QCOMPARE(s.section(), QStringLiteral("Rules"));
}

void TestRule::subject_sectionCanBeOverridden()
{
    Rule r;
    RuleStyleSubject s(&r, QStringLiteral("rule.0"), QStringLiteral("Custom"));
    QCOMPARE(s.section(), QStringLiteral("Custom"));
}

void TestRule::subjectsFromRuleList_emptyForNullList()
{
    auto subjects = subjectsFromRuleList(nullptr);
    QCOMPARE(static_cast<int>(subjects.size()), 0);
}

void TestRule::subjectsFromRuleList_emptyForEmptyList()
{
    RuleList rl;
    auto subjects = subjectsFromRuleList(&rl);
    QCOMPARE(static_cast<int>(subjects.size()), 0);
}

void TestRule::subjectsFromRuleList_oneSubjectPerRule()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("c"), nullptr));

    auto subjects = subjectsFromRuleList(&rl);
    QCOMPARE(static_cast<int>(subjects.size()), 3);
}

void TestRule::subjectsFromRuleList_routingIdsAreSequential()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("b"), nullptr));

    auto subjects = subjectsFromRuleList(&rl);
    QCOMPARE(subjects[0]->routingId(), QStringLiteral("rule.0"));
    QCOMPARE(subjects[1]->routingId(), QStringLiteral("rule.1"));
}

void TestRule::subjectsFromRuleList_titlesMatchRuleNames()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("First"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("Second"), nullptr));

    auto subjects = subjectsFromRuleList(&rl);
    QCOMPARE(subjects[0]->title(), QStringLiteral("First"));
    QCOMPARE(subjects[1]->title(), QStringLiteral("Second"));
}

QTEST_MAIN(TestRule)
#include "test_rule.moc"
