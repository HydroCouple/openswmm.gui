#include "render/resultscalargeometry.h"
#include <QTest>
using namespace OpenSWMM::Render;
using namespace openswmmvis::io;
class TestResultScalarGeometry:public QObject {
 Q_OBJECT
private slots:
 void nativeLabelsAndMaskedGap() {
  QVector<QPointF> v{{1e9,1e9},{1e9+1,1e9},{1e9+2,1e9},{1e9,1e9+1},{1e9+1,1e9+1},{1e9+2,1e9+1}};
  std::vector<std::array<int,4>> cells{{0,1,4,3},{1,2,5,4}};
  std::vector<std::array<int,3>> tris{{0,1,4},{0,4,3},{1,2,5},{1,5,4}};
  Mesh2DScalarFrame f;f.values={0,10};f.status={Mesh2DValueStatus::Valid,Mesh2DValueStatus::Waterless};
  auto g=buildResultScalarGeometry(v,cells,tris,{0,0,1,1},f,{1,5,9},true);
  QVERIFY(g.error.isEmpty());QCOMPARE(g.labels.size(),1);QCOMPARE(g.labels[0].value,0.);QCOMPARE(g.labels[0].point,QPointF(1e9+.5,1e9+.5));QVERIFY(g.contours.empty());
  f.status[1]=Mesh2DValueStatus::Valid;g=buildResultScalarGeometry(v,cells,tris,{0,0,1,1},f,{2.5,7.5},true);
  QVERIFY(g.error.isEmpty());QCOMPARE(g.labels.size(),2);QVERIFY(!g.contours.empty());
  for(const auto &s:g.contours){QVERIFY(std::isfinite(s.a.x()));QVERIFY(s.a.x()>=1e9&&s.a.x()<=1e9+2);}
 }
 void placementRetainsNativeUnitsAndAvoidsCollisions() {
  ResultScalarGeometry g;g.labels={{{100,100},0,0},{{101,100},7,1},{{300,100},12.5,2}};
  const auto labels=placeResultScalarLabels(g,QTransform(),QRectF(0,0,500,300),QFont(),2,"ug/L",false);
  QCOMPARE(labels.size(),2);QVERIFY(labels[0].text.contains("0.00 [ug/L]"));QVERIFY(!labels[0].rect.intersects(labels[1].rect));
 }
 void badTopologyAndNonfiniteLevelsFail() {
  Mesh2DScalarFrame f;f.values={1};f.status={Mesh2DValueStatus::Valid};
  const QVector<QPointF> v{{0,0},{1,0},{0,1}};
  QVERIFY(!buildResultScalarGeometry(v,{{0,1,8,-1}},{{0,1,2}},{0},f,{1},false).error.isEmpty());
  QVERIFY(!buildResultScalarGeometry(v,{{0,1,2,-1}},{{0,1,2}},{0},f,{std::numeric_limits<double>::quiet_NaN()},false).error.isEmpty());
 }
};
QTEST_MAIN(TestResultScalarGeometry)
#include "test_resultscalargeometry.moc"
