#include <QtTest>
#include <QDir>
#include <QFile>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <cmath>
#include <limits>
#include "mesh/corridorsource.h"

using namespace mesh;

class TestCorridorSource : public QObject {
    Q_OBJECT
    QString m_dir, m_path, m_wkt;
    static constexpr qint64 largeFid = 9007199254740993LL;
    CorridorSource source() const {
        CorridorSource s;
        s.path = m_path; s.layerName = QStringLiteral("roads");
        s.sourceCRSWkt = s.meshCRSWkt = m_wkt;
        s.featureIds = {7}; s.width = 2; s.across = 2; s.along = 2;
        return s;
    }
    bool add(OGRLayer *layer, qint64 fid, const char *text, double width) {
        auto *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
        f->SetFID(fid); f->SetField("width", width); f->SetField("label", "road");
        OGRGeometry *g = nullptr;
        if (OGRGeometryFactory::createFromWkt(text, nullptr, &g) != OGRERR_NONE) {
            OGRFeature::DestroyFeature(f); return false;
        }
        f->SetGeometryDirectly(g);
        const bool ok = layer->CreateFeature(f) == OGRERR_NONE;
        OGRFeature::DestroyFeature(f); return ok;
    }
private slots:
    void initTestCase() {
        GDALAllRegister();
        m_dir = qEnvironmentVariable("SWMMVIS_CORRIDOR_TEST_OUTPUT");
        if (m_dir.isEmpty())
            m_dir = QDir::current().absoluteFilePath("workplans/artifacts/phase_26_corridor_sources/helper");
        QVERIFY(QDir().mkpath(m_dir));
        m_path = QDir(m_dir).filePath("selected_roads.gpkg");
        OGRSpatialReference srs;
        QCOMPARE(srs.importFromEPSG(32618), OGRERR_NONE);
        char *wkt = nullptr; srs.exportToWkt(&wkt); m_wkt = QString::fromUtf8(wkt); CPLFree(wkt);
    }
    void init() {
        auto *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
        QVERIFY(driver);
        if (QFile::exists(m_path)) QCOMPARE(driver->Delete(m_path.toUtf8().constData()), CE_None);
        auto *ds = driver->Create(m_path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
        QVERIFY(ds);
        OGRSpatialReference srs; srs.SetFromUserInput(m_wkt.toUtf8().constData());
        auto *layer = ds->CreateLayer("roads", &srs, wkbUnknown, nullptr);
        QVERIFY(layer);
        OGRFieldDefn width("width", OFTReal), label("label", OFTString);
        QCOMPARE(layer->CreateField(&width), OGRERR_NONE);
        QCOMPARE(layer->CreateField(&label), OGRERR_NONE);
        QVERIFY(add(layer, 7, "LINESTRING (500000 4500000,500010 4500000)", 4));
        QVERIFY(add(layer, largeFid, "MULTILINESTRING ((500000 4500020,500010 4500020),(500000 4500040,500010 4500040))", 6));
        QVERIFY(add(layer, 8, "POINT (500000 4500060)", -1));
        QVERIFY(add(layer, 9, "LINESTRING (500000 4500080,500010 4500080)", -1));
        auto *f = layer->GetFeature(9); f->SetFieldNull(f->GetFieldIndex("width"));
        QCOMPARE(layer->SetFeature(f), OGRERR_NONE); OGRFeature::DestroyFeature(f);
        GDALClose(ds);
    }
    void selectedLineAndMultipart() {
        auto s = source(); s.featureIds = {largeFid,7}; s.widthField = "width";
        const auto r = readCorridorSources({s},m_wkt);
        QVERIFY2(r.ok(), qPrintable(r.error)); QCOMPARE(r.patches.size(),3);
        QCOMPARE(r.resolvedSources.size(),1);
        QCOMPARE(r.resolvedSources[0].featureIds,QVector<qint64>({7,largeFid}));
        QVERIFY(!r.resolvedSources[0].geometryDigest.isEmpty());
        QVERIFY(r.resolvedSources[0].sourceFiles.contains(QFileInfo(m_path).canonicalFilePath()));
        QCOMPARE(r.patches[0].xy[0].y()-r.patches[0].xy[2].y(),-4.0);
        for (const auto &p:r.patches) QVERIFY2(validate(p).isEmpty(),qPrintable(validate(p)));
        const auto again = readCorridorSources(r.resolvedSources,m_wkt);
        QVERIFY2(again.ok(),qPrintable(again.error));
        QCOMPARE(again.resolvedSources[0].geometryDigest,r.resolvedSources[0].geometryDigest);
        QVERIFY(corridorSourceFilesUnchanged(r.sourceStamps));
    }
    void constantWidthAndExplicitTag() {
        auto s=source(); s.tag="river";
        const auto r=readCorridorSources({s},m_wkt);
        QVERIFY2(r.ok(),qPrintable(r.error)); QCOMPARE(r.patches.size(),1);
        QCOMPARE(r.patches[0].tag,QString("river"));
        QCOMPARE(std::abs(r.patches[0].xy[0].y()-r.patches[0].xy[2].y()),2.0);
    }
    void emptySelection() { auto s=source();s.featureIds.clear(); const auto r=readCorridorSources({s},m_wkt); QVERIFY(!r.ok()); QVERIFY(r.error.contains("select",Qt::CaseInsensitive)); }
    void missingFeatureAtomic() { auto s=source();s.featureIds={7,12345};const auto r=readCorridorSources({s},m_wkt);QVERIFY(!r.ok());QVERIFY(r.patches.isEmpty());QVERIFY(r.resolvedSources.isEmpty());QVERIFY(r.error.contains("12345")); }
    void nonLine() { auto s=source();s.featureIds={8};const auto r=readCorridorSources({s},m_wkt);QVERIFY(!r.ok());QVERIFY(r.error.contains("8")); }
    void badWidth_data() { QTest::addColumn<QString>("field");QTest::addColumn<qint64>("fid");QTest::newRow("missing")<<QString("absent")<<qint64(7);QTest::newRow("text")<<QString("label")<<qint64(7);QTest::newRow("null")<<QString("width")<<qint64(9); }
    void badWidth() { QFETCH(QString,field);QFETCH(qint64,fid);auto s=source();s.widthField=field;s.featureIds={fid};const auto r=readCorridorSources({s},m_wkt);QVERIFY(!r.ok());QVERIFY(r.error.contains("width",Qt::CaseInsensitive)); }
    void missingCRS() { auto s=source();s.sourceCRSWkt.clear();const auto r=readCorridorSources({s},m_wkt);QVERIFY(!r.ok());QVERIFY(r.error.contains("CRS")); }
    void changedMeshCRS() { auto s=source();const auto r=readCorridorSources({s},"EPSG:32617");QVERIFY(!r.ok());QVERIFY(r.error.contains("CRS")); }
    void geographicTarget() { auto s=source();s.meshCRSWkt="EPSG:4326";const auto r=readCorridorSources({s},s.meshCRSWkt);QVERIFY(!r.ok());QVERIFY(r.error.contains("planar",Qt::CaseInsensitive)); }
    void changedSelectedData() {
        auto s=source();s.widthField="width";
        const auto first=readCorridorSources({s},m_wkt);QVERIFY2(first.ok(),qPrintable(first.error));
        auto *ds=static_cast<GDALDataset*>(GDALOpenEx(m_path.toUtf8().constData(),GDAL_OF_VECTOR|GDAL_OF_UPDATE,nullptr,nullptr,nullptr));QVERIFY(ds);
        auto *layer=ds->GetLayerByName("roads");auto *f=layer->GetFeature(7);QVERIFY(f);f->SetField("width",8.0);QCOMPARE(layer->SetFeature(f),OGRERR_NONE);OGRFeature::DestroyFeature(f);GDALClose(ds);
        const auto r=readCorridorSources(first.resolvedSources,m_wkt);QVERIFY(!r.ok());QVERIFY(r.error.contains("changed",Qt::CaseInsensitive));QVERIFY(r.patches.isEmpty());
    }
    void cancelled() { const auto r=readCorridorSources({source()},m_wkt,[]{return true;});QVERIFY(!r.ok());QVERIFY(r.error.contains("cancel",Qt::CaseInsensitive));QVERIFY(r.patches.isEmpty()); }
    void emptySources() { const auto r=readCorridorSources({},{});QVERIFY(r.ok());QVERIFY(r.patches.isEmpty()); }
    void lateCancellationIsAtomic() {
        auto s=source();s.featureIds={7,largeFid};int checks=0;
        const auto r=readCorridorSources({s},m_wkt,[&checks]{return ++checks>=8;});
        QVERIFY(!r.ok());QVERIFY(r.error.contains("cancel",Qt::CaseInsensitive));
        QVERIFY(r.patches.isEmpty());QVERIFY(r.resolvedSources.isEmpty());
    }
    void changedDependencyAfterRead() {
        const auto first=readCorridorSources({source()},m_wkt);QVERIFY2(first.ok(),qPrintable(first.error));
        QVERIFY(!first.sourceStamps.isEmpty());
        QFile file(m_path);QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(file.fileTime(QFileDevice::FileModificationTime).addSecs(10),QFileDevice::FileModificationTime));file.close();
        QString error;QVERIFY(!corridorSourceFilesUnchanged(first.sourceStamps,&error));QVERIFY(error.contains("changed"));
    }
    void invalidSpacingAndWidth_data() {
        QTest::addColumn<double>("width");QTest::addColumn<double>("along");
        QTest::newRow("zero-width")<<0.0<<2.0;QTest::newRow("negative-width")<<-1.0<<2.0;
        QTest::newRow("nan-width")<<std::numeric_limits<double>::quiet_NaN()<<2.0;
        QTest::newRow("negative-spacing")<<2.0<<-1.0;
    }
    void invalidSpacingAndWidth() {QFETCH(double,width);QFETCH(double,along);auto s=source();s.width=width;s.along=along;const auto r=readCorridorSources({s},m_wkt);QVERIFY(!r.ok());QVERIFY(r.patches.isEmpty());}
    void failedProjectionRejectsWholeFeature() {
        // The second latitude cannot project. It must not be omitted, leaving
        // the first and third vertices connected into a plausible wrong road.
        auto s=source();s.sourceCRSWkt="EPSG:4326";
        auto *ds=static_cast<GDALDataset*>(GDALOpenEx(m_path.toUtf8().constData(),GDAL_OF_VECTOR|GDAL_OF_UPDATE,nullptr,nullptr,nullptr));QVERIFY(ds);
        auto *layer=ds->GetLayerByName("roads");auto *f=layer->GetFeature(7);OGRLineString line;
        line.addPoint(-75,40);line.addPoint(-75,100);line.addPoint(-74.999,40);
        f->SetGeometry(&line);QCOMPARE(layer->SetFeature(f),OGRERR_NONE);OGRFeature::DestroyFeature(f);GDALClose(ds);
        const auto r=readCorridorSources({s},m_wkt);QVERIFY(!r.ok());QVERIFY(r.error.contains("vertex 2"));QVERIFY(r.patches.isEmpty());
    }
    void reprojectsTraditionalXY() {
        auto s=source();s.sourceCRSWkt="EPSG:4326";
        auto *ds=static_cast<GDALDataset*>(GDALOpenEx(m_path.toUtf8().constData(),GDAL_OF_VECTOR|GDAL_OF_UPDATE,nullptr,nullptr,nullptr));QVERIFY(ds);
        auto *layer=ds->GetLayerByName("roads");auto *f=layer->GetFeature(7);OGRLineString line;line.addPoint(-75,40);line.addPoint(-74.999,40);f->SetGeometry(&line);QCOMPARE(layer->SetFeature(f),OGRERR_NONE);OGRFeature::DestroyFeature(f);GDALClose(ds);
        const auto r=readCorridorSources({s},m_wkt);QVERIFY2(r.ok(),qPrintable(r.error));
        QVERIFY(std::abs(r.patches[0].xy[0].x()-500000)<2);QVERIFY(r.patches[0].xy[0].y()>4400000);QVERIFY(r.patches[0].xy[0].y()<4500000);
    }
};
QTEST_APPLESS_MAIN(TestCorridorSource)
#include "test_corridorsource.moc"
