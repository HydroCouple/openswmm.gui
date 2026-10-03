// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/terrainerrorfield.h"

#include <QCache>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStorageInfo>
#include <QMap>
#include <QMutex>
#include <QSet>
#include <QThread>
#include <QtConcurrent/QtConcurrentMap>
#include <QVector>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace mesh {
namespace {
constexpr int kTile = 256, kLeaf = 16;
constexpr double nan = std::numeric_limits<double>::quiet_NaN();
constexpr double inf = std::numeric_limits<double>::infinity();
struct Node {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    double a = 0, b = 0, c = 0, error = 0;
    quint64 count = 0, missing = 0;
    double at(double x, double y) const { return a*x + b*y + c; }
};
struct Level { int cols = 0, rows = 0; QVector<Node> nodes; };
// Raw band values at their native width (float when exact); mesh coordinates
// are stored only under reprojection, otherwise derived from the geotransform.
struct Tile { int x = 0, y = 0, cols = 0, rows = 0; QVector<float> zf; QVector<double> zd, xMesh, yMesh; };
// Band types whose every value a float holds exactly.
bool exactInFloat(GDALDataType t) {
    return t == GDT_Byte || t == GDT_Int8 || t == GDT_UInt16 || t == GDT_Int16 || t == GDT_Float32;
}
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
    QRectF overrideBounds;
    std::function<double(double,double,double)> queryOverride;
    std::function<bool(const QRectF &)> overrideIntersects;
    GDALDataset *ds = nullptr;
    QString path;   ///< Source file; empty for in-memory grids.
    OGRCoordinateTransformation *toMesh = nullptr, *toDEM = nullptr;
    int width = 0, height = 0, c0 = 0, r0 = 0, cols = 0, rows = 0;
    double geo[6] = {}, inv[6] = {}, scale = 1, noData = nan;
    QPointF origin;
    QVector<Level> levels;
    mutable QCache<quint64,Tile> cache;
    mutable QMutex cacheMutex;   ///< Guards cache only during the parallel build.
    bool floatTiles = false;
    mutable QString error;
    bool wholeUnits = true;
    double minZ = std::numeric_limits<double>::infinity(), maxZ = -std::numeric_limits<double>::infinity();
    std::function<bool()> cancelled;
    bool indexLoaded = false, indexSaved = false;
    struct Triangle;
    bool prepare(const QPointF *xy, const double *z, Triangle &T) const;
    bool nodeBound(const Node &n, const Triangle &T, double &bound, bool &overridden) const;
    ~Impl() {
        if (toMesh) OGRCoordinateTransformation::DestroyCT(toMesh);
        if (toDEM) OGRCoordinateTransformation::DestroyCT(toDEM);
        if (ds) GDALClose(ds);
    }
    // Reads tile (tc, tr) through src/ct, which the caller owns, so build
    // workers can read in parallel with their own handles.
    bool readTile(GDALDataset *src, OGRCoordinateTransformation *ct, int tc, int tr, Tile &t, QString &err) const {
        t.x = tc*kTile; t.y = tr*kTile;
        t.cols = std::min(kTile,width-t.x); t.rows = std::min(kTile,height-t.y);
        const int n = t.cols*t.rows;
        const CPLErr io = floatTiles
            ? (t.zf.resize(n), src->GetRasterBand(1)->RasterIO(GF_Read,t.x,t.y,t.cols,t.rows,
                   t.zf.data(),t.cols,t.rows,GDT_Float32,0,0))
            : (t.zd.resize(n), src->GetRasterBand(1)->RasterIO(GF_Read,t.x,t.y,t.cols,t.rows,
                   t.zd.data(),t.cols,t.rows,GDT_Float64,0,0));
        if (io != CE_None) {
            err = QStringLiteral("Cannot read terrain tile at %1, %2").arg(t.x).arg(t.y);
            return false;
        }
        if (ct) {
            t.xMesh.resize(n); t.yMesh.resize(n);
            for (int r = 0; r < t.rows; ++r) for (int c = 0; c < t.cols; ++c) {
                const int i = r*t.cols+c;
                const double px = t.x+c+.5, py = t.y+r+.5;
                t.xMesh[i] = geo[0]+px*geo[1]+py*geo[2];
                t.yMesh[i] = geo[3]+px*geo[4]+py*geo[5];
            }
            if (!ct->Transform(n,t.xMesh.data(),t.yMesh.data())) {
                err = QStringLiteral("Terrain coordinates cannot be transformed to the mesh CRS.");
                return false;
            }
            for (int i = 0; i < n; ++i) {
                if (!std::isfinite(t.xMesh[i]) || !std::isfinite(t.yMesh[i])) {
                    err = QStringLiteral("Nonfinite terrain coordinates after reprojection."); return false;
                }
                t.xMesh[i] -= origin.x(); t.yMesh[i] -= origin.y();
            }
        }
        return true;
    }
    const Tile *tile(int col, int row) const {
        if (col < 0 || row < 0 || col >= width || row >= height) return nullptr;
        const int tc = col/kTile, tr = row/kTile;
        const quint64 key = (quint64(quint32(tr)) << 32) | quint32(tc);
        if (const Tile *t = cache.object(key)) return t;
        auto t = std::make_unique<Tile>();
        if (!readTile(ds,toMesh,tc,tr,*t,error)) return nullptr;
        return insertTile(std::move(t));
    }
    const Tile *insertTile(std::unique_ptr<Tile> t) const {
        const quint64 key = (quint64(quint32(t->y/kTile)) << 32) | quint32(t->x/kTile);
        const int n = t->cols*t->rows;
        const int cost = n*int(floatTiles ? sizeof(float) : sizeof(double)) + (toMesh ? n*2*int(sizeof(double)) : 0);
        cache.insert(key,t.release(),cost);
        return cache.object(key);
    }
    // Scaled elevation at tile index i (row-major within the tile); NaN for
    // NoData. Float storage widens exactly, so results match double reads.
    double z(const Tile *t, int i) const {
        const double v = floatTiles ? double(t->zf[i]) : t->zd[i];
        return !std::isfinite(v) || v == noData ? nan : v*scale;
    }
    // Mesh-CRS pixel centre relative to origin, for absolute column/row.
    void meshXY(const Tile *t, int col, int row, int i, double &x, double &y) const {
        if (toMesh) { x = t->xMesh[i]; y = t->yMesh[i]; return; }
        const double px = col+.5, py = row+.5;
        x = geo[0]+px*geo[1]+py*geo[2];
        y = geo[3]+px*geo[4]+py*geo[5];
        x -= origin.x(); y -= origin.y();
    }
    // Order-independent reductions over a block of tiles.
    struct Stats {
        bool wholeUnits = true;
        double minZ = std::numeric_limits<double>::infinity(), maxZ = -std::numeric_limits<double>::infinity();
    };
    // Fits every leaf of tile t (at tx, ty) into nodes; leaves of different
    // tiles are disjoint, so tiles may be summarized concurrently.
    void summarizeTile(const Tile *t, int tx, int ty, Node *nodes, int leafCols, Stats &st) const {
        bool &wholeUnits = st.wholeUnits; double &minZ = st.minZ, &maxZ = st.maxZ;
                for (int y = std::max(ty,r0); y < std::min(ty+kTile,r0+rows); y += kLeaf)
                    for (int x = std::max(tx,c0); x < std::min(tx+kTile,c0+cols); x += kLeaf) {
                        Node &n = nodes[((y-r0)/kLeaf)*leafCols+(x-c0)/kLeaf];
                        const int xe = std::min(x+kLeaf,c0+cols), ye = std::min(y+kLeaf,r0+rows);
                        double sx = 0, sy = 0, sz = 0;
                        for (int r = y; r < ye; ++r) for (int c = x; c < xe; ++c) {
                            const int i = (r-t->y)*t->cols+c-t->x;
                            double mx, my; meshXY(t,c,r,i,mx,my);
                            include(n,mx,my);
                            const double zi = z(t,i);
                            if (!std::isfinite(zi)) { ++n.missing; continue; }
                            const double raw=zi/scale;
                            wholeUnits = wholeUnits && std::abs(raw-std::round(raw))<1e-6;
                            minZ=std::min(minZ,zi); maxZ=std::max(maxZ,zi);
                            ++n.count;
                            sx += mx; sy += my; sz += zi;
                        }
                        if (!n.count) continue;
                        sx /= n.count; sy /= n.count; sz /= n.count;
                        double xx=0, xy=0, yy=0, xz=0, yz=0;
                        for (int r = y; r < ye; ++r) for (int c = x; c < xe; ++c) {
                            const int i = (r-t->y)*t->cols+c-t->x;
                            const double zi = z(t,i);
                            if (!std::isfinite(zi)) continue;
                            double mx, my; meshXY(t,c,r,i,mx,my);
                            const double dx=mx-sx, dy=my-sy, dz=zi-sz;
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
                            const double zi = z(t,i);
                            if (!std::isfinite(zi)) continue;
                            double mx, my; meshXY(t,c,r,i,mx,my);
                            n.error=std::max(n.error,std::abs(zi-n.at(mx,my)));
                        }
                        n.error += roundoff(std::abs(n.c)+std::abs(n.a)*(std::abs(n.x0)+std::abs(n.x1))
                                           +std::abs(n.b)*(std::abs(n.y0)+std::abs(n.y1)));
                    }
    }
    // Persisted index: a header fixing everything the summaries depend on,
    // then the raw node arrays. Any mismatch or short read is a miss.
    static constexpr quint32 kIndexMagic = 0x54454649;   // "TEFI"
    static constexpr quint16 kIndexVersion = 1;
    void writeHeader(QDataStream &s) const {
        s << kIndexMagic << kIndexVersion << quint32(sizeof(Node)) << qint32(kTile) << qint32(kLeaf)
          << qint32(width) << qint32(height) << qint32(c0) << qint32(r0) << qint32(cols) << qint32(rows);
        for (double g : geo) s << g;
        s << scale << noData << origin.x() << origin.y() << bool(toMesh);
    }
    bool loadIndex(const QString &file) {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly)) return false;
        QByteArray expected;
        { QDataStream h(&expected,QIODevice::WriteOnly); h.setVersion(QDataStream::Qt_6_0); writeHeader(h); }
        if (f.read(expected.size()) != expected) return false;   // NaN noData compares bitwise here
        QDataStream s(&f); s.setVersion(QDataStream::Qt_6_0);
        bool whole = true; double lo = 0, hi = 0; qint32 count = 0;
        s >> whole >> lo >> hi >> count;
        if (s.status() != QDataStream::Ok || count < 1 || count > 64) return false;
        QVector<Level> loaded(count);
        for (Level &l : loaded) {
            qint32 c = 0, r = 0; s >> c >> r;
            if (s.status() != QDataStream::Ok || c < 1 || r < 1 || qint64(c)*r > std::numeric_limits<int>::max()) return false;
            l.cols = c; l.rows = r; l.nodes.resize(qsizetype(c)*r);
            const qint64 bytes = qint64(l.nodes.size())*qint64(sizeof(Node));
            if (f.read(reinterpret_cast<char *>(l.nodes.data()),bytes) != bytes) return false;
        }
        if (!f.atEnd() || loaded.first().cols != (cols+kLeaf-1)/kLeaf || loaded.first().rows != (rows+kLeaf-1)/kLeaf
            || loaded.last().cols != 1 || loaded.last().rows != 1) return false;
        levels = std::move(loaded); wholeUnits = whole; minZ = lo; maxZ = hi;
        return true;
    }
    bool saveIndex(const QString &file) const {
        qint64 bytes = 4096;
        for (const Level &l : levels) bytes += 8+qint64(l.nodes.size())*qint64(sizeof(Node));
        const QStorageInfo volume(QFileInfo(file).absolutePath());
        if (!volume.isValid() || volume.bytesAvailable() < 2*bytes) return false;
        QSaveFile f(file);
        if (!f.open(QIODevice::WriteOnly)) return false;
        QDataStream s(&f); s.setVersion(QDataStream::Qt_6_0);
        writeHeader(s);
        s << wholeUnits << minZ << maxZ << qint32(levels.size());
        for (const Level &l : levels) {
            s << qint32(l.cols) << qint32(l.rows);
            const qint64 n = qint64(l.nodes.size())*qint64(sizeof(Node));
            if (f.write(reinterpret_cast<const char *>(l.nodes.constData()),n) != n) { f.cancelWriting(); return false; }
        }
        return s.status() == QDataStream::Ok && f.commit();
    }
    // After loading a saved index: read the window's tiles in parallel when
    // they all fit the cache, as the build would have left them resident.
    // Purely a warm-up; any failure leaves tiles to load on demand.
    void prefetch() const {
        const int bytesPerPixel = (floatTiles ? 4 : 8) + (toMesh ? 16 : 0);
        if (path.isEmpty() || qint64(cols)*rows*bytesPerPixel > cache.maxCost()) return;
        QVector<int> tileRows;
        for (int ty = r0/kTile*kTile; ty < r0+rows; ty += kTile) tileRows.append(ty);
        QtConcurrent::blockingMap(tileRows,[&](int ty) {
            GDALDataset *src = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),GA_ReadOnly));
            if (!src) return;
            OGRCoordinateTransformation *ct = toMesh ? toMesh->Clone() : nullptr;
            QString err;
            for (int tx = c0/kTile*kTile; tx < c0+cols && (ct || !toMesh); tx += kTile) {
                auto t = std::make_unique<Tile>();
                if (!readTile(src,ct,tx/kTile,ty/kTile,*t,err)) break;
                QMutexLocker lock(&cacheMutex);
                insertTile(std::move(t));
            }
            if (ct) OGRCoordinateTransformation::DestroyCT(ct);
            GDALClose(src);
        });
    }
    void prefetchBoxes(const QVector<QRectF> &boxes) const {
        if (path.isEmpty() || boxes.isEmpty()) return;
        const int bytesPerTile = kTile*kTile*((floatTiles ? 4 : 8) + (toMesh ? 16 : 0));
        const qint64 budget = cache.maxCost()/2/std::max(1,bytesPerTile);
        QSet<quint64> keys;
        for (const QRectF &b : boxes) {
            double xs[4] = {b.left(),b.right(),b.left(),b.right()}, ys[4] = {b.top(),b.top(),b.bottom(),b.bottom()};
            if (toDEM && !toDEM->Transform(4,xs,ys)) continue;
            double cMin = inf, cMax = -inf, rMin = inf, rMax = -inf;
            for (int k = 0; k < 4; ++k) {
                const double c = inv[0]+xs[k]*inv[1]+ys[k]*inv[2], r = inv[3]+xs[k]*inv[4]+ys[k]*inv[5];
                cMin = std::min(cMin,c); cMax = std::max(cMax,c); rMin = std::min(rMin,r); rMax = std::max(rMax,r);
            }
            if (!(cMax >= c0 && rMax >= r0 && cMin < c0+cols && rMin < r0+rows)) continue;
            const int tc0 = std::max(c0,int(std::floor(cMin))-1)/kTile, tc1 = std::min(c0+cols-1,int(std::ceil(cMax))+1)/kTile;
            const int tr0 = std::max(r0,int(std::floor(rMin))-1)/kTile, tr1 = std::min(r0+rows-1,int(std::ceil(rMax))+1)/kTile;
            for (int tr = tr0; tr <= tr1; ++tr) for (int tc = tc0; tc <= tc1; ++tc)
                keys.insert((quint64(quint32(tr)) << 32) | quint32(tc));
        }
        QMap<int,QVector<int>> byRow;   // row-major, as the file is laid out
        qint64 n = 0;
        for (const quint64 key : std::as_const(keys)) {
            if (cache.contains(key)) continue;
            byRow[int(key >> 32)].append(int(quint32(key)));
            if (++n >= budget) break;
        }
        if (byRow.isEmpty()) return;
        QVector<QPair<int,QVector<int>>> rows;
        for (auto it = byRow.begin(); it != byRow.end(); ++it) { std::sort(it->begin(),it->end()); rows.append({it.key(),*it}); }
        QtConcurrent::blockingMap(rows,[&](const QPair<int,QVector<int>> &row) {
            GDALDataset *src = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),GA_ReadOnly));
            if (!src) return;
            OGRCoordinateTransformation *ct = toMesh ? toMesh->Clone() : nullptr;
            QString err;
            for (int tc : row.second) {
                if (toMesh && !ct) break;
                auto t = std::make_unique<Tile>();
                if (!readTile(src,ct,tc,row.first,*t,err)) break;
                QMutexLocker lock(&cacheMutex);
                insertTile(std::move(t));
            }
            if (ct) OGRCoordinateTransformation::DestroyCT(ct);
            GDALClose(src);
        });
    }
    bool build(const std::function<bool(double)> &progress) {
        if (progress && !progress(0)) { error = QStringLiteral("Cancelled."); return false; }
        Level leaf; leaf.cols = (cols+kLeaf-1)/kLeaf; leaf.rows = (rows+kLeaf-1)/kLeaf;
        const qint64 count = qint64(leaf.cols)*leaf.rows;
        if (count > std::numeric_limits<int>::max()) { error = QStringLiteral("Terrain summary exceeds the index limit."); return false; }
        leaf.nodes.resize(count);
        Node *nodes = leaf.nodes.data();
        // Window and leaves are aligned to 16 pixels; process a complete tile
        // before advancing so each DEM tile is read once during preprocessing.
        QVector<int> tileRows;
        for (int ty = r0/kTile*kTile; ty < r0+rows; ty += kTile) tileRows.append(ty);
        auto reportRow = [&](int ty) {
            if (progress && !progress(.9*double(std::min(ty+kTile,r0+rows)-r0)/rows)) {
                error=QStringLiteral("Cancelled."); return false;
            }
            return true;
        };
        auto merge = [&](const Stats &st) {
            wholeUnits = wholeUnits && st.wholeUnits;
            minZ = std::min(minZ,st.minZ); maxZ = std::max(maxZ,st.maxZ);
        };
        const int workers = std::max(1,QThread::idealThreadCount());
        if (path.isEmpty() || workers == 1 || tileRows.size() < 2) {
            for (int ty : std::as_const(tileRows)) {
                Stats st;
                for (int tx = c0/kTile*kTile; tx < c0+cols; tx += kTile) {
                    const Tile *t = tile(tx,ty);
                    if (!t) return false;
                    summarizeTile(t,tx,ty,nodes,leaf.cols,st);
                }
                merge(st);
                if (!reportRow(ty)) return false;
            }
        } else {
            // Tile rows in parallel, each with its own dataset and transform
            // (neither is thread-safe). Leaf arithmetic is unchanged, so the
            // summary is identical to the serial build.
            struct Row { int ty = 0; Stats st; QString err; };
            for (int first = 0; first < tileRows.size(); first += 2*workers) {
                QVector<Row> chunk;
                for (int k = first; k < std::min<int>(first+2*workers,tileRows.size()); ++k) chunk.append({tileRows[k],{},{}});
                QtConcurrent::blockingMap(chunk,[&](Row &row) {
                    GDALDataset *src = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),GA_ReadOnly));
                    if (!src) { row.err = QStringLiteral("Cannot open terrain %1").arg(path); return; }
                    OGRCoordinateTransformation *ct = toMesh ? toMesh->Clone() : nullptr;
                    if (toMesh && !ct) { row.err = QStringLiteral("Cannot transform terrain coordinates."); GDALClose(src); return; }
                    for (int tx = c0/kTile*kTile; tx < c0+cols && row.err.isEmpty(); tx += kTile) {
                        auto t = std::make_unique<Tile>();
                        if (!readTile(src,ct,tx/kTile,row.ty/kTile,*t,row.err)) break;
                        summarizeTile(t.get(),tx,row.ty,nodes,leaf.cols,row.st);
                        // Warm the query cache as the serial build does.
                        QMutexLocker lock(&cacheMutex);
                        insertTile(std::move(t));
                    }
                    if (ct) OGRCoordinateTransformation::DestroyCT(ct);
                    GDALClose(src);
                });
                for (const Row &row : std::as_const(chunk)) {
                    if (!row.err.isEmpty()) { error = row.err; return false; }
                    merge(row.st);
                }
                if (!reportRow(chunk.last().ty)) return false;
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
                             double zScale, int cacheMiB, const std::function<bool(double)> &progress,
                             const QString &indexFile)
{
    d=std::make_unique<Impl>();
    if (!(zScale > 0) || !std::isfinite(zScale)) { d->error=QStringLiteral("Invalid terrain vertical scale."); return false; }
    GDALAllRegister();
    d->ds=static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),GA_ReadOnly));
    d->path=path;
    if (!d->ds) { d->error=QStringLiteral("Cannot open terrain %1").arg(path); return false; }
    d->width=d->ds->GetRasterXSize(); d->height=d->ds->GetRasterYSize();
    if (d->ds->GetRasterCount()<1 || d->ds->GetGeoTransform(d->geo)!=CE_None || !GDALInvGeoTransform(d->geo,d->inv)) {
        d->error=QStringLiteral("Terrain requires an invertible geotransform and a raster band."); return false;
    }
    d->scale=zScale; d->origin=domain.isValid()?domain.center():QPointF();
    int hasNd=0; const double nd=d->ds->GetRasterBand(1)->GetNoDataValue(&hasNd); d->noData=hasNd?nd:nan;
    d->floatTiles=exactInFloat(d->ds->GetRasterBand(1)->GetRasterDataType());
    d->cache.setMaxCost(qsizetype(std::clamp(cacheMiB,8,65536))*1024*1024);
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
    if (!indexFile.isEmpty() && d->loadIndex(indexFile)) {
        d->indexLoaded = true;
        d->prefetch();
        if (progress && !progress(1)) { d->error = QStringLiteral("Cancelled."); return false; }
        return true;
    }
    if (!d->build(progress)) return false;
    if (!indexFile.isEmpty()) d->indexSaved = d->saveIndex(indexFile);
    return true;
}
bool TerrainErrorField::indexLoaded() const { return d->indexLoaded; }
bool TerrainErrorField::indexSaved() const { return d->indexSaved; }

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
    d->floatTiles=true;
    if (d->ds->GetRasterBand(1)->RasterIO(GF_Write,0,0,cols,rows,const_cast<float *>(z),cols,rows,GDT_Float32,0,0)!=CE_None) return false;
    return d->build(progress);
}

void TerrainErrorField::prefetch(const QVector<QRectF> &meshBoxes) const
{
    if (d->ds) d->prefetchBoxes(meshBoxes);
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
        z[i++]=d->z(t,(r-t->y)*t->cols+c-t->x);
    }
    for (double v:z) if (!std::isfinite(v)) return nan;
    return (z[0]*(1-dx)+z[1]*dx)*(1-dy)+(z[2]*(1-dx)+z[3]*dx)*dy;
}

// Triangle plane, local bounding box and rounding slack shared by both
// queries, so they test pixels with identical arithmetic.
struct TerrainErrorField::Impl::Triangle {
    QPointF p[3]; Node plane;
    double dx=0, dy=0, ex=0, ey=0, det=0, x0=0, x1=0, y0=0, y1=0, slack=0;
    QRectF queryBounds; bool hasOverride=false;
};

bool TerrainErrorField::Impl::prepare(const QPointF *xy,const double *z,Triangle &T) const
{
    if (levels.isEmpty()) return false;
    for (int i=0;i<3;++i) if (!std::isfinite(z[i])) return false;
    QPointF *p=T.p;
    p[0]=xy[0]-origin; p[1]=xy[1]-origin; p[2]=xy[2]-origin;
    T.dx=p[1].x()-p[0].x(); T.dy=p[1].y()-p[0].y();
    T.ex=p[2].x()-p[0].x(); T.ey=p[2].y()-p[0].y(); T.det=T.dx*T.ey-T.dy*T.ex;
    const double dx=T.dx,dy=T.dy,ex=T.ex,ey=T.ey,det=T.det;
    if (!std::isfinite(det) || det==0) return false;
    Node &plane=T.plane; plane.a=((z[1]-z[0])*ey-(z[2]-z[0])*dy)/det;
    plane.b=((z[2]-z[0])*dx-(z[1]-z[0])*ex)/det; plane.c=z[0]-plane.a*p[0].x()-plane.b*p[0].y();
    T.x0=std::min({p[0].x(),p[1].x(),p[2].x()}); T.x1=std::max({p[0].x(),p[1].x(),p[2].x()});
    T.y0=std::min({p[0].y(),p[1].y(),p[2].y()}); T.y1=std::max({p[0].y(),p[1].y(),p[2].y()});
    T.slack=roundoff(std::max({std::abs(z[0]),std::abs(z[1]),std::abs(z[2])})
                    +std::abs(plane.a)*(std::abs(T.x0)+std::abs(T.x1))
                    +std::abs(plane.b)*(std::abs(T.y0)+std::abs(T.y1)));
    T.queryBounds=QRectF(QPointF(T.x0,T.y0)+origin,QPointF(T.x1,T.y1)+origin);
    T.hasOverride=queryOverride && overrideBounds.intersects(T.queryBounds)
        && (!overrideIntersects || overrideIntersects(T.queryBounds));
    return true;
}

// Whether node n is outside T or empty (skip), and otherwise its error bound
// and whether a channel override reaches it.
bool TerrainErrorField::Impl::nodeBound(const Node &n,const Triangle &T,double &bound,bool &overridden) const
{
    if ((!n.count && !n.missing) || n.x1<T.x0 || n.x0>T.x1 || n.y1<T.y0 || n.y0>T.y1) return false;
    bound=n.error+planeDifference(n,T.plane)+T.slack;
    const QRectF nodeBounds=QRectF(QPointF(n.x0,n.y0)+origin,
        QPointF(n.x1,n.y1)+origin).adjusted(-1e-9,-1e-9,1e-9,1e-9);
    const QRectF overlap=nodeBounds.intersected(T.queryBounds);
    overridden = T.hasOverride && overrideBounds.intersects(overlap)
        && (!overrideIntersects || overrideIntersects(overlap));
    return true;
}

TerrainErrorField::Query TerrainErrorField::queryTriangle(const QPointF *xy,const double *z,double tolerance,bool exhaustive) const
{
    Query out;
    Impl::Triangle T;
    if (!d->prepare(xy,z,T)) { out.valid=false; return out; }
    const QPointF *p=T.p;
    const double dx=T.dx,dy=T.dy,ex=T.ex,ey=T.ey,det=T.det,x0=T.x0,x1=T.x1,y0=T.y0,y1=T.y1;
    int visited=0;
    auto visit=[&](auto &&self,int level,int c,int r)->void {
        if ((++visited & 255)==0 && d->cancelled && d->cancelled()) { out.valid=false; return; }
        const Level &l=d->levels[level]; const Node &n=l.nodes[r*l.cols+c];
        double bound=0; bool overridden=false;
        if (!out.valid || !d->nodeBound(n,T,bound,overridden)) return;
        if (!overridden && !exhaustive && !n.missing && bound<=tolerance) { out.upperBound=std::max(out.upperBound,bound); return; }
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
                double x,y; d->meshXY(t,cc,rr,i,x,y);
                double zz=d->z(t,i);
                if(x<x0 || x>x1 || y<y0 || y>y1) continue;
                const double px=x-p[0].x(),py=y-p[0].y();
                const double u=(px*ey-py*ex)/det,v=(dx*py-dy*px)/det;
                if(u < -1e-12 || v < -1e-12 || u+v > 1+1e-12) continue;
                if(d->queryOverride) zz=d->queryOverride(x+d->origin.x(),y+d->origin.y(),zz);
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

TerrainErrorField::Query TerrainErrorField::queryWorst(const QPointF *xy,const double *z,double tolerance) const
{
    // Best-first branch and bound over the same uncertified nodes that
    // queryTriangle() scans. A node is opened only while its bound could
    // still reach the current worst residual, so maxError and point equal
    // queryTriangle()'s: equal residuals keep the first pixel in its
    // depth-first order (leaf Morton key, row-major within the leaf).
    Query out;
    Impl::Triangle T;
    if (!d->prepare(xy,z,T)) { out.valid=false; return out; }
    const QPointF *p=T.p;
    const double dx=T.dx,dy=T.dy,ex=T.ex,ey=T.ey,det=T.det,x0=T.x0,x1=T.x1,y0=T.y0,y1=T.y1;
    struct Open { double bound; int level, c, r; bool operator<(const Open &o) const { return bound<o.bound; } };
    std::priority_queue<Open> open;
    auto push=[&](int level,int c,int r) {
        const Level &l=d->levels[level]; const Node &n=l.nodes[r*l.cols+c];
        double bound=0; bool overridden=false;
        if (!d->nodeBound(n,T,bound,overridden)) return;
        if (!overridden && !n.missing && bound<=tolerance) return;   // certified, as in queryTriangle
        // Overrides replace pixel values, so the terrain bound says nothing.
        open.push({overridden?std::numeric_limits<double>::infinity():bound,level,c,r});
    };
    // Computed residuals may exceed the certified bound by rounding only;
    // keep any node within that margin of the current worst.
    auto margin=[](double e){ return 1e-9*(1+std::abs(e)); };
    quint64 bestKey=std::numeric_limits<quint64>::max();
    int visited=0;
    push(d->levels.size()-1,0,0);
    while (!open.empty()) {
        if ((++visited & 255)==0 && d->cancelled && d->cancelled()) { out.valid=false; return out; }
        const Open o=open.top(); open.pop();
        if (o.bound+margin(out.maxError)<out.maxError) break;
        if (o.level) {
            const Level &child=d->levels[o.level-1];
            for(int dr=0;dr<2;++dr) for(int dc=0;dc<2;++dc)
                if(2*o.c+dc<child.cols && 2*o.r+dr<child.rows) push(o.level-1,2*o.c+dc,2*o.r+dr);
            continue;
        }
        quint64 leafKey=0;
        for (int b=31;b>=0;--b) leafKey=(leafKey<<2)|(quint64((o.r>>b)&1)<<1)|quint64((o.c>>b)&1);
        const int bx=d->c0+o.c*kLeaf,by=d->r0+o.r*kLeaf;
        const Tile *t=d->tile(bx,by);
        if(!t) { out.valid=false; return out; }
        for(int rr=by;rr<std::min(by+kLeaf,d->r0+d->rows);++rr)
            for(int cc=bx;cc<std::min(bx+kLeaf,d->c0+d->cols);++cc) {
                const int i=(rr-t->y)*t->cols+cc-t->x;
                double x,y; d->meshXY(t,cc,rr,i,x,y);
                double zz=d->z(t,i);
                if(x<x0 || x>x1 || y<y0 || y>y1) continue;
                const double px=x-p[0].x(),py=y-p[0].y();
                const double u=(px*ey-py*ex)/det,v=(dx*py-dy*px)/det;
                if(u < -1e-12 || v < -1e-12 || u+v > 1+1e-12) continue;
                if(d->queryOverride) zz=d->queryOverride(x+d->origin.x(),y+d->origin.y(),zz);
                if(!std::isfinite(zz)) continue;
                const double e=std::abs(zz-(z[0]+u*(z[1]-z[0])+v*(z[2]-z[0])));
                const quint64 key=(leafKey<<8)|quint64((rr-by)*kLeaf+(cc-bx));
                // queryTriangle keeps maxError 0 and a default point until a
                // residual exceeds zero; among equal positive residuals the
                // first in its traversal order wins.
                if(e>out.maxError || (e==out.maxError && e>0 && key<bestKey)) {
                    out.maxError=e; out.point=QPointF(x,y)+d->origin; bestKey=key;
                }
            }
    }
    return out;
}
QString TerrainErrorField::errorMsg() const { return d->error; }
quint64 TerrainErrorField::referenceSamples() const { return d->levels.isEmpty()?0:d->levels.last().nodes[0].count; }
qint64 TerrainErrorField::summaryBytes() const { qint64 n=0; for(const auto &l:d->levels) n+=l.nodes.size()*qint64(sizeof(Node)); return n; }
double TerrainErrorField::verticalQuantum() const { return d->wholeUnits && d->maxZ-d->minZ>=d->scale ? d->scale : 0; }
double TerrainErrorField::leafResidualQuantile(double q) const
{
    if(d->levels.isEmpty()) return nan;
    std::vector<double> e;
    e.reserve(d->levels.first().nodes.size());
    for(const Node &n:d->levels.first().nodes) if(n.count>=16 && !n.missing) e.push_back(n.error);
    if(e.empty()) return nan;
    const size_t k=std::min(e.size()-1,size_t(std::clamp(q,0.0,1.0)*double(e.size()-1)));
    std::nth_element(e.begin(),e.begin()+qsizetype(k),e.end());
    return e[k];
}
void TerrainErrorField::setCancellation(std::function<bool()> cancelled) { d->cancelled=std::move(cancelled); }
void TerrainErrorField::setQueryOverride(const QRectF &bounds,std::function<double(double,double,double)> value,
                                        std::function<bool(const QRectF &)> intersects)
{ d->overrideBounds=bounds; d->queryOverride=std::move(value);d->overrideIntersects=std::move(intersects); }
}
