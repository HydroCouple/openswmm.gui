#include "assignment/rasterfluxintegration.h"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <QDir>
#include <QFileInfo>
#include <QtTest>
#include <cmath>
using namespace openswmmvis::assignment;
class TestRasterFluxIntegration:public QObject {
 Q_OBJECT
 QString out;
 QString wkt(int epsg=32618){OGRSpatialReference s;s.importFromEPSG(epsg);char*p=nullptr;s.exportToWkt(&p);QString r=QString::fromUtf8(p);CPLFree(p);return r;}
 QString raster(QString name,int nx,int ny,QVector<double> values,QVector<double> gt={0,1,0,0,0,1},QString crs=QString(),bool mask=false){
  const QString path=out+"/"+name+".tif";auto*d=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path.toUtf8().constData(),nx,ny,1,GDT_Float64,nullptr);if(!d)return {};
  d->SetGeoTransform(gt.data());if(!crs.isNull())d->SetProjection(crs.toUtf8().constData());else d->SetProjection(wkt().toUtf8().constData());auto*b=d->GetRasterBand(1);b->SetNoDataValue(-9999);
  const bool ok=b->RasterIO(GF_Write,0,0,nx,ny,values.data(),nx,ny,GDT_Float64,0,0)==CE_None;
  if(mask){b->CreateMaskBand(GMF_PER_DATASET);QVector<unsigned char> bytes(nx*ny,255);bytes[0]=0;b->GetMaskBand()->RasterIO(GF_Write,0,0,nx,ny,bytes.data(),nx,ny,GDT_Byte,0,0);}
  GDALClose(d);return ok?path:QString();
 }
 RasterFluxRequest request(const QString&path,QVector<QVector<QPointF>> polygons){RasterFluxRequest r;r.path=path;r.meshCrsWkt=wkt();r.densityUnit="m/s";r.footprints=polygons;for(int i=0;i<polygons.size();++i)r.cells.append(i);return r;}
 QVector<QPointF> box(double x,double y,double w,double h){return {{x,y},{x+w,y},{x+w,y+h},{x,y+h}};}
private slots:
 void initTestCase(){GDALAllRegister();out=qEnvironmentVariable("SWMMVIS_RASTER_FLUX_TEST_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../../workplans/artifacts/phase_36_acceptance/raster_flux/unit"));QVERIFY(QDir().mkpath(out));}
 void mixedCellsIntegratePartialPixelsAndPreserveSignedTotal(){auto j=request(raster("mixed",2,1,{2,-4}),{{{0,0},{1,0},{0,1}},box(1,0,1,1)});auto r=integrateRasterFlux(j);QVERIFY2(r.error.isEmpty(),qPrintable(r.error));QCOMPARE(r.flows.size(),2);QVERIFY(qAbs(r.flows[0]-1)<1e-12);QCOMPARE(r.flows[1],-4.);QCOMPARE(r.totalFlow,-3.);QCOMPARE(r.validArea,1.5);}
 void oppositeSignsWithinCellRefused_data(){QTest::addColumn<double>("withdrawal");QTest::newRow("balanced-zero-net")<<-2.;QTest::newRow("unequal-nonzero-net")<<-4.;}
 void oppositeSignsWithinCellRefused(){QFETCH(double,withdrawal);auto j=request(raster(QString("opposite_")+QTest::currentDataTag(),2,1,{2,withdrawal}),{box(0,0,2,1)});auto r=integrateRasterFlux(j);QVERIFY(r.error.contains("both injection and extraction"));QVERIFY(r.error.contains("water-quality"));QVERIFY(r.flows.isEmpty());QCOMPARE(r.totalFlow,0.);j.scale=-1;r=integrateRasterFlux(j);QVERIFY(!r.error.isEmpty());QVERIFY(r.flows.isEmpty());}
 void partitionInvariantAndSubpixel(){auto path=raster("partition",2,1,{2,6});auto full=integrateRasterFlux(request(path,{box(0,0,2,1)}));auto split=integrateRasterFlux(request(path,{{{0,0},{2,0},{0,1}},{{2,0},{2,1},{0,1}}}));QVERIFY(full.error.isEmpty());QVERIFY(split.error.isEmpty());QCOMPARE(full.totalFlow,split.totalFlow);auto small=integrateRasterFlux(request(path,{box(.75,.25,.5,.5)}));QVERIFY(small.error.isEmpty());QCOMPARE(small.totalFlow,1.);}
 void rotatedAffineAndTranslatedCoordinates(){QVector<double> gt={500000,2,1,4400000,1,-3};auto p=[&](double x,double y){return QPointF(gt[0]+gt[1]*x+gt[2]*y,gt[3]+gt[4]*x+gt[5]*y);};auto j=request(raster("rotated",2,1,{2,1},gt),{{p(0,0),p(2,0),p(2,1),p(0,1)}});auto r=integrateRasterFlux(j);QVERIFY2(r.error.isEmpty(),qPrintable(r.error));QVERIFY(qAbs(r.totalFlow-21)<1e-8);QVERIFY(qAbs(r.selectedArea-14)<1e-8);}
 void projectedFeetAndDeclaredUnits(){auto crs=wkt(2263);auto j=request(raster("feet",1,1,{25.4},{0,1,0,0,0,1},crs),{box(0,0,1,1)});j.meshCrsWkt=crs;j.densityUnit="mm/h";auto r=integrateRasterFlux(j);QVERIFY2(r.error.isEmpty(),qPrintable(r.error));OGRSpatialReference s;s.importFromEPSG(2263);const double area=s.GetLinearUnits()*s.GetLinearUnits();QVERIFY(qAbs(r.totalFlow-area*.0254/3600)<1e-16);j.densityUnit="in/h";j.scale=1./25.4;QVERIFY(qAbs(integrateRasterFlux(j).totalFlow-r.totalFlow)<1e-16);j.densityUnit="m/day";j.scale=1;j.offset=-24.4;QVERIFY(qAbs(integrateRasterFlux(j).totalFlow-area/86400)<1e-16);}
 void coverage_data(){QTest::addColumn<double>("bad");QTest::addColumn<bool>("mask");QTest::newRow("nodata")<<-9999.<<false;QTest::newRow("nonfinite")<<std::numeric_limits<double>::quiet_NaN()<<false;QTest::newRow("mask")<<2.<<true;}
 void coverage(){QFETCH(double,bad);QFETCH(bool,mask);auto j=request(raster(QString("coverage_")+QTest::currentDataTag(),2,1,{bad,4},{0,1,0,0,0,1},QString(),mask),{box(0,0,2,1),box(3,0,1,1)});auto refused=integrateRasterFlux(j);QVERIFY(!refused.error.isEmpty());QVERIFY(refused.flows.isEmpty());j.coverage=RasterFluxCoverage::ValidAreaOnly;auto r=integrateRasterFlux(j);QVERIFY2(r.error.isEmpty(),qPrintable(r.error));QCOMPARE(r.flows[0],4.);QVERIFY(std::isnan(r.flows[1]));QCOMPARE(r.validArea,1.);QCOMPARE(r.uncoveredArea,2.);QCOMPARE(r.partialCells,1);QCOMPARE(r.emptyCells,1);}
 void outsidePartialAndZeroAreDistinct(){auto j=request(raster("zero",1,1,{0}),{box(.5,0,1,1)});QVERIFY(!integrateRasterFlux(j).error.isEmpty());j.coverage=RasterFluxCoverage::ValidAreaOnly;auto r=integrateRasterFlux(j);QVERIFY(r.error.isEmpty());QCOMPARE(r.flows[0],0.);QCOMPARE(r.validArea,.5);QCOMPARE(r.uncoveredArea,.5);}
 void crsRefusalsAndAssignedCrs(){auto j=request(raster("no_crs",1,1,{1},{0,1,0,0,0,1},QStringLiteral("")),{box(0,0,1,1)});QVERIFY(!integrateRasterFlux(j).error.isEmpty());j.assignedSourceCrsWkt=wkt();QVERIFY(integrateRasterFlux(j).error.isEmpty());j.meshCrsWkt=wkt(32619);QVERIFY(!integrateRasterFlux(j).error.isEmpty());j.meshCrsWkt=wkt(4326);j.assignedSourceCrsWkt=j.meshCrsWkt;QVERIFY(!integrateRasterFlux(j).error.isEmpty());j.meshCrsWkt="bad";QVERIFY(!integrateRasterFlux(j).error.isEmpty());}
 void invalidInputsRefuseWithoutValues(){auto base=request(raster("invalid",1,1,{1}),{box(0,0,1,1)});auto j=base;j.densityUnit.clear();QVERIFY(!integrateRasterFlux(j).error.isEmpty());j=base;j.band=2;QVERIFY(!integrateRasterFlux(j).error.isEmpty());j=base;j.footprints[0]={{0,0},{1,1},{0,1},{1,0}};QVERIFY(!integrateRasterFlux(j).error.isEmpty());j=base;j.footprints[0]={{0,0},{1,0},{.2,.2},{0,1}};QVERIFY(!integrateRasterFlux(j).error.isEmpty());j=base;j.cells.append(0);j.footprints.append(base.footprints[0]);QVERIFY(!integrateRasterFlux(j).error.isEmpty());j=base;j.scale=std::numeric_limits<double>::infinity();QVERIFY(!integrateRasterFlux(j).error.isEmpty());j=base;j.offset=std::numeric_limits<double>::max();j.scale=std::numeric_limits<double>::max();auto r=integrateRasterFlux(j);QVERIFY(!r.error.isEmpty());QVERIFY(r.flows.isEmpty());}
 void originalResolutionBeyondLegacyLimitAndCancellation(){const int n=300;QVector<double> values(n*n,0);values[1]=90000;auto j=request(raster("large",n,n,values),{box(0,0,n,n)});auto r=integrateRasterFlux(j);QVERIFY2(r.error.isEmpty(),qPrintable(r.error));QCOMPARE(r.totalFlow,90000.);auto cancelled=integrateRasterFlux(j,[]{return true;});QVERIFY(cancelled.cancelled);QVERIFY(cancelled.flows.isEmpty());int polls=0;cancelled=integrateRasterFlux(j,[&]{return ++polls>10;});QVERIFY(cancelled.cancelled);QVERIFY(cancelled.flows.isEmpty());QCOMPARE(cancelled.totalFlow,0.);}
};
QTEST_APPLESS_MAIN(TestRasterFluxIntegration)
#include "test_rasterfluxintegration.moc"
