// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/terrainerrorfield.h"
#include "mesh/meshcdt.h"
#include <QTest>
#include <QElapsedTimer>
#include <QDir>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
#include <cmath>
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
};
QTEST_APPLESS_MAIN(TestTerrainErrorField)
#include "test_terrainerrorfield.moc"
