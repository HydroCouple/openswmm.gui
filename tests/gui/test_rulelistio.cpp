/*!
 * \file   test_rulelistio.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.17-files — RuleList .swmm-rule.json I/O.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.17):
 *           - save → load round-trip preserves every Rule field.
 *           - Save format includes envelope (format / version / rules /
 *             symbolLevels).
 *           - Loader permissive on missing format / version keys
 *             (warnings, not failures).
 *           - Malformed JSON / non-object root / missing rules array
 *             → ok=false with a descriptive error.
 *           - Unknown renderer ids inside a rule entry are skipped with
 *             warning, not abort.
 *           - Atomic semantics: failing load leaves destination
 *             RuleList unchanged.
 */

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/rulelistio.h"

using namespace OpenSWMM::Render;

class TestRuleListIO : public QObject
{
    Q_OBJECT
private slots:
    void init();

    // Save
    void save_writesEnvelopeKeys();
    void save_emitsRulesArray();
    void save_nullListWritesEmptyRules();
    void save_emptyPathReportsError();

    // Load
    void load_roundTripsThreeRules();
    void load_missingFileReportsError();
    void load_malformedJsonReportsError();
    void load_nonObjectRootReportsError();
    void load_missingRulesArrayReportsError();
    void load_missingFormatKeyWarnsButLoads();
    void load_versionMismatchWarnsButLoads();
    void load_unknownEnvelopeKeysWarn();
    void load_unknownRendererIdSkipsRuleWithWarning();
    void load_nullDestReportsError();
    void load_clearsExistingDestOnSuccess();

    // Combined
    void roundTrip_preservesEveryFieldOnRule();

private:
    QTemporaryDir m_tmpDir;
    QString tempPath(const QString &name) const {
        return m_tmpDir.path() + QDir::separator() + name;
    }

    static QString writeFile(const QString &path, const QByteArray &bytes)
    {
        QFile f(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
        f.write(bytes);
        f.close();
        return path;
    }
};

void TestRuleListIO::init()
{
    QVERIFY(m_tmpDir.isValid());
}

// ── Save ────────────────────────────────────────────────────────────

void TestRuleListIO::save_writesEnvelopeKeys()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("a"), nullptr));

    const QString path = tempPath(QStringLiteral("a.swmm-rule.json"));
    auto res = RuleListIO::save(&rl, path);
    QVERIFY2(res.ok, qPrintable(res.error));

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonObject env = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(env.value(QStringLiteral("format")).toString(),
             QStringLiteral("swmm-rule-json"));
    QCOMPARE(env.value(QStringLiteral("version")).toInt(), 1);
    QVERIFY(env.contains(QStringLiteral("rules")));
}

void TestRuleListIO::save_emitsRulesArray()
{
    RuleList rl;
    rl.append(std::make_unique<Rule>(QStringLiteral("first"), nullptr));
    rl.append(std::make_unique<Rule>(QStringLiteral("second"), nullptr));

    const QString path = tempPath(QStringLiteral("array.swmm-rule.json"));
    QVERIFY(RuleListIO::save(&rl, path).ok);

    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QJsonObject env = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonArray rules = env.value(QStringLiteral("rules")).toArray();
    QCOMPARE(rules.size(), 2);
    QCOMPARE(rules[0].toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("first"));
}

void TestRuleListIO::save_nullListWritesEmptyRules()
{
    const QString path = tempPath(QStringLiteral("null.swmm-rule.json"));
    auto res = RuleListIO::save(nullptr, path);
    QVERIFY(res.ok);

    QFile f(path);
    f.open(QIODevice::ReadOnly);
    const QJsonObject env = QJsonDocument::fromJson(f.readAll()).object();
    QCOMPARE(env.value(QStringLiteral("rules")).toArray().size(), 0);
}

void TestRuleListIO::save_emptyPathReportsError()
{
    RuleList rl;
    auto res = RuleListIO::save(&rl, QString());
    QVERIFY(!res.ok);
    QVERIFY(!res.error.isEmpty());
}

// ── Load ────────────────────────────────────────────────────────────

void TestRuleListIO::load_roundTripsThreeRules()
{
    RuleList src;
    src.append(std::make_unique<Rule>(QStringLiteral("a"),
                                      std::make_unique<SingleSymbolRenderer>()));
    src.append(std::make_unique<Rule>(QStringLiteral("b"),
                                      std::make_unique<GraduatedRenderer>()));
    src.append(std::make_unique<Rule>(QStringLiteral("c"),
                                      std::make_unique<CategorizedRenderer>()));
    src.at(1)->setVisible(false);

    const QString path = tempPath(QStringLiteral("round.swmm-rule.json"));
    QVERIFY(RuleListIO::save(&src, path).ok);

    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY2(res.ok, qPrintable(res.error));
    QCOMPARE(res.rulesLoaded, 3);
    QCOMPARE(res.rulesSkipped, 0);

    QCOMPARE(dst.count(), 3);
    QCOMPARE(dst.at(0)->name(), QStringLiteral("a"));
    QCOMPARE(dst.at(1)->name(), QStringLiteral("b"));
    QCOMPARE(dst.at(1)->isVisible(), false);
    QCOMPARE(dst.at(2)->renderer()->rendererId(), QStringLiteral("categorized"));
}

void TestRuleListIO::load_missingFileReportsError()
{
    RuleList dst;
    auto res = RuleListIO::load(tempPath(QStringLiteral("does-not-exist.json")), &dst);
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("not found")));
}

void TestRuleListIO::load_malformedJsonReportsError()
{
    const QString path = writeFile(
        tempPath(QStringLiteral("garbage.json")),
        QByteArrayLiteral("{ this is not json"));
    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("Malformed JSON")));
}

void TestRuleListIO::load_nonObjectRootReportsError()
{
    const QString path = writeFile(
        tempPath(QStringLiteral("array-root.json")),
        QByteArrayLiteral("[1, 2, 3]"));
    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("not a JSON object")));
}

void TestRuleListIO::load_missingRulesArrayReportsError()
{
    const QString path = writeFile(
        tempPath(QStringLiteral("no-rules.json")),
        QByteArrayLiteral("{\"format\":\"swmm-rule-json\",\"version\":1}"));
    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("rules")));
}

void TestRuleListIO::load_missingFormatKeyWarnsButLoads()
{
    const QString path = writeFile(
        tempPath(QStringLiteral("no-format.json")),
        QByteArrayLiteral("{\"version\":1,\"rules\":[]}"));
    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY2(res.ok, qPrintable(res.error));
    QVERIFY(!res.warnings.isEmpty());
}

void TestRuleListIO::load_versionMismatchWarnsButLoads()
{
    const QString path = writeFile(
        tempPath(QStringLiteral("v999.json")),
        QByteArrayLiteral("{\"format\":\"swmm-rule-json\",\"version\":999,\"rules\":[]}"));
    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY(res.ok);
    bool sawVersion = false;
    for (const auto &w : res.warnings)
        if (w.contains(QStringLiteral("Version mismatch"))) sawVersion = true;
    QVERIFY(sawVersion);
}

void TestRuleListIO::load_unknownEnvelopeKeysWarn()
{
    const QString path = writeFile(
        tempPath(QStringLiteral("extra.json")),
        QByteArrayLiteral("{\"format\":\"swmm-rule-json\",\"version\":1,"
                          "\"rules\":[],\"futureFeature\":42}"));
    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY(res.ok);
    bool sawUnknown = false;
    for (const auto &w : res.warnings)
        if (w.contains(QStringLiteral("futureFeature"))) sawUnknown = true;
    QVERIFY(sawUnknown);
}

void TestRuleListIO::load_unknownRendererIdSkipsRuleWithWarning()
{
    // Build a JSON file with one valid rule + one with bogus renderer id.
    QJsonObject good;
    good[QStringLiteral("name")] = QStringLiteral("kept");
    QJsonObject goodRend;
    goodRend[QStringLiteral("id")] = QStringLiteral("single");
    good[QStringLiteral("renderer")] = goodRend;

    QJsonObject bad;
    bad[QStringLiteral("name")] = QStringLiteral("dropped");
    QJsonObject badRend;
    badRend[QStringLiteral("id")] = QStringLiteral("noSuchRenderer");
    bad[QStringLiteral("renderer")] = badRend;

    QJsonObject env;
    env[QStringLiteral("format")] = QStringLiteral("swmm-rule-json");
    env[QStringLiteral("version")] = 1;
    env[QStringLiteral("rules")] = QJsonArray{ good, bad };

    const QString path = writeFile(
        tempPath(QStringLiteral("mixed.json")),
        QJsonDocument(env).toJson());

    RuleList dst;
    auto res = RuleListIO::load(path, &dst);
    QVERIFY(res.ok);
    QCOMPARE(res.rulesLoaded, 1);
    QCOMPARE(res.rulesSkipped, 1);
    QCOMPARE(dst.count(), 1);
    QCOMPARE(dst.at(0)->name(), QStringLiteral("kept"));

    bool sawSkipNote = false;
    for (const auto &w : res.warnings)
        if (w.contains(QStringLiteral("noSuchRenderer"))) sawSkipNote = true;
    QVERIFY(sawSkipNote);
}

void TestRuleListIO::load_nullDestReportsError()
{
    auto res = RuleListIO::load(tempPath(QStringLiteral("any.json")), nullptr);
    QVERIFY(!res.ok);
    QVERIFY(res.error.contains(QStringLiteral("null")));
}

void TestRuleListIO::load_clearsExistingDestOnSuccess()
{
    RuleList dst;
    dst.append(std::make_unique<Rule>(QStringLiteral("preExisting"), nullptr));
    QCOMPARE(dst.count(), 1);

    // Write a 2-rule file.
    RuleList src;
    src.append(std::make_unique<Rule>(QStringLiteral("new1"), nullptr));
    src.append(std::make_unique<Rule>(QStringLiteral("new2"), nullptr));
    const QString path = tempPath(QStringLiteral("clear.json"));
    QVERIFY(RuleListIO::save(&src, path).ok);

    auto res = RuleListIO::load(path, &dst);
    QVERIFY(res.ok);
    QCOMPARE(dst.count(), 2);
    QCOMPARE(dst.at(0)->name(), QStringLiteral("new1"));
}

// ── Combined ────────────────────────────────────────────────────────

void TestRuleListIO::roundTrip_preservesEveryFieldOnRule()
{
    // Build a "complete" Rule heap-side so we can append the owning
    // unique_ptr without copy/move of Rule itself (Rule is non-copyable
    // / non-movable by design).
    auto rule = std::make_unique<Rule>(QStringLiteral("complete"),
                                       std::make_unique<GraduatedRenderer>());
    rule->setVisible(false);
    rule->setFilterExpression(QStringLiteral("flow > 0.5"));
    rule->setMinScale(100.0);
    rule->setMaxScale(5000.0);
    rule->setBlendMode(QStringLiteral("Multiply"));
    rule->setRebinPerFrame(true);
    rule->setSymbolLevelsEnabled(true);

    RuleList src;
    src.append(std::move(rule));

    const QString path = tempPath(QStringLiteral("complete.json"));
    QVERIFY(RuleListIO::save(&src, path).ok);

    RuleList dst;
    QVERIFY(RuleListIO::load(path, &dst).ok);

    QCOMPARE(dst.count(), 1);
    auto *back = dst.at(0);
    QCOMPARE(back->name(),                 QStringLiteral("complete"));
    QCOMPARE(back->isVisible(),            false);
    QCOMPARE(back->filterExpression(),     QStringLiteral("flow > 0.5"));
    QCOMPARE(back->minScale(),             100.0);
    QCOMPARE(back->maxScale(),             5000.0);
    QCOMPARE(back->blendMode(),            QStringLiteral("Multiply"));
    QCOMPARE(back->rebinPerFrame(),        true);
    QCOMPARE(back->symbolLevelsEnabled(),  true);
    QCOMPARE(back->renderer()->rendererId(), QStringLiteral("graduated"));
}

QTEST_MAIN(TestRuleListIO)
#include "test_rulelistio.moc"
