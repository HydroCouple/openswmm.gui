#include "layers/cellwatergeometry.h"
#include "contour/marchingtriangles.h"
#include <QtTest>
#include <random>

namespace {
using Cell = VertexDepthReconstruct::CellSplit;
Cell triangle() {
    Cell c; c.v = {0,1,2,-1}; c.sub[0] = {0,1,2}; c.area[0] = 0.5; return c;
}
// Integrate an affine depth over a clipped polygon using triangle moments.
// This does not call the VFR forward formula or its inverse.
double volume(const CellWaterGeometry::WetPolygon& p) {
    double sum = 0;
    for (int k = 1; k+1 < p.size; ++k) {
        const auto& a=p.vertices[0]; const auto& b=p.vertices[k]; const auto& c=p.vertices[k+1];
        const auto ab=b.point-a.point, ac=c.point-a.point;
        const double area=0.5*std::abs(ab.x()*ac.y()-ab.y()*ac.x());
        sum += area*(a.depth+b.depth+c.depth)/3;
    }
    return sum;
}
}

class TestCellWaterGeometry : public QObject {
    Q_OBJECT
private slots:
    void triangleConservesStorage();
    void quadUsesOneStage();
    void datumDoesNotChangeDepth();
    void wetIntervalsAndTerrainCrossings();
    void invalidAndDryNeverPaint();
    void equilibriumAndIndependentPools();
    void smoothConnectedStages();
    void smoothLakeStaysLevelAtDryCorners();
    void smoothDoesNotCrossCrestsOrPointContacts();
    void thinFilmsAndPartialPoolsAreDistinct();
    void hiddenFilmCannotChangeNeighbourStage();
};

void TestCellWaterGeometry::thinFilmsAndPartialPoolsAreDistinct()
{
    using namespace CellWaterGeometry;
    const VisibilityPolicy policy{0.001,false};
    const auto cell=triangle();
    for (double datum : {0.0,100000.0}) {
        const std::vector<double> flat(3,datum);
        for (double depth : {0.0005,double(float(0.001))}) {
            const auto surface=reconstruct(cell,depth,flat);
            QCOMPARE(surface.state,State::Wet); // raw storage is retained
            QCOMPARE(displayState(cell,surface,flat,policy),DisplayState::ThinFilm);
            QVERIFY(!visible(DisplayState::ThinFilm,policy));
            QVERIFY(visible(DisplayState::ThinFilm,{0.001,true}));
        }
        const std::vector<double> slope{datum,datum,datum+4};
        // A small whole-cell mean is a deeper local pool, not a uniform film.
        const auto pool=reconstruct(cell,0.00001,slope);
        QCOMPARE(displayState(cell,pool,slope,policy),DisplayState::PartiallyWet);
        QVERIFY(pool.level>0.001);
        double lo,hi;
        QVERIFY(wetInterval(pool.signedDepth(datum),pool.signedDepth(datum+4),0,lo,hi));
        QVERIFY(std::abs(hi-pool.level/4)<1e-12);
        QCOMPARE(displayState(cell,reconstruct(cell,0,flat),flat,policy),DisplayState::Dry);
        QCOMPARE(displayState(cell,reconstruct(cell,0.01,flat),flat,policy),DisplayState::Wet);
        QCOMPARE(displayState(cell,{},flat,policy),DisplayState::Invalid);
    }
    Cell quad; quad.v={0,1,2,3};quad.nSub=2;quad.sub={{{0,1,2},{0,2,3}}};quad.area={1,1};
    const std::vector<double> slope{0,0,4,4};
    const auto pool=reconstruct(quad,0.00001,slope);
    QCOMPARE(displayState(quad,pool,slope,policy),DisplayState::PartiallyWet);
}

void TestCellWaterGeometry::hiddenFilmCannotChangeNeighbourStage()
{
    using namespace CellWaterGeometry;
    Cell a=triangle(), b=triangle(); b.v={1,3,2,-1}; b.sub[0]={1,3,2};
    const std::vector<Cell> cells{a,b};
    const std::vector<double> z(4,0);
    const std::vector<Surface> surfaces{reconstruct(a,1,z),reconstruct(b,0.0005,z)};
    std::vector<CornerDepths> depths;
    smoothCornerDepths(cells,surfaces,z,smoothTopology(cells),depths,{0.001,false});
    for (int i=0;i<3;++i) { QCOMPARE(depths[0][i],1.0); QVERIFY(std::isnan(depths[1][i])); }
    smoothCornerDepths(cells,surfaces,z,smoothTopology(cells),depths,{0.001,true});
    QVERIFY(std::isfinite(depths[1][0]));
    QVERIFY(depths[0][1]<1.0);
}

void TestCellWaterGeometry::triangleConservesStorage()
{
    std::mt19937 rng(20260930);
    std::uniform_real_distribution<double> bed(-3,5), amount(0.000001,4);
    for (int i=0; i<2000; ++i) {
        const std::vector<double> z={bed(rng),bed(rng),bed(rng)};
        const double h=amount(rng);
        const auto s=CellWaterGeometry::reconstruct(triangle(),h,z);
        QCOMPARE(s.state,CellWaterGeometry::State::Wet);
        const auto p=CellWaterGeometry::clipTriangle({0,0},{1,0},{0,1},
            s.signedDepth(z[0]),s.signedDepth(z[1]),s.signedDepth(z[2]),0);
        QVERIFY2(std::abs(volume(p)-0.5*h) < 1e-10*std::max(h,1e-6),"Clipped volume differs from stored volume");
    }
    for (const auto& z : {std::vector<double>{0,0,4}, {0,4,4}, {4,4,4}})
        for (double h : {1e-9,1e-5,0.1,5.0}) {
            const auto s=CellWaterGeometry::reconstruct(triangle(),h,z);
            const auto p=CellWaterGeometry::clipTriangle({0,0},{1,0},{0,1},
                s.signedDepth(z[0]),s.signedDepth(z[1]),s.signedDepth(z[2]),0);
            QVERIFY(std::abs(volume(p)-h*0.5) < 1e-10*std::max(h,1e-6));
        }
}

void TestCellWaterGeometry::quadUsesOneStage()
{
    Cell c; c.v={0,1,2,3}; c.nSub=2; c.sub={{{0,1,2},{0,2,3}}}; c.area={1,2};
    const QPointF a(0,0),b(2,0),cc(2,1),d(0,2);
    const std::vector<double> z={0,3,1,4};
    for (double h : {1e-8,0.01,0.5,4.0}) {
        const auto s=CellWaterGeometry::reconstruct(c,h,z);
        double q[4]; for (int i=0;i<4;++i) q[i]=s.signedDepth(z[i]);
        const auto p1=CellWaterGeometry::clipTriangle(a,b,cc,q[0],q[1],q[2],0);
        const auto p2=CellWaterGeometry::clipTriangle(a,cc,d,q[0],q[2],q[3],0);
        QVERIFY(std::abs(volume(p1)+volume(p2)-3*h) < 1e-9*std::max(h,1e-6));
        for (int i=1;i<4;++i) QVERIFY(std::abs(z[i]+q[i]-z[0]-q[0]) < 1e-12);
    }
}

void TestCellWaterGeometry::datumDoesNotChangeDepth()
{
    for (double datum : {0.0,1000.0,100000.0}) {
        const auto s=CellWaterGeometry::reconstruct(triangle(),0.001,{datum,datum,datum});
        QVERIFY(std::abs(s.signedDepth(datum)-0.001) < 1e-15);
    }
}

void TestCellWaterGeometry::wetIntervalsAndTerrainCrossings()
{
    double lo,hi;
    QVERIFY(CellWaterGeometry::wetInterval(1,-3,0,lo,hi));
    QCOMPARE(lo,0.0); QCOMPARE(hi,0.25);
    QVERIFY(CellWaterGeometry::wetInterval(-3,1,0,lo,hi));
    QCOMPARE(lo,0.75); QCOMPARE(hi,1.0);
    QVERIFY(!CellWaterGeometry::wetInterval(0,0,0,lo,hi));
    QVERIFY(CellWaterGeometry::triangleInterval({-1,0.25},{2,0.25},{0,0},{1,0},{0,1},lo,hi));
    QVERIFY(std::abs(lo-1.0/3) < 1e-14);
    QVERIFY(std::abs(hi-1.75/3) < 1e-14);
    QVERIFY(CellWaterGeometry::triangleInterval({2,0.25},{-1,0.25},{0,0},{1,0},{0,1},lo,hi));
    QVERIFY(std::abs(lo-1.25/3) < 1e-14);
    QVERIFY(std::abs(hi-2.0/3) < 1e-14);
    QVERIFY(CellWaterGeometry::triangleInterval({0,0},{1,0},{0,0},{1,0},{0,1},lo,hi));
    QCOMPARE(lo,0.0); QCOMPARE(hi,1.0);
}

void TestCellWaterGeometry::invalidAndDryNeverPaint()
{
    const double nan=std::numeric_limits<double>::quiet_NaN();
    auto dry=CellWaterGeometry::reconstruct(triangle(),0,{0,0,4});
    QCOMPARE(dry.state,CellWaterGeometry::State::Dry);
    QVERIFY(std::isnan(dry.signedDepth(0)));
    QCOMPARE(CellWaterGeometry::reconstruct(triangle(),nan,{0,0,4}).state,CellWaterGeometry::State::Invalid);
    QCOMPARE(CellWaterGeometry::reconstruct(triangle(),1,{0,nan,4}).state,CellWaterGeometry::State::Invalid);
    QCOMPARE(CellWaterGeometry::clipTriangle({0,0},{1,0},{0,1},nan,nan,nan,0).size,0);
    auto extract=[nan](int,QPointF& a,QPointF& b,QPointF& c,double& q0,double& q1,double& q2) {
        a={0,0};b={1,0};c={0,1};q0=q1=q2=nan;
    };
    QVERIFY(OpenSWMM::Contour::marchingTrianglesIsobands(std::vector<int>{0},std::vector<double>{0,1},extract).empty());
    QVERIFY(OpenSWMM::Contour::marchingTriangles(std::vector<int>{0},std::vector<double>{0.5},extract).empty());
}

void TestCellWaterGeometry::equilibriumAndIndependentPools()
{
    // z=4y; stage 1.5 has hbar=eta^2/4-eta^3/48.
    const double eta=1.5, h=eta*eta/4-eta*eta*eta/48;
    const auto s=CellWaterGeometry::reconstruct(triangle(),h,{0,0,4});
    QVERIFY(std::abs(s.level-eta) < 1e-12);
    double lo,hi;
    QVERIFY(CellWaterGeometry::wetInterval(s.signedDepth(0),s.signedDepth(4),0,lo,hi));
    QVERIFY(std::abs(hi-0.375) < 1e-12);
    const auto p=CellWaterGeometry::clipTriangle({0,0},{1,0},{0,1},s.level,s.level,s.level-4,0);
    for (int i=0;i<p.size;++i) QVERIFY(p.vertices[i].point.y() <= 0.375+1e-12);
    const auto other=CellWaterGeometry::reconstruct(triangle(),2,{0,0,0});
    QCOMPARE(other.level,2.0);
    QVERIFY(std::abs(s.level-1.5) < 1e-12); // no shared-vertex averaging
}
void TestCellWaterGeometry::smoothConnectedStages()
{
    Cell a=triangle(), b=triangle(); b.v={1,3,2,-1}; b.sub[0]={1,3,2};
    const std::vector<Cell> cells{a,b};
    const auto topology=CellWaterGeometry::smoothTopology(cells);
    for (double datum : {0.0,100000.0}) {
        const std::vector<double> z(4,datum);
        std::vector<CellWaterGeometry::Surface> surfaces;
        surfaces.push_back(CellWaterGeometry::reconstruct(a,1,z));
        surfaces.push_back(CellWaterGeometry::reconstruct(b,3,z));
        std::vector<CellWaterGeometry::CornerDepths> q;
        CellWaterGeometry::smoothCornerDepths(cells,surfaces,z,topology,q);
        QCOMPARE(q[0][0],1.0); QCOMPARE(q[1][1],3.0);
        QCOMPARE(q[0][1],2.0); QCOMPARE(q[0][2],2.0);
        QCOMPARE(q[0][1],q[1][0]); QCOMPARE(q[0][2],q[1][2]);
        // The common trace is identical everywhere along the shared edge.
        for (double t : {0.0,0.25,0.5,0.75,1.0})
            QCOMPARE((1-t)*q[0][1]+t*q[0][2],(1-t)*q[1][0]+t*q[1][2]);
        surfaces[1]=CellWaterGeometry::reconstruct(b,0,z);
        CellWaterGeometry::smoothCornerDepths(cells,surfaces,z,topology,q);
        QCOMPARE(q[0][1],1.0);
        QVERIFY(std::isnan(q[1][0])); // no borrowed water in the dry cell
    }
}

void TestCellWaterGeometry::smoothLakeStaysLevelAtDryCorners()
{
    Cell a=triangle(),b=triangle();b.v={1,3,2,-1};b.sub[0]={1,3,2};
    const std::vector<Cell> cells{a,b};
    const std::vector<double> z{0,0,4,4};
    std::vector<CellWaterGeometry::Surface> surfaces{
        CellWaterGeometry::reconstruct(a,0.4921875,z),
        CellWaterGeometry::reconstruct(b,0.0703125,z)};
    std::vector<CellWaterGeometry::CornerDepths> q;
    CellWaterGeometry::smoothCornerDepths(cells,surfaces,z,CellWaterGeometry::smoothTopology(cells),q);
    for (int c=0;c<2;++c)
        for (int k=0;k<3;++k)
            QVERIFY(std::abs(z[cells[c].v[k]]+q[c][k]-1.5)<1e-12);
    double lo,hi;
    QVERIFY(CellWaterGeometry::wetInterval(q[1][0],q[1][1],0,lo,hi));
    QVERIFY(std::abs(hi-0.375)<1e-12);
}

void TestCellWaterGeometry::smoothDoesNotCrossCrestsOrPointContacts()
{
    Cell a=triangle(),b=triangle();b.v={1,3,2,-1};b.sub[0]={1,3,2};
    std::vector<Cell> cells{a,b};
    std::vector<double> z{0,4,4,0};
    std::vector<CellWaterGeometry::Surface> surfaces{
        CellWaterGeometry::reconstruct(a,1.0/48,z),
        CellWaterGeometry::reconstruct(b,8.0/48,z)};
    std::vector<CellWaterGeometry::CornerDepths> q;
    CellWaterGeometry::smoothCornerDepths(cells,surfaces,z,CellWaterGeometry::smoothTopology(cells),q);
    QVERIFY(std::abs(q[0][1]+3)<1e-12);
    QVERIFY(std::abs(q[1][0]+2)<1e-12); // ridge remains dry from both sides
    cells[1].v={0,3,4,-1};cells[1].sub[0]={0,3,4};z.assign(5,0);
    surfaces={CellWaterGeometry::reconstruct(cells[0],1,z),CellWaterGeometry::reconstruct(cells[1],3,z)};
    CellWaterGeometry::smoothCornerDepths(cells,surfaces,z,CellWaterGeometry::smoothTopology(cells),q);
    QCOMPARE(q[0][0],1.0);QCOMPARE(q[1][0],3.0); // only a vertex is shared
}

QTEST_APPLESS_MAIN(TestCellWaterGeometry)
#include "test_cellwatergeometry.moc"
