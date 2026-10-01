#include "assignment/meshassignmentsampling.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <QDir>
#include <QFileInfo>
#include <QtTest>
using namespace openswmmvis::assignment;
using S=MeshAssignmentSampling;
class TestGroundwaterAssignmentSampling:public QObject {
 Q_OBJECT
 QString out;
 QString wkt(){OGRSpatialReference s;s.importFromEPSG(32618);char *p=nullptr;s.exportToWkt(&p);QString text=QString::fromUtf8(p);CPLFree(p);return text;}
 QString raster(const QString&name,bool crs,double value){
    QString path=QDir(out).filePath(name+".tif");auto *d=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path.toUtf8().constData(),4,4,1,GDT_Float64,nullptr);
    double geo[]={0,1,0,4,0,-1};d->SetGeoTransform(geo);if(crs)d->SetProjection(wkt().toUtf8().constData());
    d->GetRasterBand(1)->Fill(value);d->GetRasterBand(1)->SetNoDataValue(-9999);GDALClose(d);return path;
 }
 QString vector(bool duplicate){
    QString path=QDir(out).filePath(duplicate?"overlap.geojson":"coverage.geojson");QFile::remove(path);
    auto *d=GetGDALDriverManager()->GetDriverByName("GeoJSON")->Create(path.toUtf8().constData(),0,0,0,GDT_Unknown,nullptr);
    OGRSpatialReference sr;sr.importFromEPSG(32618);auto*l=d->CreateLayer("coverage",&sr,wkbPolygon,nullptr);
    OGRFieldDefn field("value",OFTReal);l->CreateField(&field);
    for(int i=0;i<(duplicate?2:1);++i){auto*f=OGRFeature::CreateFeature(l->GetLayerDefn());f->SetField("value",2.);
        OGRLinearRing ring;ring.addPoint(0,0);ring.addPoint(4,0);ring.addPoint(4,4);ring.addPoint(0,4);ring.closeRings();OGRPolygon polygon;polygon.addRing(&ring);f->SetGeometry(&polygon);l->CreateFeature(f);OGRFeature::DestroyFeature(f);}
    GDALClose(d);return path;
 }
 S::Job job(){S::Job j;j.strictCrs=true;j.rejectOverlaps=true;j.meshCrsWkt=wkt();j.source=S::Source::Raster;j.bands={1};j.targetKeys={"KS"};j.targetMin={-100};j.targetMax={100};j.triangles={0,1};j.centroids={{1.5,1.5},{2.5,2.5}};return j;}
private slots:
 void initTestCase(){GDALAllRegister();out=qEnvironmentVariable("SWMMVIS_FORCING_TEST_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../../workplans/artifacts/phase_35_profiles_forcing/sampling"));QVERIFY(QDir().mkpath(out));}
 void equivalentVectorRasterAndSignedValues(){auto r=job();r.rasterPath=raster("constant",true,2);auto a=sampleMeshAssignment(r);QVERIFY2(a.error.isEmpty(),qPrintable(a.error));QCOMPARE(a.values[0],QVector<double>({2,2}));r.source=S::Source::Vector;r.vectorPath=vector(false);r.fields={"value"};auto b=sampleMeshAssignment(r);QVERIFY2(b.error.isEmpty(),qPrintable(b.error));QCOMPARE(a.triangles,b.triangles);QCOMPARE(a.values,b.values);r.source=S::Source::Raster;r.rasterPath=raster("signed",true,-2);QCOMPARE(sampleMeshAssignment(r).values[0],QVector<double>({-2,-2}));}
 void missingCrsRefused(){auto j=job();j.meshCrsWkt.clear();j.rasterPath=raster("missingmesh",true,2);QVERIFY(!sampleMeshAssignment(j).error.isEmpty());j=job();j.rasterPath=raster("missingsource",false,2);QVERIFY(!sampleMeshAssignment(j).error.isEmpty());}
 void assignedCrsAccepted(){auto j=job();j.rasterPath=raster("assigned",false,2);j.assignedSourceCrsWkt=wkt();auto r=sampleMeshAssignment(j);QVERIFY2(r.error.isEmpty(),qPrintable(r.error));QCOMPARE(r.triangles.size(),2);}
 void overlappingCoverageRefused(){auto j=job();j.source=S::Source::Vector;j.vectorPath=vector(true);j.fields={"value"};QVERIFY(!sampleMeshAssignment(j).error.isEmpty());}
 void noDataReportedAndCancellation(){auto j=job();j.rasterPath=raster("nodata",true,-9999);auto r=sampleMeshAssignment(j);QVERIFY(r.error.isEmpty());QVERIFY(r.triangles.isEmpty());QCOMPARE(r.skippedNoData,2);r=sampleMeshAssignment(j,[]{return true;});QVERIFY(r.cancelled);QVERIFY(r.triangles.isEmpty());}
};
QTEST_APPLESS_MAIN(TestGroundwaterAssignmentSampling)
#include "test_groundwaterassignmentsampling.moc"
