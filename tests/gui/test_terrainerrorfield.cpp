// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/terrainerrorfield.h"
#include "mesh/terrainlocalcopy.h"
#include "mesh/meshcdt.h"
#include <QTest>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
#include <cmath>
#include <cstdio>
#include <limits>

using namespace mesh;
class TestTerrainErrorField : public QObject {
    Q_OBJECT
private slots:
    void planesCertifyWithoutReadingSamples() {
        constexpr int n=513;
        QVector<float> z(n*n);
        for(int r=0;r<n;++r) for(int c=0;c<n;++c) z[r*n+c]=float(2+.25*(c+.5)-.5*(r+.5));
        TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),n,n));
        QCOMPARE(f.referenceSamples(),quint64(n)*n);
        QVERIFY(f.summaryBytes()<qint64(n)*n);
        QPointF p[3]={{.5,.5},{n-.5,.5},{.5,n-.5}};
        double zz[3]; for(int i=0;i<3;++i) zz[i]=f.sampleAt(p[i].x(),p[i].y());
        auto q=f.queryTriangle(p,zz,.001);
        QVERIFY(q.valid); QCOMPARE(q.samples,quint64(0)); QVERIFY(q.upperBound<.001);
        q=f.queryTriangle(p,zz,.001,true);
        QVERIFY(q.samples>100000); QVERIFY(q.maxError<1e-10);
    }
    void saddleCannotPassAsABilinearPlane() {
        const int n=65; QVector<float> z(n*n);
        for(int r=0;r<n;++r) for(int c=0;c<n;++c) z[r*n+c]=float(.01*c*r);
        TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),n,n));
        QPointF p[3]={{.5,.5},{64.5,.5},{.5,64.5}}; double zz[3]={0,0,0};
        auto q=f.queryTriangle(p,zz,.1);
        QVERIFY(q.valid); QVERIFY(std::abs(q.maxError-10.24)<1e-5);
        QCOMPARE(q.point,QPointF(32.5,32.5));
    }
    void boundedNoiseDoesNotLockFineBlocks() {
        const int n=129; QVector<float> z(n*n);
        for(int i=0;i<z.size();++i) z[i]=(i%3?-.04f:.04f);
        TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),n,n));
        QPointF p[3]={{.5,.5},{128.5,.5},{.5,128.5}}; double zz[3]={0,0,0};
        const auto q=f.queryTriangle(p,zz,.05);
        QVERIFY(q.valid); QVERIFY(q.maxError<=.05); QVERIFY(q.upperBound<=.05);
    }
    void certificateMatchesExhaustiveChecks() {
        const int n=79; QVector<float> z(n*n);
        for(int r=0;r<n;++r) for(int c=0;c<n;++c) z[r*n+c]=float(.002*c*c+.007*r+std::sin(c*.3)*.04);
        z[31*n+42]=std::numeric_limits<float>::quiet_NaN();
        TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),n,n));
        for(int k=0;k<35;++k) {
            QPointF p[3]={{k*.3+.5,1.5},{75.5,k*.2+2.5},{4.5,77.5-k*.1}};
            double zz[3]; for(int i=0;i<3;++i) zz[i]=f.sampleAt(p[i].x(),p[i].y());
            const double tol=.03+k*.08;
            const auto a=f.queryTriangle(p,zz,tol),b=f.queryTriangle(p,zz,tol,true);
            QVERIFY(a.valid && b.valid);
            QVERIFY(a.upperBound+1e-9>=b.maxError);
            QCOMPARE(a.maxError>tol,b.maxError>tol);
        }
    }
    void branchAndBoundMatchesFullScan() {
        // Quantized terraces force many equal residuals, so ties must pick
        // the same pixel; NaN holes and an override region are included.
        const int n=203; QVector<float> z(n*n);
        for(int r=0;r<n;++r) for(int c=0;c<n;++c)
            z[r*n+c]=float(std::round(4*(std::sin(c*.07)*std::cos(r*.05)+.01*c))/4);
        for(int k=0;k<40;++k) z[(17+k*3)*n+(29+k*2)]=std::numeric_limits<float>::quiet_NaN();
        for(bool overridden:{false,true}) {
            TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),n,n));
            if(overridden) f.setQueryOverride(QRectF(60,60,40,30),[](double x,double y,double v){
                return QRectF(60,60,40,30).contains(x,y)?v-1.5:v; });
            quint32 seed=12345;
            auto rnd=[&]{ seed=seed*1664525u+1013904223u; return (seed>>8)/double(1u<<24); };
            int compared=0;
            for(int k=0;k<600;++k) {
                QPointF p[3]; double zz[3];
                const double span=k%3==0?200:(k%3==1?40:8);
                const QPointF o(rnd()*(n-span)+.5,rnd()*(n-span)+.5);
                for(int i=0;i<3;++i) { p[i]=o+QPointF(rnd()*span,rnd()*span); zz[i]=f.sampleAt(p[i].x(),p[i].y()); }
                if(k%5==0) zz[1]=zz[0]=zz[2];
                const double tol=k%4==0?0:.05*(k%7);
                const auto a=f.queryTriangle(p,zz,tol),b=f.queryWorst(p,zz,tol);
                QCOMPARE(b.valid,a.valid);
                if(!a.valid) continue;
                QCOMPARE(b.maxError,a.maxError);
                QCOMPARE(b.point,a.point);
                ++compared;
            }
            QVERIFY(compared>500);
        }
    }
    void refinementCapturesAnInteriorMound() {
        const int n=97; QVector<float> z(n*n);
        for(int r=0;r<n;++r) for(int c=0;c<n;++c)
            z[r*n+c]=float(4*std::exp(-((c-43.)*(c-43.)+(r-51.)*(r-51.))/140));
        TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),n,n));
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build({{.5,.5},{96.5,.5},{96.5,96.5},{.5,96.5}},&ids));
        for(int i=0;i<4;++i) QVERIFY(cdt.insertConstraint(ids[i],ids[(i+1)%4]));
        cdt.removeExterior();
        ConstrainedDelaunay::QualityOptions o;
        o.minAngleDeg=28; o.terrainTolerance=.15; o.minEdge=.05; o.maxTriangles=100000;
        o.terrainElevationAt=[&](double x,double y){ return f.sampleAt(x,y); };
        o.terrainError=[&](const QPointF *p,const double *v,QPointF *out){
            auto q=f.queryTriangle(p,v,o.terrainTolerance); *out=q.point; return q.valid?q.maxError:std::numeric_limits<double>::quiet_NaN();
        };
        const auto rep=cdt.refineQuality(o);
        QVERIFY(!rep.capped); QVERIFY(rep.terrainInsertions>0);
        QCOMPARE(rep.terrainUnknown,0);
        double worst=0;
        for(const auto &t:cdt.triangles()) if(t.alive) {
            QPointF p[3]; double zz[3];
            for(int k=0;k<3;++k) { p[k]=cdt.vertices()[t.v[k]]; zz[k]=f.sampleAt(p[k].x(),p[k].y()); }
            const auto q=f.queryTriangle(p,zz,.15,true); QVERIFY(q.valid); worst=std::max(worst,q.maxError);
        }
        QVERIFY2(worst<=.1500001,qPrintable(QString::number(worst)));
        QCOMPARE(rep.terrainUnresolved,0);
        QVERIFY(cdt.liveTriangleCount()<1500);
    }
    void worstFirstSpendsACappedBudgetOnTheWorstError() {
        // Two mounds of different height on a plane and a tolerance no cap
        // can reach: worst-first must end with a lower maximum error than
        // queue order for the same budget, and meet the tolerance uncapped.
        const int n=161; QVector<float> z(n*n);
        for(int r=0;r<n;++r) for(int c=0;c<n;++c)
            z[r*n+c]=float(6*std::exp(-((c-40.)*(c-40.)+(r-50.)*(r-50.))/120)
                          +2*std::exp(-((c-120.)*(c-120.)+(r-110.)*(r-110.))/300)+.01*c);
        TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),n,n));
        auto run=[&](bool worstFirst,int cap,double tol,int *cells) {
            ConstrainedDelaunay cdt; QVector<int> ids;
            cdt.build({{.5,.5},{n-.5,.5},{n-.5,n-.5},{.5,n-.5}},&ids);
            for(int i=0;i<4;++i) cdt.insertConstraint(ids[i],ids[(i+1)%4]);
            cdt.removeExterior();
            ConstrainedDelaunay::QualityOptions o;
            o.minAngleDeg=28; o.terrainTolerance=tol; o.minEdge=.5; o.maxTriangles=cap;
            o.terrainWorstFirst=worstFirst;
            o.terrainElevationAt=[&](double x,double y){ return f.sampleAt(x,y); };
            o.terrainError=[&](const QPointF *p,const double *v,QPointF *out){
                auto q=f.queryWorst(p,v,o.terrainTolerance); *out=q.point; return q.valid?q.maxError:std::numeric_limits<double>::quiet_NaN();
            };
            cdt.refineQuality(o);
            double worst=0; int live=0;
            for(const auto &t:cdt.triangles()) if(t.alive) {
                ++live; QPointF p[3]; double zz[3];
                for(int k=0;k<3;++k) { p[k]=cdt.vertices()[t.v[k]]; zz[k]=f.sampleAt(p[k].x(),p[k].y()); }
                worst=std::max(worst,f.queryTriangle(p,zz,tol,true).maxError);
            }
            if(cells) *cells=live;
            return worst;
        };
        int inlineCells=0, worstCells=0;
        const double inlineErr=run(false,600,.01,&inlineCells), worstErr=run(true,600,.01,&worstCells);
        qInfo("capped at 600: inline max error %g (%d cells), worst-first %g (%d cells)",inlineErr,inlineCells,worstErr,worstCells);
        QVERIFY2(worstErr<inlineErr,qPrintable(QString("worst-first %1 vs inline %2").arg(worstErr).arg(inlineErr)));
        int uncapped=0;
        QVERIFY(run(true,1000000,.15,&uncapped)<=.1500001);
    }
    void cancellationDuringBuild() {
        QVector<float> z(1024*1024,0); TerrainErrorField f; int calls=0;
        QVERIFY(!f.buildFromGrid(z.data(),1024,1024,[&](double){ return ++calls<3; }));
        QVERIFY(f.errorMsg().contains("Cancelled"));
    }
    void noDataIsReportedEvenInAPlanarBlock() {
        QVector<float> z(64*64,2); z[12*64+15]=std::numeric_limits<float>::quiet_NaN();
        TerrainErrorField f; QVERIFY(f.buildFromGrid(z.data(),64,64));
        QPointF p[3]={{.5,.5},{63.5,.5},{.5,63.5}}; double zz[3]={2,2,2};
        const auto q=f.queryTriangle(p,zz,.1);
        QVERIFY(q.valid); QCOMPARE(q.noDataSamples,quint64(1)); QCOMPARE(q.maxError,0.);
        zz[1]=std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!f.queryTriangle(p,zz,.1).valid);
    }
    void rotatedPixelsAndReprojection() {
        GDALAllRegister();
        const QString folder=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/terrain_adaptive_mesh_2026-10/rasters");
        QVERIFY(QDir().mkpath(folder));
        const QString path=folder+"/rotated.tif";
        constexpr int cols=520,rows=270;
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path.toUtf8().constData(),cols,rows,1,GDT_Float32,nullptr);
        QVERIFY(ds);
        double gt[6]={500000,2,.3,6000000,.2,-3}; ds->SetGeoTransform(gt);
        OGRSpatialReference src,dst; src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER); dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        QVERIFY(src.SetFromUserInput("EPSG:25832")==OGRERR_NONE); QVERIFY(dst.SetFromUserInput("EPSG:25833")==OGRERR_NONE);
        char *wkt=nullptr; src.exportToWkt(&wkt); ds->SetProjection(wkt); CPLFree(wkt);
        QVector<float> z(cols*rows);
        auto point=[&](int c,int r) { return QPointF(gt[0]+(c+.5)*gt[1]+(r+.5)*gt[2],gt[3]+(c+.5)*gt[4]+(r+.5)*gt[5]); };
        for(int r=0;r<rows;++r) for(int c=0;c<cols;++c) { const auto p=point(c,r); z[r*cols+c]=float(2+.04*(p.x()-gt[0])+.09*(p.y()-gt[3])); }
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,cols,rows,z.data(),cols,rows,GDT_Float32,0,0),CE_None);
        GDALClose(ds);
        for (bool reproject:{false,true}) {
            TerrainErrorField f;
            QVERIFY2(f.open(path,reproject?"EPSG:25833":"EPSG:25832",{},.3048,8),qPrintable(f.errorMsg()));
            QPointF p[3]={point(1,1),point(518,1),point(1,268)};
            if(reproject) {
                auto *ct=OGRCreateCoordinateTransformation(&src,&dst); QVERIFY(ct);
                for(auto &v:p) { double x=v.x(),y=v.y(); QVERIFY(ct->Transform(1,&x,&y)); v={x,y}; }
                OGRCoordinateTransformation::DestroyCT(ct);
            }
            double zz[3]; for(int k=0;k<3;++k) zz[k]=f.sampleAt(p[k].x(),p[k].y());
            for(double tol:{.00001,.01,.1}) {
                const auto q=f.queryTriangle(p,zz,tol),exact=f.queryTriangle(p,zz,tol,true);
                QVERIFY(q.valid && exact.valid); QVERIFY(q.upperBound+1e-10>=exact.maxError);
                QCOMPARE(q.maxError>tol,exact.maxError>tol);
                if(!reproject) QVERIFY(exact.maxError<.00001);
            }
        }
    }
    void savedIndexReloadsIdentically() {
        GDALAllRegister();
        const QString folder=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/mesh_largedem_perf_2026-10/index_roundtrip");
        QVERIFY(QDir().mkpath(folder));
        const QString path=folder+"/terrain.tif", index=folder+"/terrain.index";
        QFile::remove(index);
        constexpr int cols=300,rows=211;
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path.toUtf8().constData(),cols,rows,1,GDT_Float32,nullptr);
        QVERIFY(ds);
        double gt[6]={1000,2,0,5000,0,-2}; ds->SetGeoTransform(gt);
        QVector<float> z(cols*rows);
        for(int r=0;r<rows;++r) for(int c=0;c<cols;++c) z[r*cols+c]=float(3*std::sin(c*.05)+.02*r*c/50);
        z[40*cols+70]=std::numeric_limits<float>::quiet_NaN();
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,cols,rows,z.data(),cols,rows,GDT_Float32,0,0),CE_None);
        GDALClose(ds);
        const QRectF domain(1010,4600,560,380);
        TerrainErrorField built,loaded,other;
        QVERIFY(built.open(path,{},domain,1.,64,{},index));
        QVERIFY(built.indexSaved() && !built.indexLoaded());
        QVERIFY(loaded.open(path,{},domain,1.,64,{},index));
        QVERIFY(loaded.indexLoaded());
        QCOMPARE(loaded.referenceSamples(),built.referenceSamples());
        QCOMPARE(loaded.summaryBytes(),built.summaryBytes());
        QCOMPARE(loaded.verticalQuantum(),built.verticalQuantum());
        for(int k=0;k<60;++k) {
            QPointF p[3]={{1020.+k*7,4610.+k*3},{1500.-k*2,4700.+k},{1100.+k,4950.-k*4}};
            double zz[3]; for(int i=0;i<3;++i) zz[i]=built.sampleAt(p[i].x(),p[i].y());
            const auto a=built.queryTriangle(p,zz,.1),b=loaded.queryTriangle(p,zz,.1);
            QCOMPARE(b.valid,a.valid); QCOMPARE(b.maxError,a.maxError); QCOMPARE(b.upperBound,a.upperBound);
            QCOMPARE(b.point,a.point); QCOMPARE(b.samples,a.samples); QCOMPARE(b.noDataSamples,a.noDataSamples);
        }
        // A different window or scale must not reuse it: rebuilt and replaced.
        QVERIFY(other.open(path,{},domain.adjusted(0,0,-100,0),1.,64,{},index));
        QVERIFY(!other.indexLoaded() && other.indexSaved());
        TerrainErrorField scaled;
        QVERIFY(scaled.open(path,{},domain.adjusted(0,0,-100,0),.3048,64,{},index));
        QVERIFY(!scaled.indexLoaded());
        // A truncated file is a miss, not a failure.
        { QFile f(index); QVERIFY(f.open(QIODevice::ReadWrite)); f.resize(f.size()/2); }
        TerrainErrorField truncated;
        QVERIFY(truncated.open(path,{},domain.adjusted(0,0,-100,0),.3048,64,{},index));
        QVERIFY(!truncated.indexLoaded());
    }
    // prefetch() is a warm-up: queries after it give exactly the answers
    // they give reading tile by tile, with a cache too small for the window
    // and with reprojection (the mesh boxes go through the inverse transform).
    void prefetchedQueriesMatchOnDemandReads() {
        GDALAllRegister();
        const QString folder=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/mesh_speed_2026-10/prefetch");
        QVERIFY(QDir().mkpath(folder));
        const QString path=folder+"/terrain.tif";
        constexpr int cols=900,rows=700;
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path.toUtf8().constData(),cols,rows,1,GDT_Float32,nullptr);
        QVERIFY(ds);
        double gt[6]={500000,2,0,6000000,0,-2}; ds->SetGeoTransform(gt);
        OGRSpatialReference src,dst; src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER); dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        QVERIFY(src.SetFromUserInput("EPSG:25832")==OGRERR_NONE); QVERIFY(dst.SetFromUserInput("EPSG:25833")==OGRERR_NONE);
        char *wkt=nullptr; src.exportToWkt(&wkt); ds->SetProjection(wkt); CPLFree(wkt);
        QVector<float> z(cols*rows);
        for(int r=0;r<rows;++r) for(int c=0;c<cols;++c) z[r*cols+c]=float(4*std::sin(c*.03)+2*std::cos(r*.05)+.01*((c*7+r*13)%11));
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,cols,rows,z.data(),cols,rows,GDT_Float32,0,0),CE_None);
        GDALClose(ds);
        for (bool reproject:{false,true}) {
            auto *ct=reproject?OGRCreateCoordinateTransformation(&src,&dst):nullptr;
            auto toMesh=[&](double x,double y) { if(ct) ct->Transform(1,&x,&y); return QPointF(x,y); };
            TerrainErrorField plain,warm;
            const QString crs=reproject?"EPSG:25833":"EPSG:25832";
            QVERIFY2(plain.open(path,crs,{},1.,1),qPrintable(plain.errorMsg()));
            QVERIFY2(warm.open(path,crs,{},1.,1),qPrintable(warm.errorMsg()));
            unsigned seed=7;
            auto next=[&] { seed=seed*1664525u+1013904223u; return double(seed>>8)/double(1u<<24); };
            for(int k=0;k<200;++k) {
                QPointF p[3];
                const double cx=gt[0]+(40+next()*820)*gt[1], cy=gt[3]+(40+next()*620)*gt[5];
                for(auto &v:p) v=toMesh(cx+(next()-.5)*160,cy+(next()-.5)*160);
                double zz[3]; for(int i=0;i<3;++i) zz[i]=plain.sampleAt(p[i].x(),p[i].y());
                double x0=p[0].x(),x1=x0,y0=p[0].y(),y1=y0;
                for(const auto &v:p) { x0=std::min(x0,v.x()); x1=std::max(x1,v.x()); y0=std::min(y0,v.y()); y1=std::max(y1,v.y()); }
                const QRectF box(QPointF(x0,y0),QPointF(x1,y1));
                warm.prefetch({box});
                const auto a=plain.queryTriangle(p,zz,.05),b=warm.queryTriangle(p,zz,.05);
                QCOMPARE(b.valid,a.valid); QCOMPARE(b.maxError,a.maxError); QCOMPARE(b.upperBound,a.upperBound);
                QCOMPARE(b.point,a.point); QCOMPARE(b.samples,a.samples); QCOMPARE(b.noDataSamples,a.noDataSamples);
            }
            if(ct) OGRCoordinateTransformation::DestroyCT(ct);
        }
    }
    // The local float32 window: same grid offset to the window, values
    // rounded to float, an out-of-range Float64 NoData converted as GDAL
    // converts its pixels and still read as NoData, the margin clamped at the raster edge, and a
    // second call reusing the file.
    void localTerrainCopyKeepsTheGridAndNoData() {
        GDALAllRegister();
        const QString folder=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/mesh_speed_2026-10/local_copy");
        QVERIFY(QDir(folder).removeRecursively() || !QDir(folder).exists());
        QVERIFY(QDir().mkpath(folder));
        const QString path=folder+"/source.tif", cacheDir=folder+"/cache";
        constexpr int cols=600,rows=500;
        constexpr double nd=1.79e308;
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path.toUtf8().constData(),cols,rows,1,GDT_Float64,nullptr);
        QVERIFY(ds);
        double gt[6]={1000,2,0,9000,0,-2}; ds->SetGeoTransform(gt);
        ds->GetRasterBand(1)->SetNoDataValue(nd);
        QVector<double> z(cols*rows);
        for(int r=0;r<rows;++r) for(int c=0;c<cols;++c) z[r*cols+c]=(c>300 && c<310 && r>200 && r<210)?nd:100.123456789+.01*c-.003*r;
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,cols,rows,z.data(),cols,rows,GDT_Float64,0,0),CE_None);
        GDALClose(ds);
        // Domain columns 100..499, rows 50..449; the margin is 64 px (clamped).
        const QRectF domain(QPointF(gt[0]+100*gt[1],gt[3]+449*gt[5]),QPointF(gt[0]+500*gt[1],gt[3]+50*gt[5]));
        const auto first=mesh::prepareLocalTerrain(path,{},domain,cacheDir);
        QVERIFY2(!first.path.isEmpty(),qPrintable(first.note));
        QVERIFY(!first.reused);
        auto *copy=static_cast<GDALDataset *>(GDALOpen(first.path.toUtf8().constData(),GA_ReadOnly));
        QVERIFY(copy);
        double cgt[6]; copy->GetGeoTransform(cgt);
        const int c0=int(std::lround((cgt[0]-gt[0])/gt[1])), r0=int(std::lround((cgt[3]-gt[3])/gt[5]));
        QCOMPARE(c0,36); QCOMPARE(r0,0);   // 100-64; 50-64 clamps to 0
        QCOMPARE(copy->GetRasterXSize(),500+64-36); QCOMPARE(copy->GetRasterYSize(),rows);   // both row margins clamp
        QCOMPARE(cgt[1],gt[1]); QCOMPARE(cgt[5],gt[5]);
        auto *b=copy->GetRasterBand(1);
        QCOMPARE(b->GetRasterDataType(),GDT_Float32);
        int hasNd=0; const double cnd=b->GetNoDataValue(&hasNd);
        float converted=0; GDALCopyWords64(&nd,GDT_Float64,0,&converted,GDT_Float32,0,1);
        QVERIFY(hasNd); QCOMPARE(cnd,double(converted));
        QVector<float> v(copy->GetRasterXSize()*copy->GetRasterYSize());
        QCOMPARE(b->RasterIO(GF_Read,0,0,copy->GetRasterXSize(),copy->GetRasterYSize(),v.data(),copy->GetRasterXSize(),copy->GetRasterYSize(),GDT_Float32,0,0),CE_None);
        int mismatched=0,noData=0;
        for(int r=0;r<copy->GetRasterYSize();++r) for(int c=0;c<copy->GetRasterXSize();++c) {
            const double src=z[(r+r0)*cols+c+c0], got=v[r*copy->GetRasterXSize()+c];
            if(src==nd) { noData+=got==cnd; continue; }
            mismatched+=got!=float(src);
        }
        GDALClose(copy);
        QCOMPARE(mismatched,0);
        QCOMPARE(noData,81);
        const auto second=mesh::prepareLocalTerrain(path,{},domain,cacheDir);
        QCOMPARE(second.path,first.path);
        QVERIFY(second.reused);
    }
    // Averaged to the cell size: each copy pixel is the mean of a whole
    // d x d block of source pixels (NoData skipped), on a grid of d x the
    // source pixel aligned to the source.
    void localTerrainCopyAveragesToTheCellSize() {
        GDALAllRegister();
        const QString folder=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/mesh_speed_2026-10/local_copy_average");
        QVERIFY(QDir(folder).removeRecursively() || !QDir(folder).exists());
        QVERIFY(QDir().mkpath(folder));
        const QString path=folder+"/source.tif", cacheDir=folder+"/cache";
        constexpr int cols=400,rows=300;
        constexpr double nd=-9999;
        auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(path.toUtf8().constData(),cols,rows,1,GDT_Float64,nullptr);
        QVERIFY(ds);
        double gt[6]={0,1.5,0,600,0,-1.5}; ds->SetGeoTransform(gt);
        ds->GetRasterBand(1)->SetNoDataValue(nd);
        QVector<double> z(cols*rows);
        for(int r=0;r<rows;++r) for(int c=0;c<cols;++c) z[r*cols+c]=(c==200 && r==150)?nd:double((c*7+r*3)%11);
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,cols,rows,z.data(),cols,rows,GDT_Float64,0,0),CE_None);
        GDALClose(ds);
        const QRectF domain(QPointF(gt[0]+120*gt[1],gt[3]+220*gt[5]),QPointF(gt[0]+300*gt[1],gt[3]+80*gt[5]));
        const auto local=mesh::prepareLocalTerrain(path,{},domain,cacheDir,{},6.0);   // 6 / 1.5 = 4 px blocks
        QVERIFY2(!local.path.isEmpty(),qPrintable(local.note));
        QCOMPARE(local.decimation,4);
        auto *copy=static_cast<GDALDataset *>(GDALOpen(local.path.toUtf8().constData(),GA_ReadOnly));
        QVERIFY(copy);
        double cgt[6]; copy->GetGeoTransform(cgt);
        QCOMPARE(cgt[1],4*gt[1]); QCOMPARE(cgt[5],4*gt[5]);
        const int c0=int(std::lround((cgt[0]-gt[0])/gt[1])), r0=int(std::lround((cgt[3]-gt[3])/gt[5]));
        const int w=copy->GetRasterXSize(), h=copy->GetRasterYSize();
        QVector<float> v(w*h);
        QCOMPARE(copy->GetRasterBand(1)->RasterIO(GF_Read,0,0,w,h,v.data(),w,h,GDT_Float32,0,0),CE_None);
        GDALClose(copy);
        int checked=0,off=0;
        for(int R=0;R<h;++R) for(int C=0;C<w;++C) {
            double sum=0; int n=0;
            for(int r=0;r<4;++r) for(int c=0;c<4;++c) { const double s=z[(r0+R*4+r)*cols+c0+C*4+c]; if(s!=nd) { sum+=s; ++n; } }
            off+=std::abs(v[R*w+C]-sum/n)>1e-5; ++checked;
        }
        QCOMPARE(off,0);
        QVERIFY(checked>1000);
        // A different averaging is a different copy.
        const auto full=mesh::prepareLocalTerrain(path,{},domain,cacheDir);
        QVERIFY(full.path!=local.path);
        QCOMPARE(full.decimation,1);
    }
    void capsAndCancellationAreReported() {
        ConstrainedDelaunay cdt; QVector<int> ids;
        QVERIFY(cdt.build({{0,0},{100,0},{100,100},{0,100}},&ids));
        for(int i=0;i<4;++i) QVERIFY(cdt.insertConstraint(ids[i],ids[(i+1)%4]));
        cdt.removeExterior();
        ConstrainedDelaunay::QualityOptions o; o.hAt=[](double,double){return .2;}; o.maxTriangles=1000;
        const auto capped=cdt.refineQuality(o); QVERIFY(capped.capped);
        QVERIFY(cdt.liveTriangleCount()<=1002);
        o.maxTriangles=1000000; o.cancelled=[] { return true; };
        const auto cancelled=cdt.refineQuality(o); QVERIFY(cancelled.cancelled);
    }
    void scaleBenchmark() {
        const int target=qEnvironmentVariableIntValue("SWMMVIS_TERRAIN_SCALE_CELLS");
        if(target<100000) QSKIP("Opt-in 1/5/10 million-cell benchmark.");
        QElapsedTimer clock; clock.start();
        ConstrainedDelaunay cdt; QVector<int> ids;
        QVERIFY(cdt.build({{0,0},{1000,0},{1000,1000},{0,1000}},&ids));
        for(int i=0;i<4;++i) QVERIFY(cdt.insertConstraint(ids[i],ids[(i+1)%4]));
        cdt.removeExterior();
        ConstrainedDelaunay::QualityOptions o;
        const double h=std::sqrt(3.34e6/target);
        o.hAt=[=](double,double){ return h; };
        const auto rep=cdt.refineQuality(o);
        const qint64 ms=clock.elapsed();
        qint64 rss=0;
#ifdef Q_OS_UNIX
        struct rusage usage{}; getrusage(RUSAGE_SELF,&usage); rss=usage.ru_maxrss;
#ifndef Q_OS_MACOS
        rss*=1024;
#endif
#endif
        qInfo("scale: target=%d cells=%d ms=%lld peakRSS=%lld size=%d quality=%d splits=%d",target,
              cdt.liveTriangleCount(),(long long)ms,(long long)rss,
              rep.sizeInsertions,rep.qualityInsertions,rep.segmentSplits);
        QVERIFY(!rep.capped); QVERIFY(!rep.cancelled);
        QVERIFY(cdt.liveTriangleCount()>target*.85);
    }
    // Opt-in large-DEM timing: SWMMVIS_MESH_LARGEDEM=<GeoTIFF path>, or
    // SWMMVIS_MESH_LARGEDEM_SYNTH=<side in pixels> to write a rolling-terrain
    // DEFLATE GeoTIFF under tests/output/mesh_largedem_perf_2026-10/.
    void largeDemBenchmark() {
        QString path=qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM");
        const int side=qEnvironmentVariableIntValue("SWMMVIS_MESH_LARGEDEM_SYNTH");
        if(path.isEmpty() && side<1024) QSKIP("Opt-in large-DEM benchmark.");
        GDALAllRegister();
        const QString folder=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/mesh_largedem_perf_2026-10");
        QVERIFY(QDir().mkpath(folder));
        if(path.isEmpty()) {
            path=folder+QStringLiteral("/synthetic_%1.tif").arg(side);
            if(!QFileInfo::exists(path)) {
                char **opt=nullptr;
                opt=CSLSetNameValue(opt,"COMPRESS","DEFLATE");
                opt=CSLSetNameValue(opt,"BIGTIFF","IF_SAFER");
                auto *ds=GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
                    path.toUtf8().constData(),side,side,1,GDT_Float32,opt);
                CSLDestroy(opt);
                QVERIFY(ds);
                double gt[6]={500000,1,0,6000000,0,-1}; ds->SetGeoTransform(gt);
                OGRSpatialReference s; s.SetFromUserInput("EPSG:25832");
                char *wkt=nullptr; s.exportToWkt(&wkt); ds->SetProjection(wkt); CPLFree(wkt);
                QVector<float> row(side);
                for(int r=0;r<side;++r) {
                    for(int c=0;c<side;++c)
                        row[c]=float(20+8*std::sin(c*.0021)*std::cos(r*.0017)+3*std::sin((c+2*r)*.011)
                                     +.05*std::sin(c*1.3+r*.7));
                    QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write,0,r,side,1,row.data(),side,1,GDT_Float32,0,0),CE_None);
                }
                GDALClose(ds);
            }
        }
        auto *probe=static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),GA_ReadOnly));
        QVERIFY(probe);
        double gt[6]; QCOMPARE(probe->GetGeoTransform(gt),CE_None);
        const int cols=probe->GetRasterXSize(),rows=probe->GetRasterYSize();
        const QString crs=QString::fromUtf8(probe->GetProjectionRef());
        GDALClose(probe);
        const QRectF domain=QRectF(QPointF(gt[0],gt[3]),QPointF(gt[0]+cols*gt[1],gt[3]+rows*gt[5])).normalized();
        const int cacheMiB=qEnvironmentVariableIntValue("SWMMVIS_MESH_LARGEDEM_CACHE_MIB");
        const double tol=qEnvironmentVariableIsSet("SWMMVIS_MESH_LARGEDEM_TOL")
            ? qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM_TOL").toDouble() : .5;
        QElapsedTimer clock; clock.start();
        TerrainErrorField f;
        const QString meshCrs=qEnvironmentVariableIsSet("SWMMVIS_MESH_LARGEDEM_MESHCRS")
            ? qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM_MESHCRS") : crs;
        QRectF meshDomain=domain;
        if(meshCrs!=crs) {
            OGRSpatialReference s,m; s.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER); m.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            QVERIFY(s.SetFromUserInput(crs.toUtf8().constData())==OGRERR_NONE);
            QVERIFY(m.SetFromUserInput(meshCrs.toUtf8().constData())==OGRERR_NONE);
            auto *ct=OGRCreateCoordinateTransformation(&s,&m); QVERIFY(ct);
            double x0,y0,x1,y1;
            QVERIFY(ct->TransformBounds(domain.left(),domain.top(),domain.right(),domain.bottom(),&x0,&y0,&x1,&y1,21));
            OGRCoordinateTransformation::DestroyCT(ct);
            const double shrink=.02*std::max(x1-x0,y1-y0);
            meshDomain=QRectF(QPointF(x0,y0),QPointF(x1,y1)).adjusted(shrink,shrink,-shrink,-shrink);
        }
        QVERIFY2(f.open(path,meshCrs,meshDomain,1.,cacheMiB>0?cacheMiB:64),qPrintable(f.errorMsg()));
        const qint64 indexMs=clock.restart();
        if(qEnvironmentVariableIsSet("SWMMVIS_MESH_LARGEDEM_INDEX_ONLY")) {
            qInfo("largedem index: %s %lld px in %lld ms; leaf residual P10 %g P50 %g P90 %g P99 %g; vertical quantum %g",
                  qPrintable(QFileInfo(path).fileName()),(long long)f.referenceSamples(),(long long)indexMs,
                  f.leafResidualQuantile(.1),f.leafResidualQuantile(.5),f.leafResidualQuantile(.9),
                  f.leafResidualQuantile(.99),f.verticalQuantum());
            return;
        }
        const double inset=std::abs(gt[1]);
        const QRectF d=meshDomain.adjusted(inset,inset,-inset,-inset);
        ConstrainedDelaunay cdt; QVector<int> ids;
        QVERIFY(cdt.build({d.topLeft(),d.topRight(),d.bottomRight(),d.bottomLeft()},&ids));
        for(int i=0;i<4;++i) QVERIFY(cdt.insertConstraint(ids[i],ids[(i+1)%4]));
        cdt.removeExterior();
        ConstrainedDelaunay::QualityOptions o;
        o.minAngleDeg=30; o.terrainTolerance=tol; o.minEdge=2*inset; o.maxTriangles=20000000;
        const double hMax=std::max(d.width(),d.height())/8;
        o.hAt=[=](double,double){ return hMax; };
        o.terrainElevationAt=[&](double x,double y){ return f.sampleAt(x,y); };
        o.terrainError=[&](const QPointF *p,const double *v,QPointF *out){
            auto q=f.queryWorst(p,v,o.terrainTolerance); *out=q.point;
            return q.valid?q.maxError:std::numeric_limits<double>::quiet_NaN();
        };
        const auto rep=cdt.refineQuality(o);
        const qint64 refineMs=clock.elapsed();
        qint64 rss=0;
#ifdef Q_OS_UNIX
        struct rusage usage{}; getrusage(RUSAGE_SELF,&usage); rss=usage.ru_maxrss;
#ifndef Q_OS_MACOS
        rss*=1024;
#endif
#endif
        const QString line=QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9\n")
            .arg(QFileInfo(path).fileName()).arg(qint64(cols)*rows).arg(cacheMiB>0?cacheMiB:64).arg(tol)
            .arg(indexMs).arg(refineMs).arg(cdt.liveTriangleCount()).arg(rep.terrainInsertions).arg(rss);
        qInfo("largedem: %s",qPrintable(line.trimmed()));
        QFile csv(folder+"/largedem_benchmark.csv");
        const bool fresh=!csv.exists();
        QVERIFY(csv.open(QIODevice::Append|QIODevice::Text));
        if(fresh) csv.write("dem,pixels,cache_mib,tolerance,index_ms,refine_ms,triangles,terrain_insertions,peak_rss\n");
        csv.write(line.toUtf8());
        // Parity dump: refined vertices plus exhaustive queries, all as exact
        // hex doubles, so two builds can be compared with cmp.
        const QString dump=qEnvironmentVariable("SWMMVIS_MESH_LARGEDEM_DUMP");
        if(!dump.isEmpty()) {
            QFile out(folder+"/"+dump); QVERIFY(out.open(QIODevice::WriteOnly|QIODevice::Text));
            auto hex=[](double v){ char b[40]; std::snprintf(b,sizeof b,"%a",v); return QByteArray(b); };
            for(const QPointF &v:cdt.vertices()) out.write(hex(v.x())+" "+hex(v.y())+"\n");
            for(const auto &t:cdt.triangles()) if(t.alive) {
                QPointF p[3]; double zz[3];
                for(int k=0;k<3;++k) { p[k]=cdt.vertices()[t.v[k]]; zz[k]=f.sampleAt(p[k].x(),p[k].y()); }
                const auto a=f.queryTriangle(p,zz,tol),b=f.queryTriangle(p,zz,tol,true);
                out.write(QByteArray::number(t.v[0])+" "+QByteArray::number(t.v[1])+" "+QByteArray::number(t.v[2])
                          +" "+hex(a.maxError)+" "+hex(a.upperBound)+" "+hex(a.point.x())+" "+hex(a.point.y())
                          +" "+QByteArray::number(a.samples)+" "+QByteArray::number(a.noDataSamples)
                          +" "+hex(b.maxError)+" "+hex(b.point.x())+" "+hex(b.point.y())
                          +" "+QByteArray::number(b.samples)+" "+QByteArray::number(b.noDataSamples)+"\n");
            }
        }
        QVERIFY(!rep.cancelled);
    }
};
QTEST_APPLESS_MAIN(TestTerrainErrorField)
#include "test_terrainerrorfield.moc"
