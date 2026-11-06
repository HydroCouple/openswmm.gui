// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      test_meshcorridorrecipe.cpp
 * @brief     Tests for meshcorridorrecipe.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "project/meshcorridorrecipe.h"
#include <QTest>
#include <QJsonDocument>
#include <limits>

namespace
{
/**
 * @brief Construct an owned corridor-source recipe with large exact 64 bit feature IDs and
 * complete provenance.
 * @return Centerline source referencing current-directory-relative fixture paths, placeholder
 * CRS strings, width 12.5,along 20,across 3,tag and 64 hex digest. Does not open those files or
 * validate CRS text; IDs include 2^53+1 and qint64 maximum to exercise lossless string
 * serialization.
 */
mesh::CorridorSource source()
{
    mesh::CorridorSource row;
    row.path = QDir::current().absoluteFilePath("tests/gui/data/corridors/roads.gpkg");
    row.layerName = "river_centerlines";
    row.sourceCRSWkt = "source CRS";
    row.meshCRSWkt = "mesh CRS";
    row.featureIds = {9007199254740993LL, std::numeric_limits<qint64>::max()};
    row.widthField = "channel_width";
    row.width = 12.5;
    row.along = 20;
    row.across = 3;
    row.tag = "river corridor";
    row.geometryDigest = QString(64, 'a');
    row.sourceFiles = {row.path,
                       QDir::current().absoluteFilePath("tests/gui/data/corridors/roads.prj")};
    return row;
}
/**
 * @brief Resolve the synthetic project sidecar location used as the recipe-relative path base.
 * @return Absolute current-directory/tests/gui/data/corridor_recipe/project.oswp path, owned
 * QString; no existence check, creation or file access.
 */
QString sidecar()
{
    return QDir::current().absoluteFilePath("tests/gui/data/corridor_recipe/project.oswp");
}
}

class TestMeshCorridorRecipe : public QObject
{
    Q_OBJECT
private slots:
    /**
     * @brief Verify centerline recipes preserve provenance, settings and exact feature IDs
     * through JSON and Save As rebasing.
     * @note Requires relative encoded path and decimal-string 2^53+1, then moves only the
     * sidecar base and checks original absolute source/sourceFiles remain identical.
     * Serialization only, no fixture IO.
     */
    void roundTripAndSaveAs()
    {
        const auto original = source();
        QJsonObject encoded;
        QString error;
        QVERIFY2(MeshCorridorRecipe::encode({original}, sidecar(), &encoded, &error),
                 qPrintable(error));
        const auto row = encoded.value("sources").toArray().first().toObject();
        QCOMPARE(row.value("role").toString(), QString("centerline"));
        QCOMPARE(row.value("selection").toString(), QString("features"));
        QVERIFY(!QDir::isAbsolutePath(row.value("path").toString()));
        QCOMPARE(row.value("featureIds").toArray().first().toString(), QString("9007199254740993"));
        QVector<mesh::CorridorSource> restored;
        QVERIFY2(MeshCorridorRecipe::decode(
                     QJsonDocument::fromJson(QJsonDocument(encoded).toJson()).object(), sidecar(),
                     &restored, &error),
                 qPrintable(error));
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().path, original.path);
        QCOMPARE(restored.first().sourceFiles, original.sourceFiles);
        QCOMPARE(restored.first().featureIds, original.featureIds);
        QCOMPARE(restored.first().sourceCRSWkt, original.sourceCRSWkt);
        QCOMPARE(restored.first().meshCRSWkt, original.meshCRSWkt);
        QCOMPARE(restored.first().geometryDigest, original.geometryDigest);
        QCOMPARE(restored.first().widthField, original.widthField);
        QCOMPARE(restored.first().width, original.width);
        QCOMPARE(restored.first().along, original.along);
        QCOMPARE(restored.first().across, original.across);
        QCOMPARE(restored.first().tag, original.tag);
        const QString moved = QDir::current().absoluteFilePath("tests/gui/data/moved/project.oswp");
        QVERIFY(MeshCorridorRecipe::encode(restored, moved, &encoded, &error));
        QVERIFY(MeshCorridorRecipe::decode(encoded, moved, &restored, &error));
        QCOMPARE(restored.first().path, original.path);
        QCOMPARE(restored.first().sourceFiles, original.sourceFiles);
    }
    /**
     * @brief Verify mixed bank-pair/centerline recipes use version 2 and retain roles and bank
     * provenance after rebasing.
     * @note Bank pair has exactly two selected IDs and no width field; all paths, IDs,
     * CRS/digest and subdivision settings survive. No GIS layers opened or project files
     * written.
     */
    void bankPairRoundTripAndSaveAs()
    {
        auto bank = source();
        bank.bankPair = true;
        bank.widthField.clear();
        const auto centerline = source();
        QJsonObject encoded;
        QString error;
        QVERIFY2(MeshCorridorRecipe::encode({bank, centerline}, sidecar(), &encoded, &error),
                 qPrintable(error));
        QCOMPARE(encoded.value("version").toInt(), 2);
        const auto rows = encoded.value("sources").toArray();
        QCOMPARE(rows[0].toObject().value("role").toString(), QString("bankPair"));
        QCOMPARE(rows[1].toObject().value("role").toString(), QString("centerline"));
        QVector<mesh::CorridorSource> restored;
        QVERIFY2(MeshCorridorRecipe::decode(encoded, sidecar(), &restored, &error),
                 qPrintable(error));
        QCOMPARE(restored.size(), 2);
        QVERIFY(restored[0].bankPair);
        QVERIFY(!restored[1].bankPair);
        QCOMPARE(restored[0].featureIds, bank.featureIds);
        QCOMPARE(restored[0].path, bank.path);
        QCOMPARE(restored[0].sourceFiles, bank.sourceFiles);
        QCOMPARE(restored[0].sourceCRSWkt, bank.sourceCRSWkt);
        QCOMPARE(restored[0].meshCRSWkt, bank.meshCRSWkt);
        QCOMPARE(restored[0].geometryDigest, bank.geometryDigest);
        QCOMPARE(restored[0].across, bank.across);
        QCOMPARE(restored[0].along, bank.along);
        const QString moved =
            QDir::current().absoluteFilePath("tests/gui/data/moved_bank/project.oswp");
        QVERIFY(MeshCorridorRecipe::encode(restored, moved, &encoded, &error));
        QVERIFY(MeshCorridorRecipe::decode(encoded, moved, &restored, &error));
        QVERIFY(restored[0].bankPair);
        QCOMPARE(restored[0].path, bank.path);
        QCOMPARE(restored[0].sourceFiles, bank.sourceFiles);
        QCOMPARE(restored[0].featureIds, bank.featureIds);
    }
    /**
     * @brief Verify centerline-only recipes decode versions 1/2 and encode back to compatible
     * version 1.
     * @note Bank-pair flag remains false across both decode paths; local JSON only.
     */
    void centerlineVersionCompatibility()
    {
        QJsonObject encoded;
        QString error;
        QVERIFY(MeshCorridorRecipe::encode({source()}, sidecar(), &encoded, &error));
        QCOMPARE(encoded.value("version").toInt(), 1);
        QVector<mesh::CorridorSource> restored;
        QVERIFY(MeshCorridorRecipe::decode(encoded, sidecar(), &restored, &error));
        QVERIFY(!restored[0].bankPair);
        encoded["version"] = 2;
        QVERIFY2(MeshCorridorRecipe::decode(encoded, sidecar(), &restored, &error),
                 qPrintable(error));
        QVERIFY(!restored[0].bankPair);
        QVERIFY(MeshCorridorRecipe::encode(restored, sidecar(), &encoded, &error));
        QCOMPARE(encoded.value("version").toInt(),
                 1); // All-centerline output retains v1 compatibility.
    }
    /**
     * @brief Register malformed bank-pair cardinality, duplicate IDs, width field/value, role
     * and version cases.
     * @note Eight rows include version 1 bank rejection and unknown version 3.
     */
    void malformedBankPair_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const char *name :
             {"one-id", "three-ids", "duplicate-ids", "width-field", "zero-width", "unknown-role",
              "unknown-version", "version-one-bank"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    /**
     * @brief Verify malformed bank-pair JSON returns a diagnostic without replacing the prior
     * working recipe.
     * @note Starts from valid centerline, corrupts row/version by data case, requires false,
     * nonempty error and original non-bank feature IDs retained. No filesystem mutation.
     */
    void malformedBankPair()
    {
        QFETCH(QString, scenario);
        QJsonObject encoded;
        QString error;
        QVERIFY(MeshCorridorRecipe::encode({source()}, sidecar(), &encoded, &error));
        encoded["version"] = 2;
        auto row = encoded.value("sources").toArray().first().toObject();
        row["role"] = "bankPair";
        row["widthField"] = "";
        if (scenario == "one-id")
            row["featureIds"] = QJsonArray{"7"};
        if (scenario == "three-ids")
            row["featureIds"] = QJsonArray{"7", "19", "42"};
        if (scenario == "duplicate-ids")
            row["featureIds"] = QJsonArray{"7", "7"};
        if (scenario == "width-field")
            row["widthField"] = "width_m";
        if (scenario == "zero-width")
            row["width"] = 0;
        if (scenario == "unknown-role")
            row["role"] = "pairedBanks";
        if (scenario == "unknown-version")
            encoded["version"] = 3;
        if (scenario == "version-one-bank")
            encoded["version"] = 1;
        encoded["sources"] = QJsonArray{row};
        QVector<mesh::CorridorSource> restored{source()};
        QVERIFY(!MeshCorridorRecipe::decode(encoded, sidecar(), &restored, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(restored.size(), 1);
        QVERIFY(!restored[0].bankPair);
        QCOMPARE(restored[0].featureIds, source().featureIds);
    }
    /**
     * @brief Verify encoding and decoding an empty recipe succeeds and clears previously
     * restored rows.
     * @note Local vectors/JSON only; empty recipe is valid rather than a decode failure.
     */
    void emptyRecipe()
    {
        QJsonObject encoded;
        QVector<mesh::CorridorSource> restored{source()};
        QString error;
        QVERIFY(MeshCorridorRecipe::encode({}, sidecar(), &encoded, &error));
        QVERIFY(MeshCorridorRecipe::decode(encoded, sidecar(), &restored, &error));
        QVERIFY(restored.isEmpty());
    }
    /**
     * @brief Register 21 malformed centerline/root-schema cases spanning IDs, paths, dimensions
     * and provenance.
     * @note Includes numeric/overflow/negative/duplicate IDs, fractional across count, unknown
     * fields, wrong CRS type and invalid SHA256 text; no files opened.
     */
    void malformed_data()
    {
        QTest::addColumn<QString>("scenario");
        for (const char *name : {"root",
                                 "version",
                                 "sources",
                                 "row",
                                 "role",
                                 "selection",
                                 "empty-ids",
                                 "numeric-id",
                                 "overflow-id",
                                 "negative-id",
                                 "duplicate-id",
                                 "empty-path",
                                 "empty-layer",
                                 "zero-width",
                                 "negative-along",
                                 "fractional-across",
                                 "source-files",
                                 "unknown-field",
                                 "unknown-root-field",
                                 "wrong-crs-type",
                                 "digest"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    /**
     * @brief Verify every malformed schema row refuses atomically with a nonempty diagnostic.
     * @note Existing one-source recipe and exact feature IDs must survive failed decode,
     * including invalid root/nonobject row and unknown future fields. Pure serialization
     * validation.
     */
    void malformed()
    {
        QFETCH(QString, scenario);
        QJsonObject encoded;
        QString error;
        QVERIFY(MeshCorridorRecipe::encode({source()}, sidecar(), &encoded, &error));
        auto row = encoded.value("sources").toArray().first().toObject();
        if (scenario == "role")
            row["role"] = "bank-pair";
        if (scenario == "selection")
            row["selection"] = "all";
        if (scenario == "empty-ids")
            row["featureIds"] = QJsonArray();
        if (scenario == "numeric-id")
            row["featureIds"] = QJsonArray{42};
        if (scenario == "overflow-id")
            row["featureIds"] = QJsonArray{"9223372036854775808"};
        if (scenario == "negative-id")
            row["featureIds"] = QJsonArray{"-1"};
        if (scenario == "duplicate-id")
            row["featureIds"] = QJsonArray{"42", "42"};
        if (scenario == "empty-path")
            row["path"] = "";
        if (scenario == "empty-layer")
            row["layerName"] = "";
        if (scenario == "zero-width")
            row["width"] = 0;
        if (scenario == "negative-along")
            row["along"] = -1;
        if (scenario == "fractional-across")
            row["across"] = 2.5;
        if (scenario == "source-files")
            row["sourceFiles"] = QJsonArray{123};
        if (scenario == "unknown-field")
            row["futureBehavior"] = true;
        if (scenario == "wrong-crs-type")
            row["meshCRSWkt"] = 17;
        if (scenario == "digest")
            row["geometryDigest"] = "not-a-sha256";
        encoded["sources"] = QJsonArray{row};
        if (scenario == "version")
            encoded["version"] = 3;
        if (scenario == "sources")
            encoded["sources"] = QJsonObject();
        if (scenario == "row")
            encoded["sources"] = QJsonArray{17};
        if (scenario == "unknown-root-field")
            encoded["futureBehavior"] = true;
        const QJsonValue value =
            scenario == "root" ? QJsonValue(QJsonArray()) : QJsonValue(encoded);
        QVector<mesh::CorridorSource> restored{source()};
        QVERIFY2(!MeshCorridorRecipe::decode(value, sidecar(), &restored, &error),
                 qPrintable(scenario));
        QVERIFY(!error.isEmpty());
        QCOMPARE(restored.size(), 1); // failure must not erase the previous working recipe
        QCOMPARE(restored.first().featureIds, source().featureIds);
    }
};

QTEST_GUILESS_MAIN(TestMeshCorridorRecipe)
#include "test_meshcorridorrecipe.moc"
