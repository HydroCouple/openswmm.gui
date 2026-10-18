/*!
 * \file   test_control_rule_apply.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Control-rule identity through SWMMModelLayer::applyControlRule*.
 *
 *         A rule's identity is its `RULE <name>` header: the engine derives
 *         the id from it and the registry is keyed by it. The tutorial bug
 *         report (2026-10-17) found two ways to break that agreement — a body
 *         saved without its header, and a name with spaces that the
 *         whitespace-delimited .inp format truncates to its first word — after
 *         which every later edit failed with "No rule named … found". These
 *         cases pin that such edits are refused with the engine untouched, and
 *         that a legitimately retyped header renames the rule on both sides.
 *
 *         Drives the real layer over the typed-selection fixture (J1, X1, O1,
 *         C1), so the rules reference objects that exist.
 */
#include "controls/controlruleprovider.h"
#include "controls/controlruleregistry.h"
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_controls.h>
#include <openswmm/engine/openswmm_engine.h>

#include <QDir>
#include <QObject>
#include <QTest>

using openswmmvis::controls::ControlRuleRegistry;

namespace {

QString fixturePath()
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(QStringLiteral("typed_selection_fixture.inp"));
}

const QString kR1Body = QStringLiteral(
    "RULE R1\nIF NODE J1 DEPTH > 5\nTHEN LINK C1 STATUS = CLOSED\n");
const QString kR2Body = QStringLiteral(
    "RULE R2\nIF NODE X1 DEPTH > 5\nTHEN LINK X1 STATUS = CLOSED\n");

QString engineRuleId(SWMM_Engine eng, int slot)
{
    char buf[256] = {};
    if (swmm_control_get_id(eng, slot, buf, sizeof(buf)) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

QString engineRuleBody(SWMM_Engine eng, int slot)
{
    char buf[8192] = {};
    if (swmm_control_get_rule(eng, slot, buf, sizeof(buf)) != SWMM_OK) return {};
    return QString::fromUtf8(buf);
}

} // namespace

class TestControlRuleApply : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void headerlessReplaceIsRefusedAndKeepsEveryRule();
    void retypedHeaderWithSpacesIsRefused();
    void renameWithSpacesIsRefused();
    void retypedHeaderRenamesEngineAndRegistry();
    void renameRewritesHeaderOnBothSides();

private:
    ControlRuleRegistry *registry() const;

    SWMMModelLayer *mLayer = nullptr;
};

void TestControlRuleApply::init()
{
    mLayer = new SWMMModelLayer(fixturePath(), nullptr);
    QList<QString> warnings, errors;
    QVERIFY2(mLayer->loadModel(warnings, errors), "fixture failed to load");
    QVERIFY(mLayer->engine());
    QString err;
    QVERIFY2(mLayer->applyControlRuleAdd(QStringLiteral("R1"), kR1Body, &err), qPrintable(err));
    QVERIFY2(mLayer->applyControlRuleAdd(QStringLiteral("R2"), kR2Body, &err), qPrintable(err));
    QCOMPARE(swmm_control_count(mLayer->engine()), 2);
}

void TestControlRuleApply::cleanup()
{
    delete mLayer;
    mLayer = nullptr;
}

ControlRuleRegistry *TestControlRuleApply::registry() const
{
    return qobject_cast<ControlRuleRegistry *>(mLayer->ensureControlRuleRegistry());
}

void TestControlRuleApply::headerlessReplaceIsRefusedAndKeepsEveryRule()
{
    SWMM_Engine eng = mLayer->engine();
    QString err;
    const bool ok = mLayer->applyControlRuleReplace(
        QStringLiteral("R1"),
        QStringLiteral("IF NODE J1 DEPTH > 1\nTHEN LINK C1 STATUS = OPEN\n"), &err);
    QVERIFY2(!ok, "a body without a RULE header must be refused");
    QVERIFY(!err.isEmpty());

    QCOMPARE(swmm_control_count(eng), 2);
    QCOMPARE(engineRuleId(eng, 0), QStringLiteral("R1"));
    QCOMPARE(engineRuleBody(eng, 0).trimmed(), kR1Body.trimmed());
    QCOMPARE(engineRuleId(eng, 1), QStringLiteral("R2"));
    QVERIFY(registry()->hasName(QStringLiteral("R1")));
    QCOMPARE(registry()->findByName(QStringLiteral("R1"))->body(), kR1Body);
}

void TestControlRuleApply::retypedHeaderWithSpacesIsRefused()
{
    SWMM_Engine eng = mLayer->engine();
    QString err;
    const bool ok = mLayer->applyControlRuleReplace(
        QStringLiteral("R1"),
        QStringLiteral("RULE my rule\nIF NODE J1 DEPTH > 5\nTHEN LINK C1 STATUS = CLOSED\n"),
        &err);
    QVERIFY2(!ok, "the .inp format would save 'my rule' as 'my'");
    QVERIFY(!err.isEmpty());
    QCOMPARE(swmm_control_count(eng), 2);
    QCOMPARE(engineRuleId(eng, 0), QStringLiteral("R1"));

    // The rule is still addressable by the name the registry holds.
    QVERIFY2(mLayer->applyControlRuleReplace(QStringLiteral("R1"), kR1Body, &err), qPrintable(err));
}

void TestControlRuleApply::renameWithSpacesIsRefused()
{
    SWMM_Engine eng = mLayer->engine();
    QString err;
    QVERIFY(!mLayer->applyControlRuleRename(QStringLiteral("R1"), QStringLiteral("my rule"), &err));
    QVERIFY(!err.isEmpty());
    QCOMPARE(engineRuleId(eng, 0), QStringLiteral("R1"));
    QVERIFY(registry()->hasName(QStringLiteral("R1")));
    QVERIFY(!registry()->hasName(QStringLiteral("my rule")));
}

void TestControlRuleApply::retypedHeaderRenamesEngineAndRegistry()
{
    SWMM_Engine eng = mLayer->engine();
    const QString renamed = QStringLiteral(
        "RULE R9\nIF NODE J1 DEPTH > 5\nTHEN LINK C1 STATUS = CLOSED\n");
    QString err;
    QVERIFY2(mLayer->applyControlRuleReplace(QStringLiteral("R1"), renamed, &err), qPrintable(err));

    QCOMPARE(swmm_control_count(eng), 2);
    QCOMPARE(engineRuleId(eng, 0), QStringLiteral("R9"));
    QVERIFY2(registry()->hasName(QStringLiteral("R9")),
             "the registry must follow the header the engine now reports");
    QVERIFY(!registry()->hasName(QStringLiteral("R1")));

    // The rule stays editable under its new name — this is the edit that used
    // to fail forever with "No rule named … found".
    const QString edited = QStringLiteral(
        "RULE R9\nIF NODE J1 DEPTH > 2\nTHEN LINK C1 STATUS = OPEN\n");
    QVERIFY2(mLayer->applyControlRuleReplace(QStringLiteral("R9"), edited, &err), qPrintable(err));
    QCOMPARE(engineRuleBody(eng, 0).trimmed(), edited.trimmed());
    QCOMPARE(registry()->findByName(QStringLiteral("R9"))->body(), edited);

    // A retyped header may not collide with another rule.
    QVERIFY(!mLayer->applyControlRuleReplace(QStringLiteral("R9"), kR2Body, &err));
    QCOMPARE(engineRuleId(eng, 0), QStringLiteral("R9"));
}

void TestControlRuleApply::renameRewritesHeaderOnBothSides()
{
    SWMM_Engine eng = mLayer->engine();
    QString err;
    QVERIFY2(mLayer->applyControlRuleRename(QStringLiteral("R1"), QStringLiteral("Overflow"), &err),
             qPrintable(err));
    QCOMPARE(engineRuleId(eng, 0), QStringLiteral("Overflow"));
    QVERIFY(registry()->hasName(QStringLiteral("Overflow")));
    QVERIFY(engineRuleBody(eng, 0).startsWith(QStringLiteral("RULE Overflow")));
    QVERIFY2(mLayer->applyControlRuleReplace(
                 QStringLiteral("Overflow"),
                 QStringLiteral("RULE Overflow\nIF NODE J1 DEPTH > 9\nTHEN LINK C1 STATUS = CLOSED\n"),
                 &err),
             qPrintable(err));
}

QTEST_MAIN(TestControlRuleApply)
#include "test_control_rule_apply.moc"
