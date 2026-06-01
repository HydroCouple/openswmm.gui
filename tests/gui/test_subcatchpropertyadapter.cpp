/*!
 * \file   test_subcatchpropertyadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice TA round-trip tests for SWMMSubcatchPropertyAdapter.
 *         Mirror of `test_linkpropertyadapter.cpp` (Slice SA) for the
 *         subcatchment-side `tag` Q_PROPERTY. Exercises:
 *           - The `tag` getter+setter round-trip via the
 *             `swmm_subcatch_get_tag` / `_set_tag` engine accessors.
 *           - Rename keeps tag (tag is index-keyed in the engine, so it
 *             survives a `swmm_subcatch_rename`).
 *           - Multi-subcatchment isolation: setting a tag on one
 *             subcatchment must not change another's tag.
 *           - That the adapter advertises `tag` as a writable
 *             Q_PROPERTY (mirror of the link-side metaobject check).
 *           - That `displayLabelFor("tag")` resolves to a non-empty
 *             localized string.
 */

#include "ui/properties/swmmsubcatchpropertyadapter.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QObject>
#include <QSignalSpy>
#include <QTest>

namespace {

// Tiny fixture: two subcatchments S1 + S2. Both exist so the multi-
// subcatchment isolation case can verify writes to one don't bleed
// into the other.
SWMM_Engine buildTwoSubcatchFixture()
{
    SWMM_Engine e = swmm_engine_new();
    if (!e) return nullptr;
    swmm_subcatch_add(e, "S1");
    swmm_subcatch_add(e, "S2");
    return e;
}

} // namespace

class TestSubcatchPropertyAdapter : public QObject
{
    Q_OBJECT

private slots:

    // ====================================================================
    // Slice TA — tag round-trip
    // ====================================================================

    void tagRoundTrip()
    {
        SWMM_Engine e = buildTwoSubcatchFixture();
        QVERIFY(e);

        SWMMSubcatchPropertyAdapter a(e, QStringLiteral("S1"));
        QCOMPARE(a.tag(), QString{});

        QSignalSpy spy(&a, &SWMMSubcatchPropertyAdapter::changed);
        a.setTag(QStringLiteral("residential"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(a.tag(), QStringLiteral("residential"));

        // Overwriting with a different value re-reads the new string.
        a.setTag(QStringLiteral("commercial"));
        QCOMPARE(a.tag(), QStringLiteral("commercial"));

        // Clearing via empty string round-trips to empty.
        a.setTag(QString{});
        QCOMPARE(a.tag(), QString{});

        swmm_engine_destroy(e);
    }

    // Tag must survive a rename — the engine stores tags index-keyed,
    // so renaming the subcatchment id does not touch the tag storage.
    void renameKeepsTag()
    {
        SWMM_Engine e = buildTwoSubcatchFixture();
        QVERIFY(e);

        SWMMSubcatchPropertyAdapter a(e, QStringLiteral("S1"));
        a.setTag(QStringLiteral("downtown"));
        QCOMPARE(a.tag(), QStringLiteral("downtown"));

        // Rename via engine directly; then re-point the adapter at the
        // new name (mirrors what AttributePanel does after a successful
        // rename round-trip).
        const int i = swmm_subcatch_index(e, "S1");
        QVERIFY(i >= 0);
        QCOMPARE(swmm_subcatch_rename(e, i, "S1-renamed"), SWMM_OK);
        a.updateStoredName(QStringLiteral("S1-renamed"));
        QCOMPARE(a.tag(), QStringLiteral("downtown"));

        swmm_engine_destroy(e);
    }

    // Setting a tag on S1 must not bleed into S2.
    void multiSubcatchTagIsolation()
    {
        SWMM_Engine e = buildTwoSubcatchFixture();
        QVERIFY(e);

        SWMMSubcatchPropertyAdapter a1(e, QStringLiteral("S1"));
        SWMMSubcatchPropertyAdapter a2(e, QStringLiteral("S2"));

        a1.setTag(QStringLiteral("upstream"));
        a2.setTag(QStringLiteral("downstream"));

        QCOMPARE(a1.tag(), QStringLiteral("upstream"));
        QCOMPARE(a2.tag(), QStringLiteral("downstream"));

        // Re-write only S1; S2 must be untouched.
        a1.setTag(QStringLiteral("upstream-rev2"));
        QCOMPARE(a1.tag(), QStringLiteral("upstream-rev2"));
        QCOMPARE(a2.tag(), QStringLiteral("downstream"));

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice TA — meta-object contract: tag is writable
    // ====================================================================

    void tagAdvertisedAsWritable()
    {
        SWMM_Engine e = buildTwoSubcatchFixture();
        QVERIFY(e);

        SWMMSubcatchPropertyAdapter a(e, QStringLiteral("S1"));
        const auto *mo = a.metaObject();
        const int idx = mo->indexOfProperty("tag");
        QVERIFY2(idx >= 0, "Subcatchment adapter missing tag property");
        QVERIFY2(mo->property(idx).isWritable(),
                 "expected SWMMSubcatchPropertyAdapter.tag to be writable");

        swmm_engine_destroy(e);
    }

    // ====================================================================
    // Slice TA — display label resolves
    // ====================================================================

    void tagDisplayLabelResolves()
    {
        SWMM_Engine e = buildTwoSubcatchFixture();
        QVERIFY(e);

        SWMMSubcatchPropertyAdapter a(e, QStringLiteral("S1"));
        const QString label = a.displayLabelFor(QStringLiteral("tag"));
        QVERIFY(!label.isEmpty());

        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestSubcatchPropertyAdapter)
#include "test_subcatchpropertyadapter.moc"
