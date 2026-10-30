/**
 * @file      test_groundwaterrecipestore.cpp
 * @brief     Tests for groundwaterrecipestore.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/groundwaterrecipestore.h"
#include <QtTest>
class TestGroundwaterRecipeStore : public QObject
{
    Q_OBJECT
    /**
     *
     * @brief Build a minimal saved groundwater recipe fixture without sampling a raster.
     * @return Owned version-one snapshot-only Ks recipe for cells one/three, absolute
     * source path and placeholder hash abc; these strings are metadata, not verified
     * file content. Does not read/create recharge.tif or apply cell attributes.
     *
     */
    QJsonObject recipe()
    {
        return {{"version", 1},
                {"refreshPolicy", "snapshot-only"},
                {"target", "Ks"},
                {"cells", QJsonArray{1, 3}},
                {"sourcePath", "/project/data/recharge.tif"},
                {"sourceSha256", QJsonObject{{"/project/data/recharge.tif", "abc"}}}};
    }
private slots:
    /**
     *
     * @brief Require recipe append to emit once, restore to stay silent and serialization to
     * rebase source/hash-key paths without refreshing data.
     * All operations are in-memory JSON/path transformations; no raster/project file IO.
     *
     */
    void saveAsRebasesAndPreservesSnapshot()
    {
        GroundwaterRecipeStore s, t;
        QString error;
        QSignalSpy edits(&s, &GroundwaterRecipeStore::edited);
        QVERIFY(s.append(recipe(), &error));
        QCOMPARE(edits.count(), 1);
        QJsonArray data;
        QVERIFY(s.serialize("/project", data, &error));
        QCOMPARE(data[0].toObject()["sourcePath"].toString(), QString("data/recharge.tif"));
        QSignalSpy restored(&t, &GroundwaterRecipeStore::edited);
        QVERIFY(t.restore(data, "/project", &error));
        QCOMPARE(restored.count(), 0);
        QCOMPARE(t.events()[0].toObject()["sourcePath"].toString(),
                 QString("/project/data/recharge.tif"));
        QJsonArray relocated;
        QVERIFY(t.serialize("/project/other", relocated, &error));
        QCOMPARE(relocated[0].toObject()["sourcePath"].toString(), QString("../data/recharge.tif"));
        QVERIFY(
            relocated[0].toObject()["sourceSha256"].toObject().contains("../data/recharge.tif"));
    }
    /**
     *
     * @brief Require an unsupported recipe version during restore to preserve prior event count
     * and block subsequent serialization/appending.
     * Checks the caller output array stays unchanged on failed serialize; no file IO.
     *
     */
    void malformedPayloadCannotBeSilentlyLost()
    {
        GroundwaterRecipeStore s;
        QString error;
        QVERIFY(s.append(recipe(), &error));
        auto invalid = recipe();
        invalid["version"] = 99;
        QVERIFY(!s.restore(QJsonArray{recipe(), invalid}, "/project", &error));
        QCOMPARE(s.events().size(), 1);
        QJsonArray untouched{42};
        QVERIFY(!s.serialize("/project", untouched, &error));
        QCOMPARE(untouched, QJsonArray{42});
        QVERIFY(!s.append(recipe(), &error));
    }
    /**
     *
     * @brief Require a manual recipe empty source path to stay empty on serialization and
     * separate QObject owners to retain independent stores.
     *
     */
    void manualSourcesStayEmptyAndOwnersStaySeparate()
    {
        QObject a, b;
        auto *s = GroundwaterRecipeStore::forOwner(&a);
        auto *t = GroundwaterRecipeStore::forOwner(&b);
        auto manual = recipe();
        manual["sourcePath"] = "";
        manual.remove("sourceSha256");
        QVERIFY(s->append(manual));
        QJsonArray out;
        QVERIFY(s->serialize("/project", out));
        QVERIFY(out[0].toObject()["sourcePath"].toString().isEmpty());
        QVERIFY(t->events().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestGroundwaterRecipeStore)
#include "test_groundwaterrecipestore.moc"
