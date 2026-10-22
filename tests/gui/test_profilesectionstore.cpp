/**
 * @file      test_profilesectionstore.cpp
 * @brief     Tests for profilesectionstore.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/profilesectionstore.h"
#include <QtTest>
class TestProfileSectionStore : public QObject
{
    Q_OBJECT
    /**
     *
     * @brief Build the valid profile definition reused by owner/restore tests.
     * @return Owned section-a definition with two-point scene path in EPSG:32618/metres,
     * source-a and path /models/run.h5. That path is metadata only and need not exist;
     * no file/model is opened or store mutated here.
     *
     */
    ProfileSection::Definition definition()
    {
        ProfileSection::Definition d;
        d.id = "section-a";
        d.title = "Combined levels";
        d.scenePolyline = {{0, 0}, {10, 0}};
        d.sceneCRS = "EPSG:32618";
        d.horizontalUnits = "m";
        d.primarySourceId = "source-a";
        d.sources.append({"source-a", "/models/run.h5", {}});
        return d;
    }
private slots:
    /**
     *
     * @brief Require per-owner definition isolation, silent identical save and exact edit signals
     * for replacement/removal.
     *
     */
    void saveReplaceNoOpAndOwnerIsolation()
    {
        QObject a, b;
        auto *s = ProfileSectionStore::forOwner(&a);
        auto *t = ProfileSectionStore::forOwner(&b);
        QSignalSpy edits(s, &ProfileSectionStore::edited);
        QString error;
        auto d = definition();
        QVERIFY2(s->saveDefinition(d, &error), qPrintable(error));
        QCOMPARE(edits.count(), 1);
        QVERIFY(s->saveDefinition(d, &error));
        QCOMPARE(edits.count(), 1);
        QVERIFY(t->definitions().isEmpty());
        d.title = "Revised";
        QVERIFY(s->saveDefinition(d, &error));
        QCOMPARE(s->definitions().size(), 1);
        QCOMPARE(edits.count(), 2);
        QVERIFY(!s->removeDefinition("unknown"));
        QCOMPARE(edits.count(), 2);
        QVERIFY(s->removeDefinition(d.id));
        QCOMPARE(edits.count(), 3);
    }
    /**
     *
     * @brief Require JSON restore to preserve an unavailable source path without emitting edited
     * or opening that source.
     *
     */
    void restoreDoesNotMarkDirtyAndRetainsUnavailableSource()
    {
        ProfileSectionStore original, restored;
        QString error;
        QVERIFY(original.saveDefinition(definition(), &error));
        QJsonArray data;
        QVERIFY(original.serialize("/models", data, &error));
        QSignalSpy edits(&restored, &ProfileSectionStore::edited);
        QVERIFY2(restored.restore(data, "/models", &error), qPrintable(error));
        QCOMPARE(edits.count(), 0);
        QCOMPARE(restored.definitions()[0].sources[0].path, QString("/models/run.h5"));
    }
    /**
     *
     * @brief Require malformed restore to retain prior sections and block later
     * serialization/save/remove, preserving the caller output sentinel.
     *
     */
    void malformedRestoreCannotEraseOrResavePriorSections()
    {
        ProfileSectionStore s;
        QString error;
        QVERIFY(s.saveDefinition(definition(), &error));
        QVERIFY(!s.restore(QJsonArray{42}, "/models", &error));
        QCOMPARE(s.definitions().size(), 1);
        QJsonArray out{QString("sentinel")};
        QVERIFY(!s.serialize("/models", out, &error));
        QCOMPARE(out, QJsonArray{QString("sentinel")});
        QVERIFY(!s.saveDefinition(definition(), &error));
        QVERIFY(!s.removeDefinition("section-a"));
    }
    /**
     *
     * @brief Require 1000 saved definitions to serialize/restore while a 1001st is refused
     * without edited signal.
     *
     */
    void saveLimitMatchesRestoreLimit()
    {
        ProfileSectionStore store;
        QString error;
        auto d = definition();
        for (int i = 0; i < 1000; ++i) {
            d.id = QString::number(i);
            QVERIFY(store.saveDefinition(d, &error));
        }
        QSignalSpy edits(&store, &ProfileSectionStore::edited);
        d.id = "overflow";
        QVERIFY(!store.saveDefinition(d, &error));
        QCOMPARE(edits.count(), 0);
        QJsonArray data;
        QVERIFY(store.serialize("/models", data, &error));
        ProfileSectionStore restored;
        QVERIFY(restored.restore(data, "/models", &error));
        QCOMPARE(restored.definitions().size(), 1000);
    }
    /**
     *
     * @brief Require restore of duplicate section IDs to fail without publishing either
     * definition.
     *
     */
    void duplicateIdentifiersRefusedAtomically()
    {
        ProfileSectionStore s;
        QString error;
        auto d = definition();
        auto j = ProfileSection::definitionToJson(d, "/models");
        QVERIFY(!s.restore(QJsonArray{j, j}, "/models", &error));
        QVERIFY(s.definitions().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestProfileSectionStore)
#include "test_profilesectionstore.moc"
