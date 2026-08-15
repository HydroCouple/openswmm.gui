/*!
 * \file   test_aboutdialog.cpp
 * \brief  Slice K QtTest: AboutDialog manifest parser + dialog construction.
 *
 * The dialog itself is hard to drive without a full QApplication + resource
 * bundle (it loads the manifest from a Qt resource). What we CAN test in a
 * leaf binary is the static parseManifest() helper, which contains all of
 * the manifest schema logic and is the most likely place a typo or schema
 * regression would land.
 */

#include "ui/dialogs/aboutdialog.h"

#include <QObject>
#include <QTest>

class TestAboutDialog : public QObject
{
    Q_OBJECT
private slots:
    void parsesValidManifest();
    void emptyArrayIsEmpty();
    void rejectsNonArrayRoot();
    void skipsRowsWithoutName();
    void copiesAllExpectedFields();
};

void TestAboutDialog::parsesValidManifest()
{
    const QByteArray json = R"([
        {"category":"X","name":"a","version":"1.0","spdx":"MIT"},
        {"category":"X","name":"b","version":"2.0","spdx":"BSD"}
    ])";
    QString err;
    auto rows = AboutDialog::parseManifest(json, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0].name, QStringLiteral("a"));
    QCOMPARE(rows[1].name, QStringLiteral("b"));
}

void TestAboutDialog::emptyArrayIsEmpty()
{
    QString err;
    auto rows = AboutDialog::parseManifest("[]", &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(rows.size(), 0);
}

void TestAboutDialog::rejectsNonArrayRoot()
{
    QString err;
    auto rows = AboutDialog::parseManifest("{\"name\":\"x\"}", &err);
    QVERIFY(rows.isEmpty());
    QVERIFY(!err.isEmpty());
}

void TestAboutDialog::skipsRowsWithoutName()
{
    const QByteArray json = R"([
        {"category":"X","version":"1.0"},
        {"category":"X","name":"valid"}
    ])";
    auto rows = AboutDialog::parseManifest(json);
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows[0].name, QStringLiteral("valid"));
}

void TestAboutDialog::copiesAllExpectedFields()
{
    const QByteArray json = R"([{
        "category":"Geospatial",
        "name":"GDAL",
        "version":"3.9.2",
        "role":"vector / raster IO",
        "homepage":"https://gdal.org",
        "source":"https://github.com/OSGeo/gdal",
        "spdx":"MIT",
        "provenance":"vcpkg",
        "licenseFile":":/licenses/gdal.txt"
    }])";
    auto rows = AboutDialog::parseManifest(json);
    QCOMPARE(rows.size(), 1);
    const auto &c = rows[0];
    QCOMPARE(c.category,    QStringLiteral("Geospatial"));
    QCOMPARE(c.name,        QStringLiteral("GDAL"));
    QCOMPARE(c.version,     QStringLiteral("3.9.2"));
    QCOMPARE(c.role,        QStringLiteral("vector / raster IO"));
    QCOMPARE(c.homepage,    QStringLiteral("https://gdal.org"));
    QCOMPARE(c.sourceUrl,   QStringLiteral("https://github.com/OSGeo/gdal"));
    QCOMPARE(c.spdx,        QStringLiteral("MIT"));
    QCOMPARE(c.provenance,  QStringLiteral("vcpkg"));
    QCOMPARE(c.licenseFile, QStringLiteral(":/licenses/gdal.txt"));
}

QTEST_MAIN(TestAboutDialog)
#include "test_aboutdialog.moc"
