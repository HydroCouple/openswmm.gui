// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/terrainerrorfield.h"

#include <QCache>
#include <QVector>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {
namespace {
constexpr int kTile = 256, kLeaf = 16;
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
struct Node {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double a = 0, b = 0, c = 0, error = 0;
    quint64 count = 0, missing = 0;
    double at(double x, double y) const { return a*x + b*y + c; }
};
struct Level { int cols = 0, rows = 0; QVector<Node> nodes; };
struct Tile { int x = 0, y = 0, cols = 0, rows = 0; QVector<double> xMesh, yMesh, z; };
double roundoff(double magnitude) { return 128 * std::numeric_limits<double>::epsilon() * (1 + magnitude); }
void include(Node &n, double x, double y) {
    if (!n.count && !n.missing) { n.x0 = n.x1 = x; n.y0 = n.y1 = y; }
    else { n.x0 = std::min(n.x0,x); n.x1 = std::max(n.x1,x);
           n.y0 = std::min(n.y0,y); n.y1 = std::max(n.y1,y); }
}
double planeDifference(const Node &n, const Node &p) {
    double best = 0;
    for (double x : {n.x0,n.x1}) for (double y : {n.y0,n.y1})
        best = std::max(best, std::abs((n.a-p.a)*x + (n.b-p.b)*y + n.c-p.c));
    return best;
}
}

struct TerrainErrorField::Impl {
    GDALDataset *ds = nullptr;
    OGRCoordinateTransformation *toMesh = nullptr, *toDEM = nullptr;
    int width = 0, height = 0, c0 = 0, r0 = 0, cols = 0, rows = 0;
    double geo[6] = {}, inv[6] = {}, scale = 1, noData = nan;
    QPointF origin;
    QVector<Level> levels;
    mutable QCache<quint64,Tile> cache;
    mutable QString error;
    bool wholeUnits = true;
    double minZ = std::numeric_limits<double>::infinity(), maxZ = -std::numeric_limits<double>::infinity();
    std::function<bool()> cancelled;
    ~Impl() {
        if (toMesh) OGRCoordinateTransformation::DestroyCT(toMesh);
        if (toDEM) OGRCoordinateTransformation::DestroyCT(toDEM);
        if (ds) GDALClose(ds);
    }
    const Tile *tile(int col, int row) const {
        if (col < 0 || row < 0 || col >= width || row >= height) return nullptr;
        const int tc = col/kTile, tr = row/kTile;
        const quint64 key = (quint64(quint32(tr)) << 32) | quint32(tc);
        if (const Tile *t = cache.object(key)) return t;
        auto t = std::make_unique<Tile>();
        t->x = tc*kTile; t->y = tr*kTile;
        t->cols = std::min(kTile,width-t->x); t->rows = std::min(kTile,height-t->y);
        const int n = t->cols*t->rows;
        t->z.resize(n); t->xMesh.resize(n); t->yMesh.resize(n);
        if (ds->GetRasterBand(1)->RasterIO(GF_Read,t->x,t->y,t->cols,t->rows,
                t->z.data(),t->cols,t->rows,GDT_Float64,0,0) != CE_None) {
            error = QStringLiteral("Cannot read terrain tile at %1, %2").arg(t->x).arg(t->y);
            return nullptr;
        }
        for (int r = 0; r < t->rows; ++r) for (int c = 0; c < t->cols; ++c) {
            const int i = r*t->cols+c;
            const double px = t->x+c+.5, py = t->y+r+.5;
            t->xMesh[i] = geo[0]+px*geo[1]+py*geo[2];
            t->yMesh[i] = geo[3]+px*geo[4]+py*geo[5];
            t->z[i] = !std::isfinite(t->z[i]) || t->z[i] == noData ? nan : t->z[i]*scale;
        }
        if (toMesh && !toMesh->Transform(n,t->xMesh.data(),t->yMesh.data())) {
            error = QStringLiteral("Terrain coordinates cannot be transformed to the mesh CRS.");
            return nullptr;
        }
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(t->xMesh[i]) || !std::isfinite(t->yMesh[i])) {
                error = QStringLiteral("Nonfinite terrain coordinates after reprojection."); return nullptr;
            }
            t->xMesh[i] -= origin.x(); t->yMesh[i] -= origin.y();
        }
        const int cost = n*3*int(sizeof(double));
        Tile *raw = t.release();
        cache.insert(key,raw,cost);
        return cache.object(key);
    }
    bool build(const std::function<bool(double)> &progress) {
        if (progress && !progress(0)) { error = QStringLiteral("Cancelled."); return false; }
        Level leaf; leaf.cols = (cols+kLeaf-1)/kLeaf; leaf.rows = (rows+kLeaf-1)/kLeaf;
        const qint64 count = qint64(leaf.cols)*leaf.rows;
        if (count > std::numeric_limits<int>::max()) { error = QStringLiteral("Terrain summary exceeds the index limit."); return false; }
        leaf.nodes.resize(count);
        // Window and leaves are aligned to 16 pixels; process a complete tile
        // before advancing so each DEM tile is read once during preprocessing.
        for (int ty = r0/kTile*kTile; ty < r0+rows; ty += kTile) {
            for (int tx = c0/kTile*kTile; tx < c0+cols; tx += kTile) {
                const Tile *t = tile(tx,ty);
                if (!t) return false;
                for (int y = std::max(ty,r0); y < std::min(ty+kTile,r0+rows); y += kLeaf)
                    for (int x = std::max(tx,c0); x < std::min(tx+kTile,c0+cols); x += kLeaf) {
                        Node &n = leaf.nodes[((y-r0)/kLeaf)*leaf.cols+(x-c0)/kLeaf];
                        const int xe = std::min(x+kLeaf,c0+cols), ye = std::min(y+kLeaf,r0+rows);
                        double sx = 0, sy = 0, sz = 0;
                        for (int r = y; r < ye; ++r) for (int c = x; c < xe; ++c) {
                            const int i = (r-t->y)*t->cols+c-t->x;
                            include(n,t->xMesh[i],t->yMesh[i]);
                            if (!std::isfinite(t->z[i])) { ++n.missing; continue; }
                            const double raw=t->z[i]/scale;
                            wholeUnits = wholeUnits && std::abs(raw-std::round(raw))<1e-6;
                            minZ=std::min(minZ,t->z[i]); maxZ=std::max(maxZ,t->z[i]);
                            ++n.count;
                            sx += t->xMesh[i]; sy += t->yMesh[i]; sz += t->z[i];
                        }
                        if (!n.count) continue;
                        sx /= n.count; sy /= n.count; sz /= n.count;
                        double xx=0, xy=0, yy=0, xz=0, yz=0;
                        for (int r = y; r < ye; ++r) for (int c = x; c < xe; ++c) {
                            const int i = (r-t->y)*t->cols+c-t->x;
                            if (!std::isfinite(t->z[i])) continue;
                            const double dx=t->xMesh[i]-sx, dy=t->yMesh[i]-sy, dz=t->z[i]-sz;
                            xx+=dx*dx; xy+=dx*dy; yy+=dy*dy; xz+=dx*dz; yz+=dy*dz;
                        }
                        const double det = xx*yy-xy*xy;
                        if (det > 1e-14*xx*yy && det > 0) {
                            n.a=(xz*yy-yz*xy)/det; n.b=(yz*xx-xz*xy)/det;
                        } else if (xx >= yy && xx > 0) n.a=xz/xx;
                        else if (yy > 0) n.b=yz/yy;
                        n.c=sz-n.a*sx-n.b*sy;
                        for (int r = y; r < ye; ++r) for (int c = x; c < xe; ++c) {
                            const int i = (r-t->y)*t->cols+c-t->x;
                            if (std::isfinite(t->z[i]))
                                n.error=std::max(n.error,std::abs(t->z[i]-n.at(t->xMesh[i],t->yMesh[i])));
                        }
                        n.error += roundoff(std::abs(n.c)+std::abs(n.a)*(std::abs(n.x0)+std::abs(n.x1))
                                           +std::abs(n.b)*(std::abs(n.y0)+std::abs(n.y1)));
                    }
                if (progress && !progress(.9*double(std::min(ty+kTile,r0+rows)-r0)/rows)) {
                    error=QStringLiteral("Cancelled."); return false;
                }
            }
        }
        levels.append(std::move(leaf));
        while (levels.last().cols > 1 || levels.last().rows > 1) {
            const Level &child=levels.last(); Level parent;
            parent.cols=(child.cols+1)/2; parent.rows=(child.rows+1)/2;
            parent.nodes.resize(qsizetype(parent.cols)*parent.rows);
            for (int r=0; r<parent.rows; ++r) for (int c=0; c<parent.cols; ++c) {
                Node &n=parent.nodes[r*parent.cols+c];
                auto children=[&](auto fn) {
                    for (int dr=0; dr<2; ++dr) for (int dc=0; dc<2; ++dc) {
                        const int x=2*c+dc,y=2*r+dr;
                        if (x<child.cols && y<child.rows) {
                            const Node &s=child.nodes[y*child.cols+x]; if (s.count || s.missing) fn(s);
                        }
                    }
                };
                children([&](const Node &s) {
                    if (!n.count && !n.missing) { n.x0=s.x0; n.x1=s.x1; n.y0=s.y0; n.y1=s.y1; }
                    else { n.x0=std::min(n.x0,s.x0); n.x1=std::max(n.x1,s.x1);
                           n.y0=std::min(n.y0,s.y0); n.y1=std::max(n.y1,s.y1); }
                    n.count+=s.count; n.missing+=s.missing; n.a+=s.a*s.count; n.b+=s.b*s.count; n.c+=s.c*s.count;
                });
                if (n.count) {
                    n.a/=n.count; n.b/=n.count; n.c/=n.count;
                    children([&](const Node &s){ if(s.count) n.error=std::max(n.error,s.error+planeDifference(s,n)); });
                    n.error += roundoff(std::abs(n.c)+n.error);
                }
            }
            levels.append(std::move(parent));
            if (progress && !progress(.95)) { error=QStringLiteral("Cancelled."); return false; }
        }
        if (progress && !progress(1)) { error=QStringLiteral("Cancelled."); return false; }
        return true;
    }
};

TerrainErrorField::TerrainErrorField() : d(std::make_unique<Impl>()) {}
TerrainErrorField::~TerrainErrorField() = default;

bool TerrainErrorField::open(const QString &path, const QString &meshCRS, const QRectF &domain,
                             double zScale, int cacheMiB, const std::function<bool(double)> &progress)
{
    d=std::make_unique<Impl>();
    if (!(zScale > 0) || !std::isfinite(zScale)) { d->error=QStringLiteral("Invalid terrain vertical scale."); return false; }
    GDALAllRegister();
    d->ds=static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),GA_ReadOnly));
    if (!d->ds) { d->error=QStringLiteral("Cannot open terrain %1").arg(path); return false; }
    d->width=d->ds->GetRasterXSize(); d->height=d->ds->GetRasterYSize();
    if (d->ds->GetRasterCount()<1 || d->ds->GetGeoTransform(d->geo)!=CE_None || !GDALInvGeoTransform(d->geo,d->inv)) {
        d->error=QStringLiteral("Terrain requires an invertible geotransform and a raster band."); return false;
    }
    d->scale=zScale; d->origin=domain.isValid()?domain.center():QPointF();
    int hasNd=0; const double nd=d->ds->GetRasterBand(1)->GetNoDataValue(&hasNd); d->noData=hasNd?nd:nan;
    d->cache.setMaxCost(std::clamp(cacheMiB,8,1024)*1024*1024);
    const QString demCRS=QString::fromUtf8(d->ds->GetProjectionRef());
    if (!meshCRS.isEmpty() && !demCRS.isEmpty()) {
        OGRSpatialReference m,s; m.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER); s.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (m.SetFromUserInput(meshCRS.toUtf8().constData())!=OGRERR_NONE || s.SetFromUserInput(demCRS.toUtf8().constData())!=OGRERR_NONE) {
            d->error=QStringLiteral("Invalid mesh or terrain CRS."); return false;
        }
        if (!m.IsSame(&s)) {
            d->toMesh=OGRCreateCoordinateTransformation(&s,&m); d->toDEM=OGRCreateCoordinateTransformation(&m,&s);
            if (!d->toMesh || !d->toDEM) { d->error=QStringLiteral("Cannot transform terrain coordinates."); return false; }
        }
    }
    d->cols=d->width; d->rows=d->height;
    if (!d->toMesh && domain.isValid()) {
        double loX=d->width,loY=d->height,hiX=0,hiY=0;
        for (double x:{domain.left(),domain.right()}) for(double y:{domain.top(),domain.bottom()}) {
            const double c=d->inv[0]+x*d->inv[1]+y*d->inv[2],r=d->inv[3]+x*d->inv[4]+y*d->inv[5];
            loX=std::min(loX,c); hiX=std::max(hiX,c); loY=std::min(loY,r); hiY=std::max(hiY,r);
        }
        d->c0=int(std::clamp(std::floor(loX/kLeaf)*kLeaf,0.,double(d->width)));
        d->r0=int(std::clamp(std::floor(loY/kLeaf)*kLeaf,0.,double(d->height)));
        d->cols=int(std::clamp(std::ceil(hiX/kLeaf)*kLeaf,0.,double(d->width)))-d->c0;
        d->rows=int(std::clamp(std::ceil(hiY/kLeaf)*kLeaf,0.,double(d->height)))-d->r0;
    }
    if (d->cols<=0 || d->rows<=0) { d->error=QStringLiteral("Terrain does not overlap the meshing domain."); return false; }
    // Nonlinear CRS: summarize transformed sample bounds over the full raster;
    // a four-corner crop could silently exclude part of the curved footprint.
    return d->build(progress);
}

bool TerrainErrorField::buildFromGrid(const float *z,int cols,int rows,const std::function<bool(double)> &progress)
{
    d=std::make_unique<Impl>();
    if (!z || cols<=0 || rows<=0) return false;
    GDALAllRegister();
    d->ds=GetGDALDriverManager()->GetDriverByName("MEM")->Create("",cols,rows,1,GDT_Float32,nullptr);
    if (!d->ds) return false;
    d->geo[1]=d->geo[5]=d->inv[1]=d->inv[5]=1;
    d->ds->SetGeoTransform(d->geo);
    d->width=d->cols=cols; d->height=d->rows=rows;
    d->cache.setMaxCost(8*1024*1024);
    if (d->ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,cols,rows,const_cast<float *>(z),cols,rows,GDT_Float32,0,0)!=CE_None) return false;
    return d->build(progress);
}

double TerrainErrorField::sampleAt(double x,double y) const
{
    if (!d->ds) return nan;
    if (d->toDEM && !d->toDEM->Transform(1,&x,&y)) return nan;
    const double cf=d->inv[0]+x*d->inv[1]+y*d->inv[2]-.5;
    const double rf=d->inv[3]+x*d->inv[4]+y*d->inv[5]-.5;
    if (!(cf>=-1 && rf>=-1 && cf<d->width && rf<d->height)) return nan;
    const double fc=std::floor(cf),fr=std::floor(rf);
    const int c0=std::max(0,int(fc)),r0=std::max(0,int(fr));
    const int c1=std::min(c0+1,d->width-1),r1=std::min(r0+1,d->height-1);
    const double dx=fc<0 || c0==c1?0:cf-fc,dy=fr<0 || r0==r1?0:rf-fr;
    double z[4]; int i=0;
    for (int r:{r0,r1}) for(int c:{c0,c1}) {
        const Tile *t=d->tile(c,r); if (!t) return nan;
        z[i++]=t->z[(r-t->y)*t->cols+c-t->x];
    }
    for (double v:z) if (!std::isfinite(v)) return nan;
    return (z[0]*(1-dx)+z[1]*dx)*(1-dy)+(z[2]*(1-dx)+z[3]*dx)*dy;
}

TerrainErrorField::Query TerrainErrorField::queryTriangle(const QPointF *xy,const double *z,double tolerance,bool exhaustive) const
{
    Query out;
    if (d->levels.isEmpty()) { out.valid=false; return out; }
    for (int i=0;i<3;++i) if (!std::isfinite(z[i])) { out.valid=false; return out; }
    QPointF p[3]={xy[0]-d->origin,xy[1]-d->origin,xy[2]-d->origin};
    const double dx=p[1].x()-p[0].x(),dy=p[1].y()-p[0].y();
    const double ex=p[2].x()-p[0].x(),ey=p[2].y()-p[0].y(),det=dx*ey-dy*ex;
    if (!std::isfinite(det) || det==0) { out.valid=false; return out; }
    Node plane; plane.a=((z[1]-z[0])*ey-(z[2]-z[0])*dy)/det;
    plane.b=((z[2]-z[0])*dx-(z[1]-z[0])*ex)/det; plane.c=z[0]-plane.a*p[0].x()-plane.b*p[0].y();
    const double x0=std::min({p[0].x(),p[1].x(),p[2].x()}),x1=std::max({p[0].x(),p[1].x(),p[2].x()});
    const double y0=std::min({p[0].y(),p[1].y(),p[2].y()}),y1=std::max({p[0].y(),p[1].y(),p[2].y()});
    const double slack=roundoff(std::max({std::abs(z[0]),std::abs(z[1]),std::abs(z[2])})
                              +std::abs(plane.a)*(std::abs(x0)+std::abs(x1))
                              +std::abs(plane.b)*(std::abs(y0)+std::abs(y1)));
    int visited=0;
    auto visit=[&](auto &&self,int level,int c,int r)->void {
        if ((++visited & 255)==0 && d->cancelled && d->cancelled()) { out.valid=false; return; }
        const Level &l=d->levels[level]; const Node &n=l.nodes[r*l.cols+c];
        if ((!n.count && !n.missing) || n.x1<x0 || n.x0>x1 || n.y1<y0 || n.y0>y1 || !out.valid) return;
        const double bound=n.error+planeDifference(n,plane)+slack;
        if (!exhaustive && !n.missing && bound<=tolerance) { out.upperBound=std::max(out.upperBound,bound); return; }
        if (level) {
            const Level &child=d->levels[level-1];
            for(int dr=0;dr<2;++dr) for(int dc=0;dc<2;++dc)
                if(2*c+dc<child.cols && 2*r+dr<child.rows) self(self,level-1,2*c+dc,2*r+dr);
            return;
        }
        const int bx=d->c0+c*kLeaf,by=d->r0+r*kLeaf;
        const Tile *t=d->tile(bx,by);
        if(!t) { out.valid=false; return; }
        for(int rr=by;rr<std::min(by+kLeaf,d->r0+d->rows);++rr)
            for(int cc=bx;cc<std::min(bx+kLeaf,d->c0+d->cols);++cc) {
                const int i=(rr-t->y)*t->cols+cc-t->x;
                const double zz=t->z[i],x=t->xMesh[i],y=t->yMesh[i];
                if(x<x0 || x>x1 || y<y0 || y>y1) continue;
                const double px=x-p[0].x(),py=y-p[0].y();
                const double u=(px*ey-py*ex)/det,v=(dx*py-dy*px)/det;
                if(u < -1e-12 || v < -1e-12 || u+v > 1+1e-12) continue;
                if(!std::isfinite(zz)) { ++out.noDataSamples; continue; }
                ++out.samples;
                const double e=std::abs(zz-(z[0]+u*(z[1]-z[0])+v*(z[2]-z[0])));
                out.upperBound=std::max(out.upperBound,e);
                if(e>out.maxError) { out.maxError=e; out.point=QPointF(x,y)+d->origin; }
            }
    };
    visit(visit,d->levels.size()-1,0,0);
    return out;
}
QString TerrainErrorField::errorMsg() const { return d->error; }
quint64 TerrainErrorField::referenceSamples() const { return d->levels.isEmpty()?0:d->levels.last().nodes[0].count; }
qint64 TerrainErrorField::summaryBytes() const { qint64 n=0; for(const auto &l:d->levels) n+=l.nodes.size()*qint64(sizeof(Node)); return n; }
double TerrainErrorField::verticalQuantum() const { return d->wholeUnits && d->maxZ-d->minZ>=d->scale ? d->scale : 0; }
void TerrainErrorField::setCancellation(std::function<bool()> cancelled) { d->cancelled=std::move(cancelled); }
}
