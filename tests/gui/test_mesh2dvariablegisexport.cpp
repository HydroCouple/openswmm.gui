#include "io/mesh2dvariablegisexport.h"
#include "layers/swmm2dresultslayer.h"
#include <QTest>
#include <QDir>
#include <QFile>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
using namespace openswmmvis::io;
class StaticGisSource:public IMesh2DSource{public:int called=-1;int vertexCount()const override{return 3;}int triangleCount()const override{return 1;}int timeCount()const override{return 8;}bool readMeshGeometry(std::vector<double>&x,std::vector<double>&y,std::vector<double>&z,std::vector<std::array<int,3>>&t)override{x={0,1,0};y={0,0,1};z={0,0,0};t={{0,1,2}};return true;}bool readDepthsAt(int,std::vector<float>&v)override{v={1};return true;}QVector<Mesh2DResultVariable> faceVariables(QStringList * =nullptr)const override{Mesh2DResultVariable d;d.dataset="Mesh2_face_gw_base_el";d.units="m";d.unitsKnown=true;d.temporal=Mesh2DResultVariable::Temporal::Static;return {d};}bool readFaceVariableAt(const Mesh2DResultVariable&,int t,std::vector<float>&v,std::vector<Mesh2DValueStatus>&s)override{called=t;v={9};s={Mesh2DValueStatus::Valid};return t==0;}};
class TestMesh2DVariableGisExport:public QObject {
 Q_OBJECT
 QString root;
 Mesh2DVariableGisSnapshot snapshot() {
  Mesh2DVariableGisSnapshot s;s.crsWkt="EPSG:32618";s.requestedFrame=1;s.sourcePath=QDir(root).filePath("input.h5");
  s.scalar.descriptor.dataset="Mesh2_face_gw_sat_conc";s.scalar.descriptor.species="Nitrate, \"A\"";s.scalar.descriptor.units="ug/L";s.scalar.descriptor.unitsKnown=true;
  s.scalar.descriptor.domain=Mesh2DResultVariable::Domain::Groundwater;s.scalar.descriptor.zone=Mesh2DResultVariable::Zone::Saturated;
  for(int i=0;i<4;++i){int n=s.vertices.size();s.vertices<<QPointF(i,0)<<QPointF(i+1,0)<<QPointF(i+1,1)<<QPointF(i,1);s.cells.push_back({n,n+1,n+2,n+3});}
  s.scalar.values={0,std::numeric_limits<float>::quiet_NaN(),7,std::numeric_limits<float>::quiet_NaN()};
  s.scalar.status={Mesh2DValueStatus::Valid,Mesh2DValueStatus::Missing,Mesh2DValueStatus::Waterless,Mesh2DValueStatus::NotApplicable};return s;
 }
 Mesh2DVariableGisOptions options(QString name,Mesh2DVariableGisFormat format) {
  // These exact basenames belong to this test, including optional PAM copies
  // produced by GDAL's subsequent read-only inspection. Never sweep the root.
  const QStringList owned{"native.gpkg","native.tif","preserved.gpkg","extreme.tif","companion.tif","mid-write.gpkg","native-scalar-foot-geometry.gpkg"};
  if(!owned.contains(name))return {};
  Mesh2DVariableGisOptions o;o.path=QDir(root).filePath(name);QFile::remove(o.path);QFile::remove(o.path+".aux.xml");o.format=format;o.pixelSize=.5;ProjectSaveOutputs::captureDestination(o.path,&o.destination);return o;
 }
private slots:
 void initTestCase(){root=qEnvironmentVariable("SWMMVIS_SCALAR_GIS_OUTPUT",QFileInfo(QString::fromUtf8(__FILE__)).absoluteDir().filePath("../../workplans/artifacts/phase_36_acceptance/scalar-gis"));QVERIFY(QDir().mkpath(root));GDALAllRegister();}
 void vectorNativeValuesAndAllStatuses(){auto s=snapshot();auto o=options("native.gpkg",Mesh2DVariableGisFormat::GeoPackage);QString e;QVERIFY2(exportMesh2DVariableGis(s,o,{},&e),qPrintable(e));
  QVERIFY(!QFileInfo::exists(o.path+".aux.xml"));
  auto *d=static_cast<GDALDataset*>(GDALOpenEx(o.path.toUtf8(),GDAL_OF_VECTOR,nullptr,nullptr,nullptr));QVERIFY(d);auto *l=d->GetLayerByName("scalar_cells");QVERIFY(l);QCOMPARE(l->GetFeatureCount(),GIntBig(4));
  // Metadata must survive in the GeoPackage itself, not depend on optional
  // PAM duplicates that this GDAL driver may create when closing readback.
  auto *metadata=d->ExecuteSQL("SELECT metadata FROM gpkg_metadata",nullptr,nullptr);QVERIFY(metadata);auto *record=metadata->GetNextFeature();QVERIFY(record);
  const QString xml=QString::fromUtf8(record->GetFieldAsString(0));QVERIFY(xml.contains("NATIVE_UNITS"));QVERIFY(xml.contains("ug/L"));QVERIFY(xml.contains(s.scalar.descriptor.key()));
  OGRFeature::DestroyFeature(record);d->ReleaseResultSet(metadata);
  for(int c=0;c<4;++c){auto *f=l->GetNextFeature();QVERIFY(f);QCOMPARE(f->GetFieldAsInteger("cell_index"),c);QCOMPARE(QString(f->GetFieldAsString("units")),QString("ug/L"));QCOMPARE(QString(f->GetFieldAsString("species")),s.scalar.descriptor.species);const int v=f->GetFieldIndex("value");QCOMPARE(bool(f->IsFieldSetAndNotNull(v)),c==0||c==2);if(c==0)QCOMPARE(f->GetFieldAsDouble(v),0.);OGRFeature::DestroyFeature(f);}GDALClose(d);
 }
 void rasterMaskRetainsZeroAndNoData(){auto s=snapshot();s.scalar.values[0]=255;auto o=options("native.tif",Mesh2DVariableGisFormat::GeoTiff);QString e;QVERIFY2(exportMesh2DVariableGis(s,o,{},&e),qPrintable(e));auto *d=static_cast<GDALDataset*>(GDALOpen(o.path.toUtf8(),GA_ReadOnly));QVERIFY(d);QCOMPARE(QString(d->GetRasterBand(1)->GetUnitType()),QString("ug/L"));double values[8],states[8];QCOMPARE(d->GetRasterBand(1)->RasterIO(GF_Read,0,0,8,1,values,8,1,GDT_Float64,0,0,nullptr),CE_None);QCOMPARE(d->GetRasterBand(2)->RasterIO(GF_Read,0,0,8,1,states,8,1,GDT_Float64,0,0,nullptr),CE_None);QCOMPARE(values[0],255.);QVERIFY(std::isnan(d->GetRasterBand(1)->GetNoDataValue()));QVERIFY(std::isnan(d->GetRasterBand(2)->GetNoDataValue()));for(int x=0;x<8;++x){QCOMPARE(states[x],double(x/2));if(x>=2)QVERIFY(std::isnan(values[x]));}GDALClose(d);}
 void refusedAndCancelledExportsPreserveDestination(){auto s=snapshot();auto o=options("preserved.gpkg",Mesh2DVariableGisFormat::GeoPackage);QFile f(o.path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("sentinel");f.close();QVERIFY(ProjectSaveOutputs::captureDestination(o.path,&o.destination));QString e;
  s.scalar.descriptor.unitsKnown=false;QVERIFY(!exportMesh2DVariableGis(s,o,{},&e));s.scalar.descriptor.unitsKnown=true;QVERIFY(!exportMesh2DVariableGis(s,o,[]{return true;},&e));
  auto changed=o;changed.destination.fingerprint="changed";QVERIFY(!exportMesh2DVariableGis(s,changed,{},&e));s.sourcePath=o.path;QVERIFY(!exportMesh2DVariableGis(s,o,{},&e));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),QByteArray("sentinel"));
 }
 void extremeSignedExtentRefusesUnsafeSpatialIndex_data(){QTest::addColumn<double>("offset");QTest::newRow("positive")<<1e12;QTest::newRow("negative")<<-1e12;}
 void extremeSignedExtentRefusesUnsafeSpatialIndex(){QFETCH(double,offset);auto s=snapshot();for(int v=0;v<4;++v)s.vertices[v]+=QPointF(offset,0);auto o=options("extreme.tif",Mesh2DVariableGisFormat::GeoTiff);o.pixelSize=1e12;QString error;QVERIFY(!exportMesh2DVariableGis(s,o,{},&error));QVERIFY2(error.contains("spatial index"),qPrintable(error));QVERIFY(!QFileInfo::exists(o.path));}
 void destinationCompanionsRefused(){auto s=snapshot();auto o=options("companion.tif",Mesh2DVariableGisFormat::GeoTiff);QFile sidecar(o.path+".aux.xml");QVERIFY(sidecar.open(QIODevice::WriteOnly));sidecar.write("external metadata");sidecar.close();QString e;QVERIFY(!exportMesh2DVariableGis(s,o,{},&e));QVERIFY(e.contains("companion"));QVERIFY(!QFileInfo::exists(o.path));QVERIFY(sidecar.exists());QFile::remove(sidecar.fileName());}
 void midWriteCancellationPreservesExistingTarget(){
  auto s=snapshot();auto o=options("mid-write.gpkg",Mesh2DVariableGisFormat::GeoPackage);QFile file(o.path);QVERIFY(file.open(QIODevice::WriteOnly));file.write("existing export");file.close();QVERIFY(ProjectSaveOutputs::captureDestination(o.path,&o.destination));
  bool cancel=false,enteredWriter=false;QString error;
  QVERIFY(!exportMesh2DVariableGis(s,o,[&]{return cancel;},&error,[&](int progress){if(progress<100){enteredWriter=true;cancel=true;}}));
  QVERIFY(enteredWriter);QVERIFY(error.contains("cancelled"));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("existing export"));
  QVERIFY(QDir(root).entryList({".scalar-export-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty());
 }
 void nonUnitGeometryScalingPreservesNativeScalar(){
  StaticGisSource source;QString error;const double factor=1./.3048006096012192;
  auto s=captureMesh2DVariableGis(source,source.faceVariables()[0].key(),7,factor,"EPSG:2263",&error);QVERIFY2(s,qPrintable(error));
  QCOMPARE(s->vertices[1].x(),factor);QCOMPARE(s->scalar.values[0],9.f);QCOMPARE(s->scalar.descriptor.units,QString("m"));
  auto o=options("native-scalar-foot-geometry.gpkg",Mesh2DVariableGisFormat::GeoPackage);QVERIFY2(exportMesh2DVariableGis(*s,o,{},&error),qPrintable(error));
  auto *dataset=static_cast<GDALDataset*>(GDALOpenEx(o.path.toUtf8(),GDAL_OF_VECTOR,nullptr,nullptr,nullptr));QVERIFY(dataset);auto *layer=dataset->GetLayerByName("scalar_cells");QVERIFY(layer);auto *feature=layer->GetNextFeature();QVERIFY(feature);
  QCOMPARE(feature->GetFieldAsDouble("value"),9.);QCOMPARE(QString(feature->GetFieldAsString("units")),QString("m"));OGREnvelope envelope;feature->GetGeometryRef()->getEnvelope(&envelope);QVERIFY(std::abs(envelope.MaxX-factor)<1e-12);QVERIFY(std::abs(layer->GetSpatialRef()->GetLinearUnits()-.3048006096012192)<1e-14);
  OGRFeature::DestroyFeature(feature);GDALClose(dataset);
 }
 void staticCaptureReadsFrameZero(){StaticGisSource source;
  QString e;auto s=captureMesh2DVariableGis(source,source.faceVariables()[0].key(),7,1,"EPSG:32618",&e);QVERIFY2(s,qPrintable(e));QCOMPARE(source.called,0);QCOMPARE(s->scalar.values[0],9.f);
 }
};
QTEST_MAIN(TestMesh2DVariableGisExport)
#include "test_mesh2dvariablegisexport.moc"
