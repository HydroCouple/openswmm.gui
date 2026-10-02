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
    void inclinedSurfaceConservesStorage();
    void flowingSlopeHasNoCellStacks();
    void flowReconstructionPreservesPoolsAndDryCells();
    void flowingSurfaceDepthsStayBounded();
    void flowingPatchHasNoCornerPockets();
    void clippedStorageDerivativeMatchesGeometry();
    void irregularMovingSheetStaysContinuous();
    void neighborhoodClosesVolumeWithoutCrossingDryGap();
};

namespace {
struct SlopeStrip {
    std::vector<double> x,y,z;
    std::vector<Cell> cells;
    std::vector<float> depths,flux;
    std::vector<CellWaterGeometry::Surface> surfaces;
    SlopeStrip(bool mixed=false,double datum=0) {
        for (int j=0;j<=5;++j) for (int k=0;k<2;++k) {
            x.push_back(j); y.push_back(k); z.push_back(datum+10-j);
        }
        for (int j=0;j<5;++j) {
            Cell a; a.v={2*j,2*j+2,2*j+3,-1}; a.sub[0]={2*j,2*j+2,2*j+3}; a.area[0]=0.5;
            if (mixed && j%2==0) {
                a.v[3]=2*j+1; a.nSub=2; a.sub[1]={2*j,2*j+3,2*j+1}; a.area[1]=0.5;
                cells.push_back(a);
            } else {
                cells.push_back(a);
                a.v={2*j,2*j+3,2*j+1,-1};a.sub[0]={2*j,2*j+3,2*j+1};cells.push_back(a);
            }
        }
        depths.assign(cells.size(),0.01f); flux.assign(4*cells.size(),0);
        for (size_t c=0;c<cells.size();++c) {
            const auto& cell=cells[c]; const int nv=cell.vertexCount();
            for (int e=0;e<nv;++e) // uniform discharge in +x; integrated outward flux
                flux[4*c+e]=0.1f*float(y[size_t(cell.v[(e+2)%nv])]-y[size_t(cell.v[(e+1)%nv])]);
        }
        update();
    }
    void update() {
        surfaces.clear();
        for (size_t c=0;c<cells.size();++c) surfaces.push_back(CellWaterGeometry::reconstruct(cells[c],depths[c],z));
    }
    std::vector<CellWaterGeometry::CornerDepths> reconstruct() {
        return CellWaterGeometry::flowingCornerDepths(cells,surfaces,x,y,z,
            CellWaterGeometry::smoothTopology(cells),depths,flux,{0.001,false});
    }
};
}

void TestCellWaterGeometry::inclinedSurfaceConservesStorage()
{
    using namespace CellWaterGeometry;
    SlopeStrip strip(true);
    std::mt19937 rng(20261001);
    std::uniform_real_distribution<double> slope(-3,3), amount(1e-7,2);
    for (int run=0;run<250;++run) for (const auto& c:strip.cells) {
        CornerDepths offsets{}; const double gx=slope(rng), gy=slope(rng), h=amount(rng);
        for (int k=0;k<c.vertexCount();++k) offsets[k]=gx*strip.x[c.v[k]]+gy*strip.y[c.v[k]];
        double level;
        const auto q=inclinedCorners(c,h,strip.z,offsets,level);
        double integrated=0,area=0;
        for (int s=0;s<c.nSub;++s) {
            int local[3]; QPointF p[3];
            for (int k=0;k<3;++k) {
                const int v=c.sub[s][k]; local[k]=int(std::find(c.v.begin(),c.v.end(),v)-c.v.begin());
                p[k]={strip.x[v],strip.y[v]};
            }
            integrated+=volume(clipTriangle(p[0],p[1],p[2],q[local[0]],q[local[1]],q[local[2]],0));
            area+=c.area[s];
        }
        QVERIFY(std::abs(integrated-area*h)<1e-9*std::max(h,1e-5));
    }
}

void TestCellWaterGeometry::flowingSlopeHasNoCellStacks()
{
    using namespace CellWaterGeometry;
    for (bool mixed:{false,true}) for (double datum:{0.0,100000.0}) for (double angle:{0.0,0.47}) {
        SlopeStrip strip(mixed,datum);
        for (size_t v=0;v<strip.x.size();++v) {
            const double x=strip.x[v],y=strip.y[v];
            strip.x[v]=500000+std::cos(angle)*x-std::sin(angle)*y;
            strip.y[v]=6000000+std::sin(angle)*x+std::cos(angle)*y;
        }
        for (float direction:{1.0f,-1.0f}) {
            for (auto& q:strip.flux) q*=direction;
            const auto raw=strip.reconstruct();
            QCOMPARE(raw.size(),strip.cells.size());
            std::vector<CornerDepths> field;
            smoothCornerDepths(strip.cells,strip.surfaces,strip.z,smoothTopology(strip.cells),field,{0.001,false},raw);
            for (size_t c=0;c<strip.cells.size();++c) for (int k=0;k<strip.cells[c].vertexCount();++k) {
                // One-centimetre sheet over one-metre cell relief stays wet
                // at BOTH ends, rather than becoming low-corner wedges.
                QVERIFY2(std::abs(raw[c][k]-strip.depths[c])<1e-7,qPrintable(QString("cell %1 corner %2 depth %3").arg(c).arg(k).arg(raw[c][k],0,'g',12)));
                QVERIFY(std::abs(field[c][k]-strip.depths[c])<1e-7);
            }
        }
    }
}

void TestCellWaterGeometry::flowReconstructionPreservesPoolsAndDryCells()
{
    using namespace CellWaterGeometry;
    SlopeStrip strip;
    // Missing/zero/inconsistent flow must not invent a moving sheet.
    const auto flux=strip.flux;
    strip.flux.clear(); QVERIFY(strip.reconstruct().empty());
    strip.flux.assign(flux.size(),0); QVERIFY(strip.reconstruct().empty());
    strip.flux=flux;
    for (auto& q:strip.flux) q=std::abs(q);
    QVERIFY(strip.reconstruct().empty());
    strip.flux=flux;
    strip.depths[4]=0; strip.update();
    const auto q=strip.reconstruct();
    QVERIFY(!q.empty());
    for (int k=0;k<3;++k) QVERIFY(std::isnan(q[4][k]));
    // Even with a through-flow stencil, equal stages remain a lake, including
    // partially wet banks. Varying mean depths must not be mistaken for slope.
    const double eta=9.5;
    for (size_t c=0;c<strip.cells.size();++c) {
        const auto& v=strip.cells[c].v;
        strip.depths[c]=float(VertexDepthReconstruct::triMeanDepthFromEta(eta,strip.z[v[0]],strip.z[v[1]],strip.z[v[2]]));
    }
    strip.update();
    const auto lake=strip.reconstruct();
    QVERIFY(!lake.empty());
    for (size_t c=0;c<strip.cells.size();++c) for (int k=0;k<3;++k)
        QVERIFY(std::abs(strip.z[strip.cells[c].v[k]]+lake[c][k]-eta)<1e-6);
}

void TestCellWaterGeometry::flowingSurfaceDepthsStayBounded()
{
    SlopeStrip strip(true);
    std::mt19937 rng(20261001);
    std::uniform_real_distribution<float> depth(0.0001f,3.0f);
    for(int run=0;run<100;++run) {
        for(auto& h:strip.depths) h=depth(rng);
        strip.update();
        double bound=0;
        for(size_t c=0;c<strip.cells.size();++c) for(int k=0;k<strip.cells[c].vertexCount();++k)
            bound=std::max(bound,strip.surfaces[c].signedDepth(strip.z[strip.cells[c].v[k]]));
        const auto result=strip.reconstruct();
        QVERIFY(!result.empty());
        for(size_t c=0;c<strip.cells.size();++c) for(int k=0;k<strip.cells[c].vertexCount();++k)
            QVERIFY(result[c][k]<=bound+1e-9);
    }
}

void TestCellWaterGeometry::flowingPatchHasNoCornerPockets()
{
    using namespace CellWaterGeometry;
    SlopeStrip patch;patch.x.clear();patch.y.clear();patch.z.clear();patch.cells.clear();
    constexpr int n=8;
    auto vertex=[](int j,int k){return j*(n+1)+k;};
    for(int j=0;j<=n;++j)for(int k=0;k<=n;++k){
        patch.x.push_back(5*j);patch.y.push_back(5*k);patch.z.push_back(30-0.35*j+0.15*k);
    }
    for(int j=0;j<n;++j)for(int k=0;k<n;++k){
        const int a=vertex(j,k),b=vertex(j+1,k),c=vertex(j+1,k+1),d=vertex(j,k+1);
        Cell t;t.area[0]=12.5;t.v={a,b,c,-1};t.sub[0]={a,b,c};patch.cells.push_back(t);
        t.v={a,c,d,-1};t.sub[0]={a,c,d};patch.cells.push_back(t);
    }
    patch.depths.assign(patch.cells.size(),.01f);patch.flux.assign(4*patch.cells.size(),0);
    for(size_t c=0;c<patch.cells.size();++c)for(int e=0;e<3;++e){
        const int a=patch.cells[c].v[(e+1)%3],b=patch.cells[c].v[(e+2)%3];
        patch.flux[4*c+e]=float(.1*(patch.y[b]-patch.y[a])-.03*(patch.x[b]-patch.x[a]));
    }
    patch.update();
    const auto raw=patch.reconstruct();QCOMPARE(raw.size(),patch.cells.size());
    std::vector<CornerDepths> field;
    smoothCornerDepths(patch.cells,patch.surfaces,patch.z,smoothTopology(patch.cells),field,{},raw);
    for(size_t c=0;c<patch.cells.size();++c)for(int k=0;k<3;++k){
        QVERIFY(std::abs(raw[c][k]-patch.depths[c])<1e-7);
        QVERIFY(std::abs(field[c][k]-patch.depths[c])<1e-7);
    }
}

void TestCellWaterGeometry::clippedStorageDerivativeMatchesGeometry()
{
    using namespace CellWaterGeometry;
    std::mt19937 rng(20261002);std::uniform_real_distribution<double> depth(-3,3);
    for(int run=0;run<1000;++run) {
        std::array<double,3> q{depth(rng),depth(rng),depth(rng)},derivative;
        const double mean=NeighborhoodFit::triangleStorage(q,derivative);
        QVERIFY(std::abs(0.5*mean-volume(clipTriangle({0,0},{1,0},{0,1},q[0],q[1],q[2],0)))<1e-12);
        for(int k=0;k<3;++k) {
            auto plus=q,minus=q;plus[k]+=1e-6;minus[k]-=1e-6;
            std::array<double,3> scratch;
            const double finite=(NeighborhoodFit::triangleStorage(plus,scratch)-NeighborhoodFit::triangleStorage(minus,scratch))/2e-6;
            QVERIFY(std::abs(finite-derivative[k])<1e-8);
            QVERIFY(derivative[k]>=0 && derivative[k]<=1.0/3+1e-12);
        }
    }
}

void TestCellWaterGeometry::irregularMovingSheetStaysContinuous()
{
    using namespace CellWaterGeometry;
    for(bool mixed:{false,true}) {
        SlopeStrip strip(mixed);
        for(size_t v=0;v<strip.z.size();++v) {
            strip.x[v]+=0.1*std::sin(2.1*v);strip.y[v]+=0.1*std::cos(1.3*v);
            strip.z[v]+=0.7*std::sin(2.8*v)+0.4*std::cos(0.7*v);
        }
        for(size_t c=0;c<strip.cells.size();++c) {
            auto& cell=strip.cells[c];
            for(int s=0;s<cell.nSub;++s) {
                const auto t=cell.sub[s];const QPointF a(strip.x[t[0]],strip.y[t[0]]),b(strip.x[t[1]],strip.y[t[1]]),d(strip.x[t[2]],strip.y[t[2]]);
                cell.area[s]=0.5*std::abs((b.x()-a.x())*(d.y()-a.y())-(b.y()-a.y())*(d.x()-a.x()));
            }
            for(int e=0;e<cell.vertexCount();++e) {
                const int a=cell.v[(e+1)%cell.vertexCount()],b=cell.v[(e+2)%cell.vertexCount()];
                strip.flux[4*c+e]=float(.1*(strip.y[b]-strip.y[a])-.03*(strip.x[b]-strip.x[a]));
            }
        }
        strip.update();const auto source=strip.depths;const auto field=strip.reconstruct();
        QCOMPARE(field.size(),strip.cells.size());
        for(size_t c=0;c<strip.cells.size();++c)for(int k=0;k<strip.cells[c].vertexCount();++k)
            QVERIFY(std::abs(field[c][k]-source[c])<1e-8);
        QCOMPARE(strip.depths,source);
    }
}

void TestCellWaterGeometry::neighborhoodClosesVolumeWithoutCrossingDryGap()
{
    using namespace CellWaterGeometry;
    for(bool mixed:{false,true}) {
        SlopeStrip strip(mixed);
        for(size_t c=0;c<strip.cells.size();++c) {
            const double x0=strip.x[strip.cells[c].v[0]];
            strip.depths[c]=x0==2?0:float(.004+.01*(c+1));
        }
        strip.update();const auto topology=smoothTopology(strip.cells);
        const auto field=flowingCornerDepths(strip.cells,strip.surfaces,strip.x,strip.y,strip.z,topology,strip.depths,strip.flux,{0,true});
        QCOMPARE(field.size(),strip.cells.size());
        double stored[2]{},displayed[2]{};
        for(size_t c=0;c<strip.cells.size();++c) {
            const auto& cell=strip.cells[c];const double x0=strip.x[cell.v[0]];
            if(x0==2) {
                for(int k=0;k<cell.vertexCount();++k)QVERIFY(std::isnan(field[c][k]));
                continue;
            }
            const int group=x0<2?0:1;
            for(int half=0;half<cell.nSub;++half) {
                const auto t=cell.sub[half];int local[3];
                for(int k=0;k<3;++k)local[k]=int(std::find(cell.v.begin(),cell.v.end(),t[k])-cell.v.begin());
                stored[group]+=cell.area[half]*strip.depths[c];
                displayed[group]+=volume(clipTriangle({strip.x[t[0]],strip.y[t[0]]},{strip.x[t[1]],strip.y[t[1]]},{strip.x[t[2]],strip.y[t[2]]},field[c][local[0]],field[c][local[1]],field[c][local[2]],0));
            }
        }
        for(int g=0;g<2;++g)QVERIFY(std::abs(displayed[g]-stored[g])<1e-9*stored[g]);
        for(const auto& e:topology.edges) {
            const auto &a=field[size_t(e.a0/4)],&b=field[size_t(e.a1/4)];
            if(!std::isfinite(a[0]) || !std::isfinite(b[0]))continue;
            QCOMPARE(a[e.a0%4],b[e.a1%4]);QCOMPARE(a[e.b0%4],b[e.b1%4]);
        }
    }
}

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
    // Inclined traces can wet opposite ends without any common wet length.
    const std::vector<CellWaterGeometry::CornerDepths> separated{{1,1,-3,0},{-3,1,1,0}};
    CellWaterGeometry::smoothCornerDepths(cells,surfaces,z,CellWaterGeometry::smoothTopology(cells),q,{},separated);
    QCOMPARE(q[0][1],1.0);QCOMPARE(q[1][0],-3.0);
    cells[1].v={0,3,4,-1};cells[1].sub[0]={0,3,4};z.assign(5,0);
    surfaces={CellWaterGeometry::reconstruct(cells[0],1,z),CellWaterGeometry::reconstruct(cells[1],3,z)};
    CellWaterGeometry::smoothCornerDepths(cells,surfaces,z,CellWaterGeometry::smoothTopology(cells),q);
    QCOMPARE(q[0][0],1.0);QCOMPARE(q[1][0],3.0); // only a vertex is shared
}

QTEST_APPLESS_MAIN(TestCellWaterGeometry)
#include "test_cellwatergeometry.moc"
