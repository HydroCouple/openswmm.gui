// GPL-3.0-or-later

/**
 * @file      test_flowstreaks.cpp
 * @brief     Tests for flowstreaks.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/swmm2dresultslayer.h"
#include "layers/swmmresultslayer.h"
#include "animation/animationcontroller.h"
#include "ui/widgets/cursorwindowslider.h"
#include <QAction>
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/flowparticlenode.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/mapcanvas.h"
#include "map/openswmmvisscene.h"
#include "map/openswmmvisgraphicsview.h"
#include "map/tools/maptool.h"
#include "render/smoothfillraster.h"
#include "render/flowparticlecontroller.h"
#include "render/flowparticlelayercontroller.h"
#include <QGraphicsRectItem>
#include "project/projectserializer.h"
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "layers/swmmmodellayer.h"
#include "layers/gisvectorlayer.h"
#include "project/openswmmvisworkspace.h"
#include "render/maskclipresolver.h"
#include "simulation/simulationrunner.h"
#include "core/preferencesmanager.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/swmm2dresultsstylepanel.h"
#include <QTest>
#include <QEventLoop>
#include <QTimer>
#include <QSignalSpy>
#include <QSGTextureProvider>
#include <QScopeGuard>
#include <QGraphicsScene>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QTabWidget>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QTextStream>
#include <QPainter>
#include <QMouseEvent>
#include <QGestureEvent>
#include <QPinchGesture>
#include <QSettings>
#include <QtQml/qqml.h>
#include <QQuickWidget>
#include <QScreen>
#include <QQuickWindow>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <cstring>
#include "core/swmmdatetime.h"
#include <cmath>
#include <map>
#include <limits>
#ifdef Q_OS_MACOS
#include <mach/mach.h>
#endif
using namespace OpenSWMM::Render;

class FlowSource : public IMesh2DSource
{
public:
    bool live = false, fluxEnabled = true;
    float speed = 2;
    int revision = 0, reads = 0, generation = 0;
    qint64 timeOrigin = 1700000000;
    /**
     *
     * @brief Return the synthetic hydraulic-history generation.
     * @return Current generation integer, read without mutation/synchronization.
     *
     */
    int historyGeneration() const override { return generation; }
    std::vector<float> depths{.25f, .5f, .25f};
    /**
     *
     * @brief Report the fixed two-triangle fixture vertex count.
     * @return Four XY/Z vertices; no geometry allocation or reading.
     *
     */
    int vertexCount() const override { return 4; }
    /**
     *
     * @brief Report native cell count for the triangular fixture.
     * @return Two cells; the count refers to native cells rather than render fan triangles.
     *
     */
    int triangleCount() const override { return 2; }
    /**
     *
     * @brief Report available synthetic depth frames.
     * @return depths.size() narrowed to int; requires a representable small fixture count.
     *
     */
    int timeCount() const override { return int(depths.size()); }
    /**
     *
     * @brief Return the fixture live-source flag.
     * @return live unchanged; no source/worker synchronization is performed.
     *
     */
    bool isLive() const override { return live; }
    /**
     *
     * @brief Return one shared content revision for all synthetic frame requests.
     * @param time Unused frame index, not validated.
     * @return revision metadata unchanged, even for an out-of-range time.
     *
     */
    int frameContentRevision(int time) const override { return revision; }
    /**
     *
     * @brief Replace output arrays with the fixed 100-metre square triangle mesh.
     * @param x Borrowed output vertex x coordinates in metres, four entries on return.
     * @param y Borrowed output y coordinates in metres, four entries.
     * @param z Borrowed output bed elevations in metres, all zero.
     * @param t Borrowed output zero-based triangles (0,1,2) and (0,2,3).
     * @return Always true on ordinary completion. Outputs are independently assigned;
     * allocation can throw after partial updates. No retained output references.
     *
     */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &t) override
    {
        x = {0, 100, 100, 0};
        y = {0, 0, 100, 100};
        z = {0, 0, 0, 0};
        t = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }
    /**
     *
     * @brief Read one uniform depth for both triangular cells and count the read.
     * @param t Zero-based frame index in [0,depths.size()); vector::at throws otherwise.
     * @param d Borrowed output replaced by two copies of the selected metre depth.
     * @return True on completion; increments reads before indexed access/allocation,
     * including failed attempts. No asynchronous IO or source ownership transfer.
     *
     */
    bool readDepthsAt(int t, std::vector<float> &d) override
    {
        ++reads;
        d.assign(2, depths.at(size_t(t)));
        return true;
    }
    /**
     *
     * @brief Map a synthetic report index to its UTC instant.
     * @param i Frame index, normally valid but not checked; integer products must fit.
     * @return UTC epoch instant timeOrigin+i*60 seconds. Does not read depth values or
     * change the source clock/history.
     *
     */
    QDateTime simTimeAt(int i) const override
    {
        return QDateTime::fromSecsSinceEpoch(timeOrigin + i * 60, Qt::UTC);
    }
    /**
     *
     * @brief Replace fixed triangular edge lengths and outward normal components.
     * @param len Borrowed output eight cell-major slots, stride four; metre lengths,
     * with unused fourth slots zero.
     * @param nx Borrowed output dimensionless x normals in matching slot order.
     * @param ny Borrowed output dimensionless y normals.
     * @return Always true on ordinary completion; sequential allocations can partially
     * update outputs. Native orientation follows the fixture's cell/edge convention.
     *
     */
    bool readEdgeGeometry(std::vector<float> &len, std::vector<float> &nx,
                          std::vector<float> &ny) override
    {
        const float n = std::sqrt(.5f);
        len = {100, 100 / n, 100, 0, 100, 100, 100 / n, 0};
        nx = {1, -n, 0, 0, 0, -1, n, 0};
        ny = {0, n, -1, 0, 1, 0, -n, 0};
        return true;
    }
    /**
     *
     * @brief Build signed x-directed discharge from current fixture depth and speed.
     * @param t Valid zero-based frame index; vector::at throws for invalid time when enabled.
     * @param f Borrowed output cell-major edge discharge in m3/s, using virtual edge
     * geometry lengths/normals so the quad subclass shares this implementation.
     * @return False after clearing f when fluxEnabled is false; otherwise true after
     * setting len*nx*depth*speed. Allocations/index failures propagate with partial output.
     * Positive/negative follows native outward-edge convention; source speed is m/s.
     *
     */
    bool readEdgeFluxAt(int t, std::vector<float> &f) override
    {
        if (!fluxEnabled) {
            f.clear();
            return false;
        }
        std::vector<float> len, nx, ny;
        readEdgeGeometry(len, nx, ny);
        f.resize(len.size());
        for (size_t i = 0; i < f.size(); ++i)
            f[i] = len[i] * nx[i] * depths.at(size_t(t)) * speed;
        return true;
    }
};
class QuadFlowSource : public FlowSource
{
public:
    /**
     *
     * @brief Report the fixture native quadrilateral cell count.
     * @return One native cell despite two render triangles produced by consumers.
     *
     */
    int triangleCount() const override { return 1; }
    /**
     *
     * @brief Replace outputs with one counterclockwise 100-metre square native quad.
     * @param x Borrowed output four metre x coordinates.
     * @param y Borrowed output four metre y coordinates.
     * @param z Borrowed output four zero metre bed elevations.
     * @param cells Borrowed output zero-based native quad (0,1,2,3).
     * @return Always true on ordinary completion; sequential assignments are not an
     * exception transaction and no references are retained.
     *
     */
    bool readCells(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                   std::vector<std::array<int, 4>> &cells) override
    {
        x = {0, 100, 100, 0};
        y = {0, 0, 100, 100};
        z = {0, 0, 0, 0};
        cells = {{0, 1, 2, 3}};
        return true;
    }
    /**
     *
     * @brief Read the selected uniform depth for the single native quad.
     * @param t Valid zero-based frame index; vector::at throws otherwise.
     * @param d Borrowed output replaced by one metre depth value.
     * @return True on completion. Unlike FlowSource's implementation this does not
     * increment reads; no file IO, output-reference retention or local validation.
     *
     */
    bool readDepthsAt(int t, std::vector<float> &d) override
    {
        d.assign(1, depths.at(size_t(t)));
        return true;
    }
    /**
     *
     * @brief Replace quad edge metadata with four axis-aligned 100-metre edges.
     * @param len Borrowed output four lengths in metres.
     * @param nx Borrowed output dimensionless x normals {1,0,-1,0}.
     * @param ny Borrowed output dimensionless y normals {0,1,0,-1}.
     * @return Always true on ordinary completion. Arrays mutate independently and
     * allocation exceptions propagate; no dummy stride slots for a four-edge quad.
     *
     */
    bool readEdgeGeometry(std::vector<float> &len, std::vector<float> &nx,
                          std::vector<float> &ny) override
    {
        len = {100, 100, 100, 100};
        nx = {1, 0, -1, 0};
        ny = {0, 1, 0, -1};
        return true;
    }
};
class VelocityOnlyFlowSource : public FlowSource
{
public:
    /**
     *
     * @brief Construct the two-triangle fixture with edge-flux reads disabled.
     * Retains inherited default speed/depth/time metadata; callers later own it through
     * the layer source unique_ptr. No dataset or thread is started.
     *
     */
    VelocityOnlyFlowSource() { fluxEnabled = false; }
    int subsetReads = 0;
    bool readFaceFieldAt(const char *name, int, std::vector<float> &values) override
    {
        values.assign(2, QString::fromLatin1(name) == "Mesh2_face_vx" ? speed : 0);
        return true;
    }
    /**
     *
     * @brief Advertise exactly the two synthetic Cartesian face-velocity fields.
     * @param name Borrowed null-terminated Latin-1 dataset name.
     * @return True only for exact Mesh2_face_vx or Mesh2_face_vy; no state changes,
     * field read or unit conversion.
     *
     */
    bool hasFaceField(const char *name) const override
    {
        return QString::fromLatin1(name) == "Mesh2_face_vx" ||
               QString::fromLatin1(name) == "Mesh2_face_vy";
    }
    /**
     *
     * @brief Fill the requested output length with the synthetic Cartesian component.
     * @param name Borrowed field name; exact Mesh2_face_vx selects speed, all other
     * names (including unknown ones) select zero.
     * @param time Unused frame index, not validated.
     * @param ids Borrowed cell IDs used only for their count; indices are not checked.
     * @param values Borrowed output replaced with ids.size() values in m/s.
     * @return Always true on completion and increments subsetReads even for unknown names.
     * No source lookup, retained references or atomic rollback on allocation failure.
     *
     */
    bool readFaceFieldCellsAt(const char *name, int time, const std::vector<int> &ids,
                              std::vector<float> &values) override
    {
        ++subsetReads;
        values.assign(ids.size(), QString::fromLatin1(name) == "Mesh2_face_vx" ? speed : 0);
        return true;
    }
};
class SlopingFlowSource : public FlowSource
{
public:
    /**
     *
     * @brief Construct the velocity-unavailable sloping-bed fixture with flux disabled.
     * Inherited hydraulic frames/time metadata remain defaults; no physical solve.
     *
     */
    SlopingFlowSource() { fluxEnabled = false; }
    /**
     *
     * @brief Build the base square geometry, then replace its bed elevations by a slope.
     * @param x Borrowed output four metre x coordinates.
     * @param y Borrowed output four metre y coordinates.
     * @param z Borrowed output metre elevations {0,1,1,0}.
     * @param t Borrowed output base zero-based triangle topology.
     * @return True on ordinary completion; base output assignments and z replacement
     * are not transactional on allocation exceptions. Does not alter source depth values.
     *
     */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &t) override
    {
        FlowSource::readMeshGeometry(x, y, z, t);
        z = {0, 1, 1, 0};
        return true;
    }
};
class GridFlowSource : public FlowSource
{
public:
    /**
     *
     * @brief Construct a live synthetic unit-grid hydraulic source.
     * @param cols Positive square columns, default 400.
     * @param rows Positive square rows, default 400; products/counts must fit int and
     * allocated container capacities. Dimensions are not checked here.
     * Sets live=true and stores dimensions; no geometry is allocated until requested.
     * Callbacks are fixture-owned and require serialized source access while flags vary.
     *
     */
    GridFlowSource(int cols = 400, int rows = 400) : cols(cols), rows(rows) { live = true; }
    int cols, rows, regionalReads = 0;
    double wetFraction = 1;
    bool regionalMissing = false, regionalMismatch = false;
    /**
     *
     * @brief Map a triangular native cell to its unit-grid square.
     * @param cell Valid zero-based native triangle index, normally nonnegative.
     * @return Integer cell/2; no bounds check or state change.
     *
     */
    virtual int squareIndex(int cell) const { return cell / 2; }
    /**
     *
     * @brief Return uniform wet depth or zero according to the column wet mask.
     * @param time Valid zero-based index into depths; indexed access throws when a wet
     * cell needs an unavailable time.
     * @param cell Valid native cell ID mapped through virtual squareIndex.
     * @return Metre depth when wetFraction>=1 or square-column centre is below
     * cols*wetFraction; otherwise zero. Requires positive cols; no bounds/finite checks
     * or source mutation. The dry branch need not access the time vector.
     *
     */
    float cellDepth(int time, int cell) const
    {
        return wetFraction >= 1 || squareIndex(cell) % cols + .5 < cols * wetFraction
                   ? depths.at(size_t(time))
                   : 0;
    }
    /**
     *
     * @brief Advertise regional hydraulic snapshots in the synthetic grid source.
     * @return Always true, independent of regionalMissing/regionalMismatch test flags.
     *
     */
    bool supportsHydraulicCells() const override { return true; }
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    /**
     *
     * @brief Build an immutable selected-cell depth/flux packet for the triangular grid.
     * @param t Valid frame index for requested wet cells.
     * @param ids Borrowed valid ordered native cell selection accepted by validHydraulicCells.
     * @return Shared owned packet with requested IDs, metre depths and stride-four m3/s
     * flux. Invalid IDs return null without increment; valid requests increment regionalReads,
     * then regionalMissing can force null. regionalMismatch deliberately publishes t+1,
     * otherwise t, with current revision/generation. Right-half discharge is doubled.
     * Allocations/index errors propagate; no reference to ids is retained.
     *
     */
    readHydraulicCellsAt(int t, const std::vector<int> &ids) override
    {
        if (!openswmmvis::io::validHydraulicCells(ids, triangleCount()))
            return {};
        ++regionalReads;
        if (regionalMissing)
            return {};
        auto out = std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>();
        out->time = regionalMismatch ? t + 1 : t;
        out->revision = revision;
        out->generation = generation;
        out->cells = ids;
        const float signs[8] = {1, -1, 0, 0, 0, -1, 1, 0};
        for (int c : ids) {
            const float d = cellDepth(t, c);
            out->depths.push_back(d);
            for (int e = 0; e < 4; ++e)
                out->flux.push_back(signs[(4 * c + e) % 8] * d * speed *
                                    ((c / 2) % cols >= cols / 2 ? 2 : 1));
        }
        return out;
    }
    /**
     *
     * @brief Report generated unit-grid vertex count.
     * @return (cols+1)*(rows+1), requiring representable positive fixture dimensions.
     *
     */
    int vertexCount() const override { return (cols + 1) * (rows + 1); }
    /**
     *
     * @brief Report generated native triangular cell count.
     * @return 2*cols*rows, requiring representable positive fixture dimensions.
     *
     */
    int triangleCount() const override { return cols * rows * 2; }
    /**
     *
     * @brief Fill grid coordinates and append counterclockwise triangle pairs.
     * @param x Borrowed output replaced by unit-spaced x coordinates in metres.
     * @param y Borrowed output replaced by metre y coordinates.
     * @param z Borrowed output replaced by zero metre bed elevations.
     * @param triangles Borrowed output appended with 2*cols*rows zero-based triangles;
     * caller must supply an empty vector for an exact complete mesh (reserve does not clear).
     * @return Always true on ordinary completion. Requires valid dimensions/products;
     * allocation can leave partial arrays/topology. No input/source state mutation.
     *
     */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &triangles) override
    {
        x.resize(vertexCount());
        y.resize(vertexCount());
        z.assign(vertexCount(), 0);
        for (int r = 0; r <= rows; ++r)
            for (int c = 0; c <= cols; ++c) {
                const int i = r * (cols + 1) + c;
                x[i] = c;
                y[i] = r;
            }
        triangles.reserve(triangleCount());
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const int a = r * (cols + 1) + c, b = a + 1, d = a + cols + 1, e = d + 1;
                triangles.push_back({a, b, e});
                triangles.push_back({a, e, d});
            }
        return true;
    }
    /**
     *
     * @brief Fill every native cell depth and count one full-frame read.
     * @param t Valid zero-based frame index; initial vector::at access always occurs.
     * @param d Borrowed output replaced by triangleCount() metre depths, then masked
     * with virtual cellDepth when wetFraction<1.
     * @return True on completion; increments reads before access/allocation and propagates
     * failures with partial outputs. No file IO or cell-selection cache.
     *
     */
    bool readDepthsAt(int t, std::vector<float> &d) override
    {
        ++reads;
        d.assign(triangleCount(), depths.at(size_t(t)));
        if (wetFraction < 1)
            for (int c = 0; c < triangleCount(); ++c)
                d[size_t(c)] = cellDepth(t, c);
        return true;
    }
    /**
     *
     * @brief Fill repeated unit-square triangle edge geometry in stride-four order.
     * @param len Borrowed output triangleCount()*4 metre lengths, including sqrt(2)
     * diagonals and zero fourth slots.
     * @param nx Borrowed output matching dimensionless x normals.
     * @param ny Borrowed output matching dimensionless y normals.
     * @return True on ordinary completion. Requires valid count arithmetic; vectors resize
     * independently and can remain partially updated after allocation exceptions.
     *
     */
    bool readEdgeGeometry(std::vector<float> &len, std::vector<float> &nx,
                          std::vector<float> &ny) override
    {
        const float s = std::sqrt(.5f);
        const float l[8] = {1, 1 / s, 1, 0, 1, 1, 1 / s, 0}, a[8] = {1, -s, 0, 0, 0, -1, s, 0},
                    b[8] = {0, s, -1, 0, 1, 0, -s, 0};
        const size_t n = size_t(triangleCount()) * 4;
        len.resize(n);
        nx.resize(n);
        ny.resize(n);
        for (size_t i = 0; i < n; ++i) {
            len[i] = l[i % 8];
            nx[i] = a[i % 8];
            ny[i] = b[i % 8];
        }
        return true;
    }
    /**
     *
     * @brief Fill full-frame unit-grid triangle discharges from depth and x speed.
     * @param t Valid frame index for wet cells.
     * @param flux Borrowed output triangleCount()*4 cell-major m3/s values; fourth slots
     * zero and right-half square discharges doubled.
     * @return True on completion. Uses virtual cellDepth, fixed oriented signs and speed
     * in m/s; does not consult fluxEnabled. Allocation/index exceptions propagate without
     * rolling back partial output or changing read counters.
     *
     */
    bool readEdgeFluxAt(int t, std::vector<float> &flux) override
    {
        const float sign[8] = {1, -1, 0, 0, 0, -1, 1, 0};
        flux.resize(size_t(triangleCount()) * 4);
        for (size_t i = 0; i < flux.size(); ++i) {
            const bool right = (i / 8) % size_t(cols) >= size_t(cols / 2);
            flux[i] = sign[i % 8] * cellDepth(t, int(i / 4)) * speed * (right ? 2 : 1);
        }
        return true;
    }
};
class QuadGridFlowSource : public GridFlowSource
{
public:
    using GridFlowSource::GridFlowSource;
    /**
     *
     * @brief Map a native quad cell directly to its unit square.
     * @param cell Valid zero-based cell ID.
     * @return cell unchanged; no local bounds check.
     *
     */
    int squareIndex(int cell) const override { return cell; }
    /**
     *
     * @brief Report native quad cell count through the legacy-named interface.
     * @return cols*rows native cells, requiring representable dimensions.
     *
     */
    int triangleCount() const override { return cols * rows; }
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    /**
     *
     * @brief Build selected native-quad hydraulics with unit-edge oriented discharge.
     * @param t Valid depth frame index for wet selected cells.
     * @param ids Borrowed selection accepted by validHydraulicCells against native count.
     * @return Shared owned packet with copied IDs, metre depths and four m3/s edge fluxes
     * per cell, current revision/generation and time t. Increments regionalReads after
     * validating IDs. Unlike the triangular base, ignores regionalMissing/regionalMismatch
     * and does not double right-half speed. Invalid IDs return null; exceptions propagate.
     *
     */
    readHydraulicCellsAt(int t, const std::vector<int> &ids) override
    {
        if (!openswmmvis::io::validHydraulicCells(ids, triangleCount()))
            return {};
        ++regionalReads;
        auto out = std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>();
        out->time = t;
        out->revision = revision;
        out->generation = generation;
        out->cells = ids;
        const float signs[4] = {1, 0, -1, 0};
        for (int c : ids) {
            const float d = cellDepth(t, c);
            out->depths.push_back(d);
            for (float sign : signs)
                out->flux.push_back(sign * d * speed);
        }
        return out;
    }
    /**
     *
     * @brief Fill unit-grid coordinates and append native counterclockwise quads.
     * @param x Borrowed output replaced by metre x coordinates.
     * @param y Borrowed output replaced by metre y coordinates.
     * @param z Borrowed output replaced by zero metre elevations.
     * @param cells Borrowed output appended with cols*rows four-index quads; must start
     * empty for exact mesh output. No implicit clear or geometric validation.
     * @return True on completion with sequential allocations/partial mutation on exception;
     * requires valid dimensions/count arithmetic and retains no output references.
     *
     */
    bool readCells(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                   std::vector<std::array<int, 4>> &cells) override
    {
        x.resize(vertexCount());
        y.resize(vertexCount());
        z.assign(vertexCount(), 0);
        for (int r = 0; r <= rows; ++r)
            for (int c = 0; c <= cols; ++c) {
                const int i = r * (cols + 1) + c;
                x[i] = c;
                y[i] = r;
            }
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) {
                const int a = r * (cols + 1) + c;
                cells.push_back({a, a + 1, a + cols + 2, a + cols + 1});
            }
        return true;
    }
    /**
     *
     * @brief Fill four unit-length axis-aligned edges per native quad.
     * @param len Borrowed output 4*cellCount metre lengths, all one.
     * @param nx Borrowed output repeated {1,0,-1,0} dimensionless normals.
     * @param ny Borrowed output repeated {0,1,0,-1} dimensionless normals.
     * @return True on completion. Vectors allocate independently; no dummy slots,
     * read counters or local dimension validation.
     *
     */
    bool readEdgeGeometry(std::vector<float> &len, std::vector<float> &nx,
                          std::vector<float> &ny) override
    {
        len.assign(size_t(triangleCount()) * 4, 1);
        nx.resize(len.size());
        ny.resize(len.size());
        const float a[4] = {1, 0, -1, 0}, b[4] = {0, 1, 0, -1};
        for (size_t i = 0; i < len.size(); ++i) {
            nx[i] = a[i % 4];
            ny[i] = b[i % 4];
        }
        return true;
    }
    /**
     *
     * @brief Fill each native quad's four signed x-flow edge discharges.
     * @param t Valid depth frame index for wet cells.
     * @param flux Borrowed output 4*cellCount m3/s entries, sign*cellDepth*speed.
     * @return True on completion. Does not consult fluxEnabled or double right-half
     * flow; allocation/index exceptions may leave partial output. No references retained.
     *
     */
    bool readEdgeFluxAt(int t, std::vector<float> &flux) override
    {
        const float sign[4] = {1, 0, -1, 0};
        flux.resize(size_t(triangleCount()) * 4);
        for (size_t i = 0; i < flux.size(); ++i)
            flux[i] = sign[i % 4] * cellDepth(t, int(i / 4)) * speed;
        return true;
    }
};
class VelocityOnlyGridSource : public GridFlowSource
{
public:
    using GridFlowSource::GridFlowSource;
    int subsetReads = 0;
    /**
     *
     * @brief Deliberately refuse full-frame edge flux for velocity fallback tests.
     * @param time Unused frame index, not validated.
     * @param values Borrowed output cleared on every call.
     * @return Always false; does not change read counters or source flags.
     *
     */
    bool readEdgeFluxAt(int time, std::vector<float> &values) override
    {
        values.clear();
        return false;
    }
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    /**
     *
     * @brief Copy the base regional packet while removing all discharge values.
     * @param time Frame index passed to triangular grid builder.
     * @param ids Borrowed regional selection passed to base validation/read.
     * @return Shared owned packet with copied depth/identity/time metadata and empty flux,
     * or null for base refusal. Base flags/counters/index exceptions apply. Does not
     * mutate the base packet or retain ids; allocation failures propagate.
     *
     */
    readHydraulicCellsAt(int time, const std::vector<int> &ids) override
    {
        const auto raw = GridFlowSource::readHydraulicCellsAt(time, ids);
        if (!raw)
            return {};
        auto out = std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>(*raw);
        out->flux.clear();
        return out;
    }
    /**
     *
     * @brief Advertise exactly the velocity component names for regional fallback.
     * @param name Borrowed null-terminated Latin-1 dataset name.
     * @return True for exact Mesh2_face_vx or Mesh2_face_vy, false otherwise; no IO.
     *
     */
    bool hasFaceField(const char *name) const override
    {
        return QString::fromLatin1(name) == "Mesh2_face_vx" ||
               QString::fromLatin1(name) == "Mesh2_face_vy";
    }
    /**
     *
     * @brief Return a synthetic selected-cell velocity component without ID lookup.
     * @param name Exact Mesh2_face_vx selects speed, any other name selects zero.
     * @param time Unused frame index, not validated.
     * @param ids Borrowed ID vector used only for its length, with no per-ID validation.
     * @param values Borrowed output replaced by ids.size() m/s values.
     * @return True on completion and increments subsetReads before allocation. Unknown
     * names are accepted as zero by this fixture; no ownership transfer or rollback.
     *
     */
    bool readFaceFieldCellsAt(const char *name, int time, const std::vector<int> &ids,
                              std::vector<float> &values) override
    {
        ++subsetReads;
        values.assign(ids.size(), QString::fromLatin1(name) == "Mesh2_face_vx" ? speed : 0);
        return true;
    }
};
class TestRenderer : public SWMM2DResultsQSGRenderer
{
public:
    using SWMM2DResultsQSGRenderer::updatePaintNode;
};
class ParticleReviewCanvas : public MapCanvas
{
public:
    std::vector<double> paintMs;

protected:
    /**
     *
     * @brief Measure complete MapCanvas widget painting for visible-review evidence.
     * @param event Borrowed Qt paint event passed unchanged to the base handler.
     * GUI-thread callback appends elapsed milliseconds after paint returns. It measures
     * widget CPU duration rather than display presentation; vector allocation can throw.
     *
     */
    void paintEvent(QPaintEvent *event) override
    {
        QElapsedTimer clock;
        clock.start();
        MapCanvas::paintEvent(event);
        paintMs.push_back(clock.nsecsElapsed() / 1e6);
    }
};
class CompositionBackdrop : public OpenSWMMVisLayer
{
public:
    /**
     *
     * @brief Construct a raster or scene-vector composition background fixture.
     * @param raster True selects dark-blue raster fill; false translucent orange scene
     * rectangle. Stores the flag and fixed layer name, with no parent or geometry yet.
     * Caller owns this layer and must detach borrowed canvas references before destruction.
     *
     */
    explicit CompositionBackdrop(bool raster = false)
        : OpenSWMMVisLayer("composition fixture"), m_raster(raster)
    {
    }
    /**
     *
     * @brief Report which composition-backdrop path this fixture requests.
     * @return Stored m_raster flag without other state checks.
     *
     */
    bool isRasterLayer() const override { return m_raster; }
    /**
     *
     * @brief Paint the raster-mode background directly in device logical pixels.
     * @param p Borrowed active painter, used only when m_raster is true.
     * @param extent Unused map bounds, not retained.
     * @param size Target logical-pixel dimensions defining the filled rectangle.
     * @param canvasSrs Unused borrowed optional CRS, not retained.
     * Raster mode fills blue; vector mode does nothing. No painter-state guard, source
     * projection, image allocation or physical unit interpretation.
     *
     */
    void render(QPainter *p, const MapExtent &extent, const QSize &size,
                const SpatialReferenceSystem *canvasSrs) override
    {
        if (m_raster)
            p->fillRect(QRect(QPoint(), size), QColor(20, 60, 100));
    }
    /**
     *
     * @brief Append the vector-mode translucent rectangle to a borrowed scene.
     * @param scene Nonnull borrowed scene owning the created graphics item.
     * @param extent Unused map bounds.
     * @param canvasSrs Unused borrowed optional CRS.
     * Raster mode does nothing. Vector mode appends scene-unit rectangle (20,-80,60,60),
     * records raw this identity in item data and sets layer z. Repeated calls append again;
     * scene items must not outlive the layer if consumers dereference that raw identity.
     *
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &extent,
                       const SpatialReferenceSystem *canvasSrs) override
    {
        if (m_raster)
            return;
        auto *item = scene->addRect(QRectF(20, -80, 60, 60), Qt::NoPen, QColor(200, 100, 0, 128));
        item->setData(0, QVariant::fromValue(reinterpret_cast<quintptr>(this)));
        item->setZValue(layerZValue());
    }

private:
    bool m_raster;
};
class CompositionTool : public OpenSWMMVisMapTool
{
public:
    /**
     *
     * @brief Construct the input/foreground-paint probe tool.
     * @param canvas Borrowed live canvas passed to base tool, which owns/borrows per its
     * normal tool contract. Caller must keep canvas valid through this stack-owned tool's
     * lifetime and detach it before destruction. No interaction is started here.
     *
     */
    explicit CompositionTool(MapCanvas *canvas) : OpenSWMMVisMapTool("composition input", canvas) {}
    int presses = 0;
    /**
     *
     * @brief Count one delivered fixture mouse press without forwarding or accepting it.
     * @param event Borrowed mouse event, unused and not retained.
     * Increments presses only; no model, selection or drag-state edits.
     *
     */
    void mousePressEvent(QMouseEvent *event) override { ++presses; }
    /**
     *
     * @brief Paint the fixed bright-green device-coordinate foreground probe.
     * @param p Borrowed active painter, not retained.
     * @param extent Unused borrowed map bounds.
     * @param canvasSrs Unused borrowed optional CRS.
     * Fills logical-pixel rectangle (10,10,20,20), without projection, painter-state save
     * or input/model mutation. GUI painting thread only.
     *
     */
    void paint(QPainter *p, const MapExtent &extent,
               const SpatialReferenceSystem *canvasSrs) override
    {
        p->fillRect(QRect(10, 10, 20, 20), QColor(10, 220, 30));
    }
};
class ParticleHandoffPreview : public QQuickItem
{
public:
    std::shared_ptr<const FlowParticleFrame> frame;
    FlowParticleAppearance appearance;
    double interpolation = 1, scale = 1;
    /**
     *
     * @brief Construct the Quick item used to observe native frame handoffs.
     * @param parent Borrowed visual/QObject parent owning this allocation, normally a
     * Quick window root. GUI thread only; enables scene contents without allocating nodes.
     *
     */
    explicit ParticleHandoffPreview(QQuickItem *parent) : QQuickItem(parent)
    {
        setFlag(ItemHasContents);
    }

protected:
    /**
     *
     * @brief Synchronize one immutable particle frame into the native preview graph.
     * @param old Nullable prior Qt-owned QSGTransformNode created by this item; reused.
     * @param data Unused borrowed callback metadata, not retained.
     * @return Qt-owned root with FlowParticleNode child, translated (160,120) logical
     * pixels and scaled by scale. Reuses frame/interpolation/appearance and fixed trail
     * geometry parameters. Requires valid window and Qt-synchronized item members on
     * render/synchronization callback; no rollback on allocation or backend failure.
     *
     */
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override
    {
        auto *root = static_cast<QSGTransformNode *>(old);
        if (!root) {
            root = new QSGTransformNode;
            root->appendChildNode(new FlowParticleNode(window()));
        }
        QMatrix4x4 matrix;
        matrix.translate(160, 120);
        matrix.scale(float(scale), float(scale));
        root->setMatrix(matrix);
        static_cast<FlowParticleNode *>(root->firstChild())
            ->sync(frame, interpolation, scale, 1.5, 1.5, 2.5, .6, 0, appearance);
        return root;
    }
};
/**
 *
 * @brief Search a scene-graph subtree depth-first for its first particle node.
 * @param n Borrowed nullable root, whose subtree remains stable during traversal.
 * @return Borrowed first FlowParticleNode or null; graph ownership/lifetime is unchanged.
 * Use on the graph's render/synchronization thread; recursive depth is unchecked.
 *
 */
static FlowParticleNode *findParticles(QSGNode *n)
{
    if (auto *f = dynamic_cast<FlowParticleNode *>(n))
        return f;
    for (auto *c = n ? n->firstChild() : nullptr; c; c = c->nextSibling())
        if (auto *f = findParticles(c))
            return f;
    return nullptr;
}
/**
 *
 * @brief Compare a visible capture with a temporary particle-hidden capture.
 * @param canvas Borrowed live GUI canvas, captured synchronously.
 * @param layer Borrowed layer whose flow sublayer opacity is temporarily set to zero
 * then restored, triggering normal style/render invalidation.
 * @param visible Borrowed original image in matching capture size/frame.
 * @param error Optional borrowed output diagnostic passed to captureMap.
 * @return Count of RGB-different pixels with maximum channel delta>2, or -1 for
 * null/size-mismatched hidden capture. No alpha comparison. Conversions allocate;
 * opacity restoration is ordinary-path, with no scope guard for capture exceptions.
 * This is an observation helper, not a pure/read-only renderer query.
 *
 */
static qint64 visibleParticlePixels(MapCanvas &canvas, SWMM2DResultsLayer &layer,
                                    const QImage &visible, QString *error)
{
    auto *sub = layer.flowStreakSublayer();
    const double opacity = sub->opacity();
    sub->setOpacity(0);
    const auto hidden = canvas.captureMap(error).toImage();
    sub->setOpacity(opacity);
    if (hidden.isNull() || hidden.size() != visible.size())
        return -1;
    const auto a = visible.convertToFormat(QImage::Format_ARGB32),
               b = hidden.convertToFormat(QImage::Format_ARGB32);
    qint64 pixels = 0;
    for (int y = 0; y < a.height(); ++y) {
        const auto *p = reinterpret_cast<const QRgb *>(a.constScanLine(y)),
                   *q = reinterpret_cast<const QRgb *>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            pixels +=
                std::max({std::abs(qRed(p[x]) - qRed(q[x])), std::abs(qGreen(p[x]) - qGreen(q[x])),
                          std::abs(qBlue(p[x]) - qBlue(q[x]))}) > 2;
    }
    return pixels;
}
/**
 *
 * @brief Place a review widget on the primary display and show it.
 * @param widget Borrowed GUI-thread widget. Moves near available primary-screen
 * origin when available, otherwise preserves its position; calls show either way.
 * Does not wait for exposure, change ownership or guarantee a particular DPR.
 *
 */
static void showNativeReview(QWidget &widget)
{
    // Window placement otherwise follows the pointer onto either external
    // DPR1 monitor. Keep native measurements on the primary Retina display.
    if (auto *screen = QGuiApplication::primaryScreen())
        widget.move(screen->availableGeometry().topLeft() + QPoint(16, 16));
    widget.show();
}
/**
 *
 * @brief Find a borrowed native composition window for the fixture canvas.
 * @param canvas Borrowed live canvas; first prefers its QQuickWidget descendant.
 * @return Embedded quickWindow, or the first application window named
 * flowMapNativeWindow, or null. The fallback search is process-global and does not
 * prove the window belongs to canvas; no ownership transfer or event processing.
 *
 */
static QQuickWindow *compositionWindow(MapCanvas &canvas)
{
    if (auto *widget = canvas.findChild<QQuickWidget *>())
        return widget->quickWindow();
    for (auto *window : QGuiApplication::allWindows())
        if (window->objectName() == "flowMapNativeWindow")
            return qobject_cast<QQuickWindow *>(window);
    return nullptr;
}
/**
 *
 * @brief Resolve the borrowed composition root using embedded/native fallback paths.
 * @param canvas Borrowed live GUI canvas.
 * @return Embedded Quick rootObject when a QQuickWidget exists, otherwise the
 * compositionWindow contentItem, or null. A present embedded widget with null root
 * does not trigger fallback; lifetime follows the Qt scene/window, not the caller.
 *
 */
static QQuickItem *compositionRoot(MapCanvas &canvas)
{
    if (auto *widget = canvas.findChild<QQuickWidget *>())
        return widget->rootObject();
    auto *window = compositionWindow(canvas);
    return window ? window->contentItem() : nullptr;
}
class FlowStreaksTest : public QObject
{
    Q_OBJECT
private slots:
    void playbackActionsStopAndRewind()
    {
        SWMMVis app;
        auto *animation = app.findChild<AnimationController *>();
        auto *play = app.findChild<QAction *>("actionPlay");
        auto *pause = app.findChild<QAction *>("actionPause");
        auto *stop = app.findChild<QAction *>("actionStop");
        auto *slider = app.findChild<openswmmvis::ui::CursorWindowSlider *>();
        QVERIFY(animation && play && pause && stop && slider);
        QVERIFY(!stop->isCheckable());
        play->trigger(); // No data must not leave a falsely selected Play.
        QVERIFY(!play->isChecked());
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        layer.setAsyncSurface(true);
        animation->setFallback2DLayer(&layer);
        animation->seekToPeriod(1);
        QTRY_COMPARE(layer.currentTimeIndex(), 1);
        play->trigger();
        QVERIFY(play->isChecked());
        QVERIFY(!pause->isChecked());
        pause->trigger();
        QVERIFY(!play->isChecked());
        QVERIFY(pause->isChecked());
        pause->trigger();
        QVERIFY(pause->isChecked());
        slider->setCursorNorm(.75); // Queue a scrub immediately before Stop.
        stop->trigger();
        QVERIFY(!play->isChecked());
        QVERIFY(!pause->isChecked());
        QCOMPARE(slider->cursorNorm(), 0.);
        QTRY_COMPARE(layer.currentTimeIndex(), 0);
        QTest::qWait(80);
        QCOMPARE(layer.currentTimeIndex(), 0);
        QCOMPARE(slider->cursorNorm(), 0.);
        play->trigger();
        QVERIFY(play->isChecked());
        stop->trigger();
        QVERIFY(!play->isChecked() && !pause->isChecked() && !stop->isChecked());
    }

    void liveSliderHoldsAcrossIncomingFrames_data()
    {
        QTest::addColumn<bool>("async");
        QTest::newRow("synchronous") << false;
        QTest::newRow("asynchronous") << true;
    }

    void liveSliderHoldsAcrossIncomingFrames()
    {
        QFETCH(bool, async);
        SWMMVis app;
        auto *animation = app.findChild<AnimationController *>();
        auto *slider = app.findChild<openswmmvis::ui::CursorWindowSlider *>();
        auto *live = app.findChild<QCheckBox *>("liveRendering");
        auto *pause = app.findChild<QAction *>("actionPause");
        auto *stop = app.findChild<QAction *>("actionStop");
        QVERIFY(animation && slider && live && pause && stop);
        QVERIFY(!app.findChild<QCheckBox *>("flowStreakMotion"));
        for (auto *check : app.findChildren<QCheckBox *>())
            QVERIFY(check->text() != "Live 1D" && check->text() != "Live 2D");
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        auto *stream = source.get();
        source->live = true;
        source->depths.assign(5, .5f);
        layer.setAsyncSurface(async);
        layer.setSource(std::move(source));
        animation->setFallback2DLayer(&layer);
        animation->setLiveRenderingEnabled(true);
        QTRY_COMPARE(layer.currentTimeIndex(), 4);
        QCOMPARE(slider->cursorNorm(), 1.);
        QVERIFY(live->isChecked());
        slider->resize(400, 24);
        QTest::mousePress(slider, Qt::LeftButton, Qt::NoModifier, QPoint(200, 12));
        const auto dragged = slider->cursorNorm();
        QVERIFY(!live->isChecked());
        QVERIFY(pause->isChecked());
        stream->depths.push_back(.5f);
        layer.refreshTimeRange();
        QTest::qWait(80);
        QCOMPARE(slider->cursorNorm(), dragged);
        QTest::mouseRelease(slider, Qt::LeftButton, Qt::NoModifier, QPoint(200, 12));
        QTRY_COMPARE(layer.currentTimeIndex(), 2);
        stream->depths.push_back(.5f);
        layer.refreshTimeRange();
        QTest::qWait(80);
        QCOMPARE(layer.currentTimeIndex(), 2);
        QVERIFY(layer.liveRenderEnabled());
        live->setChecked(true);
        QTRY_COMPARE(layer.currentTimeIndex(), 6);
        QVERIFY(!pause->isChecked());
        stop->trigger();
        QVERIFY(!live->isChecked() && !pause->isChecked() && !stop->isChecked());
        QCOMPARE(slider->cursorNorm(), 0.);
        stream->depths.push_back(.5f);
        layer.refreshTimeRange();
        QTest::qWait(80);
        QCOMPARE(layer.currentTimeIndex(), 0);
        QCOMPARE(slider->cursorNorm(), 0.);
    }

    void mixedLiveResultsShareOneCursor_data()
    {
        QTest::addColumn<bool>("subsecond");
        QTest::newRow("whole-seconds") << false;
        QTest::newRow("100-ms") << true;
    }

    void mixedLiveResultsShareOneCursor()
    {
        QFETCH(bool, subsecond);
        const auto path = qEnvironmentVariable("FLOW_STREAK_LIVE_OUT");
        if (path.isEmpty())
            QSKIP("Set FLOW_STREAK_LIVE_OUT to a completed SWMM output for append replay");
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        auto bytes = original.readAll();
        QVERIFY(bytes.size() > 24);
        qint32 footer[6];
        std::memcpy(footer, bytes.constData() + bytes.size() - 24, 24);
        const qint64 start = footer[2], count = footer[3];
        QVERIFY(start > 0 && count > 10 && start < bytes.size() - 24);
        const qint64 stride = (bytes.size() - 24 - start) / count;
        QVERIFY(stride >= 8);
        if (subsecond) {
            const qint32 reportSeconds = 0;
            std::memcpy(bytes.data() + start - 4, &reportSeconds, 4);
            double first;
            std::memcpy(&first, bytes.constData() + start, 8);
            for (int i = 0; i < count; ++i) {
                const double time = first + i * .1 / 86400.;
                std::memcpy(bytes.data() + start + i * stride, &time, 8);
            }
        }
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile writing(directory.filePath("live.out"));
        QVERIFY(writing.open(QIODevice::WriteOnly));
        QCOMPARE(writing.write(bytes.constData(), start), start);
        QVERIFY(writing.flush());
        SWMMResultsLayer oneD(writing.fileName(), nullptr);
        QList<QString> errors;
        QVERIFY2(oneD.openResultsLive(errors), qPrintable(errors.join("\n")));
        QCOMPARE(oneD.totalTimeSteps(), 0);
        struct TimedFlowSource : FlowSource {
            QVector<QDateTime> times;
            QDateTime simTimeAt(int i) const override { return times.value(i); }
        };
        auto source = std::make_unique<TimedFlowSource>();
        auto *stream = source.get();
        source->live = true;
        source->depths.assign(3, .5f);
        for (int i = 0; i < 12; ++i) {
            double julian;
            std::memcpy(&julian, bytes.constData() + start + i * stride, 8);
            source->times.append(openswmmvis::core::swmmDateTimeToMilliseconds(julian));
        }
        SWMM2DResultsLayer twoD;
        twoD.setSource(std::move(source));
        AnimationController animation;
        animation.setPrimaryLayer(&oneD);
        animation.setFallback2DLayer(&twoD);
        QTRY_COMPARE(twoD.currentTimeIndex(), 2); // Empty 1D must not block 2D.
        qint64 written = 0;
        auto append = [&](int frames) {
            const qint64 size = (frames - written) * stride;
            QCOMPARE(writing.write(bytes.constData() + start + written * stride, size), size);
            QVERIFY(writing.flush());
            written = frames;
            QCOMPARE(oneD.refreshLive(), frames);
        };
        append(5); // 1D arrives before the corresponding 2D packet.
        QCOMPARE(oneD.currentTimeStep(), 4);
        stream->depths.resize(5, .5f);
        twoD.refreshTimeRange();
        QTRY_COMPARE(twoD.currentTimeIndex(), 4);
        QCOMPARE(animation.currentDateTime(), stream->simTimeAt(4));
        animation.seekToTime(stream->simTimeAt(1));
        QCOMPARE(oneD.currentTimeStep(), 1);
        QTRY_COMPARE(twoD.currentTimeIndex(), 1);
        QVERIFY(!animation.liveRenderingEnabled());
        append(7);
        stream->depths.resize(7, .5f);
        twoD.refreshTimeRange();
        QTest::qWait(80);
        QCOMPARE(oneD.currentTimeStep(), 1);
        QCOMPARE(twoD.currentTimeIndex(), 1);
        animation.setLiveRenderingEnabled(true);
        QCOMPARE(oneD.currentTimeStep(), 6);
        QTRY_COMPARE(twoD.currentTimeIndex(), 6);
        animation.pause();
        append(9);
        stream->depths.resize(9, .5f);
        twoD.refreshTimeRange();
        QTest::qWait(80);
        QCOMPARE(oneD.currentTimeStep(), 6);
        QCOMPARE(twoD.currentTimeIndex(), 6);
        animation.stop();
        QCOMPARE(oneD.currentTimeStep(), 0);
        QTRY_COMPARE(twoD.currentTimeIndex(), 0);
        twoD.setCurrentTimeIndex(5);
        animation.stop(); // Repeated Stop must also rewind an independently moved 2D layer.
        QTRY_COMPARE(twoD.currentTimeIndex(), 0);
        append(10);
        stream->depths.resize(10, .5f);
        twoD.refreshTimeRange();
        QTest::qWait(80);
        QCOMPARE(oneD.currentTimeStep(), 0);
        QCOMPARE(twoD.currentTimeIndex(), 0);
        const auto remaining = bytes.size() - start - written * stride;
        QCOMPARE(writing.write(bytes.constData() + start + written * stride, remaining), remaining);
        QVERIFY(writing.flush());
        QCOMPARE(oneD.refreshLive(), int(count));
        QVERIFY(!oneD.isLive());
        QCOMPARE(oneD.currentTimeStep(), 0);
        SWMM2DResultsLayer other;
        auto otherSource = std::make_unique<FlowSource>();
        otherSource->live = true;
        other.setSource(std::move(otherSource));
        animation.setPrimaryLayer(nullptr);
        animation.setFallback2DLayer(&other);
        animation.setLiveRenderingEnabled(true);
        QTRY_COMPARE(other.currentTimeIndex(), 2);
        stream->depths.resize(11, .5f);
        twoD.refreshTimeRange();
        QTest::qWait(80);
        QCOMPARE(twoD.currentTimeIndex(), 0); // Only the selected results follow.
    }

    void asyncPlaybackAdvancesPendingCursor()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        source->depths.assign(20, .5f);
        layer.setSource(std::move(source));
        layer.setAsyncSurface(true);
        AnimationController animation;
        animation.setFallback2DLayer(&layer);
        animation.stop();
        QTRY_COMPARE(layer.currentTimeIndex(), 0);
        animation.stepForward();
        animation.stepForward();
        animation.stepForward();
        QCOMPARE(layer.requestedTimeIndex(), 3);
        animation.stop();
        QTest::qWait(100);
        QCOMPARE(layer.currentTimeIndex(), 0);
    }

    void savedVelocityVectorsWithoutEdgeFlux()
    {
        for (bool async : {false, true}) {
            SWMM2DResultsLayer layer;
            layer.setAsyncSurface(async);
            layer.setSource(std::make_unique<VelocityOnlyFlowSource>());
            for (int time = 0; time < 3; ++time) {
                layer.setCurrentTimeIndex(time);
                QTRY_COMPARE(layer.currentTimeIndex(), time);
                QVERIFY(layer.hasVelocityData());
                float vx = 0, vy = 0;
                QVERIFY(layer.velocityAtScene({50, -50}, vx, vy, layer.captureVelocityField()));
                QVERIFY(std::abs(vx - 2) < 1e-5);
                QVERIFY(std::abs(vy) < 1e-5);
            }
        }
    }

    /** @brief Review saved flow overlays in the main project window, including a close zoom.
     * Requires FLOW_STREAK_PROJECT with the T6A project and FLOW_PARTICLE_NATIVE_REVIEW.
     * Opens existing results without running/saving the model; captures the visible window.
     */
    void nativeProjectFlowOverlays()
    {
        const auto path = qEnvironmentVariable("FLOW_STREAK_PROJECT");
        if (path.isEmpty() || !qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Optional project-window flow overlay review");
        // QTEST_MAIN bypasses SWMMVisApplication, which normally selects Fusion.
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        SWMMVis app;
        app.resize(1600, 1000);
        QVERIFY(QMetaObject::invokeMethod(&app, "onOpenProject", Qt::DirectConnection,
                                          Q_ARG(QString, path)));
        SWMMVisProjectWindow *project = nullptr;
        auto loaded = [&] {
            for (auto *window : app.findChildren<SWMMVisProjectWindow *>())
                if (window->active2DResultsLayer()) {
                    project = window;
                    return true;
                }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(loaded(), 30000);
        const auto cleanup = qScopeGuard([&] { project->setHasChanges(false); });
        auto *layer = project->active2DResultsLayer();
        auto *animation = app.findChild<AnimationController *>();
        QVERIFY(animation);
        animation->pause();
        auto *primary = animation->primaryLayer();
        QVERIFY(primary);
        const auto time = layer->source()->simTimeAt(600);
        QCOMPARE(layer->source()->simTimeAt(599).msecsTo(time), qint64(100));
        QCOMPARE(primary->periodIndexForDateTime(time), 600);
        QCOMPARE(primary->periodIndexForDateTimeAsOf(time), 600);
        QCOMPARE(primary->periodIndexForDateTimeAsOf(time.addMSecs(-1)), 599);
        QCOMPARE(primary->periodIndexForDateTime(time.addMSecs(49)), 600);
        QCOMPARE(primary->periodIndexForDateTime(time.addMSecs(50)), 601);
        animation->seekToTime(time);
        QTRY_COMPARE_WITH_TIMEOUT(layer->currentTimeIndex(), 600, 30000);
        auto *canvas = project->canvas();
        const auto bounds = layer->m_sceneBBox;
        canvas->setExtent({bounds.left(), -bounds.bottom(), bounds.right(), -bounds.top()}, false);
        showNativeReview(app);
        QTRY_VERIFY_WITH_TIMEOUT(canvas->compositionStats().active, 10000);
        QTest::qWait(1500);
        qInfo() << "project flow" << layer->currentTimeIndex() << layer->requestedTimeIndex()
                << layer->hasVelocityData() << layer->flowStreakStatus() << bounds
                << canvas->extent().width();
        auto *root = compositionRoot(*canvas);
        QVERIFY(root);
        for (auto *renderer : root->findChildren<SWMM2DResultsQSGRenderer *>())
            qInfo() << "project renderer" << renderer->displayedFrameRevision()
                    << layer->frameRevision() << renderer->captureReady();
        const auto folder = qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT", "tests/output/playback");
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(app.grab().save(folder + "/project-window.png"));
        QVERIFY(layer->hasVelocityData());
        auto *controller = layer->findChild<FlowParticleController *>();
        QVERIFY(controller);
        QTRY_VERIFY_WITH_TIMEOUT(controller->latestFrame(), 10000);
        QVERIFY(!controller->latestFrame()->particles.empty());
        canvas->setExtent({-.5, -.8, 3., .8}, false);
        QTest::qWait(1000);
        qInfo() << "project zoom" << layer->flowStreakStatus();
        QVERIFY(app.grab().save(folder + "/project-zoom.png"));
        animation->play();
        QTest::qWait(2000);
        animation->pause();
        QVERIFY(layer->currentTimeIndex() > 600);
        QVERIFY(layer->hasVelocityData());
        QTRY_VERIFY_WITH_TIMEOUT(controller->latestFrame() &&
                                  !controller->latestFrame()->particles.empty(), 10000);
        QVERIFY(app.grab().save(folder + "/project-playing.png"));
    }

    void nativeSavedContourPlayback()
    {
        const auto path = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (path.isEmpty() || !qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Optional saved-output playback review");
        SWMM2DResultsLayer layer;
        layer.setAsyncSurface(true);
        auto source = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(source->open(path));
        layer.setSource(std::move(source));
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        layer.contourBandSublayer()->setVisible(true);
        layer.flowStreakSublayer()->setVisible(true);
        layer.velocityVectorSublayer()->setVisible(true);
        layer.setCurrentTimeIndex(600);
        QTRY_COMPARE_WITH_TIMEOUT(layer.currentTimeIndex(), 600, 30000);
        QVERIFY(layer.hasVelocityData());
        ParticleReviewCanvas canvas;
        canvas.resize(1280, 800);
        canvas.setShowScaleBar(false);
        canvas.addLayer(&layer, false);
        const auto bounds = layer.m_sceneBBox;
        canvas.setExtent({bounds.left(), -bounds.bottom(), bounds.right(), -bounds.top()}, false);
        showNativeReview(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        auto *root = compositionRoot(canvas);
        QVERIFY(root);
        const auto renderers = root->findChildren<SWMM2DResultsQSGRenderer *>();
        QCOMPARE(renderers.size(), 1);
        auto *renderer = renderers.front();
        auto *controller = layer.findChild<FlowParticleController *>();
        QVERIFY(controller);
        QTRY_VERIFY_WITH_TIMEOUT(controller->frame(renderer->displayedFrameRevision()), 30000);
        QTest::qWait(500);
        const auto folder = qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT", "tests/output/playback");
        QVERIFY(QDir().mkpath(folder));
        QString error;
        layer.flowStreakSublayer()->streakStyle()->setAnimated(false);
        auto withVectors = canvas.captureMap(&error).toImage();
        QVERIFY2(!withVectors.isNull(), qPrintable(error));
        layer.velocityVectorSublayer()->setVisible(false);
        auto withoutVectors = canvas.captureMap(&error).toImage();
        QVERIFY(!withoutVectors.isNull());
        QVERIFY(withVectors != withoutVectors);
        QVERIFY(withVectors.save(folder + "/vectors-on.png"));
        QVERIFY(withoutVectors.save(folder + "/vectors-off.png"));
        layer.velocityVectorSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setAnimated(true);
        AnimationController animation;
        animation.setFallback2DLayer(&layer);
        animation.setSpeed(8);
        animation.setLooping(false);
        std::atomic_int samples = 0, missing = 0, changed = 0;
        quint64 previous = renderer->displayedFrameRevision();
        const auto monitor = connect(compositionWindow(canvas), &QQuickWindow::afterSynchronizing,
                                     &canvas, [&] {
            ++samples;
            const auto revision = renderer->displayedFrameRevision();
            if (!controller->frame(revision))
                ++missing;
            if (revision != previous) {
                ++changed;
                previous = revision;
            }
        }, Qt::DirectConnection);
        const auto cleanup = qScopeGuard([&] { disconnect(monitor); });
        QElapsedTimer clock;
        clock.start();
        animation.play();
        QTest::qWait(8000);
        animation.pause();
        const double seconds = clock.elapsed() / 1000.;
        disconnect(monitor);
        QFile report(folder + "/playback.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{
            {"seconds", seconds}, {"renderSamples", samples.load()},
            {"waterFramesPresented", changed.load()}, {"missingParticleFrames", missing.load()},
            {"requestedFrame", layer.requestedTimeIndex()},
            {"loadedFrame", layer.currentTimeIndex()},
            {"particleStatus", layer.flowStreakStatus()}
        }).toJson());
        report.close();
        QVERIFY(samples > 10);
        QVERIFY(changed > 10);
        QCOMPARE(missing.load(), 0);
        QVERIFY(layer.requestedTimeIndex() > 640);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy() && renderer->captureReady() &&
                                  renderer->displayedFrameRevision() == layer.frameRevision(),
                                30000);
        const auto image = canvas.captureMap(&error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(image.save(folder + "/playback.png"));
        animation.stop();
        QTRY_COMPARE_WITH_TIMEOUT(layer.currentTimeIndex(), 0, 30000);
        canvas.takeLayer(0, false);
    }

    /**
     *
     * @brief Supply cached-pixel, cached-duration and old continuous-preview JSON with expected
     * quantized spacing/trail values.
     *
     */
    void legacyStylesMigrateToTheSingleRenderer_data()
    {
        QTest::addColumn<QJsonObject>("saved");
        QTest::addColumn<double>("spacing");
        QTest::addColumn<double>("trail");
        const QJsonObject common{{"color", "#bf123456"},
                                 {"widthPx", 2.7},
                                 {"minimumLifetimeSeconds", 12.},
                                 {"maximumLifetimeSeconds", 24.},
                                 {"animated", false},
                                 {"fade", false},
                                 {"taper", false},
                                 {"speedMultiplier", 50.}};
        auto pixel = common;
        pixel["schemaVersion"] = 2;
        pixel["spacingPx"] = 30.;
        pixel["trailLengthPx"] = 400.;
        QTest::newRow("cached-pixel") << pixel << 28. << 1.5;
        auto duration = common;
        duration["schemaVersion"] = 3;
        duration["continuousParticles"] = false;
        duration["spacingPx"] = 44.;
        duration["velocityScaledTrail"] = true;
        duration["trailDurationSeconds"] = 2.6;
        QTest::newRow("cached-duration") << duration << 40. << 3.;
        auto preview = common;
        preview["schemaVersion"] = 3;
        preview["continuousParticles"] = true;
        preview["spacingPx"] = 60.;
        preview["velocityScaledTrail"] = true;
        preview["trailDurationSeconds"] = .9;
        QTest::newRow("physical-particle-preview") << preview << 56. << .75;
    }
    /**
     *
     * @brief Require old motion styles to migrate once, preserve appearance/lifetimes, drop
     * obsolete properties and stabilize schema-5 re-save.
     *
     */
    void legacyStylesMigrateToTheSingleRenderer()
    {
        QFETCH(QJsonObject, saved);
        QFETCH(double, spacing);
        QFETCH(double, trail);
        FlowStreakStyle style;
        style.fromJson(saved);
        QCOMPARE(style.spacingPx(), spacing);
        QCOMPARE(style.trailDurationSeconds(), trail);
        QCOMPARE(style.speedMultiplier(), 1.);
        QCOMPARE(style.color(), QColor("#bf123456"));
        QCOMPARE(style.widthPx(), 2.7);
        QCOMPARE(style.headWidthPx(), 2.7);
        QCOMPARE(style.minimumLifetimeSeconds(), 12.);
        QCOMPARE(style.maximumLifetimeSeconds(), 24.);
        QVERIFY(!style.animated());
        QVERIFY(style.motionWasMigrated());
        QCOMPARE(style.toJson()["schemaVersion"].toInt(), 5);
        for (const char *key :
             {"continuousParticles", "trailLengthPx", "pathLengthPx", "animationTimestepMs",
              "velocityScaledTrail", "fade", "taper", "dryDepthCutoff"}) {
            QVERIFY(!style.toJson().contains(key));
            QCOMPARE(style.metaObject()->indexOfProperty(key), -1);
        }
        FlowStreakStyle reopened;
        reopened.fromJson(style.toJson());
        QCOMPARE(reopened.toJson(), style.toJson());
        QVERIFY(!reopened.motionWasMigrated());
    }
    /**
     *
     * @brief Require already normalized schema-4 particle settings to retain exact
     * spacing/motion/trail/head width without migration flag.
     *
     */
    void normalizedSavedStylesKeepExactValues()
    {
        FlowStreakStyle style;
        auto saved = style.toJson();
        saved["schemaVersion"] = 4;
        saved["continuousParticles"] = true;
        saved["spacingPx"] = 22.;
        saved["speedMultiplier"] = 2.75;
        saved["trailDurationSeconds"] = 4.5;
        saved["headWidthPx"] = 6.2;
        style.fromJson(saved);
        QCOMPARE(style.spacingPx(), 22.);
        QCOMPARE(style.speedMultiplier(), 2.75);
        QCOMPARE(style.trailDurationSeconds(), 4.5);
        QCOMPARE(style.headWidthPx(), 6.2);
        QVERIFY(!style.motionWasMigrated());
    }
    /**
     *
     * @brief Require two live style panels to synchronize
     * density/motion/trail/lifetime/classification controls while another layer stays
     * independent.
     *
     */
    void presetsAndAdvancedSettingsStaySynchronized()
    {
        SWMM2DResultsLayer layer, other;
        openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer), second(&layer);
        auto *style = layer.flowStreakSublayer()->streakStyle();
        auto *advanced = panel.findChild<QWidget *>("flowAdvanced");
        QVERIFY(advanced);
        QVERIFY(advanced->isHidden());
        auto *dense = panel.findChild<QPushButton *>("flowDensity2");
        QVERIFY(dense);
        dense->click();
        QCOMPARE(style->spacingPx(), 28.);
        QVERIFY(second.findChild<QPushButton *>("flowDensity2")->isChecked());
        QCOMPARE(other.flowStreakSublayer()->streakStyle()->spacingPx(), 40.);
        panel.findChild<QPushButton *>("flowDensity3")->click();
        QCOMPARE(style->spacingPx(), 14.);
        QVERIFY(second.findChild<QPushButton *>("flowDensity3")->isChecked());
        panel.findChild<QDoubleSpinBox *>("spacingPx")->setValue(2);
        QCOMPARE(style->spacingPx(), 2.);
        style->setSpacingPx(22);
        QVERIFY(!dense->isChecked());
        QVERIFY(!panel.findChild<QLabel *>("flowDensityCustom")->isHidden());
        auto *motion = panel.findChild<QSlider *>("flowMotion");
        QVERIFY(motion);
        motion->setValue(175);
        QCOMPARE(style->speedMultiplier(), 1.75);
        auto *exact = panel.findChild<QDoubleSpinBox *>("motionPixelsPerSecond");
        QVERIFY(exact);
        QCOMPARE(exact->value(), 84.);
        exact->setValue(96);
        QCOMPARE(style->speedMultiplier(), 2.);
        QCOMPARE(second.findChild<QSlider *>("flowMotion")->value(), 200);
        panel.findChild<QPushButton *>("flowTrail2")->click();
        QCOMPARE(style->trailDurationSeconds(), 3.);
        style->setTrailDurationSeconds(4.5);
        QCOMPARE(panel.findChild<QDoubleSpinBox *>("trailDurationSeconds")->value(), 4.5);
        panel.findChild<QToolButton *>("flowAdvancedToggle")->click();
        QVERIFY(!advanced->isHidden());
        auto *minimum = panel.findChild<QDoubleSpinBox *>("minimumLifetimeSeconds");
        auto *maximum = panel.findChild<QDoubleSpinBox *>("maximumLifetimeSeconds");
        minimum->setValue(50);
        QCOMPARE(maximum->value(), 50.);
        maximum->setValue(10);
        QCOMPARE(minimum->value(), 10.);
        panel.findChild<QCheckBox *>("colorByMagnitude")->setChecked(true);
        QVERIFY(style->colorByMagnitude());
        QTRY_VERIFY(!panel.findChild<QWidget *>("flowClassification")->isHidden());
        for (const char *name : {"animated", "continuousParticles", "fade", "taper",
                                 "trailLengthPx", "pathLengthPx", "animationTimestepMs"})
            QVERIFY(!panel.findChild<QWidget *>(name));
        FlowStreakStyle reopened;
        reopened.fromJson(style->toJson());
        QCOMPARE(reopened.toJson(), style->toJson());
    }
    /**
     *
     * @brief Require spacing-two population above 250 and below 4000, then zoom residency to
     * replenish visible particles without failed births.
     *
     */
    void higherDensityExceedsTheFormerParticleLimit()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        layer.flowStreakSublayer()->setVisible(true);
        auto *style = layer.flowStreakSublayer()->streakStyle();
        style->setSpacingPx(2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 16));
        const auto frame = layer.flowParticles(layer.m_sceneBBox, 16, layer.frameRevision());
        QVERIFY(frame);
        QVERIFY2(layer.flowParticleDiagnostics().targetPopulation > 250,
                 qPrintable(layer.flowStreakStatus()));
        QVERIFY(frame->particles.size() > 250);
        QVERIFY(frame->particles.size() <= 4000);
        QCOMPARE(layer.flowParticleDiagnostics().failedBirths, quint64(0));
        const QRectF zoom(75, -75, 25, 25);
        QVERIFY(layer.prepareFlowStreaksForCapture(zoom, 64));
        const auto zoomed = layer.flowParticles(zoom, 64, layer.frameRevision());
        QVERIFY(zoomed);
        int visible = 0;
        for (const auto &p : zoomed->particles)
            visible += p.stop == FlowParticleStop::None && zoom.contains(p.position);
        QVERIFY2(visible >= layer.flowParticleDiagnostics().targetPopulation - 1,
                 qPrintable(QString("visible=%1 total=%2 target=%3 recycled=%4 busy=%5 %6")
                                .arg(visible)
                                .arg(zoomed->particles.size())
                                .arg(layer.flowParticleDiagnostics().targetPopulation)
                                .arg(layer.flowParticleDiagnostics().offscreenReplacements)
                                .arg(layer.flowStreakBusy())
                                .arg(layer.flowStreakStatus())));
        QVERIFY(layer.flowParticleDiagnostics().offscreenReplacements > 0);
    }
    /**
     *
     * @brief Require extended numeric edits to stage until Enter, synchronize another panel and
     * round-trip large valid style settings.
     *
     */
    void advancedEditorsPreserveExtendedSavedValues()
    {
        SWMM2DResultsLayer layer;
        openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
        auto *style = layer.flowStreakSublayer()->streakStyle();
        for (const auto &setting :
             {qMakePair("widthPx", 24.), qMakePair("headWidthPx", 48.),
              qMakePair("trailDurationSeconds", 120.), qMakePair("spacingPx", 1000.),
              qMakePair("minimumLifetimeSeconds", 600.), qMakePair("maximumLifetimeSeconds", 3600.),
              qMakePair("lifetimeFadeSeconds", 30.)}) {
            auto *control = panel.findChild<QDoubleSpinBox *>(setting.first);
            QVERIFY(control);
            const auto old = style->property(setting.first);
            control->selectAll();
            QTest::keyClicks(control, QString::number(setting.second));
            QCOMPARE(style->property(setting.first), old);
            QTest::keyClick(control, Qt::Key_Return);
            QCOMPARE(style->property(setting.first).toDouble(), setting.second);
        }
        auto *motion = panel.findChild<QDoubleSpinBox *>("motionPixelsPerSecond");
        motion->setValue(240000);
        QCOMPARE(style->speedMultiplier(), 5000.);
        openswmmvis::ui::Swmm2DResultsStylePanel second(&layer);
        QCOMPARE(second.findChild<QDoubleSpinBox *>("motionPixelsPerSecond")->value(), 240000.);
        FlowStreakStyle reopened;
        reopened.fromJson(style->toJson());
        QCOMPARE(reopened.toJson(), style->toJson());
    }
    /**
     *
     * @brief Require CPU particle image motion, paused clock retention and completely white
     * output after hiding the flow sublayer.
     *
     */
    void cpuParticlesAnimatePauseAndHide()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setColor(Qt::red);
        auto paint = [&] {
            QImage image(800, 800, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.translate(0, 800);
            painter.scale(8, 8);
            layer.paintFlowStreaks(&painter, layer.m_sceneBBox);
            return image;
        };
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 8));
        layer.advanceFlowStreaks(.5);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 8));
        const auto first = paint();
        layer.advanceFlowStreaks(.5);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 8));
        QVERIFY(paint() != first);
        const auto before = layer.flowParticles(layer.m_sceneBBox, 8, layer.frameRevision());
        style->setAnimated(false);
        layer.advanceFlowStreaks(1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 8));
        QCOMPARE(layer.flowParticles(layer.m_sceneBBox, 8, layer.frameRevision())->clock,
                 before->clock);
        sub->setVisible(false);
        const auto hidden = paint();
        QCOMPARE(hidden.pixelColor(400, 400), QColor(Qt::white));
        QImage white = hidden;
        white.fill(Qt::white);
        QCOMPARE(hidden, white);
    }
    /**
     *
     * @brief Optionally capture the exposed native five-control/advanced panels when
     * FLOW_PARTICLE_NATIVE_REVIEW is set.
     * Writes controls.png/advanced.png under FLOW_PARTICLE_NATIVE_OUTPUT or default
     * verification directory; requires native window support and leaves image evidence.
     *
     */
    void nativeFiveControlPanel()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Requires a native panel capture");
        SWMM2DResultsLayer layer;
        layer.flowStreakSublayer()->setVisible(true);
        openswmmvis::ui::Swmm2DResultsStylePanel panel(&layer);
        panel.resize(600, 680);
        auto *page = panel.findChild<QWidget *>("flowStreakTab");
        QVERIFY(page);
        for (auto *tabs : panel.findChildren<QTabWidget *>())
            for (int i = 0; i < tabs->count(); ++i)
                if (tabs->widget(i) == page || tabs->widget(i)->isAncestorOf(page))
                    tabs->setCurrentIndex(i);
        panel.setWindowFlags(panel.windowFlags() | Qt::WindowTransparentForInput |
                             Qt::WindowDoesNotAcceptFocus);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QTest::qWait(100);
        QVERIFY(panel.findChild<QWidget *>("flowAdvanced")->isHidden());
        const QString folder = qEnvironmentVariable(
            "FLOW_PARTICLE_NATIVE_OUTPUT", "tests/verification/flow_particles/native-panel");
        QDir().mkpath(folder);
        QVERIFY(panel.grab().save(folder + "/controls.png"));
        panel.findChild<QToolButton *>("flowAdvancedToggle")->click();
        QTest::qWait(100);
        QVERIFY(panel.grab().save(folder + "/advanced.png"));
    }
    /**
     *
     * @brief Pin normalized particle defaults and require old speed migration while preserving
     * appearance and stable re-save.
     *
     */
    void particleDefaultsAndOldMotionMigration()
    {
        FlowStreakStyle style;
        QCOMPARE(style.spacingPx(), 40.);
        QCOMPARE(style.speedMultiplier(), 1.);
        QCOMPARE(style.minimumLifetimeSeconds(), 20.);
        QCOMPARE(style.maximumLifetimeSeconds(), 40.);
        QCOMPARE(style.trailDurationSeconds(), 1.5);
        QCOMPARE(style.headWidthPx(), 2.5);
        auto old = style.toJson();
        old["schemaVersion"] = 3;
        old["speedMultiplier"] = 50.;
        old["color"] = "#ff123456";
        style.fromJson(old);
        QCOMPARE(style.speedMultiplier(), 1.);
        QCOMPARE(style.color(), QColor("#ff123456"));
        style.setSpeedMultiplier(2.5);
        FlowStreakStyle reopened;
        reopened.fromJson(style.toJson());
        QCOMPARE(reopened.toJson(), style.toJson());
    }
    /**
     *
     * @brief Require physical-SI and velocity-only fixtures at several map scales to yield
     * 48-pixel/second normalized motion and zero failed births.
     *
     */
    void defaultParticlesNormalizeNativeMotionAndUseVelocityOnlyOutputs()
    {
        for (double unit : {1., .3048})
            for (double scale : {.5, 2., 8.})
                for (bool fallback : {false, true}) {
                    SWMM2DResultsLayer layer;
                    layer.setFallbackCoordinateScale(unit);
                    if (fallback)
                        layer.setSource(std::make_unique<VelocityOnlyFlowSource>());
                    else
                        layer.setSource(std::make_unique<FlowSource>());
                    layer.flowStreakSublayer()->setVisible(true);
                    QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, scale));
                    for (int i = 0; i < 120; ++i) {
                        layer.advanceFlowStreaks(1. / 60);
                        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, scale));
                    }
                    const auto frame =
                        layer.flowParticles(layer.m_sceneBBox, scale, layer.frameRevision());
                    QVERIFY(frame);
                    const auto diagnostics = layer.flowParticleDiagnostics();
                    QCOMPARE(diagnostics.failedBirths, quint64(0));
                    QCOMPARE(diagnostics.referenceSpeedSI, 2.);
                    QVERIFY(std::abs(diagnostics.motionGain * (2 / unit) * scale - 48) < 1e-5);
                    int moving = 0;
                    for (const auto &p : frame->particles)
                        if (p.stop == FlowParticleStop::None && p.history.size() > 1) {
                            const auto &a = p.history.front(), &b = p.history.back();
                            const double pixels =
                                (b.position.x() - a.position.x()) * scale / (b.age - a.age);
                            QVERIFY(std::abs(pixels - 48) < 1e-4);
                            ++moving;
                        }
                    QVERIFY(moving > 0);
                    if (fallback)
                        QVERIFY(static_cast<VelocityOnlyFlowSource *>(layer.source())->subsetReads >
                                0);
                }
    }
    /**
     *
     * @brief Enable regional mode within a restored environment scope and require velocity-only
     * regional packets to move particles without full-frame rereads.
     *
     */
    void regionalVelocityFallbackIgnoresPlaceholderFluxBuffers()
    {
        const auto previous = qgetenv("OPENSWMM_REGIONAL_SURFACE");
        qputenv("OPENSWMM_REGIONAL_SURFACE", "1");
        const auto restore = qScopeGuard([previous] {
            if (previous.isNull())
                qunsetenv("OPENSWMM_REGIONAL_SURFACE");
            else
                qputenv("OPENSWMM_REGIONAL_SURFACE", previous);
        });
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<VelocityOnlyGridSource>(100, 100);
        auto *raw = source.get();
        layer.setSource(std::move(source));
        layer.flowStreakSublayer()->setVisible(true);
        const QRectF view(50, -50, 10, 10);
        QVERIFY(layer.prepareFlowStreaksForCapture(view, 32));
        QVERIFY(layer.regionalHydraulicsPublished());
        const auto input = layer.captureFlowParticleInput(view, 32);
        QVERIFY(input);
        for (const auto &t : input->triangles) {
            QVERIFY(t.available);
            QVERIFY(t.velocityOnly);
        }
        const int fullReads = raw->reads;
        layer.advanceFlowStreaks(.1);
        QVERIFY(layer.prepareFlowStreaksForCapture(view, 32));
        const auto frame = layer.flowParticles(view, 32, layer.frameRevision());
        QVERIFY(frame && !frame->particles.empty());
        QCOMPARE(raw->reads, fullReads);
        QVERIFY(raw->subsetReads > 0);
        QCOMPARE(layer.flowParticleDiagnostics().failedBirths, quint64(0));
        for (const auto &p : frame->particles) {
            QVERIFY(p.history.size() > 1);
            QVERIFY(!p.waitingForData);
        }
    }

    /**
     *
     * @brief Require triangular/quad flux reconstruction to retain 2-m/s physical speed under
     * metre/foot coordinate scaling and cross a cell without stopping.
     *
     */
    void nativeParticlesHonorSIAndQuadFluxDiagonals()
    {
        for (bool quad : {false, true})
            for (double unit : {1., .3048}) {
                SWMM2DResultsLayer layer;
                layer.setFallbackCoordinateScale(unit);
                if (quad)
                    layer.setSource(std::make_unique<QuadFlowSource>());
                else
                    layer.setSource(std::make_unique<FlowSource>());
                auto input = layer.captureFlowParticleInput(layer.m_sceneBBox, 8);
                QVERIFY(input);
                std::atomic_bool cancel = false;
                const auto field = buildFlowParticleField(*input, cancel);
                QVERIFY(field);
                for (const auto &f : field->fields)
                    for (int k = 0; k < 3; ++k) {
                        QVERIFY(std::abs(f.velocity[k].x() - 2 / unit) < 1e-5);
                        QVERIFY(std::abs(f.velocity[k].y()) < 1e-5);
                        QVERIFY(std::abs(f.speed[k] - 2) < 1e-5);
                    }
                FlowParticleSystem system;
                FlowParticleSettings settings;
                settings.minimumLifetime = settings.maximumLifetime = 100;
                settings.trailSeconds = 30;
                settings.maxHistory = 5000;
                system.setSettings(settings);
                system.setField(field);
                const auto id = system.spawn({10 / unit, -50 / unit});
                QVERIFY(id);
                system.advance(30);
                const auto &p = system.particles().front();
                QCOMPARE(p.id, id);
                QVERIFY(p.crossings > 0);
                QCOMPARE(p.stop, FlowParticleStop::None);
                QVERIFY(std::abs(p.position.x() - 70 / unit) < 1e-4);
            }
    }
    /**
     *
     * @brief Require one particle ID/history to survive 1000 compatible hydraulic revisions
     * without new births/deaths or source rereads during pure advancement.
     *
     */
    void nativeCompatibleUpdatesRetainHistoryAcrossOneThousandPackets()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        auto *raw = source.get();
        source->live = true;
        layer.setSource(std::move(source));
        const auto initial = layer.captureFlowParticleInput(layer.m_sceneBBox, 8);
        QVERIFY(initial);
        std::atomic_bool cancel = false;
        FlowParticleSystem system;
        FlowParticleSettings settings;
        settings.minimumLifetime = settings.maximumLifetime = 100;
        settings.trailSeconds = 30;
        settings.maxHistory = 5000;
        system.setSettings(settings);
        system.setField(buildFlowParticleField(*initial, cancel));
        const auto id = system.spawn({10, -50});
        QVERIFY(id);
        system.advance(.2);
        const auto history = system.particles().front().history;
        for (int i = 0; i < 1000; ++i) {
            auto packet = std::make_shared<FlowParticleNativeInput>(*initial);
            packet->revision = i + 10;
            const double factor = i % 2 ? 1. : 1.25;
            for (auto &t : packet->triangles)
                for (auto &q : t.flux)
                    q *= factor;
            QVERIFY(system.setField(buildFlowParticleField(*packet, cancel), .25));
            system.advance(1. / 60);
            QCOMPARE(system.particles().front().id, id);
        }
        QCOMPARE(system.frame()->births, quint64(1));
        QCOMPARE(system.frame()->deaths, quint64(0));
        for (const auto &sample : system.particles().front().history)
            if (sample.age <= history.back().age) {
                const auto original =
                    std::find_if(history.begin(), history.end(),
                                 [&](const auto &s) { return s.age == sample.age; });
                QVERIFY(original != history.end());
                QCOMPARE(sample.position, original->position);
            }
        const int reads = raw->reads;
        system.advance(.1);
        QCOMPARE(raw->reads, reads);
    }
    /**
     *
     * @brief Require coalesced live revisions to preserve particle IDs/lifetimes through speed
     * changes, stagnation and unavailable flux, with render-disable pause/resume.
     *
     */
    void continuousControllerCoalescesLiveUpdatesWithoutParticleReplacement()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        auto *raw = source.get();
        source->live = true;
        layer.setSource(std::move(source));
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setMinimumLifetimeSeconds(20);
        style->setMaximumLifetimeSeconds(40);
        style->setTrailDurationSeconds(1);
        style->setSpeedMultiplier(.1);
        const auto get = [&] {
            return layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        };
        QTRY_VERIFY_WITH_TIMEOUT(get() != nullptr, 5000);
        QVERIFY(!get()->particles.empty());
        const auto selected =
            std::find_if(get()->particles.begin(), get()->particles.end(), [](const auto &p) {
                return p.stop == FlowParticleStop::None && p.lifetime - p.age > 1;
            });
        QVERIFY(selected != get()->particles.end());
        const auto id = selected->id;
        const double lifetime = selected->lifetime;
        const int reads = raw->reads;
        layer.advanceFlowStreaks(.2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        QCOMPARE(raw->reads, reads);
        const auto old = get();
        const auto before = std::find_if(old->particles.begin(), old->particles.end(),
                                         [&](const auto &p) { return p.id == id; });
        QVERIFY(before != old->particles.end());
        for (int i = 0; i < 10; ++i) {
            raw->speed = 2 + .01f * i;
            ++raw->revision;
            layer.refreshCurrentFrame();
            get();
        }
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision() != old->revision, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(get() != nullptr && !layer.flowStreakBusy(), 5000);
        layer.advanceFlowStreaks(.2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        bool found = false;
        for (const auto &p : get()->particles)
            if (p.id == id) {
                found = true;
                QCOMPARE(p.lifetime, lifetime);
                QVERIFY(p.age >= before->age);
            }
        QVERIFY(found);
        // Stagnation and an incomplete live flux packet must not disable the
        // pool or replace surviving IDs with a newly seeded population.
        auto revision = layer.frameRevision();
        raw->speed = 0;
        ++raw->revision;
        layer.refreshCurrentFrame();
        QTRY_VERIFY(layer.frameRevision() != revision);
        QTRY_VERIFY(get() != nullptr);
        QVERIFY(layer.flowStreaksEnabled());
        layer.advanceFlowStreaks(.1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        revision = layer.frameRevision();
        raw->fluxEnabled = false;
        ++raw->revision;
        layer.refreshCurrentFrame();
        QTRY_VERIFY(layer.frameRevision() != revision);
        QTRY_VERIFY(get() != nullptr);
        layer.advanceFlowStreaks(.1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        found = false;
        for (const auto &p : get()->particles)
            if (p.id == id) {
                found = true;
                QVERIFY(p.waitingForData);
                QCOMPARE(p.stop, FlowParticleStop::None);
            }
        QVERIFY(found);
        revision = layer.frameRevision();
        raw->fluxEnabled = true;
        raw->speed = 2;
        ++raw->revision;
        layer.refreshCurrentFrame();
        QTRY_VERIFY(layer.frameRevision() != revision);
        QTRY_VERIFY(get() != nullptr);
        layer.setLiveRenderEnabled(false);
        QVERIFY(!get());
        const auto clock = old->clock;
        layer.advanceFlowStreaks(1);
        QCOMPARE(old->clock, clock);
        layer.setLiveRenderEnabled(true);
        QTRY_VERIFY(get() != nullptr);
    }
    /**
     *
     * @brief Require native particle geometry/color/alpha, one upload on head-width change,
     * stable births and scene-graph sublayer order/hidden clearing.
     *
     */
    void continuousLayerRendererPreservesSublayerOrderAndAppearance()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setColor(QColor(20, 40, 80, 128));
        style->setSpeedMultiplier(1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        layer.advanceFlowStreaks(.5);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        TestRenderer renderer;
        renderer.setSize({800, 800});
        renderer.setProperty("canvasDevicePixelRatio", 1);
        renderer.setLayer(&layer);
        renderer.setMapExtent({0, 0, 100, 100});
        QSGNode *root = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((root = renderer.updatePaintNode(root, nullptr),
                                  findParticles(root)->geometry()->vertexCount() > 0),
                                 5000);
        auto *node = findParticles(root);
        const auto *v = static_cast<const FlowParticleVertex *>(node->geometry()->vertexData());
        QCOMPARE(v->r, uchar(20));
        QCOMPARE(v->a, uchar(128));
        const int uploads = node->uploads();
        const auto frame = layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        style->setHeadWidthPx(6);
        root = renderer.updatePaintNode(root, nullptr);
        QCOMPARE(node->uploads(), uploads + 1);
        QCOMPARE(layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision())->births,
                 frame->births);
        QVERIFY(layer.moveSublayer(layer.sublayers().indexOf(sub), 0));
        root = renderer.updatePaintNode(root, nullptr);
        QCOMPARE(root->firstChild(), node->parent());
        sub->setVisible(false);
        root = renderer.updatePaintNode(root, nullptr);
        QCOMPARE(node->geometry()->vertexCount(), 0);
        delete root;
    }
    /**
     *
     * @brief Require accepted displayed hydraulics to keep particle clock/motion advancing while
     * a newer prepared field waits, then adopt without time jump.
     *
     */
    void preparedFieldKeepsDisplayedWaterMovingWithoutCatchUpJump()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        auto *raw = source.get();
        source->live = true;
        layer.setSource(std::move(source));
        layer.flowStreakSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setSpeedMultiplier(.1);

        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        layer.advanceFlowStreaks(.2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        const auto old = layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        QVERIFY(!old->particles.empty());
        const auto selected =
            std::find_if(old->particles.begin(), old->particles.end(), [](const auto &p) {
                return p.stop == FlowParticleStop::None && p.lifetime - p.age > 1 &&
                       p.position.x() < 95;
            });
        QVERIFY(selected != old->particles.end());
        const auto id = selected->id;
        const auto position = selected->position;
        raw->speed = 10;
        ++raw->revision;
        layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision() != old->revision,
                                 5000); // live refresh is queued/coalesced
        const auto prepared = [&] {
            return layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision(), false);
        };
        QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(), 5000);
        QVERIFY(!prepared());
        layer.advanceFlowStreaks(.5);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(), 5000);
        const auto moving = layer.flowParticles(layer.m_sceneBBox, 1, old->revision);
        QVERIFY(moving);
        QVERIFY(std::abs(moving->clock - old->clock - .5) < 1e-8);
        for (const auto &p : moving->particles)
            if (p.id == id)
                QVERIFY(p.position.x() > position.x());
        layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        QTRY_VERIFY_WITH_TIMEOUT(prepared() != nullptr && !layer.flowStreakBusy(), 5000);
        QCOMPARE(prepared()->clock, moving->clock);
        layer.advanceFlowStreaks(.1);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.flowStreakBusy(), 5000);
        QVERIFY(std::abs(prepared()->clock - old->clock - .6) < 1e-8);
    }
    /**
     *
     * @brief Optionally read FLOW_STREAK_RESULT HDF5 and require saved/live adapter fields to
     * agree in availability, exits and vertex velocities within 1e-4.
     * Skips without a path; requires a nonempty saved native hydraulic fixture, reads only
     * its final frame and writes no model/result file.
     *
     */
    void savedAndLiveNativeParticleFieldsAgree()
    {
        const QString path = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (path.isEmpty())
            QSKIP("Provide a saved native hydraulic fixture");
        auto file = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(file->open(path));
        std::vector<double> x, y, z;
        std::vector<std::array<int, 4>> cells;
        QVERIFY(file->readCells(x, y, z, cells));
        std::vector<float> depth, flux, length, nx, ny;
        const int time = file->timeCount() - 1;
        QVERIFY(file->readDepthsAt(time, depth));
        QVERIFY(file->readEdgeFluxAt(time, flux));
        QVERIFY(file->readEdgeGeometry(length, nx, ny));
        const auto simTime = file->simTimeAt(time);
        auto stream = std::make_unique<EngineMesh2DSource>(x, y, z, cells);
        stream->setEdgeGeometry(length, nx, ny);
        stream->pushDepths(depth, simTime, 0);
        stream->pushFlux(flux, simTime, 0);
        SWMM2DResultsLayer saved, live;
        saved.setSource(std::move(file));
        saved.setCurrentTimeIndex(time);
        live.setSource(std::move(stream));
        const auto a = saved.captureFlowParticleInput(saved.m_sceneBBox, 1),
                   b = live.captureFlowParticleInput(live.m_sceneBBox, 1);
        QVERIFY(a);
        QVERIFY(b);
        QCOMPARE(a->triangles.size(), b->triangles.size());
        std::atomic_bool cancel = false;
        const auto fa = buildFlowParticleField(*a, cancel), fb = buildFlowParticleField(*b, cancel);
        QVERIFY(fa);
        QVERIFY(fb);
        for (size_t i = 0; i < fa->fields.size(); ++i) {
            QCOMPARE(fa->fields[i].available, fb->fields[i].available);
            QCOMPARE(fa->fields[i].exits, fb->fields[i].exits);
            for (int k = 0; k < 3; ++k) {
                const auto delta = fa->fields[i].velocity[k] - fb->fields[i].velocity[k];
                QVERIFY(std::hypot(delta.x(), delta.y()) < 1e-4);
            }
        }
    }
    /**
     *
     * @brief Require CPU composition to combine layer/sublayer alpha over background, preserve
     * capture and return to plain background after hiding fill.
     * Shows a disposable canvas and detaches its stack-owned layer after checks.
     *
     */
    void cpuCellFillPreservesOpacityAndCapture()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        auto *fill = layer.cellDepthFillSublayer();
        fill->setVisible(true);
        fill->setOpacity(.5);
        auto *style = fill->fillStyle();
        style->setColorRampName("");
        style->setLowColor(QColor(0, 0, 200));
        style->setHighColor(QColor(0, 0, 200));
        style->setClassified(false);
        layer.setOpacity(.5);
        auto mask = layer.maskSpec();
        mask.enabled = true;
        layer.setMaskSpec(mask);
        MapCanvas canvas;
        canvas.resize(480, 480);
        canvas.setBackgroundColor(QColor(20, 60, 100));
        canvas.setShowScaleBar(false);
        canvas.addLayer(&layer, false);
        canvas.setExtent({-10, -10, 110, 110}, false);
        canvas.show();
        QTest::qWait(100);
        QString error;
        const auto image = canvas.captureMap(&error).toImage();
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(!layer.qsgOwnsRendering());
        QVERIFY(!canvas.compositionStats().active);
        const auto color = image.pixelColor(qRound(240 * image.devicePixelRatio()),
                                            qRound(200 * image.devicePixelRatio()));
        QVERIFY2(std::abs(color.red() - 15) <= 2, qPrintable(color.name()));
        QVERIFY2(std::abs(color.green() - 45) <= 2, qPrintable(color.name()));
        QVERIFY2(std::abs(color.blue() - 125) <= 2, qPrintable(color.name()));
        fill->setVisible(false);
        const auto empty = canvas.captureMap(&error).toImage();
        QVERIFY2(!empty.isNull(), qPrintable(error));
        QCOMPARE(empty.pixelColor(qRound(240 * empty.devicePixelRatio()),
                                  qRound(200 * empty.devicePixelRatio())),
                 QColor(20, 60, 100));
        canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Require opposite-winding adjacent triangles to cover every pixel exactly once.
     * Checks explicit premultiplied RGBA interpolation and that NaN/huge offscreen
     * triangles leave the image unchanged. Uses memory-only 16-by-16 images.
     *
     */
    void smoothRasterSharesEdgesAndInterpolatesPremultipliedColors()
    {
        using namespace SmoothFillRaster;
        QImage image(16, 16, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        const QRgb color = qPremultiply(qRgba(40, 100, 200, 128));
        // The diagonal passes through pixel centres. Reversed winding must
        // neither double the opacity nor leave a one-pixel crack.
        triangle(image, {{{{0, 0}, color}, {{16, 0}, color}, {{16, 16}, color}}});
        triangle(image, {{{{0, 0}, color}, {{0, 16}, color}, {{16, 16}, color}}});
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                QCOMPARE(image.pixel(x, y), color);
        image.fill(Qt::transparent);
        triangle(image, {{{{0, 0}, qPremultiply(qRgba(255, 0, 0, 255))},
                          {{16, 0}, qPremultiply(qRgba(0, 255, 0, 128))},
                          {{0, 16}, qPremultiply(qRgba(0, 0, 255, 0))}}});
        const QRgb mid = image.pixel(3, 3); // weights 9/16, 3.5/16, 3.5/16
        QCOMPARE(qRed(mid), 143);
        QCOMPARE(qGreen(mid), 28);
        QCOMPARE(qBlue(mid), 0);
        QCOMPARE(qAlpha(mid), 171);
        const QImage before = image;
        triangle(image, {{{{NAN, 0}, color}, {{1, 0}, color}, {{0, 1}, color}}});
        QCOMPARE(image, before);
        triangle(image, {{{{-1e30, -1e30}, color}, {{-1e30, 0}, color}, {{0, -1e30}, color}}});
        QCOMPARE(image, before);
    }
    /**
     *
     * @brief Check smooth CPU fill wet/dry coverage, stack order, clipping and painter
     * transforms.
     * Exercises classified/inverted ramps and device ratios one/two. With
     * FLOW_PARTICLE_NATIVE_REVIEW, also compares sampled native RGB within five units
     * and saves CPU/native PNGs under FLOW_PARTICLE_NATIVE_OUTPUT or the review default.
     * Requires GUI rendering; native comparison additionally needs a working GPU surface.
     * Detaches the borrowed stack layer before canvas destruction.
     *
     */
    void cpuSmoothFillMatchesNativeAndRetainsOrder()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<SlopingFlowSource>());
        double shallow = 1e9, deep = -1e9;
        for (const auto &t : layer.m_sceneTris)
            for (float d : {t.dv0, t.dv1, t.dv2}) {
                shallow = std::min(shallow, double(d));
                deep = std::max(deep, double(d));
            }
        QVERIFY(shallow < .1);
        QVERIFY(deep > .4);
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        auto *fill = layer.smoothDepthFillSublayer();
        fill->setVisible(true);
        fill->setOpacity(.65);
        layer.setOpacity(.7);
        auto *style = fill->fillStyle();
        style->setUseCustomRange(true);
        style->setRangeMin(.1);
        style->setRangeMax(.4);
        auto mask = layer.maskSpec();
        mask.enabled = true;
        layer.setMaskSpec(mask);
        MapCanvas canvas;
        canvas.resize(480, 360);
        canvas.setBackgroundColor(QColor(20, 60, 100));
        canvas.setShowScaleBar(false);
        canvas.addLayer(&layer, false);
        canvas.setExtent({-10, -10, 110, 110}, false);
        canvas.show();
        QTest::qWait(100);
        QString error;
        for (bool classified : {false, true})
            for (bool invert : {false, true}) {
                style->setClassified(classified);
                style->setInvertRamp(invert);
                const auto cpu = canvas.captureMap(&error).toImage();
                QVERIFY2(!cpu.isNull(), qPrintable(error));
                QVERIFY(!layer.qsgOwnsRendering());
                int filled = 0, dry = 0;
                const auto pixel = [&](const QImage &i, int x, int y) {
                    return i.pixelColor(qRound(x * i.devicePixelRatio()),
                                        qRound(y * i.devicePixelRatio()));
                };
                for (int y = 60; y < 300; y += 10)
                    for (int x = 60; x < 420; x += 10) {
                        if (pixel(cpu, x, y) == canvas.backgroundColor())
                            ++dry;
                        else
                            ++filled;
                    }
                QVERIFY(filled > 30);
                QVERIFY(dry > 30);
                if (qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW")) {
                    mask.enabled = false;
                    layer.setMaskSpec(mask);
                    QTest::qWait(150);
                    const auto native = canvas.captureMap(&error).toImage();
                    QVERIFY2(!native.isNull(), qPrintable(error));
                    QVERIFY(layer.qsgOwnsRendering());
                    const QString folder = qEnvironmentVariable(
                        "FLOW_PARTICLE_NATIVE_OUTPUT",
                        "workplans/artifacts/flow_particle_fallback_2026-10-12");
                    QDir().mkpath(folder);
                    const QString name = QString("smooth-%1-%2")
                                             .arg(classified ? "classified" : "continuous",
                                                  invert ? "inverted" : "normal");
                    QVERIFY(cpu.save(folder + "/" + name + "-cpu.png"));
                    QVERIFY(native.save(folder + "/" + name + "-native.png"));
                    int compared = 0;
                    for (int y = 60; y < 300; y += 13)
                        for (int x = 60; x < 420; x += 11) {
                            const auto a = pixel(cpu, x, y), b = pixel(native, x, y);
                            const int diff = std::max({std::abs(a.red() - b.red()),
                                                       std::abs(a.green() - b.green()),
                                                       std::abs(a.blue() - b.blue())});
                            QVERIFY2(diff <= 5,
                                     qPrintable(QString("pixel %1,%2 cpu %3 native %4 diff %5")
                                                    .arg(x)
                                                    .arg(y)
                                                    .arg(a.name())
                                                    .arg(b.name())
                                                    .arg(diff)));
                            ++compared;
                        }
                    QVERIFY(compared > 200);
                    mask.enabled = true;
                    layer.setMaskSpec(mask);
                }
            }
        // Two fills obey their actual stack order in the CPU fallback.
        style->setColorRampName("");
        style->setLowColor(QColor(200, 0, 0));
        style->setHighColor(QColor(200, 0, 0));
        style->setClassified(false);
        style->setUseCustomRange(false);
        fill->setOpacity(1);
        layer.setOpacity(1);
        auto *cell = layer.cellDepthFillSublayer();
        cell->setVisible(true);
        cell->fillStyle()->setColorRampName("");
        cell->fillStyle()->setLowColor(QColor(0, 0, 200));
        cell->fillStyle()->setHighColor(QColor(0, 0, 200));
        cell->fillStyle()->setClassified(false);
        layer.moveSublayer(layer.sublayers().indexOf(cell), layer.sublayers().size() - 1);
        const auto blue = canvas.captureMap(&error).toImage();
        QVERIFY(!blue.isNull());
        layer.moveSublayer(layer.sublayers().indexOf(fill), layer.sublayers().size() - 1);
        const auto red = canvas.captureMap(&error).toImage();
        QVERIFY(!red.isNull());
        const auto location = canvas.overlayView()->mapFromScene(QPointF(10, -50));
        const auto at = [&](const QImage &i) {
            return i.pixelColor(qRound(location.x() * i.devicePixelRatio()),
                                qRound(location.y() * i.devicePixelRatio()));
        };
        QCOMPARE(at(blue), QColor(0, 0, 200));
        QCOMPARE(at(red), QColor(200, 0, 0));
        cell->setVisible(false);
        for (double dpr : {1., 2.}) {
            QImage transformed(QSize(600, 450) * dpr, QImage::Format_ARGB32_Premultiplied);
            transformed.setDevicePixelRatio(dpr);
            transformed.fill(QColor(3, 5, 7));
            QPainter painter(&transformed);
            painter.setClipRect(QRectF(50, 50, 400, 300));
            painter.translate(180, 80);
            painter.rotate(17);
            painter.shear(.15, .1);
            painter.scale(2, 2);
            canvas.overlayView()->scene()->render(&painter, QRectF(0, 0, 100, 100),
                                                  layer.m_sceneBBox, Qt::IgnoreAspectRatio);
            painter.end();
            int redPixels = 0;
            for (int y = 0; y < transformed.height(); ++y)
                for (int x = 0; x < transformed.width(); ++x) {
                    const auto color = transformed.pixelColor(x, y);
                    if (x < 50 * dpr || x >= 450 * dpr || y < 50 * dpr || y >= 350 * dpr)
                        QCOMPARE(color, QColor(3, 5, 7));
                    redPixels += color.red() > 150 && color.blue() < 10;
                }
            QVERIFY(redPixels > 1000);
        }
        canvas.zoomAroundCursor(1.3, {240, 180});
        canvas.pan(7, 3);
        canvas.resize(520, 400);
        QVERIFY(!canvas.captureMap(&error).isNull());
        canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Require half-open z slices to retain parent/child composition and inherited alpha.
     * Checks selected pixels, unchanged item visibility and no scene changed signal
     * after slice rendering; the infinite slice includes both fixture bands.
     *
     */
    void sceneSlicesPreserveChildrenAndDoNotInvalidate()
    {
        OpenSWMMVisScene scene;
        auto *lower = scene.addRect(QRectF(0, 0, 100, 100), Qt::NoPen, Qt::blue);
        lower->setZValue(1000);
        auto *upper = scene.addRect(QRectF(0, 0, 30, 30), Qt::NoPen, Qt::red);
        upper->setZValue(2000);
        upper->setPos(20, 20);
        upper->setOpacity(.5);
        auto *child = new QGraphicsRectItem(QRectF(5, 5, 10, 10), upper);
        child->setPen(Qt::NoPen);
        child->setBrush(Qt::green);
        child->setZValue(-10000);
        QCoreApplication::processEvents();
        QSignalSpy changed(&scene, &QGraphicsScene::changed);
        const auto slice = [&](double low, double high) {
            QImage image(100, 100, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            scene.renderSlice(&painter, QRectF(0, 0, 100, 100), QRectF(0, 0, 100, 100), low, high);
            return image;
        };
        const auto background = slice(1000, 2000), foreground = slice(2000, 3000);
        QCOMPARE(background.pixelColor(27, 27), QColor(Qt::blue));
        QCOMPARE(foreground.pixelColor(5, 5).alpha(), 0);
        QVERIFY(std::abs(foreground.pixelColor(40, 40).alpha() - 127) <= 1);
        QVERIFY(foreground.pixelColor(27, 27).green() > 100);
        QCoreApplication::processEvents();
        QCOMPARE(changed.count(), 0);
        QVERIFY(lower->isVisible());
        QVERIFY(upper->isVisible());
        QVERIFY(child->isVisible());
        const auto all = slice(-INFINITY, INFINITY);
        QVERIFY(all.pixelColor(40, 40).red() > 100);
    }
    /**
     *
     * @brief Exercise embedded GPU composition through tab, covering-dialog, size and pan
     * changes.
     * Skips without FLOW_PARTICLE_NATIVE_REVIEW; temporarily unsets and restores
     * OPENSWMM_FLOW_NATIVE_SURFACE. Reads the presented framebuffer directly to detect
     * stale pixels, then checks animation progress without capture/fallback readbacks.
     * Requires an exposed native GUI surface; detaches borrowed layers before teardown.
     *
     */
    void embeddedCompositionSurvivesWidgetChanges()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Requires GPU widget composition");
        const auto original = qgetenv("OPENSWMM_FLOW_NATIVE_SURFACE");
        const auto restore = qScopeGuard([&] {
            if (original.isNull())
                qunsetenv("OPENSWMM_FLOW_NATIVE_SURFACE");
            else
                qputenv("OPENSWMM_FLOW_NATIVE_SURFACE", original);
        });
        qunsetenv("OPENSWMM_FLOW_NATIVE_SURFACE");
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        auto *flow = layer.flowStreakSublayer();
        flow->setVisible(true);
        flow->streakStyle()->setColor(Qt::transparent);
        CompositionBackdrop raster(true);
        QTabWidget host;
        host.resize(640, 480);
        MapCanvas canvas;
        canvas.setShowScaleBar(false);
        canvas.addLayer(&raster, false);
        canvas.addLayer(&layer, false);
        canvas.setExtent({-10, -10, 110, 110}, false);
        host.addTab(&canvas, "Map");
        host.addTab(new QWidget, "Other");
        showNativeReview(host);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        QVERIFY(!canvas.compositionStats().nativeSurface);
        auto *surface = canvas.findChild<QQuickWidget *>();
        QVERIFY(surface);
        for (int step = 0; step < 4; ++step) {
            if (step == 1) {
                QDialog cover(&host);
                cover.resize(240, 160);
                cover.show();
                QTest::qWait(100);
                cover.hide();
                host.setCurrentIndex(1);
                QTest::qWait(100);
                host.setCurrentIndex(0);
            }
            if (step == 2)
                host.resize(720, 520);
            if (step == 3)
                canvas.pan(15, -8);
            QTest::qWait(250);
            // Grab the presented widget surface without captureMap's layer
            // refresh, which could conceal a stale on-screen composition.
            const auto frame = surface->grabFramebuffer();
            QVERIFY(!frame.isNull());
            for (int y = 8; y < frame.height() - 8; y += 3)
                for (int x = 8; x < frame.width() - 8; x += 3)
                    QCOMPARE(frame.pixelColor(x, y), QColor(20, 60, 100));
        }
        const auto before = canvas.compositionStats();
        const auto extent = canvas.extent();
        const QRectF view(extent.xMin(), -extent.yMax(), extent.width(), extent.height());
        auto first =
            layer.flowParticles(view, canvas.width() / view.width(), layer.frameRevision(), false);
        QVERIFY(first);
        const auto clock = first->clock;
        first.reset();
        QTest::qWait(400);
        auto last =
            layer.flowParticles(view, canvas.width() / view.width(), layer.frameRevision(), false);
        QVERIFY(last);
        QVERIFY(last->clock > clock);
        const auto after = canvas.compositionStats();
        QVERIFY(after.renderedFrames > before.renderedFrames + 3);
        QCOMPARE(after.fallbackReadbacks, before.fallbackReadbacks);
        QCOMPARE(after.captureReadbacks, before.captureReadbacks);
        while (canvas.layerCount())
            canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Exercise GPU composition input, caching, capture, masking, ordering and CPU
     * fallback.
     * Skips without FLOW_PARTICLE_NATIVE_REVIEW. Checks paused clocks, capture without
     * particle births/deaths, steady frames without new texture uploads/readbacks,
     * multiple output alpha, polygon holes, hidden/resumed windows and layer removal.
     * FLOW_PARTICLE_COMPOSITION_SECONDS selects each steady wait, clamped to 1..60 s.
     * Creates mask.geojson, PNG captures and composition.json in FLOW_PARTICLE_NATIVE_OUTPUT
     * or the verification default. Requires writable output and working native rendering.
     * Temporarily writes then unsets OPENSWMM_FLOW_COMPOSITION without preserving its
     * previous value; report and polygon writes are not checked for short writes.
     *
     */
    void visibleCompositionCachesAndFallsBack()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Requires native GPU composition");
        OpenSWMMVisWorkspace workspace(static_cast<QObject *>(nullptr));
        SWMM2DResultsLayer layer("composition results", &workspace);
        layer.setSource(std::make_unique<FlowSource>());
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setColor(Qt::transparent);
        style->setSpacingPx(25);
        style->setMinimumLifetimeSeconds(20);
        style->setMaximumLifetimeSeconds(40);
        style->setSpeedMultiplier(1);
        CompositionBackdrop raster(true), vector;
        ParticleReviewCanvas canvas;
        canvas.resize(480, 480);
        canvas.setShowScaleBar(false);
        canvas.addLayer(&raster, false);
        canvas.addLayer(&vector, false);
        canvas.addLayer(&layer, false);
        canvas.reorderLayers({&raster, &vector, &layer}, false);
        canvas.setExtent({-10, -10, 110, 110}, false);
        CompositionTool tool(&canvas);
        canvas.setActiveTool(&tool);
        showNativeReview(canvas);
        QJsonArray steadyReports;
        const auto checkSteady = [&](const QString &name) {
            QTest::qWait(500);
            const auto before = canvas.compositionStats();
            const int seconds = std::clamp(
                qEnvironmentVariableIntValue("FLOW_PARTICLE_COMPOSITION_SECONDS"), 1, 60);
            QTest::qWait(seconds * 1000);
            const auto after = canvas.compositionStats();
            QVERIFY(after.active);
            QVERIFY(after.renderedFrames > before.renderedFrames + 10);
            QCOMPARE(after.fallbackReadbacks, before.fallbackReadbacks);
            QCOMPARE(after.captureReadbacks, before.captureReadbacks);
            QCOMPARE(after.textureUploads, before.textureUploads);
            steadyReports.append(QJsonObject{
                {"case", name},
                {"seconds", seconds},
                {"frames", qint64(after.renderedFrames - before.renderedFrames)},
                {"fallbackReadbacks", qint64(after.fallbackReadbacks - before.fallbackReadbacks)},
                {"captureReadbacks", qint64(after.captureReadbacks - before.captureReadbacks)},
                {"textureUploads", qint64(after.textureUploads - before.textureUploads)},
                {"devicePixelRatio", canvas.devicePixelRatioF()}});
        };
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        QTest::qWait(1500);
        auto initial = canvas.compositionStats();
        QVERIFY(initial.renderedFrames > 10);
        auto *surface = canvas.findChild<QWidget *>("flowMapComposition");
        QVERIFY(surface);
        QVERIFY(surface->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(surface->focusPolicy(), Qt::NoFocus);
        QTest::mouseClick(canvas.windowHandle(), Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
        QCOMPARE(tool.presses, 1);
        if (canvas.compositionStats().nativeSurface) {
            QWindow *native = nullptr;
            for (auto *window : QGuiApplication::allWindows())
                if (window->objectName() == "flowMapNativeWindow")
                    native = window;
            QVERIFY(native);
            QVERIFY(!native->flags().testFlag(Qt::WindowTransparentForInput));
            QVERIFY(!native->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
            QMouseEvent click(QEvent::MouseButtonPress, QPointF(100, 100),
                              QPointF(canvas.mapToGlobal(QPoint(100, 100))), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(native, &click);
            QCOMPARE(tool.presses, 2);
            const auto original = canvas.extent();
            QPinchGesture pinch;
            pinch.setScaleFactor(1.25);
            pinch.setChangeFlags(QPinchGesture::ScaleFactorChanged);
            pinch.setCenterPoint(canvas.mapToGlobal(QPoint(240, 240)));
            QGestureEvent gesture({&pinch});
            QCoreApplication::sendEvent(native, &gesture);
            QVERIFY(qFuzzyCompare(canvas.extent().width(), original.width() / 1.25));
            canvas.setExtent(original, false);
        }
        QTest::qWait(500);
        initial = canvas.compositionStats();
        QTest::qWait(1000);
        auto ordinary = canvas.compositionStats();
        QVERIFY(ordinary.renderedFrames > initial.renderedFrames + 10);
        QCOMPARE(ordinary.captureReadbacks, initial.captureReadbacks);
        QCOMPARE(ordinary.fallbackReadbacks, initial.fallbackReadbacks);
        QCOMPARE(ordinary.textureUploads, initial.textureUploads);
        const double scale = canvas.width() / canvas.extent().width();
        const auto extent = canvas.extent();
        const QRectF view(extent.xMin(), -extent.yMax(), extent.width(), extent.height());
        {
            style->setAnimated(false);
            auto *timer = canvas.findChild<QTimer *>("flowAnimationTimer");
            QVERIFY(timer);
            QTRY_VERIFY(!timer->isActive());
            QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
            const auto paused = layer.flowParticles(view, scale, layer.frameRevision());
            QVERIFY(paused);
            QTest::qWait(150);
            const auto settled = canvas.compositionStats();
            QTest::qWait(200);
            const auto still = layer.flowParticles(view, scale, layer.frameRevision());
            QVERIFY(still);
            QCOMPARE(still->clock, paused->clock);
            QVERIFY(canvas.compositionStats().renderedFrames <= settled.renderedFrames + 1);
            style->setAnimated(true);
            QTRY_VERIFY(timer->isActive());
        }
        // Finish work already queued for this tick before checking that the
        // capture itself dispatches no new tick or particle lifecycle event.
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        const auto before = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(before);
        QString error;
        const QImage image = canvas.captureMap(&error).toImage();
        QVERIFY2(!image.isNull(), qPrintable(error));
        const auto after = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(after);
        QCOMPARE(after->clock, before->clock);
        QCOMPARE(after->births, before->births);
        QCOMPARE(after->deaths, before->deaths);
        const auto pixel = [&](const QImage &im, int x, int y) {
            return im.pixelColor(qRound(x * im.devicePixelRatio()),
                                 qRound(y * im.devicePixelRatio()));
        };
        QCOMPARE(pixel(image, 20, 20), QColor(10, 220, 30));
        QCOMPARE(pixel(image, 60, 60), QColor(20, 60, 100));
        const QColor blend = pixel(image, 240, 240);
        qInfo() << "Composition capture" << image.size() << image.devicePixelRatio() << "blend"
                << blend;
        QVERIFY2(std::abs(blend.red() - 110) <= 2, qPrintable(blend.name(QColor::HexArgb)));
        QVERIFY(std::abs(blend.green() - 80) <= 2);
        QVERIFY(std::abs(blend.blue() - 50) <= 2);
        QCOMPARE(blend.alpha(), 255);
        auto mask = layer.maskSpec();
        mask.enabled = true;
        layer.setMaskSpec(mask);
        QTRY_VERIFY(canvas.compositionStats().active);
        QVERIFY(layer.qsgOwnsRendering());
        const auto masked = canvas.captureMap(&error);
        QVERIFY2(!masked.isNull(), qPrintable(error));
        QCOMPARE(pixel(masked.toImage(), 240, 240), blend);
        mask.enabled = false;
        layer.setMaskSpec(mask);
        QTRY_VERIFY(canvas.compositionStats().active);
        auto *fill = layer.cellDepthFillSublayer();
        fill->setVisible(true);
        fill->fillStyle()->setColorRampName("");
        fill->fillStyle()->setLowColor(QColor(0, 0, 200));
        fill->fillStyle()->setHighColor(QColor(0, 0, 200));
        fill->fillStyle()->setClassified(false);
        SWMM2DResultsLayer second;
        second.setSource(std::make_unique<FlowSource>());
        second.flowStreakSublayer()->setVisible(true);
        second.flowStreakSublayer()->streakStyle()->setColor(Qt::transparent);
        for (auto *s : second.sublayers())
            if (s != second.flowStreakSublayer())
                s->setVisible(false);
        auto *secondFill = second.cellDepthFillSublayer();
        secondFill->setVisible(true);
        secondFill->fillStyle()->setColorRampName("");
        secondFill->fillStyle()->setClassified(false);
        secondFill->fillStyle()->setLowColor(QColor(200, 0, 0));
        secondFill->fillStyle()->setHighColor(QColor(200, 0, 0));
        second.setOpacity(.25);
        second.setCurrentTimeIndex(1);
        canvas.addLayer(&second, false);
        QTRY_VERIFY(canvas.compositionStats().active);
        canvas.reorderLayers({&raster, &vector, &layer, &second}, false);
        const auto multiple = canvas.captureMap(&error);
        QVERIFY2(!multiple.isNull(), qPrintable(error));
        const auto combined = pixel(multiple.toImage(), 100, 240);
        QVERIFY(std::abs(combined.red() - 50) <= 2);
        QVERIFY(combined.green() <= 2);
        QVERIFY(std::abs(combined.blue() - 150) <= 2);
        QCOMPARE(fill->fillStyle()->lowColor(), QColor(0, 0, 200));
        checkSteady("two-output");
        canvas.takeLayer(canvas.layers().indexOf(&second), false);
        QTRY_VERIFY(canvas.compositionStats().active);
        // CPU bands retain their stack position above the native output.
        const QString folder = qEnvironmentVariable(
            "FLOW_PARTICLE_NATIVE_OUTPUT", "tests/verification/flow_particles/composition");
        QVERIFY(QDir().mkpath(folder));
        QFile polygon(folder + "/mask.geojson");
        QVERIFY(polygon.open(QIODevice::WriteOnly));
        polygon.write(
            R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":{},"geometry":{"type":"Polygon","coordinates":[[[0,0],[50,0],[50,100],[0,100],[0,0]],[[20,40],[30,40],[30,60],[20,60],[20,40]]]}}]})");
        polygon.close();
        GISVectorLayer boundary(polygon.fileName(), {}, &workspace);
        QCOMPARE(boundary.featureCount(), 1);
        mask.enabled = true;
        mask.sourceLayerId = boundary.layerId();
        mask.mode = MaskMode::ClipInside;
        QVERIFY(resolveMaskClip(&layer, mask).ok);
        layer.setMaskSpec(mask);
        QTest::qWait(200);
        QVERIFY(canvas.compositionStats().active);
        const auto inside = canvas.captureMap(&error).toImage();
        QVERIFY2(!inside.isNull(), qPrintable(error));
        QCOMPARE(pixel(inside, 100, 240), QColor(0, 0, 200));   // x=15, inside exterior
        QCOMPARE(pixel(inside, 380, 240), QColor(20, 60, 100)); // x=85, outside exterior
        QCOMPARE(pixel(inside, 140, 240), blend);               // x=25, in the polygon's hole
        QVERIFY(inside.save(folder + "/mask-inside.png"));
        checkSteady("masked");
        layer.setOpacity(.4);
        QTest::qWait(100);
        const auto maskedOpacity = canvas.captureMap(&error).toImage();
        QVERIFY2(!maskedOpacity.isNull(), qPrintable(error));
        const auto maskedBlue = pixel(maskedOpacity, 100, 240);
        QVERIFY(std::abs(maskedBlue.red() - 12) <= 2);
        QVERIFY(std::abs(maskedBlue.green() - 36) <= 2);
        QVERIFY(std::abs(maskedBlue.blue() - 140) <= 2);
        layer.setOpacity(1);
        boundary.setFilterExpression("1=0");
        QVERIFY(!resolveMaskClip(&layer, mask).ok);
        canvas.invalidate(MapCanvas::Scene | MapCanvas::Overlay, "mask-source-filter");
        QTest::qWait(100);
        const auto unmasked = canvas.captureMap(&error).toImage();
        QVERIFY2(!unmasked.isNull(), qPrintable(error));
        QCOMPARE(pixel(unmasked, 380, 240), QColor(0, 0, 200));
        boundary.setFilterExpression("");
        QVERIFY(resolveMaskClip(&layer, mask).ok);
        mask.mode = MaskMode::ClipOutside;
        layer.setMaskSpec(mask);
        QTest::qWait(200);
        const auto outside = canvas.captureMap(&error).toImage();
        QVERIFY2(!outside.isNull(), qPrintable(error));
        QCOMPARE(pixel(outside, 100, 240), QColor(20, 60, 100));
        QCOMPARE(pixel(outside, 380, 240), QColor(0, 0, 200));
        QCOMPARE(pixel(outside, 140, 240), QColor(0, 0, 200));
        mask.enabled = false;
        layer.setMaskSpec(mask);
        canvas.reorderLayers({&raster, &layer, &vector}, false);
        QTRY_VERIFY(canvas.compositionStats().active);
        const auto ordered = canvas.captureMap(&error).toImage();
        QVERIFY2(!ordered.isNull(), qPrintable(error));
        const QColor over = pixel(ordered, 240, 200);
        QVERIFY(std::abs(over.red() - 100) <= 2);
        QVERIFY(std::abs(over.green() - 50) <= 2);
        QVERIFY(std::abs(over.blue() - 100) <= 2);
        QVERIFY(ordered.save(folder + "/cpu-above.png"));
        checkSteady("cpu-above");
        canvas.reorderLayers({&raster, &vector, &layer}, false);
        QTRY_VERIFY(canvas.compositionStats().active);
        layer.setOpacity(.4);
        QTest::qWait(100);
        QVERIFY(canvas.compositionStats().active);
        const auto translucent = canvas.captureMap(&error).toImage();
        QVERIFY2(!translucent.isNull(), qPrintable(error));
        const QColor opacity = pixel(translucent, 240, 200);
        QVERIFY(std::abs(opacity.red() - 66) <= 2);
        QVERIFY(std::abs(opacity.green() - 48) <= 2);
        QVERIFY(std::abs(opacity.blue() - 110) <= 2);
        QVERIFY(canvas.moveSublayer(&layer, 0, int(layer.sublayers().size()) - 1, false));
        QTest::qWait(100);
        QVERIFY(canvas.compositionStats().active);
        QVERIFY(!canvas.captureMap(&error).isNull());
        const auto oldExtent = canvas.extent();
        canvas.zoomAroundCursor(1.2, {240, 240});
        QTest::qWait(150);
        QVERIFY(canvas.extent() != oldExtent);
        QVERIFY(!canvas.captureMap(&error).isNull());
        canvas.pan(5, 3);
        canvas.resize(520, 400);
        QTest::qWait(200);
        QCOMPARE(surface->geometry(), canvas.rect());
        QVERIFY(!canvas.captureMap(&error).isNull());
        canvas.hide();
        QTest::qWait(300);
        const auto hidden = canvas.compositionStats().renderedFrames;
        QTest::qWait(300);
        QCOMPARE(canvas.compositionStats().renderedFrames, hidden);
        canvas.show();
        QTRY_VERIFY(canvas.compositionStats().renderedFrames > hidden);
        qputenv("OPENSWMM_FLOW_COMPOSITION", "0");
        canvas.update();
        QTRY_VERIFY(!canvas.compositionStats().active);
        QVERIFY(!canvas.captureMap(&error).isNull());
        qunsetenv("OPENSWMM_FLOW_COMPOSITION");
        canvas.update();
        QTRY_VERIFY(canvas.compositionStats().active);
        canvas.setActiveTool(nullptr);
        canvas.takeLayer(canvas.layers().indexOf(&layer), false);
        QVERIFY(!layer.qsgOwnsRendering());
        QVERIFY(!canvas.compositionStats().active);
        QVERIFY(!canvas.captureMap(&error).isNull());
        while (canvas.layerCount())
            canvas.takeLayer(0, false);
        QFile report(folder + "/composition.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(steadyReports).toJson());
    }
    /**
     *
     * @brief Require unchanged native water textures to survive particle updates without uploads.
     * Skips without FLOW_PARTICLE_NATIVE_REVIEW. Exercises particle visibility, density,
     * color, alpha, pause/resume and forced scene updates while checking texture-change
     * signals, animation progress and zero background uploads. A hydraulic fill-opacity
     * change must eventually refresh the texture. Requires a functioning exposed GPU
     * composition surface; the borrowed stack layer is detached after verification.
     *
     */
    void nativeStaticWaterCacheIgnoresUnchangedSync()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Native cached-water synchronization");
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        layer.smoothDepthFillSublayer()->setVisible(true);
        ParticleReviewCanvas canvas;
        canvas.resize(640, 480);
        canvas.setShowScaleBar(false);
        canvas.addLayer(&layer, false);
        canvas.setExtent({-10, -10, 110, 110}, false);
        showNativeReview(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        auto *mapWindow = compositionWindow(canvas);
        QVERIFY(mapWindow);
        QQuickItem *water = nullptr;
        for (auto *item : compositionRoot(canvas)->findChildren<QQuickItem *>())
            if (item->property("cacheStaticContent").toBool())
                water = item->property("staticItem").value<QQuickItem *>();
        QVERIFY(water);
        QTest::qWait(500);
        auto *provider = water->textureProvider();
        QVERIFY(provider);
        const auto renderers = water->findChildren<SWMM2DResultsQSGRenderer *>();
        QCOMPARE(renderers.size(), 1);
        QSignalSpy changed(provider, &QSGTextureProvider::textureChanged);
        const auto before = canvas.compositionStats().renderedFrames;
        const auto extent = canvas.extent();
        const QRectF view(extent.xMin(), -extent.yMax(), extent.width(), extent.height());
        const double scale = canvas.width() / extent.width();
        // Starting/stopping particles must preserve the already displayed
        // water too, including the first activation after a static frame.
        layer.flowStreakSublayer()->setVisible(true);
        QTest::qWait(500);
        QCOMPARE(changed.count(), 0);
        layer.flowStreakSublayer()->setVisible(false);
        QTest::qWait(100);
        layer.flowStreakSublayer()->setVisible(true);
        QTest::qWait(100);
        QCOMPARE(changed.count(), 0);
        auto initialFrame = layer.flowParticles(view, scale, layer.frameRevision(), false);
        QVERIFY(initialFrame);
        const double clock = initialFrame->clock;
        initialFrame.reset();
        QTimer request;
        request.setInterval(15);
        connect(&request, &QTimer::timeout, renderers.front(), &QQuickItem::update);
        request.start();
        QTest::qWait(500);
        request.stop();
        const auto current = layer.flowParticles(view, scale, layer.frameRevision(), false);
        QVERIFY(current);
        QVERIFY(current->clock > clock);
        QVERIFY(canvas.compositionStats().renderedFrames > before + 5);
        QCOMPARE(changed.count(), 0);
        const auto uploads = canvas.compositionStats().textureUploads;
        // With no raster/CPU layers or foreground there is nothing to upload:
        // the native clear color already supplies the complete background.
        QCOMPARE(uploads, quint64(0));
        auto *style = layer.flowStreakSublayer()->streakStyle();
        for (double spacing : {28., 14., 2.}) {
            style->setSpacingPx(spacing);
            QTest::qWait(100);
        }
        style->setColor(Qt::red);
        layer.flowStreakSublayer()->setOpacity(.7);
        QTest::qWait(100);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(canvas.compositionStats().textureUploads, uploads);
        style->setAnimated(false);
        auto *timer = canvas.findChild<QTimer *>("flowAnimationTimer");
        QVERIFY(timer);
        QTRY_VERIFY(!timer->isActive());
        QTest::qWait(100);
        QCOMPARE(changed.count(), 0);
        style->setAnimated(true);
        QTRY_VERIFY(timer->isActive());
        canvas.pan(5, 3);
        QTest::qWait(150);
        QCOMPARE(canvas.compositionStats().textureUploads, quint64(0));
        const QColor background(12, 34, 56);
        canvas.setBackgroundColor(background);
        QString error;
        const auto capture = canvas.captureMap(&error).toImage();
        QVERIFY2(!capture.isNull(), qPrintable(error));
        QCOMPARE(capture.pixelColor(10, 10), background);
        QCOMPARE(canvas.compositionStats().textureUploads, quint64(0));
        // Real hydraulic styling still refreshes the water texture.
        layer.smoothDepthFillSublayer()->setOpacity(.4);
        QTRY_VERIFY(changed.count() > 0);
        canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Register all eight live/saved, band/cell and embedded/standalone handoff rows.
     * Columns live, bands and embedded are bools consumed by the paired Qt test.
     *
     */
    void nativeDepthUpdatesNeverPublishWaterWithoutMatchingStreaks_data()
    {
        QTest::addColumn<bool>("live");
        QTest::addColumn<bool>("bands");
        QTest::addColumn<bool>("embedded");
        for (bool live : {false, true})
            for (bool bands : {false, true})
                for (bool embedded : {false, true})
                    QTest::newRow(
                        qPrintable(QString("%1-%2-%3")
                                       .arg(live ? "live" : "saved", bands ? "bands" : "cell",
                                            embedded ? "embedded" : "standalone")))
                        << live << bands << embedded;
    }
    /**
     *
     * @brief Require water and particle revisions to agree at actual scene-graph synchronization.
     * Consumes live/bands/embedded data columns. Skips without FLOW_PARTICLE_NATIVE_REVIEW
     * and requires GPU composition. Monitors afterSynchronizing through a direct render
     * thread connection while the GUI thread is locked; seven hydraulic changes must
     * produce more than seven samples with no missing controller frame or revision
     * mismatch. Scope-guarded disconnection protects captured stack state on failure.
     *
     */
    void nativeDepthUpdatesNeverPublishWaterWithoutMatchingStreaks()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Native depth-frame and particle handoff");
        QFETCH(bool, live);
        QFETCH(bool, bands);
        QFETCH(bool, embedded);
        auto source = std::make_unique<FlowSource>();
        auto *raw = source.get();
        raw->live = live;
        raw->depths = {.25f, .55f, .35f, .65f, .3f, .6f, .4f, .7f};
        SWMM2DResultsLayer layer;
        layer.setAsyncSurface(true);
        layer.setSource(std::move(source));
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        if (bands)
            layer.contourBandSublayer()->setVisible(true);
        else
            layer.cellDepthFillSublayer()->setVisible(true);
        layer.flowStreakSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setSpeedMultiplier(.02);
        QTabWidget host;
        host.resize(640, 480);
        ParticleReviewCanvas canvas;
        canvas.resize(640, 480);
        canvas.setShowScaleBar(false);
        canvas.addLayer(&layer, false);
        canvas.setExtent({-10, -10, 110, 110}, false);
        if (embedded) {
            host.addTab(&canvas, "Map");
            showNativeReview(host);
        } else
            showNativeReview(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        auto *window = compositionWindow(canvas);
        QVERIFY(window);
        auto *root = compositionRoot(canvas);
        QVERIFY(root);
        const auto renderers = root->findChildren<SWMM2DResultsQSGRenderer *>();
        QCOMPARE(renderers.size(), 1);
        auto *renderer = renderers.front();
        auto *controller = layer.findChild<FlowParticleController *>();
        QVERIFY(controller);
        const auto items = root->findChildren<QQuickItem *>("flowParticleItem");
        QCOMPARE(items.size(), 1);
        auto *particles = items.front();
        QTRY_VERIFY_WITH_TIMEOUT(controller->frame(renderer->displayedFrameRevision()) != nullptr,
                                 10000);
        QTRY_COMPARE_WITH_TIMEOUT(particles->property("displayedFrameRevision").toULongLong(),
                                  renderer->displayedFrameRevision(), 10000);
        std::atomic_int samples = 0, missing = 0, mismatched = 0;
        // Qt keeps the GUI thread locked during afterSynchronizing. Inspect
        // the actual scene-graph handoff, before another worker completion
        // can conceal a blank frame. Capture settlement is deliberately absent.
        const auto monitor = connect(
            window, &QQuickWindow::afterSynchronizing, window,
            [&] {
                ++samples;
                if (!controller->frame(renderer->displayedFrameRevision()))
                    ++missing;
                if (particles->property("displayedFrameRevision").toULongLong() !=
                    renderer->displayedFrameRevision())
                    ++mismatched;
            },
            Qt::DirectConnection);
        const auto disconnectMonitor = qScopeGuard([&] { disconnect(monitor); });
        for (int i = 1; i < 8; ++i) {
            const auto previous = layer.frameRevision();
            if (live) {
                raw->depths[0] = raw->depths[size_t(i)];
                ++raw->revision;
                layer.refreshCurrentFrame();
            } else
                layer.setCurrentTimeIndex(i);
            QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision() != previous, 5000);
            QTRY_COMPARE_WITH_TIMEOUT(renderer->displayedFrameRevision(), layer.frameRevision(),
                                      5000);
            QTest::qWait(50);
        }
        QVERIFY(samples.load() > 7);
        QCOMPARE(missing.load(), 0);
        QCOMPARE(mismatched.load(), 0);
        disconnect(monitor);
        canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Register eight waterStyle/opacity rows for native versus CPU fill comparison.
     * waterStyle is smooth, cell, flat-bands or smooth-bands; opacity is 1 or 0.6.
     *
     */
    void nativeIndexedWaterMatchesCpu_data()
    {
        QTest::addColumn<QString>("waterStyle");
        QTest::addColumn<double>("opacity");
        for (const char *style : {"smooth", "cell", "flat-bands", "smooth-bands"})
            for (double opacity : {1., .6})
                QTest::newRow(
                    qPrintable(QString::fromLatin1(style) + (opacity == 1 ? "" : "-alpha")))
                    << QString::fromLatin1(style) << opacity;
    }
    /**
     *
     * @brief Compare native indexed fill with CPU triangle interiors across index-buffer chunks.
     * Consumes waterStyle/opacity data, a 250-by-200 synthetic grid and two depth frames.
     * Skips without FLOW_PARTICLE_NATIVE_REVIEW; requires GPU rendering and writable
     * FLOW_PARTICLE_NATIVE_OUTPUT or the indexed-water verification default. Checks RGB
     * within three units, expected background blending, seamless native interior and
     * distinct frame colors. Saves four PNGs and indexed-water.json per data row;
     * report writes are not checked for short writes. Detaches/depopulates the layer.
     *
     */
    void nativeIndexedWaterMatchesCpu()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Native indexed-water pixel comparison");
        QFETCH(QString, waterStyle);
        QFETCH(double, opacity);
        const QString folder =
            qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT",
                                 "tests/verification/flow_particles/indexed-water") +
            "/" + waterStyle + (opacity == 1 ? "" : "-alpha");
        QVERIFY(QDir().mkpath(folder));
        // Cross several uint16 chunks. A zero discharge leaves an exactly
        // horizontal surface, so every interior pixel must have the same color.
        auto source = std::make_unique<GridFlowSource>(250, 200);
        source->speed = 0;
        source->depths = {.26f, .51f, .26f};
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(source));
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        if (waterStyle.endsWith("bands")) {
            auto *fill = layer.contourBandSublayer();
            fill->setVisible(true);
            fill->setOpacity(opacity);
            auto *style = fill->bandStyle();
            style->setSmoothBands(waterStyle == "smooth-bands");
            style->setUseCustomRange(true);
            style->setRangeMin(0);
            style->setRangeMax(1);
            style->setBandCount(7);
            style->setColorRampName(QString());
            style->setLowColor(Qt::blue);
            style->setHighColor(Qt::red);
        } else {
            auto *fill = waterStyle == "smooth"
                             ? static_cast<ISublayer *>(layer.smoothDepthFillSublayer())
                             : static_cast<ISublayer *>(layer.cellDepthFillSublayer());
            fill->setVisible(true);
            fill->setOpacity(opacity);
            auto *style = waterStyle == "smooth" ? layer.smoothDepthFillSublayer()->fillStyle()
                                                 : layer.cellDepthFillSublayer()->fillStyle();
            style->setUseCustomRange(true);
            style->setRangeMin(0);
            style->setRangeMax(1);
            style->setClassified(false);
            style->setColorRampName(QString());
            style->setLowColor(Qt::blue);
            style->setHighColor(Qt::red);
        }
        ParticleReviewCanvas canvas;
        canvas.resize(640, 480);
        canvas.setShowScaleBar(false);
        canvas.addLayer(&layer, false);
        canvas.setExtent({-10, -10, 260, 210}, false);
        showNativeReview(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        const auto extent = canvas.extent();
        QVector<QImage> native;
        for (int frame : {0, 1}) {
            layer.setCurrentTimeIndex(frame);
            QTest::qWait(150);
            QString error;
            native.push_back(canvas.captureMap(&error).toImage());
            QVERIFY2(!native.back().isNull(), qPrintable(error));
            QVERIFY(native.back().save(folder + QString("/water-native-%1.png").arg(frame)));
        }
        canvas.takeLayer(0, false);
        QVERIFY(!layer.qsgOwnsRendering());
        QGraphicsScene scene;
        layer.populateScene(&scene, extent, nullptr);
        const QRectF view(extent.xMin(), -extent.yMax(), extent.width(), extent.height());
        QJsonArray reports;
        for (int frame : {0, 1}) {
            layer.setCurrentTimeIndex(frame);
            QImage cpu(native[frame].size(), QImage::Format_ARGB32_Premultiplied);
            cpu.fill(Qt::white);
            QPainter painter(&cpu);
            painter.setRenderHint(QPainter::Antialiasing);
            scene.render(&painter, QRectF(cpu.rect()), view, Qt::IgnoreAspectRatio);
            painter.end();
            QVERIFY(cpu.save(folder + QString("/water-cpu-%1.png").arg(frame)));
            int samples = 0, different = 0, maximum = 0;
            for (int y = 1; y < 200; ++y)
                for (int x = 1; x < 250; ++x) {
                    // Cell vertices lie on the CPU painter's separately
                    // antialiased polygon edges. Compare triangle interiors.
                    const int px = qFloor((x + .75 - extent.xMin()) / extent.width() * cpu.width());
                    const int py =
                        qFloor((extent.yMax() - y - .25) / extent.height() * cpu.height());
                    const auto a = native[frame].pixelColor(px, py), b = cpu.pixelColor(px, py);
                    // Also reject two identically empty captures.
                    QVERIFY(a.red() + a.blue() > 200);
                    QVERIFY(std::abs(a.green() - qRound((1 - opacity) * 255)) <= 2);
                    QCOMPARE(a.alpha(), 255);
                    const int delta =
                        std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
                                  std::abs(a.blue() - b.blue())});
                    ++samples;
                    different += delta > 3;
                    maximum = std::max(maximum, delta);
                }
            reports.append(QJsonObject{{"frame", frame},
                                       {"samples", samples},
                                       {"different", different},
                                       {"maximumDelta", maximum}});
            qInfo() << "Indexed water comparison" << frame << samples << different << maximum;
            QCOMPARE(different, 0);
            // Independently require seamless GPU coverage, including shared
            // edges where the CPU's polygon antialiasing leaves pale seams.
            const QColor expected = native[frame].pixelColor(cpu.width() / 2, cpu.height() / 2);
            const int left = qCeil((1 - extent.xMin()) / extent.width() * cpu.width());
            const int right = qFloor((249 - extent.xMin()) / extent.width() * cpu.width());
            const int top = qCeil((extent.yMax() - 199) / extent.height() * cpu.height());
            const int bottom = qFloor((extent.yMax() - 1) / extent.height() * cpu.height());
            for (int py = top; py <= bottom; ++py)
                for (int px = left; px <= right; ++px) {
                    const auto color = native[frame].pixelColor(px, py);
                    QVERIFY(std::max({std::abs(color.red() - expected.red()),
                                      std::abs(color.green() - expected.green()),
                                      std::abs(color.blue() - expected.blue())}) <= 3);
                }
        }
        QVERIFY(native[0].pixelColor(native[0].width() / 2, native[0].height() / 2) !=
                native[1].pixelColor(native[1].width() / 2, native[1].height() / 2));
        layer.depopulateScene(&scene);
        QFile report(folder + "/indexed-water.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(reports).toJson());
    }
    /**
     *
     * @brief Require native foreground pixels to match CPU fallback at two pointer positions.
     * Skips without FLOW_PARTICLE_NATIVE_REVIEW and requires exposed GPU composition.
     * Checks coordinate/elevation foreground RGB within three units with animation
     * paused. Saves native/fallback PNGs under FLOW_PARTICLE_NATIVE_OUTPUT or the
     * foreground verification default. Restores the prior OPENSWMM_FLOW_COMPOSITION
     * environment value through a scope guard and detaches borrowed layers.
     *
     */
    void nativeForegroundMatchesFallback()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Native foreground comparison");
        const QString folder = qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT",
                                                    "tests/verification/flow_particles/foreground");
        QVERIFY(QDir().mkpath(folder));
        const auto original = qgetenv("OPENSWMM_FLOW_COMPOSITION");
        const auto restore = qScopeGuard([&] {
            if (original.isNull())
                qunsetenv("OPENSWMM_FLOW_COMPOSITION");
            else
                qputenv("OPENSWMM_FLOW_COMPOSITION", original);
        });
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        sub->streakStyle()->setColor(Qt::transparent);
        sub->streakStyle()->setAnimated(false);
        CompositionBackdrop raster(true);
        ParticleReviewCanvas canvas;
        canvas.resize(480, 360);
        canvas.addLayer(&raster, false);
        canvas.addLayer(&layer, false);
        canvas.setExtent({-10, -10, 110, 110}, false);
        canvas.setShowCoordinates(true);
        canvas.setTerrainElevation(46.);
        showNativeReview(canvas);
        QTRY_VERIFY(canvas.compositionStats().active);
        QTest::qWait(500);
        for (const QPoint position : {QPoint(100, 100), QPoint(350, 200)}) {
            QMouseEvent move(QEvent::MouseMove, QPointF(position),
                             QPointF(canvas.mapToGlobal(position)), Qt::NoButton, Qt::NoButton,
                             Qt::NoModifier);
            QCoreApplication::sendEvent(&canvas, &move);
            QTest::qWait(100);
            QString error;
            const auto native =
                canvas.captureMap(&error).toImage().convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(!native.isNull(), qPrintable(error));
            QVERIFY(native.save(folder + QString("/native-%1.png").arg(position.x())));
            qputenv("OPENSWMM_FLOW_COMPOSITION", "0");
            canvas.update();
            QTRY_VERIFY(!canvas.compositionStats().active);
            const auto fallback =
                canvas.captureMap(&error).toImage().convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(!fallback.isNull(), qPrintable(error));
            QCOMPARE(native.size(), fallback.size());
            QVERIFY(fallback.save(folder + QString("/fallback-%1.png").arg(position.x())));
            int different = 0, maximum = 0;
            for (int y = 0; y < native.height(); ++y)
                for (int x = 0; x < native.width(); ++x) {
                    const auto a = native.pixel(x, y), b = fallback.pixel(x, y);
                    const int delta =
                        std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)),
                                  std::abs(qBlue(a) - qBlue(b))});
                    different += delta > 3;
                    maximum = std::max(maximum, delta);
                }
            qInfo() << "Foreground comparison" << position << "different pixels" << different
                    << "maximum delta" << maximum;
            QVERIFY2(different == 0, "Native foreground differs from the directly painted map");
            qunsetenv("OPENSWMM_FLOW_COMPOSITION");
            canvas.update();
            QTRY_VERIFY(canvas.compositionStats().active);
        }
        while (canvas.layerCount())
            canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Run a 30-second native particle presentation review and write diagnostic evidence.
     * Skips without FLOW_PARTICLE_NATIVE_REVIEW. Uses FLOW_STREAK_RESULT's last HDF5
     * frame or a synthetic 400-by-100 grid. FLOW_PARTICLE_DEFAULTS keeps default style;
     * otherwise review lifetimes/trail/speed/spacing apply. FLOW_PARTICLE_NATIVE_SPEED,
     * _TRAIL, _MIN_LIFE, _MAX_LIFE and _COLOR override the corresponding style values
     * (time settings are seconds). Requires exposed GPU rendering and a nonempty saved
     * source when supplied. Saves native.png/native.json under FLOW_PARTICLE_NATIVE_OUTPUT
     * or the native-layer default. Checks formed trails, births, invalid/conflict stops
     * and visible particle pixels; reported Qt render/swap intervals are diagnostic,
     * not assertions of OS display cadence. JSON short writes are not checked.
     *
     */
    void continuousNativePresentation()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Run the native layer comparison window");
        const QString folder = qEnvironmentVariable(
            "FLOW_PARTICLE_NATIVE_OUTPUT", "tests/verification/flow_particles/native-layer");
        QDir().mkpath(folder);
        SWMM2DResultsLayer layer;
        const QString path = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (path.isEmpty())
            layer.setSource(std::make_unique<GridFlowSource>(400, 100));
        else {
            auto source = std::make_unique<HDF5Mesh2DSource>();
            QVERIFY(source->open(path));
            layer.setSource(std::move(source));
            layer.setCurrentTimeIndex(layer.source()->timeCount() - 1);
        }
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_DEFAULTS")) {
            style->setMinimumLifetimeSeconds(20);
            style->setMaximumLifetimeSeconds(40);
            style->setTrailDurationSeconds(1);
            style->setSpeedMultiplier(2);
            style->setSpacingPx(30);
        }
        bool customSpeed = false;
        const double speed =
            qEnvironmentVariable("FLOW_PARTICLE_NATIVE_SPEED").toDouble(&customSpeed);
        if (customSpeed)
            style->setSpeedMultiplier(speed);
        for (const auto &setting :
             {qMakePair("FLOW_PARTICLE_NATIVE_TRAIL", "trailDurationSeconds"),
              qMakePair("FLOW_PARTICLE_NATIVE_MIN_LIFE", "minimumLifetimeSeconds"),
              qMakePair("FLOW_PARTICLE_NATIVE_MAX_LIFE", "maximumLifetimeSeconds")}) {
            if (!qEnvironmentVariableIsSet(setting.first))
                continue;
            bool valid = false;
            const double value = qEnvironmentVariable(setting.first).toDouble(&valid);
            QVERIFY(valid);
            style->setProperty(setting.second, value);
        }
        if (qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_COLOR"))
            style->setColor(QColor(qEnvironmentVariable("FLOW_PARTICLE_NATIVE_COLOR")));
        layer.meshNodeSublayer()->setVisible(false);
        const auto box = layer.m_sceneBBox;
        ParticleReviewCanvas canvas;
        canvas.setWindowTitle("Continuous particles — native SWMM layer preview");
        canvas.setWindowFlags(canvas.windowFlags() | Qt::WindowTransparentForInput |
                              Qt::WindowDoesNotAcceptFocus);
        canvas.resize(1280, 800);
        canvas.addLayer(&layer, false);
        canvas.setExtent({box.left(), -box.bottom(), box.right(), -box.top()}, false);
        const auto extent = canvas.extent();
        const QRectF view(extent.xMin(), -extent.yMax(), extent.width(), extent.height());
        const double scale = canvas.width() / extent.width();
        QElapsedTimer startup;
        startup.start();
        showNativeReview(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(layer.flowParticles(view, scale, layer.frameRevision()) != nullptr,
                                 10000);
        const double startupMs = startup.nsecsElapsed() / 1e6;
        auto initial = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(initial);
        const int initialParticles = int(initial->particles.size());
        int initialTrails = 0;
        double youngest = INFINITY, oldest = 0;
        for (const auto &p : initial->particles) {
            initialTrails += p.history.size() > 1;
            youngest = std::min(youngest, p.age);
            oldest = std::max(oldest, p.age);
        }
        QVERIFY(initialTrails > 0);
        QVERIFY(initial->clock >= 2 - 1e-8);
        initial.reset(); // Do not retain an extra publication throughout the cadence measurement.
        QTest::qWait(1500);
        QVERIFY(layer.qsgOwnsRendering());
        QElapsedTimer elapsed;
        elapsed.start();
        QTest::qWait(30000);
        const auto frame = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(frame);
        QVERIFY(frame->births > 10);
        QVERIFY(frame->clock > 1);
        QCOMPARE(frame->stopCounts[size_t(FlowParticleStop::VectorConflict)], quint64(0));
        QCOMPARE(frame->stopCounts[size_t(FlowParticleStop::InvalidField)], quint64(0));
        QString error;
        const auto image = canvas.captureMap(&error);
        if (image.isNull()) {
            qInfo() << "Native capture pending:" << layer.flowStreakStatus() << "particle busy"
                    << layer.flowStreakBusy() << "layer revision" << layer.frameRevision();
            for (auto *widget : canvas.findChildren<QQuickWidget *>())
                if (auto *root = widget->rootObject())
                    for (auto *renderer : root->findChildren<SWMM2DResultsQSGRenderer *>())
                        qInfo() << widget->objectName() << "capture ready"
                                << renderer->captureReady() << "displayed revision"
                                << renderer->displayedFrameRevision();
        }
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(image.save(folder + "/native.png"));
        const auto composition = canvas.compositionStats();
        const auto particlePixels = visibleParticlePixels(canvas, layer, image.toImage(), &error);
        QVERIFY2(particlePixels >= 0, qPrintable(error));
        if (style->color().alpha() > 0 || style->colorByMagnitude())
            QVERIFY2(particlePixels > 20, "No visible particle pixels in the native map capture");
        std::sort(canvas.paintMs.begin(), canvas.paintMs.end());
        const auto percentile = [&](double q) {
            return canvas.paintMs.empty() ? 0.
                                          : canvas.paintMs[size_t(q * (canvas.paintMs.size() - 1))];
        };
        QJsonArray stops;
        for (auto count : frame->stopCounts)
            stops.append(qint64(count));
        int waiting = 0, limited = 0, crossings = 0, maxCrossings = 0, reducedSegments = 0;
        size_t history = 0, peakHistory = 0;
        double maxError = 0;
        for (const auto &p : frame->particles) {
            waiting += p.waitingForData;
            limited += p.historyLimited;
            crossings += p.crossings;
            maxCrossings = std::max(maxCrossings, p.crossings);
            history += p.history.size();
            peakHistory = std::max(peakHistory, p.history.size());
            for (const auto &sample : p.history)
                if (sample.error >= 0) {
                    ++reducedSegments;
                    maxError = std::max(maxError, sample.error);
                }
        }
        auto intervals = composition.renderIntervalsMs;
        std::sort(intervals.begin(), intervals.end());
        const auto renderPercentile = [&](double q) {
            return intervals.isEmpty() ? 0. : intervals[int(q * (intervals.size() - 1))];
        };
        auto swaps = composition.swapIntervalsMs;
        std::sort(swaps.begin(), swaps.end());
        const auto swapPercentile = [&](double q) {
            return swaps.isEmpty() ? 0. : swaps[int(q * (swaps.size() - 1))];
        };
        const auto diagnostics = layer.flowParticleDiagnostics();
        std::vector<double> speeds;
        for (const auto &p : frame->particles)
            if (p.stop == FlowParticleStop::None && p.history.size() > 1) {
                const auto &a = p.history[p.history.size() - 2], &b = p.history.back();
                if (b.age > a.age)
                    speeds.push_back(
                        std::hypot((b.position - a.position).x(), (b.position - a.position).y()) *
                        scale / (b.age - a.age));
            }
        std::sort(speeds.begin(), speeds.end());
        QFile report(folder + "/native.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(
            QJsonDocument(
                QJsonObject{
                    {"source", path.isEmpty() ? "synthetic native SWMM" : path},
                    {"triangles", layer.m_sceneTris.size()},
                    {"births", qint64(frame->births)},
                    {"deaths", qint64(frame->deaths)},
                    {"stopCounts", stops},
                    {"animationSeconds", frame->clock},
                    {"observedWallSeconds", elapsed.elapsed() / 1000.},
                    {"waitingForData", waiting},
                    {"historyLimited", limited},
                    {"cellCrossings", crossings},
                    {"maxParticleCrossings", maxCrossings},
                    {"historySamples", qint64(history)},
                    {"speedMultiplier", style->speedMultiplier()},
                    {"reducedSegments", reducedSegments},
                    {"maxParticleHistorySamples", qint64(peakHistory)},
                    {"maxRetainedErrorPx", maxError * scale},
                    {"trailSeconds", style->trailDurationSeconds()},
                    {"minimumLifetimeSeconds", style->minimumLifetimeSeconds()},
                    {"maximumLifetimeSeconds", style->maximumLifetimeSeconds()},
                    {"paintMedianMs", percentile(.5)},
                    {"paintP95Ms", percentile(.95)},
                    {"compositionActive", composition.active},
                    {"renderedFrames", qint64(composition.renderedFrames)},
                    {"visibleParticlePixels", particlePixels},
                    {"nativeSurface", composition.nativeSurface},
                    {"textureUploads", qint64(composition.textureUploads)},
                    {"captureReadbacks", qint64(composition.captureReadbacks)},
                    {"fallbackReadbacks", qint64(composition.fallbackReadbacks)},
                    {"renderIntervalMedianMs", renderPercentile(.5)},
                    {"renderIntervalP95Ms", renderPercentile(.95)},
                    {"frameSwappedSamples", swaps.size()},
                    {"frameSwappedMedianMs",
                     swaps.isEmpty() ? QJsonValue() : QJsonValue(swapPercentile(.5))},
                    {"frameSwappedP95Ms",
                     swaps.isEmpty() ? QJsonValue() : QJsonValue(swapPercentile(.95))},
                    {"wetFraction", diagnostics.wetFraction},
                    {"referenceVelocitySI", diagnostics.referenceSpeedSI},
                    {"motionGain", diagnostics.motionGain},
                    {"targetPopulation", diagnostics.targetPopulation},
                    {"motionExponent", diagnostics.speedExponent},
                    {"referencePixelsPerSecond", diagnostics.referencePixelsPerSecond},
                    {"status", layer.flowStreakStatus()},
                    {"unresolvedOrbits", qint64(frame->unresolvedOrbits)},
                    {"birthAttempts", qint64(diagnostics.birthAttempts)},
                    {"failedBirths", qint64(diagnostics.failedBirths)},
                    {"firstParticleFrameMs", startupMs},
                    {"prewarmWorkMs", diagnostics.prewarmMs},
                    {"initialParticles", initialParticles},
                    {"initialFormedTrails", initialTrails},
                    {"initialYoungestAge", youngest},
                    {"initialOldestAge", oldest},
                    {"publicationMs", diagnostics.publicationMs},
                    {"integrationMs", diagnostics.integrationMs},
                    {"medianParticlePixelsPerSecond",
                     speeds.empty() ? 0. : speeds[speeds.size() / 2]},
                    {"presentation",
                     "Qt Quick render-completion notifications; not OS display presentation; paint costs are separate"}})
                .toJson());
        canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Bound individual trail brightness discontinuity across saved-result publications.
     * Skips unless FLOW_PARTICLE_NATIVE_REVIEW and FLOW_STREAK_RESULT are provided.
     * Requires a readable nonempty Bellinge-style HDF5 result, exposed QQuickWindow and
     * writable FLOW_PARTICLE_NATIVE_OUTPUT or handoffs default. Advances 120 ticks at
     * 1/30 s and samples up to three eligible matched particles per changed publication;
     * requires more than 30 comparisons and maximum integrated-red brightness change
     * below 20 percent. Writes worst-before/after PNGs and JSON histories/reports;
     * this measures aggregate brightness, not a per-pixel geometric equality.
     *
     */
    void nativeBellingeParticleHandoffs()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Native saved-result particle handoffs");
        const QString path = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (path.isEmpty())
            QSKIP("Requires the Bellinge saved result");
        const QString folder = qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT",
                                                    "tests/verification/flow_particles/handoffs");
        QVERIFY(QDir().mkpath(folder));
        SWMM2DResultsLayer layer;
        layer.setAsyncSurface(true);
        auto source = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(source->open(path));
        layer.setSource(std::move(source));
        layer.setCurrentTimeIndex(layer.source()->timeCount() - 1);
        layer.flowStreakSublayer()->setVisible(true);
        const auto view = layer.m_sceneBBox;
        const double scale = 1280 / view.width();
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        QQuickWindow window;
        window.setColor(Qt::black);
        window.resize(320, 240);
        auto *preview = new ParticleHandoffPreview(window.contentItem());
        preview->scale = scale;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        const auto brightness = [](const QImage &image) {
            double value = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    value += qRed(image.pixel(x, y));
            return value;
        };
        double maximum = 0;
        QJsonArray samples;
        int comparisons = 0;
        for (int tick = 0; tick < 120; ++tick) {
            const auto before = layer.flowParticles(view, scale, layer.frameRevision());
            QVERIFY(before);
            layer.advanceFlowStreaks(1. / 30.);
            QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
            const auto after = layer.flowParticles(view, scale, layer.frameRevision());
            QVERIFY(after);
            if (after == before)
                continue;
            const double alpha = after->clock > after->previousClock
                                     ? std::clamp((before->clock - after->previousClock) /
                                                      (after->clock - after->previousClock),
                                                  0., 1.)
                                     : 0;
            int tested = 0;
            for (const auto &p : before->particles) {
                if (p.history.size() < 3 || p.stop != FlowParticleStop::None || (p.id + tick) % 7)
                    continue;
                const auto it = std::find_if(after->particles.begin(), after->particles.end(),
                                             [&](const auto &q) { return q.id == p.id; });
                if (it == after->particles.end())
                    continue;
                auto oldFrame = std::make_shared<FlowParticleFrame>();
                oldFrame->clock = before->clock;
                oldFrame->previousClock = before->previousClock;
                oldFrame->particles.push_back(p);
                auto newFrame = std::make_shared<FlowParticleFrame>();
                newFrame->clock = after->clock;
                newFrame->previousClock = after->previousClock;
                newFrame->particles.push_back(*it);
                preview->appearance.anchor = p.position;
                preview->frame = oldFrame;
                preview->interpolation = 1;
                preview->update();
                const auto oldImage = window.grabWindow();
                preview->frame = newFrame;
                preview->interpolation = alpha;
                preview->update();
                const auto newImage = window.grabWindow();
                const double total = brightness(oldImage);
                if (total < 3000)
                    continue;
                const double change = std::abs(brightness(newImage) - total) / total;
                ++comparisons;
                samples.append(QJsonObject{{"tick", tick},
                                           {"particle", qint64(p.id)},
                                           {"brightnessChange", change},
                                           {"oldSamples", int(p.history.size())},
                                           {"newSamples", int(it->history.size())},
                                           {"historyLimited", it->historyLimited},
                                           {"oldClock", before->clock},
                                           {"newClock", after->clock},
                                           {"newPreviousClock", after->previousClock},
                                           {"interpolation", alpha},
                                           {"oldAge", p.age},
                                           {"newPreviousAge", it->previousAge},
                                           {"newAge", it->age}});
                if (change > maximum) {
                    maximum = change;
                    QVERIFY(oldImage.save(folder + "/worst-before.png"));
                    QVERIFY(newImage.save(folder + "/worst-after.png"));
                    QJsonArray oldPoints, newPoints;
                    for (const auto &s : p.history)
                        oldPoints.append(QJsonArray{s.position.x(), s.position.y(), s.age});
                    for (const auto &s : it->history)
                        newPoints.append(QJsonArray{s.position.x(), s.position.y(), s.age});
                    QFile details(folder + "/worst.json");
                    QVERIFY(details.open(QIODevice::WriteOnly));
                    details.write(QJsonDocument(QJsonObject{{"scale", scale},
                                                            {"oldBrightness", total},
                                                            {"newBrightness", brightness(newImage)},
                                                            {"oldHistory", oldPoints},
                                                            {"newHistory", newPoints},
                                                            {"oldAge", p.age},
                                                            {"newPreviousAge", it->previousAge},
                                                            {"newAge", it->age}})
                                      .toJson());
                }
                if (++tested >= 3)
                    break;
            }
        }
        QFile report(folder + "/handoffs.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(QJsonDocument(QJsonObject{{"comparisons", comparisons},
                                               {"maximumBrightnessChange", maximum},
                                               {"samples", samples}})
                         .toJson());
        report.close();
        QVERIFY(comparisons > 30);
        QVERIFY2(maximum < .2,
                 qPrintable(QString("Individual trail brightness jumps by %1% at publication")
                                .arg(100 * maximum)));
    }
    /**
     *
     * @brief Run an opt-in macOS native-water/particle process-footprint soak.
     * Requires positive FLOW_PARTICLE_SOAK_SECONDS, FLOW_PARTICLE_NATIVE_REVIEW, macOS,
     * a nonempty readable FLOW_STREAK_RESULT and exposed native composition. Duration
     * must fit the millisecond timer integer. FLOW_PARTICLE_SOAK_WATER_ONLY disables
     * particles; FLOW_PARTICLE_SOAK_WATER_STYLE is smooth or bands. Samples process
     * physical footprint every 250 ms while changing view, density and report time.
     * Checks no readbacks, at most 256 MiB peak growth over water baseline and, for
     * durations at least 120 s, at most 32 MiB late-median growth. Particle runs require
     * more than ten rendered frames per requested second. FLOW_PARTICLE_CANONICAL also
     * requires 1920-by-1080 logical pixels at DPR two throughout sampled steps.
     * Writes memory-samples.jsonl/memory-soak.json under FLOW_PARTICLE_NATIVE_OUTPUT or
     * soak default. FLOW_PARTICLE_SOAK_ALLOCATION_DIAGNOSTIC enables ready/continue marker
     * rendezvous (120-s timeouts); such paused inspection runs are diagnostic evidence.
     * Only durations at least 600 s exercise the named ten-minute gate.
     *
     */
    void continuousNativeMemorySoak()
    {
        const bool waterOnly = qEnvironmentVariableIsSet("FLOW_PARTICLE_SOAK_WATER_ONLY");
        const int seconds = qEnvironmentVariableIntValue("FLOW_PARTICLE_SOAK_SECONDS");
        if (seconds <= 0)
            QSKIP("Set FLOW_PARTICLE_SOAK_SECONDS=600 for the ten-minute native memory gate");
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW"))
            QSKIP("Requires native graphics");
#ifndef Q_OS_MACOS
        QSKIP(
            "This memory gate requires macOS process footprint, including graphics allocations; native rendering checks are portable");
#endif
        const QString path = qEnvironmentVariable("FLOW_STREAK_RESULT");
        QVERIFY(!path.isEmpty());
        const QString folder = qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT",
                                                    "tests/verification/flow_particles/soak");
        QVERIFY(QDir().mkpath(folder));
        const auto memory = []() -> qint64 {
#ifdef Q_OS_MACOS
            task_vm_info_data_t info{};
            mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
            if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info),
                          &count) == KERN_SUCCESS)
                return qint64(info.phys_footprint);
#endif
            return -1;
        };
        SWMM2DResultsLayer layer;
        layer.setAsyncSurface(true);
        auto source = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(source->open(path));
        layer.setSource(std::move(source));
        layer.setCurrentTimeIndex(layer.source()->timeCount() - 1);
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        const QString waterStyle = qEnvironmentVariable("FLOW_PARTICLE_SOAK_WATER_STYLE", "smooth");
        if (waterStyle == "bands")
            layer.contourBandSublayer()->setVisible(true);
        else {
            QCOMPARE(waterStyle, QString("smooth"));
            layer.smoothDepthFillSublayer()->setVisible(true);
        }
        ParticleReviewCanvas canvas;
        canvas.resize(1920, 1080);
        canvas.setShowScaleBar(false);
        canvas.setWindowFlags(canvas.windowFlags() | Qt::WindowTransparentForInput |
                              Qt::WindowDoesNotAcceptFocus);
        canvas.addLayer(&layer, false);
        const auto bounds = layer.m_sceneBBox;
        canvas.setExtent({bounds.left(), -bounds.bottom(), bounds.right(), -bounds.top()}, false);
        showNativeReview(canvas);
        // Match saved/live layers created by SWMMVis: background water fits,
        // with the initial requested frame fully displayed before the baseline.
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        const auto initialExtent = canvas.extent();
        const QRectF initialView(initialExtent.xMin(), -initialExtent.yMax(), initialExtent.width(),
                                 initialExtent.height());
        QVERIFY(layer.prepareFlowStreaksForCapture(initialView,
                                                   canvas.width() / initialExtent.width()));
        QCOMPARE(layer.currentTimeIndex(), layer.source()->timeCount() - 1);
        QQuickWindow *mapWindow = nullptr;
        for (auto *window : QGuiApplication::allWindows())
            if (window->objectName() == "flowMapNativeWindow")
                mapWindow = qobject_cast<QQuickWindow *>(window);
        QVERIFY(mapWindow);
        const auto renderers = mapWindow->contentItem()->findChildren<SWMM2DResultsQSGRenderer *>();
        QCOMPARE(renderers.size(), 1);
        auto *displayed = renderers.front();
        QTRY_VERIFY_WITH_TIMEOUT(displayed->captureReady() &&
                                     displayed->displayedFrameRevision() == layer.frameRevision(),
                                 30000);
        const quint64 baselineRevision = layer.frameRevision();
        // Optional inspector rendezvous. Heap inspection pauses the process;
        // these runs are diagnostics, never clean memory/cadence evidence.
        const bool allocationDiagnostic =
            qEnvironmentVariableIsSet("FLOW_PARTICLE_SOAK_ALLOCATION_DIAGNOSTIC");
        const auto inspectPhase = [&](const QString &phase) {
            if (!allocationDiagnostic)
                return true;
            QFile marker(folder + "/" + phase + "-ready");
            if (!marker.open(QIODevice::WriteOnly))
                return false;
            marker.close();
            QElapsedTimer wait;
            wait.start();
            while (!QFileInfo::exists(folder + "/" + phase + "-continue") &&
                   wait.elapsed() < 120000)
                QTest::qWait(50);
            return wait.elapsed() < 120000;
        };
        QTest::qWait(3000);
        QVERIFY(inspectPhase("water-only"));
        const qint64 baseline = memory();
        QVERIFY(baseline > 0);
        layer.flowStreakSublayer()->setVisible(!waterOnly);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        QTest::qWait(3000);
        QVERIFY(inspectPhase("with-particles"));
        const auto initialStats = canvas.compositionStats();
        QElapsedTimer elapsed;
        elapsed.start();
        QJsonArray samples;
        qint64 peak = baseline;
        QJsonObject peakSample;
        QFile progress(folder + "/memory-samples.jsonl");
        QVERIFY(progress.open(QIODevice::WriteOnly));
        int previousStep = -1;
        QVector<qint64> early, late;
        bool canonical = true;
        QEventLoop soak;
        QTimer sampleTimer;
        sampleTimer.setInterval(250);
        connect(&sampleTimer, &QTimer::timeout, &soak, [&] {
            const qint64 bytes = memory();
            if (bytes > peak) {
                peak = bytes;
                peakSample = {
                    {"seconds", elapsed.elapsed() / 1000.},
                    {"physicalFootprintBytes", bytes},
                    {"fieldWorkingUpperBoundBytes", qint64(layer.flowParticleFieldBytes())},
                    {"waterAuxiliaryBytes", qint64(layer.surfaceAuxiliaryBytes())},
                    {"waterWorkingBytes", qint64(layer.surfaceWorkingBytes())},
                    {"waterVisibilityBytes", qint64(layer.waterVisibilityBytes())},
                    {"contourWorkingBytes", qint64(displayed->contourWorkingBytes())},
                    {"waterAttributeCopyBytes", qint64(displayed->retainedWaterAttributeBytes())},
                    {"waterFits", layer.surfaceFitCount()},
                    {"waterBusy", layer.surfaceBusy()},
                    {"displayedWaterRevision", qint64(displayed->displayedFrameRevision())}};
                progress.write(QJsonDocument(QJsonObject{{"newPeak", peakSample}})
                                   .toJson(QJsonDocument::Compact) +
                               "\n");
                progress.flush();
            }
            const int step = int(elapsed.elapsed() / 5000);
            if (step != previousStep) {
                previousStep = step;
                const double zoom = step % 3 == 0 ? 1 : step % 3 == 1 ? .75 : .5;
                const QPointF center =
                    bounds.center() + QPointF((step % 2 ? 1 : -1) * bounds.width() * .08,
                                              (step % 4 < 2 ? 1 : -1) * bounds.height() * .08);
                const double w = bounds.width() * zoom, h = bounds.height() * zoom;
                canvas.setExtent({center.x() - w / 2, -center.y() - h / 2, center.x() + w / 2,
                                  -center.y() + h / 2},
                                 false);
                const double spacings[] = {40, 14, 2};
                layer.flowStreakSublayer()->streakStyle()->setSpacingPx(spacings[(step / 3) % 3]);
                const int last = layer.source()->timeCount() - 1;
                layer.setCurrentTimeIndex(std::max(0, last - (step % 5) * std::max(1, last / 20)));
                if (elapsed.elapsed() >= 10000 && elapsed.elapsed() < 70000)
                    early.append(bytes);
                if (elapsed.elapsed() >= qint64(std::max(0, seconds - 60)) * 1000)
                    late.append(bytes);
                const auto stats = canvas.compositionStats();
                canonical &= canvas.width() == 1920 && canvas.height() == 1080 &&
                             qFuzzyCompare(canvas.devicePixelRatioF(), 2.);
                samples.append(QJsonObject{
                    {"seconds", elapsed.elapsed() / 1000.},
                    {"physicalFootprintBytes", bytes},
                    {"logicalWidth", canvas.width()},
                    {"logicalHeight", canvas.height()},
                    {"devicePixelRatio", canvas.devicePixelRatioF()},
                    {"renderedFrames", qint64(stats.renderedFrames)},
                    {"fallbackReadbacks", qint64(stats.fallbackReadbacks)},
                    {"captureReadbacks", qint64(stats.captureReadbacks)},
                    {"textureUploads", qint64(stats.textureUploads)},
                    {"fieldWorkingUpperBoundBytes", qint64(layer.flowParticleFieldBytes())},
                    {"waterAuxiliaryBytes", qint64(layer.surfaceAuxiliaryBytes())},
                    {"waterWorkingBytes", qint64(layer.surfaceWorkingBytes())},
                    {"waterVisibilityBytes", qint64(layer.waterVisibilityBytes())},
                    {"waterAttributeCopyBytes", qint64(displayed->retainedWaterAttributeBytes())},
                    {"contourWorkingBytes", qint64(displayed->contourWorkingBytes())},
                    {"waterFits", layer.surfaceFitCount()},
                    {"waterBusy", layer.surfaceBusy()},
                    {"displayedWaterRevision", qint64(displayed->displayedFrameRevision())},
                    {"spacingPx", layer.flowStreakSublayer()->streakStyle()->spacingPx()},
                    {"fieldCaptures", layer.flowParticleFieldCaptures()},
                    {"status", layer.flowStreakStatus()}});
                progress.write(
                    QJsonDocument(samples.last().toObject()).toJson(QJsonDocument::Compact) + "\n");
                progress.flush();
            }
        });
        sampleTimer.start();
        QTimer::singleShot(seconds * 1000, Qt::PreciseTimer, &soak, &QEventLoop::quit);
        soak.exec();
        sampleTimer.stop();
        const auto median = [](QVector<qint64> values) {
            std::sort(values.begin(), values.end());
            return values.isEmpty() ? qint64(-1) : values[values.size() / 2];
        };
        const qint64 growth = median(late) - median(early);
        const auto stats = canvas.compositionStats();
        QFile report(folder + "/memory-soak.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(
            QJsonDocument(
                QJsonObject{
                    {"source", path},
                    {"observedSeconds", elapsed.elapsed() / 1000.},
                    {"waterOnlyControl", waterOnly},
                    {"waterStyle", waterStyle},
                    {"allocationDiagnostic", allocationDiagnostic},
                    {"baselineWaterOnlyFootprintBytes", baseline},
                    {"peakFootprintBytes", peak},
                    {"peakIncreaseBytes", peak - baseline},
                    {"peakSample", peakSample},
                    {"backgroundWaterFitting", layer.asyncSurface()},
                    {"baselineWaterRevision", qint64(baselineRevision)},
                    {"earlyMedianBytes", median(early)},
                    {"lateMedianBytes", median(late)},
                    {"lateGrowthBytes", growth},
                    {"devicePixelRatio", canvas.devicePixelRatioF()},
                    {"logicalWidth", canvas.width()},
                    {"logicalHeight", canvas.height()},
                    {"nativeSurface", stats.nativeSurface},
                    {"renderedFramesDuringSoak",
                     qint64(stats.renderedFrames - initialStats.renderedFrames)},
                    {"tenMinuteGateExercised", seconds >= 600},
                    {"memoryMetric",
                     "macOS task physical footprint; includes graphics allocations charged to process"},
                    {"canonicalViewportThroughout", canonical},
                    {"samples", samples}})
                .toJson());
        report.close();
        QCOMPARE(stats.fallbackReadbacks, initialStats.fallbackReadbacks);
        QCOMPARE(stats.captureReadbacks, initialStats.captureReadbacks);
        if (qEnvironmentVariableIsSet("FLOW_PARTICLE_CANONICAL"))
            QVERIFY(canonical);
        if (!waterOnly)
            QVERIFY(stats.renderedFrames - initialStats.renderedFrames > quint64(seconds) * 10);
        QVERIFY2(peak - baseline <= 256 * 1024 * 1024,
                 qPrintable(
                     QString("Flow footprint increase: %1 MiB").arg((peak - baseline) / 1048576.)));
        if (seconds >= 120)
            QVERIFY2(growth <= 32 * 1024 * 1024,
                     qPrintable(QString("Late footprint growth: %1 MiB").arg(growth / 1048576.)));
        canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Compare three water-only and three particle-enabled live CPU solver runs.
     * Skips without FLOW_PARTICLE_SOLVER_BENCHMARK; requires functional native GUI,
     * CPU 2D solver/HDF5 output and writable FLOW_PARTICLE_NATIVE_OUTPUT or solver
     * default. FLOW_PARTICLE_SOLVER_GRID is clamped to 16..150 squares per dimension.
     * Writes generated INP/report/output/HDF5 files and solver JSON reports. Restores
     * progress-tick preference and OPENSWMM_2D_BACKEND after forcing CPU runs. Each run
     * has a five-minute completion timeout and must emit live depths; persisted final
     * depth SHA-256 hashes must agree. Particle runs must render and attempt births;
     * median elapsed slowdown must be at most five percent. Results are machine/load
     * dependent; live final timestamps need not agree. Incomplete runners are cancelled
     * by scope guard and JSON/deck short writes are not explicitly checked.
     *
     */
    void continuousNativeSolverImpact()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_SOLVER_BENCHMARK"))
            QSKIP("Explicit native solver benchmark");
        const QString folder = qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT",
                                                    "tests/verification/flow_particles/solver");
        QVERIFY(QDir().mkpath(folder));
        const int n =
            std::clamp(qEnvironmentVariableIntValue("FLOW_PARTICLE_SOLVER_GRID"), 16, 150);
        auto *prefs = PreferencesManager::instance();
        const int oldTick = prefs->progressTickMs();
        prefs->setProgressTickMs(250);
        const auto oldBackend = qgetenv("OPENSWMM_2D_BACKEND");
        qputenv("OPENSWMM_2D_BACKEND", "cpu");
        const auto restore = qScopeGuard([&] {
            prefs->setProgressTickMs(oldTick);
            if (oldBackend.isNull())
                qunsetenv("OPENSWMM_2D_BACKEND");
            else
                qputenv("OPENSWMM_2D_BACKEND", oldBackend);
        });
        QVector<double> waterOnly, withFlow;
        QJsonArray runs;
        QByteArray expectedDepthHash;
        for (int run = 0; run < 6; ++run) {
            const bool flow = run == 1 || run == 2 || run == 5;
            const QString stem = QString("%1-%2").arg(run).arg(flow ? "flow" : "water");
            const QString input = QDir(folder).absoluteFilePath(stem + ".inp");
            QFile deck(input);
            QVERIFY(deck.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream text(&deck);
            text
                << "[OPTIONS]\nFLOW_UNITS CMS\nINFILTRATION HORTON\nFLOW_ROUTING DYNWAVE\n"
                   "START_DATE 01/01/2026\nSTART_TIME 00:00:00\nEND_DATE 01/01/2026\n"
                   "END_TIME 02:00:00\nREPORT_STEP 0:05:00\nROUTING_STEP 5\n\n"
                   "[JUNCTIONS]\nJ1 10 1 0 0 0\n\n[OUTFALLS]\nO1 9 FREE NO\n\n"
                   "[CONDUITS]\nC1 J1 O1 100 0.013 0 0 0\n\n[XSECTIONS]\nC1 CIRCULAR 0.3 0 0 0 1\n\n"
                   "[INFLOWS]\nJ1 FLOW TS1 FLOW 1.0 "
                << double(n) * n / 256
                << "\n\n"
                   "[TIMESERIES]\nTS1 0:00 0.2\nTS1 2:00 0.2\n\n"
                   "[2D_OPTIONS]\nMAX_TIMESTEP 2\nDRY_DEPTH 0.002\nCOUPLING_CD 0.7\n"
                   "REPORT_2D YES\nOUTPUT_FILE "
                << stem << ".2d.h5\n\n[2D_VERTICES]\n";
            for (int y = 0; y <= n; ++y)
                for (int x = 0; x <= n; ++x)
                    text << 5 * x << ' ' << 5 * y << ' ' << 11 - .02 * x << '\n';
            text << "\n[2D_TRIANGLES]\n";
            const auto vertex = [n](int x, int y) { return y * (n + 1) + x; };
            for (int y = 0; y < n; ++y)
                for (int x = 0; x < n; ++x)
                    text << vertex(x, y) << ' ' << vertex(x + 1, y) << ' ' << vertex(x + 1, y + 1)
                         << " 0.03\n"
                         << vertex(x, y) << ' ' << vertex(x + 1, y + 1) << ' ' << vertex(x, y + 1)
                         << " 0.03\n";
            text << "\n[2D_VERTEX_NODE_MAP]\n" << vertex(n / 2, n / 2) << " J1 0.7 2.5\n";
            text.flush();
            deck.close();
            SWMM2DResultsLayer layer;
            layer.setAsyncSurface(true);
            for (auto *s : layer.sublayers())
                s->setVisible(false);
            layer.smoothDepthFillSublayer()->setVisible(true);
            layer.flowStreakSublayer()->setVisible(flow);
            layer.setLiveMinFitIntervalMs(250);
            ParticleReviewCanvas canvas;
            canvas.resize(1920, 1080);
            canvas.setShowScaleBar(false);
            canvas.setWindowFlags(canvas.windowFlags() | Qt::WindowTransparentForInput |
                                  Qt::WindowDoesNotAcceptFocus);
            canvas.addLayer(&layer, false);
            canvas.setExtent({0, 0, double(5 * n), double(5 * n)}, false);
            showNativeReview(canvas);
            EngineMesh2DSource *source = nullptr;
            int ticks = 0;
            bool done = false;
            auto *runner =
                new SimulationRunner(run, stem, input, QDir(folder).absoluteFilePath(stem + ".rpt"),
                                     QDir(folder).absoluteFilePath(stem + ".out"), "6.0.0", this);
            connect(runner, &SimulationRunner::twoDInitialized, &layer,
                    [&](int, QString, QVector<double> x, QVector<double> y, QVector<double> z,
                        QVector<int> flat) {
                        std::vector<std::array<int, 4>> cells(size_t(flat.size() / 4));
                        for (size_t i = 0; i < cells.size(); ++i)
                            for (int j = 0; j < 4; ++j)
                                cells[i][j] = flat[int(i) * 4 + j];
                        auto live = std::make_unique<EngineMesh2DSource>(
                            std::vector<double>(x.begin(), x.end()),
                            std::vector<double>(y.begin(), y.end()),
                            std::vector<double>(z.begin(), z.end()), std::move(cells));
                        live->setDryDepth(.002);
                        live->setMaxFrames(64);
                        source = live.get();
                        layer.setSource(std::move(live));
                        layer.setDryDepth(.002);
                    });
            connect(runner, &SimulationRunner::twoDEdgeGeometryAvailable, &layer,
                    [&](int, QVector<float> length, QVector<float> nx, QVector<float> ny) {
                        if (source)
                            source->setEdgeGeometry({length.begin(), length.end()},
                                                    {nx.begin(), nx.end()}, {ny.begin(), ny.end()});
                    });
            connect(runner, &SimulationRunner::twoDDepthsAvailable, &layer,
                    [&](int, QVector<float> depths, QDateTime time, double elapsed) {
                        if (source) {
                            source->pushDepths({depths.begin(), depths.end()}, time, elapsed);
                            ++ticks;
                            layer.refreshTimeRange();
                        }
                    });
            connect(runner, &SimulationRunner::twoDFluxAvailable, &layer,
                    [&](int, QVector<float> flux, QDateTime time, double elapsed) {
                        if (source) {
                            source->pushFlux({flux.begin(), flux.end()}, time, elapsed);
                            layer.refreshTimeRange();
                            layer.refreshCurrentFrame();
                        }
                    });
            connect(runner, &SimulationRunner::twoDVertexDepthsAvailable, &layer,
                    [&](int, QVector<double> depths, QDateTime time, double elapsed) {
                        if (source) {
                            source->pushVertexSignedDepths({depths.begin(), depths.end()}, time,
                                                           elapsed);
                            layer.refreshCurrentFrame();
                        }
                    });
            connect(runner, &SimulationRunner::twoDEnvelopesAvailable, &layer,
                    [&](int, QVector<float> depth, QVector<float> speed) {
                        if (source)
                            source->setEnvelopes({depth.begin(), depth.end()},
                                                 {speed.begin(), speed.end()});
                    });
            connect(runner, &SimulationRunner::finished, &layer, [&] {
                done = true;
                if (source)
                    source->markFinished();
            });
            QSignalSpy finished(runner, &SimulationRunner::finished);
            QPointer<SimulationRunner> guard(runner);
            const auto cleanup = qScopeGuard([&] {
                if (guard && !done)
                    guard->cancel();
            });
            QEventLoop simulation;
            connect(runner, &SimulationRunner::finished, &simulation, &QEventLoop::quit);
            QTimer::singleShot(300000, Qt::PreciseTimer, &simulation, &QEventLoop::quit);
            QElapsedTimer wall;
            wall.start();
            runner->start();
            simulation.exec();
            const double milliseconds = wall.nsecsElapsed() / 1e6;
            QCOMPARE(finished.count(), 1);
            QVERIFY2(finished.front()[1].toBool(), qPrintable(finished.front()[3].toString()));
            QVERIFY(source);
            QVERIFY(ticks > 0);
            // Live publications follow wall-clock progress and can end at
            // different simulated times. Compare the persisted final report.
            HDF5Mesh2DSource saved;
            QVERIFY(saved.open(QDir(folder).absoluteFilePath(stem + ".2d.h5")));
            std::vector<float> depths;
            QVERIFY(saved.readDepthsAt(saved.timeCount() - 1, depths));
            const auto hash = QCryptographicHash::hash(
                                  QByteArrayView(reinterpret_cast<const char *>(depths.data()),
                                                 qsizetype(depths.size() * sizeof(float))),
                                  QCryptographicHash::Sha256)
                                  .toHex();
            if (expectedDepthHash.isEmpty())
                expectedDepthHash = hash;
            else
                QCOMPARE(hash, expectedDepthHash);
            const auto stats = canvas.compositionStats();
            const auto diagnostics = layer.flowParticleDiagnostics();
            if (flow) {
                QVERIFY(stats.active);
                QVERIFY(stats.renderedFrames > 10);
                QVERIFY(diagnostics.birthAttempts > 0);
                withFlow.append(milliseconds);
            } else
                waterOnly.append(milliseconds);
            runs.append(QJsonObject{
                {"arm", flow ? "flow" : "water"},
                {"run", run},
                {"wallMs", milliseconds},
                {"liveTicks", ticks},
                {"nativeCells", 2 * n * n},
                {"renderedFrames", qint64(stats.renderedFrames)},
                {"particleBirthAttempts", qint64(diagnostics.birthAttempts)},
                {"finalDepthSha256", QString::fromLatin1(hash)},
                {"finalSavedTime",
                 saved.simTimeAt(saved.timeCount() - 1).toString(Qt::ISODateWithMs)},
                {"lastLiveTime",
                 source->simTimeAt(source->timeCount() - 1).toString(Qt::ISODateWithMs)},
                {"devicePixelRatio", canvas.devicePixelRatioF()}});
            QFile partial(folder + "/solver-runs.json");
            QVERIFY(partial.open(QIODevice::WriteOnly));
            partial.write(QJsonDocument(runs).toJson());
            canvas.takeLayer(0, false);
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        std::sort(waterOnly.begin(), waterOnly.end());
        std::sort(withFlow.begin(), withFlow.end());
        const double slowdown = withFlow[1] / waterOnly[1] - 1;
        QFile report(folder + "/solver-impact.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(
            QJsonDocument(
                QJsonObject{
                    {"runs", runs},
                    {"waterMedianMs", waterOnly[1]},
                    {"flowMedianMs", withFlow[1]},
                    {"slowdownFraction", slowdown},
                    {"withinFivePercent", slowdown <= .05},
                    {"scope",
                     "Actual coupled CPU solver, live hydraulic publications and native map; three alternating runs per arm"}})
                .toJson());
        QVERIFY2(slowdown <= .05,
                 qPrintable(QString("Median solver slowdown: %1%").arg(slowdown * 100)));
    }
    /**
     *
     * @brief Review native particle continuity on a six-million-cell grid or saved HDF5 mesh.
     * Skips without FLOW_PARTICLE_SCALE_REVIEW. FLOW_PARTICLE_SCALE_KIND=quad chooses
     * 2000-by-3000 quads; other values choose a 2000-by-1500 two-triangle grid.
     * FLOW_PARTICLE_SCALE_LIVE enables synthetic speed revisions every three seconds.
     * FLOW_PARTICLE_SCALE_WET and _VIEW are percentages in (0,100]; view controls area.
     * An optional FLOW_STREAK_RESULT replaces the synthetic source and forbids live mode.
     * FLOW_PARTICLE_NATIVE_SECONDS is clamped to 15..120 s; FLOW_PARTICLE_CANONICAL
     * requires a 1920-by-1080 logical viewport at DPR two. Requires substantial memory,
     * an exposed native window and writable FLOW_PARTICLE_NATIVE_OUTPUT or scale default.
     * Writes observation.json before capture, then scale.png/scale.json. Checks visible
     * moving particles, no waiting particles/readbacks, under 0.5-s continuous loss of
     * all moving heads and native swap samples. Timing percentiles/targets are reported
     * without an OS scanout measurement or a timing-threshold assertion.
     *
     */
    void continuousNativeScaleMatrix()
    {
        if (!qEnvironmentVariableIsSet("FLOW_PARTICLE_SCALE_REVIEW"))
            QSKIP("Explicit multi-million-cell scale fixture");
        const bool quad = qEnvironmentVariable("FLOW_PARTICLE_SCALE_KIND") == "quad";
        const bool live = qEnvironmentVariableIsSet("FLOW_PARTICLE_SCALE_LIVE");
        bool valid = false;
        double wet = qEnvironmentVariable("FLOW_PARTICLE_SCALE_WET", "100").toDouble(&valid) / 100;
        QVERIFY(valid && wet > 0 && wet <= 1);
        const double fraction =
            qEnvironmentVariable("FLOW_PARTICLE_SCALE_VIEW", "100").toDouble(&valid) / 100;
        QVERIFY(valid && fraction > 0 && fraction <= 1);
        const int seconds =
            std::clamp(qEnvironmentVariableIntValue("FLOW_PARTICLE_NATIVE_SECONDS"), 15, 120);
        const QString folder = qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT",
                                                    "tests/verification/flow_particles/scale");
        QVERIFY(QDir().mkpath(folder));
        SWMM2DResultsLayer layer;
        layer.setAsyncSurface(true);
        std::unique_ptr<GridFlowSource> owned =
            quad ? std::unique_ptr<GridFlowSource>(new QuadGridFlowSource(2000, 3000))
                 : std::make_unique<GridFlowSource>(2000, 1500);
        auto *source = owned.get();
        source->wetFraction = wet;
        source->live = live;
        const QString sourcePath = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (sourcePath.isEmpty())
            layer.setSource(std::move(owned));
        else {
            QVERIFY(!live);
            source = nullptr;
            owned.reset();
            auto file = std::make_unique<HDF5Mesh2DSource>();
            QVERIFY(file->open(sourcePath));
            layer.setSource(std::move(file));
        }
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        layer.smoothDepthFillSublayer()->setVisible(true);
        layer.flowStreakSublayer()->setVisible(true);
        layer.flowStreakSublayer()->streakStyle()->setColor(QColor(10, 30, 80));
        ParticleReviewCanvas canvas;
        canvas.resize(1920, 1080);
        canvas.setShowScaleBar(false);
        canvas.setWindowFlags(canvas.windowFlags() | Qt::WindowTransparentForInput |
                              Qt::WindowDoesNotAcceptFocus);
        canvas.addLayer(&layer, false);
        const auto box = layer.m_sceneBBox;
        const double w = box.width() * std::sqrt(fraction), h = box.height() * std::sqrt(fraction);
        canvas.setExtent(
            {box.left(), -box.center().y() - h / 2, box.left() + w, -box.center().y() + h / 2},
            false);
        showNativeReview(canvas);
        QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
        QQuickWindow *mapWindow = nullptr;
        for (auto *window : QGuiApplication::allWindows())
            if (window->objectName() == "flowMapNativeWindow")
                mapWindow = qobject_cast<QQuickWindow *>(window);
        QVERIFY(mapWindow);
        const auto renderers = mapWindow->contentItem()->findChildren<SWMM2DResultsQSGRenderer *>();
        QCOMPARE(renderers.size(), 1);
        auto *displayed = renderers.front();
        const auto extent = canvas.extent();
        const QRectF view(extent.xMin(), -extent.yMax(), extent.width(), extent.height());
        const double scale = canvas.width() / extent.width();
        QVERIFY2(layer.prepareFlowStreaksForCapture(view, scale),
                 qPrintable(layer.flowStreakStatus()));
        QTest::qWait(3000);
        if (qEnvironmentVariableIsSet("FLOW_PARTICLE_CANONICAL")) {
            QCOMPARE(canvas.width(), 1920);
            QCOMPARE(canvas.height(), 1080);
            QCOMPARE(canvas.devicePixelRatioF(), 2.);
        }
        const auto before = canvas.compositionStats();
        const int fullReads = source ? source->reads : 0;
        const auto *flowTimer = canvas.findChild<QTimer *>("flowAnimationTimer");
        const auto *published = layer.findChild<FlowParticleController *>();
        QVERIFY(published);
        bool exposedThroughout = mapWindow->isExposed();
        QElapsedTimer elapsed;
        elapsed.start();
        int tick = -1, noMovingHeads = 0, observations = 0;
        double noHeadSince = -1, longestNoHeadSeconds = 0, previousShownClock = -1;
        QJsonArray motionSamples;
        // Observe through the ordinary event dispatcher. qWait's polling
        // sleeps can delay frame timers and must not become display timings.
        QEventLoop observation;
        QTimer observationTimer;
        observationTimer.setInterval(100);
        connect(&observationTimer, &QTimer::timeout, &observation, [&] {
            exposedThroughout &= mapWindow->isExposed();
            if (live && int(elapsed.elapsed() / 3000) != tick) {
                tick = int(elapsed.elapsed() / 3000);
                source->speed = tick % 2 ? 2 : 3;
                ++source->revision;
                layer.refreshCurrentFrame();
            }
            const auto shownRevision = displayed->displayedFrameRevision();
            const auto observed = published->frame(shownRevision);
            const double shownClock =
                observed ? observed->previousClock + (observed->clock - observed->previousClock) *
                                                         layer.flowParticleInterpolation(observed)
                         : -1;
            const bool clockAdvanced =
                observed && (previousShownClock < 0 || shownClock > previousShownClock + 1e-8);
            if (observed)
                previousShownClock = shownClock;
            bool movingHead = false;
            if (observed)
                for (const auto &p : observed->particles)
                    if (p.stop == FlowParticleStop::None && !p.waitingForData &&
                        p.history.size() > 1) {
                        const auto delta =
                            p.history.back().position - p.history[p.history.size() - 2].position;
                        movingHead |=
                            clockAdvanced && std::hypot(delta.x(), delta.y()) * scale > 1e-5;
                    }
            motionSamples.append(QJsonObject{{"seconds", elapsed.elapsed() / 1000.},
                                             {"requestedRevision", qint64(layer.frameRevision())},
                                             {"displayedRevision", qint64(shownRevision)},
                                             {"particleClock", shownClock},
                                             {"movingHead", movingHead},
                                             {"nativeWindowExposed", mapWindow->isExposed()}});
            ++observations;
            if (!movingHead) {
                ++noMovingHeads;
                if (noHeadSince < 0)
                    noHeadSince = elapsed.elapsed() / 1000.;
                longestNoHeadSeconds =
                    std::max(longestNoHeadSeconds, elapsed.elapsed() / 1000. - noHeadSince);
            } else
                noHeadSince = -1;
        });
        observationTimer.start();
        QTimer::singleShot(seconds * 1000, Qt::PreciseTimer, &observation, &QEventLoop::quit);
        observation.exec();
        observationTimer.stop();
        const double observedSeconds = elapsed.elapsed() / 1000.;
        const auto after = canvas.compositionStats();
        const int observationFullReads = source ? source->reads - fullReads : 0;
        auto intervals = after.renderIntervalsMs;
        const auto count = after.renderedFrames - before.renderedFrames;
        if (count < quint64(intervals.size()))
            intervals = intervals.mid(intervals.size() - int(count));
        std::sort(intervals.begin(), intervals.end());
        auto swaps = after.swapIntervalsMs;
        if (count < quint64(swaps.size()))
            swaps = swaps.mid(swaps.size() - int(count));
        std::sort(swaps.begin(), swaps.end());
        // Preserve the measured window even if a later capture is incomplete.
        QFile observationReport(folder + "/observation.json");
        QVERIFY(observationReport.open(QIODevice::WriteOnly));
        observationReport.write(
            QJsonDocument(
                QJsonObject{
                    {"observedSeconds", observedSeconds},
                    {"observationScope",
                     "Read-only published controller frames; ordinary map renderer drives preparation and dispatch"},
                    {"framesBefore", qint64(before.renderedFrames)},
                    {"framesAfter", qint64(after.renderedFrames)},
                    {"nativeWindowExposedThroughout", exposedThroughout},
                    {"nativeWindowExposedAfter", mapWindow->isExposed()},
                    {"nativeWindowVisible", mapWindow->isVisible()},
                    {"surfaceBusyAfter", layer.surfaceBusy()},
                    {"canvasVisible", canvas.isVisible()},
                    {"canvasMinimized", canvas.window()->isMinimized()},
                    {"animationTimerActive", flowTimer && flowTimer->isActive()},
                    {"motionEnabled", layer.flowStreaksEnabled()},
                    {"motionAnimated", layer.flowStreakSublayer()->streakStyle()->animated()},
                    {"requestedRevision", qint64(layer.frameRevision())},
                    {"displayedRevision", qint64(displayed->displayedFrameRevision())},
                    {"motionSamples", motionSamples},
                    {"longestNoMovingHeadSeconds", longestNoHeadSeconds},
                    {"frameSwappedSamples", swaps.size()},
                    {"frameSwappedP95Ms", swaps.isEmpty()
                                              ? QJsonValue()
                                              : QJsonValue(swaps[int(.95 * (swaps.size() - 1))])},
                    {"frameSwapWithinTarget",
                     swaps.isEmpty() ? QJsonValue()
                                     : QJsonValue(swaps[int(.95 * (swaps.size() - 1))] <=
                                                  (fraction < 1 ? 16.7 : 33.0))},
                    {"status", layer.flowStreakStatus()}})
                .toJson());
        observationReport.close();
        QVERIFY(!intervals.isEmpty());
        // A final live tick may still be fitting when observation ends.
        // Settle only the subsequent field/image capture, after saving timing.
        QVERIFY2(layer.prepareFlowStreaksForCapture(view, scale),
                 qPrintable(layer.flowStreakStatus()));
        const auto frame = layer.flowParticles(view, scale, layer.frameRevision(), false);
        QVERIFY(frame);
        int moving = 0, waiting = 0;
        for (const auto &p : frame->particles) {
            waiting += p.waitingForData;
            if (p.history.size() > 1) {
                const auto distance = p.history.back().position - p.history.front().position;
                moving += std::hypot(distance.x(), distance.y()) * scale > .25;
            }
        }
        const auto diagnostics = layer.flowParticleDiagnostics();
        const auto input = layer.captureFlowParticleInput(view, scale);
        QVERIFY2(input, qPrintable(layer.flowStreakStatus()));
        QString error;
        const auto image = canvas.captureMap(&error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        QVERIFY(image.save(folder + "/scale.png"));
        const auto particlePixels = visibleParticlePixels(canvas, layer, image.toImage(), &error);
        QVERIFY2(particlePixels > 20, "The scale fixture has no visible particle pixels");
        QFile report(folder + "/scale.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(
            QJsonDocument(
                QJsonObject{
                    {"kind", quad ? "quad" : "triangle"},
                    {"source", sourcePath.isEmpty()
                                   ? QString(live ? "synthetic live" : "synthetic held")
                                   : sourcePath},
                    {"observationScope",
                     "Read-only published controller frames; ordinary map renderer drives preparation and dispatch"},
                    {"nativeCells", layer.source()->triangleCount()},
                    {"displayTriangles", layer.m_sceneTris.size()},
                    {"wetFraction", wet},
                    {"requestedViewFraction", fraction},
                    {"observedSeconds", observedSeconds},
                    {"logicalWidth", canvas.width()},
                    {"logicalHeight", canvas.height()},
                    {"devicePixelRatio", canvas.devicePixelRatioF()},
                    {"residentFlowTriangles", int(input->triangles.size())},
                    {"estimatedWetFraction", diagnostics.wetFraction},
                    {"targetPopulation", diagnostics.targetPopulation},
                    {"particles", int(frame->particles.size())},
                    {"ribbonParticleSlots", frame->ribbon.particles},
                    {"ribbonHistorySlots", frame->ribbon.history},
                    {"ribbonVertexBytes",
                     qint64(frame->ribbon.vertices.size() * sizeof(FlowParticleVertex))},
                    {"reservedRibbonVertexBytes",
                     qint64(frame->ribbon.vertices.capacity() * sizeof(FlowParticleVertex))},
                    {"movingParticles", moving},
                    {"waitingForData", waiting},
                    {"renderedFrames", qint64(count)},
                    {"visibleParticlePixels", particlePixels},
                    {"observations", observations},
                    {"observationsWithoutMovingHeads", noMovingHeads},
                    {"longestNoMovingHeadSeconds", longestNoHeadSeconds},
                    {"motionSamples", motionSamples},
                    {"renderCompletionMedianMs", intervals[int(.5 * (intervals.size() - 1))]},
                    {"renderCompletionP95Ms", intervals[int(.95 * (intervals.size() - 1))]},
                    {"nativeSurface", after.nativeSurface},
                    {"frameSwappedSamples", swaps.size()},
                    {"frameSwappedMedianMs", swaps.isEmpty()
                                                 ? QJsonValue()
                                                 : QJsonValue(swaps[int(.5 * (swaps.size() - 1))])},
                    {"frameSwappedP95Ms", swaps.isEmpty()
                                              ? QJsonValue()
                                              : QJsonValue(swaps[int(.95 * (swaps.size() - 1))])},
                    {"frameSwapWithinTarget",
                     swaps.isEmpty() ? QJsonValue()
                                     : QJsonValue(swaps[int(.95 * (swaps.size() - 1))] <=
                                                  (fraction < 1 ? 16.7 : 33.0))},
                    {"osPresentationP95Ms", QJsonValue()},
                    {"cadenceScope",
                     "Separate Qt render completions and native queued-for-presentation swaps; OS scanout not directly measured"},
                    {"steadyFallbackReadbacks",
                     qint64(after.fallbackReadbacks - before.fallbackReadbacks)},
                    {"steadyCaptureReadbacks",
                     qint64(after.captureReadbacks - before.captureReadbacks)},
                    {"fullReadsDuringObservation",
                     source ? QJsonValue(observationFullReads) : QJsonValue()},
                    {"fieldWorkingUpperBoundBytes", qint64(layer.flowParticleFieldBytes())},
                    {"status", layer.flowStreakStatus()}})
                .toJson());
        report.close();
        QVERIFY2(moving > 0, qPrintable(layer.flowStreakStatus()));
        QVERIFY(!layer.flowStreakStatus().contains("Zoom in"));
        QCOMPARE(waiting, 0);
        QVERIFY2(longestNoHeadSeconds < .5, "The overview repeatedly lost all moving heads");
        if (after.nativeSurface)
            QVERIFY(swaps.size() > 10);
        QCOMPARE(after.fallbackReadbacks, before.fallbackReadbacks);
        QCOMPARE(after.captureReadbacks, before.captureReadbacks);
        canvas.takeLayer(0, false);
    }
    /**
     *
     * @brief Register native-triangles and native-quads rows in the bool quad column.
     *
     */
    void continuousRegionalLargeMesh_data()
    {
        QTest::addColumn<bool>("quad");
        QTest::newRow("native-triangles") << false;
        QTest::newRow("native-quads") << true;
    }
    /**
     *
     * @brief Require canvas attachment to retain unprojected triangle/quad particle publications.
     * Temporarily enables and restores OPENSWMM_REGIONAL_SURFACE. Checks geometry/frame
     * revisions and shared publication identity, then position, age, lifetime and history
     * length after setting the matching extent. Detaches borrowed layers before teardown.
     *
     */
    void canvasAttachmentPreservesUnprojectedRegionalParticles()
    {
        const auto original = qgetenv("OPENSWMM_REGIONAL_SURFACE");
        qputenv("OPENSWMM_REGIONAL_SURFACE", "1");
        const auto restore = qScopeGuard([&] {
            if (original.isNull())
                qunsetenv("OPENSWMM_REGIONAL_SURFACE");
            else
                qputenv("OPENSWMM_REGIONAL_SURFACE", original);
        });
        for (bool quad : {false, true}) {
            SWMM2DResultsLayer layer;
            if (quad)
                layer.setSource(std::make_unique<QuadGridFlowSource>(160, 120));
            else
                layer.setSource(std::make_unique<GridFlowSource>(160, 120));
            layer.flowStreakSublayer()->setVisible(true);
            const QRectF view(100, -60, 20, 20);
            QVERIFY(layer.prepareFlowStreaksForCapture(view, 20));
            layer.advanceFlowStreaks(.3);
            QVERIFY(layer.prepareFlowStreaksForCapture(view, 20));
            const auto before = layer.flowParticles(view, 20, layer.frameRevision());
            QVERIFY(before && !before->particles.empty());
            const auto geometry = layer.geomRevision();
            const auto revision = layer.frameRevision();
            MapCanvas canvas;
            canvas.resize(480, 480);
            canvas.addLayer(&layer, false);
            QCOMPARE(layer.geomRevision(), geometry);
            QCOMPARE(layer.frameRevision(), revision);
            const auto attached = layer.flowParticles(view, 20, revision);
            QCOMPARE(attached, before);
            canvas.setExtent({100, 40, 120, 60}, false);
            QVERIFY(layer.prepareFlowStreaksForCapture(view, 20));
            const auto after = layer.flowParticles(view, 20, layer.frameRevision());
            QVERIFY(after);
            for (const auto &old : before->particles) {
                const auto it = std::find_if(after->particles.begin(), after->particles.end(),
                                             [&](const auto &p) { return p.id == old.id; });
                QVERIFY(it != after->particles.end());
                QCOMPARE(it->position, old.position);
                QCOMPARE(it->age, old.age);
                QCOMPARE(it->lifetime, old.lifetime);
                QCOMPARE(it->history.size(), old.history.size());
            }
            canvas.takeLayer(0, false);
        }
    }
    /**
     *
     * @brief Check bounded regional hydraulics, trajectory continuity and offscreen
     * replenishment.
     * Consumes bool quad. Uses a 160-by-120 grid unless FLOW_PARTICLE_6M selects six
     * million native cells. Checks resident payload under 32 MiB, cropped edges waiting
     * rather than acting as outlets, no full reads during advancement, compatible
     * revision history retention and target population after distant pans/return.
     * Writes regional/6m triangle/quad JSON under FLOW_PARTICLE_NATIVE_OUTPUT or the
     * review default. FLOW_PARTICLE_NATIVE_REVIEW additionally needs GPU composition,
     * saves a PNG and observes 15..120 s from FLOW_PARTICLE_NATIVE_SECONDS without new
     * readbacks/uploads. Large runs of at least 90 s additionally check lifetime-only
     * deaths; large overview runs require bounded seeded residency and continuing motion.
     * Timing is reported without performance certification; payload bound excludes
     * baseline mesh/process/renderer memory. Requires writable output and, for large
     * fixtures, substantial memory; JSON short writes are not checked.
     *
     */
    void continuousRegionalLargeMesh()
    {
        QFETCH(bool, quad);
        const bool large = qEnvironmentVariableIsSet("FLOW_PARTICLE_6M");
        const bool native = qEnvironmentVariableIsSet("FLOW_PARTICLE_NATIVE_REVIEW");
        const int cols = large ? 2000 : 160, rows = large ? (quad ? 3000 : 1500) : 120;
        QElapsedTimer timer;
        timer.start();
        SWMM2DResultsLayer layer;
        std::unique_ptr<GridFlowSource> source =
            quad ? std::unique_ptr<GridFlowSource>(new QuadGridFlowSource(cols, rows))
                 : std::make_unique<GridFlowSource>(cols, rows);
        auto *raw = source.get();
        layer.setSource(std::move(source));
        const double loadMs = timer.nsecsElapsed() / 1e6;
        QCOMPARE(layer.m_sceneTris.size(), cols * rows * 2);
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        layer.smoothDepthFillSublayer()->setVisible(true);
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setSpeedMultiplier(1);
        style->setSpacingPx(30);
        style->setMinimumLifetimeSeconds(60);
        style->setMaximumLifetimeSeconds(90);
        style->setTrailDurationSeconds(10);
        style->setColor(QColor(10, 30, 80));
        const QRectF view(cols * .65, -rows * .5, 40, 30);
        const double scale = 32;
        timer.restart();
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        const double prepareMs = timer.nsecsElapsed() / 1e6;
        if (large)
            QVERIFY(layer.regionalHydraulicsPublished());
        const auto input = layer.captureFlowParticleInput(view, scale);
        QVERIFY(input);
        // Startup support can produce a fresh prefetch packet. Its topology
        // belongs to the worker; settle that captured packet before reading it.
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        QCOMPARE(input->mesh->neighbors.size(), input->triangles.size());
        QVERIFY(input->triangles.size() < size_t(FlowParticleMesh::maxRegionTriangles));
        const size_t payload =
            input->mesh->bytes() + input->triangles.capacity() * sizeof(FlowParticleNativeTriangle);
        QVERIFY(payload < 32 * 1024 * 1024);
        qInfo() << "Resident display triangles" << input->triangles.size() << "input payload bytes"
                << payload;
        int croppedEdges = 0;
        for (size_t i = 0; i < input->triangles.size(); ++i)
            for (int e = 0; e < 3; ++e) {
                if (input->mesh->neighbors[i][e] >= 0)
                    continue;
                const auto &ids = input->mesh->triangles[i];
                const auto midpoint = (input->mesh->vertices[ids[(e + 1) % 3]] +
                                       input->mesh->vertices[ids[(e + 2) % 3]]) *
                                      .5;
                const auto box = layer.m_sceneBBox;
                if (std::abs(midpoint.x() - box.left()) < 1e-6 ||
                    std::abs(midpoint.x() - box.right()) < 1e-6 ||
                    std::abs(midpoint.y() - box.top()) < 1e-6 ||
                    std::abs(midpoint.y() - box.bottom()) < 1e-6)
                    continue;
                QVERIFY2(
                    !(input->triangles[i].residentEdges & (1 << e)),
                    "A regional edge inside the native domain must wait, not become an outlet");
                ++croppedEdges;
            }
        QVERIFY(croppedEdges > 0);
        const int fullReads = raw->reads, regionalReads = raw->regionalReads;
        timer.restart();
        for (int i = 0; i < 120; ++i) {
            layer.advanceFlowStreaks(1. / 60.);
            QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        }
        const double advanceMs = timer.nsecsElapsed() / 1e6;
        auto first = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(first);
        QVERIFY(first->particles.size() > 10);
        QCOMPARE(raw->reads, fullReads);
        QVERIFY(raw->regionalReads - regionalReads < 10);
        int crossings = 0;
        for (const auto &p : first->particles)
            crossings += p.crossings;
        QVERIFY(crossings > 0);
        // A new live packet must use a regional read and retain particle IDs,
        // assigned lifetimes, recorded history and world positions.
        raw->speed = 3;
        ++raw->revision;
        layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision() != first->revision, 10000);
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        auto updated = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(updated);
        for (const auto &old : first->particles) {
            if (old.stop != FlowParticleStop::None &&
                first->clock - old.stoppedAt >= style->lifetimeFadeSeconds())
                continue;
            const auto it = std::find_if(updated->particles.begin(), updated->particles.end(),
                                         [&](const auto &p) { return p.id == old.id; });
            QVERIFY(it != updated->particles.end());
            QCOMPARE(it->position, old.position);
            QCOMPARE(it->age, old.age);
            QCOMPARE(it->lifetime, old.lifetime);
            QCOMPARE(it->history.size(), old.history.size());
        }
        if (large) {
            QCOMPARE(raw->reads, fullReads);
            QVERIFY(raw->regionalReads > regionalReads);
        }
        updated.reset(); // Comparison complete; return this publication slot.
        const QRectF distant(cols * .1, -rows * .2, 40, 30);
        QVERIFY(layer.prepareFlowStreaksForCapture(distant, scale));
        layer.advanceFlowStreaks(.1);
        QVERIFY(layer.prepareFlowStreaksForCapture(distant, scale));
        auto away = layer.flowParticles(distant, scale, layer.frameRevision());
        QVERIFY(away);
        int continued = 0, recycled = 0;
        for (const auto &old : first->particles) {
            if (old.stop != FlowParticleStop::None || old.lifetime - old.age <= .2)
                continue;
            const auto it = std::find_if(away->particles.begin(), away->particles.end(),
                                         [&](const auto &p) { return p.id == old.id; });
            if (it == away->particles.end()) {
                // A full pool may recycle only trajectories whose entire
                // bounds are outside the new view. No-pressure retention is
                // covered separately by the controller core regression.
                double left = old.position.x(), right = left, top = old.position.y(), bottom = top;
                for (const auto &s : old.history) {
                    left = std::min(left, s.position.x());
                    right = std::max(right, s.position.x());
                    top = std::min(top, s.position.y());
                    bottom = std::max(bottom, s.position.y());
                }
                QVERIFY(right < distant.left() || left > distant.right() ||
                        bottom < distant.top() || top > distant.bottom());
                ++recycled;
                continue;
            }
            QVERIFY(it->position.x() > old.position.x());
            QVERIFY(!it->waitingForData);
            QCOMPARE(it->lifetime, old.lifetime);
            ++continued;
        }
        QVERIFY(continued + recycled > 10);
        QVERIFY(layer.flowParticleDiagnostics().offscreenReplacements >= quint64(recycled));
        int visibleAway = 0;
        for (const auto &p : away->particles)
            visibleAway += p.stop == FlowParticleStop::None && distant.contains(p.position);
        QVERIFY(visibleAway >= layer.flowParticleDiagnostics().targetPopulation - 1);
        away.reset(); // Refill can publish several bounded batches on return.
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        layer.advanceFlowStreaks(.1);
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        auto returned = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(returned);
        int resumed = 0;
        for (const auto &old : first->particles)
            for (const auto &p : returned->particles)
                if (p.id == old.id && p.position.x() > old.position.x())
                    ++resumed;
        QVERIFY(resumed + recycled > 10);
        int visibleReturned = 0;
        for (const auto &p : returned->particles)
            visibleReturned += p.stop == FlowParticleStop::None && view.contains(p.position);
        QVERIFY2(visibleReturned >= layer.flowParticleDiagnostics().targetPopulation - 1,
                 qPrintable(QString("visible=%1 total=%2 target=%3 recycled=%4 busy=%5 %6")
                                .arg(visibleReturned)
                                .arg(returned->particles.size())
                                .arg(layer.flowParticleDiagnostics().targetPopulation)
                                .arg(layer.flowParticleDiagnostics().offscreenReplacements)
                                .arg(layer.flowStreakBusy())
                                .arg(layer.flowStreakStatus())));
        QJsonObject report{
            {"nativeCells", raw->triangleCount()},
            {"displayTriangles", layer.m_sceneTris.size()},
            {"cellType", quad ? "quad" : "triangle"},
            {"loadMs", loadMs},
            {"regionPrepareMs", prepareMs},
            {"manualAdvanceMs", advanceMs},
            {"manualAnimationSeconds", 2},
            {"particles", int(first->particles.size())},
            {"cellCrossings", crossings},
            {"regionTriangles", int(input->triangles.size())},
            {"inputPayloadBytes", qint64(payload)},
            {"fullReadsAfterLoad", raw->reads - fullReads},
            {"regionalReads", raw->regionalReads},
            {"resumedParticles", resumed},
            {"offscreenRecycledParticles", recycled},
            {"memoryScope",
             "input payload only; process high-water includes baseline mesh, map and renderer"}};
        const QString folder = qEnvironmentVariable(
            "FLOW_PARTICLE_NATIVE_OUTPUT", "workplans/artifacts/flow_particle_fallback_2026-10-12");
        QDir().mkpath(folder);
        const QString name =
            QString(large ? "6m-%1" : "regional-%1").arg(quad ? "quads" : "triangles");
        if (native) {
            ParticleReviewCanvas canvas;
            canvas.resize(1920, 1080);
            canvas.setShowScaleBar(false);
            canvas.setWindowFlags(canvas.windowFlags() | Qt::WindowTransparentForInput |
                                  Qt::WindowDoesNotAcceptFocus);
            canvas.addLayer(&layer, false);
            canvas.setExtent({view.left(), -view.bottom(), view.right(), -view.top()}, false);
            canvas.show();
            QTRY_VERIFY_WITH_TIMEOUT(canvas.compositionStats().active, 10000);
            QTest::qWait(3000);
            const auto before = canvas.compositionStats();
            const int seconds =
                std::clamp(qEnvironmentVariableIntValue("FLOW_PARTICLE_NATIVE_SECONDS"), 15, 120);
            QTest::qWait(seconds * 1000);
            const auto after = canvas.compositionStats();
            QVERIFY(after.renderedFrames > before.renderedFrames + 100);
            QCOMPARE(after.fallbackReadbacks, before.fallbackReadbacks);
            QCOMPARE(after.captureReadbacks, before.captureReadbacks);
            QCOMPARE(after.textureUploads, before.textureUploads);
            // Select only new intervals, including after the bounded history
            // has evicted the beginning of a longer residency/lifetime run.
            const auto newFrames = after.renderedFrames - before.renderedFrames;
            auto intervals = after.renderIntervalsMs.mid(
                int(std::max<qint64>(0, after.renderIntervalsMs.size() - qint64(newFrames))));
            std::sort(intervals.begin(), intervals.end());
            QVERIFY(intervals.size() > 100);
            report.insert("renderIntervalMedianMs", intervals[int(.5 * (intervals.size() - 1))]);
            report.insert("renderIntervalP95Ms", intervals[int(.95 * (intervals.size() - 1))]);
            report.insert("intervalWithin30FpsTarget",
                          intervals[int(.95 * (intervals.size() - 1))] <= 33.3);
            report.insert("renderedFrames", qint64(after.renderedFrames - before.renderedFrames));
            report.insert("logicalViewportWidth", canvas.width());
            report.insert("logicalViewportHeight", canvas.height());
            report.insert("devicePixelRatio", canvas.devicePixelRatioF());
            report.insert("steadyFallbackReadbacks",
                          qint64(after.fallbackReadbacks - before.fallbackReadbacks));
            report.insert("steadyTextureUploads",
                          qint64(after.textureUploads - before.textureUploads));
            report.insert(
                "cadenceScope",
                "Qt Quick render completions; synthetic live source; not solver impact or OS presentation");
            const auto current = layer.flowParticles(view, scale, layer.frameRevision(), false);
            QVERIFY(current);
            int waiting = 0, limited = 0, moving = 0;
            size_t history = 0;
            for (const auto &p : current->particles) {
                waiting += p.waitingForData;
                limited += p.historyLimited;
                history += p.history.size();
                moving += !p.waitingForData && p.stop == FlowParticleStop::None;
            }
            report.insert("steadyParticles", int(current->particles.size()));
            report.insert("waitingForData", waiting);
            report.insert("shortenedTails", limited);
            report.insert("historySamples", qint64(history));
            report.insert("animationSeconds", current->clock);
            report.insert("movingParticles", moving);
            report.insert("motionPresentAtEnd", moving > 0);
            report.insert("fieldWorkingUpperBoundBytes", qint64(layer.flowParticleFieldBytes()));
            report.insert("fieldCaptures", layer.flowParticleFieldCaptures());
            report.insert("fieldSubsetReads", layer.flowParticleFieldReads());
            if (large && seconds >= 90) {
                QVERIFY(moving > 0);
                QCOMPARE(waiting, 0);
                QVERIFY(current->deaths > 0);
                QCOMPARE(current->deaths, current->stopCounts[size_t(FlowParticleStop::Lifetime)]);
                QCOMPARE(raw->reads, fullReads);
            }
            report.insert("performanceCertified",
                          false); // short profile; residency/lifetime/memory gates remain
            report.insert("births", qint64(current->births));
            report.insert("deaths", qint64(current->deaths));
            QJsonArray stops;
            for (auto count : current->stopCounts)
                stops.append(qint64(count));
            report.insert("stopCounts", stops);
            QString error;
            const auto image = canvas.captureMap(&error);
            QVERIFY2(!image.isNull(), qPrintable(error));
            QVERIFY(image.save(folder + "/" + name + ".png"));
            canvas.takeLayer(0, false);
        }
        QFile file(folder + "/" + name + ".json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(report).toJson());
        if (large) {
            // The four-slot publication pool intentionally backpressures a
            // consumer retaining four old frames. These comparisons are done.
            first.reset();
            updated.reset();
            away.reset();
            returned.reset();
            const int readsBeforeOverview = raw->reads;
            const double overviewScale = 1280. / layer.m_sceneBBox.width();
            timer.restart();
            const auto overview = layer.captureFlowParticleInput(layer.m_sceneBBox, overviewScale);
            QVERIFY2(overview, qPrintable(layer.flowStreakStatus()));
            QCOMPARE(raw->reads, readsBeforeOverview);
            QVERIFY(overview->triangles.size() < 40000);
            QVERIFY(!overview->mesh->seedWeights.empty());
            const double captureMs = timer.nsecsElapsed() / 1e6;
            qInfo() << "Overview initial capture" << overview->triangles.size() << "triangles"
                    << captureMs << "ms" << layer.flowStreakStatus();
            QVERIFY2(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, overviewScale),
                     qPrintable(layer.flowStreakStatus()));
            const auto beforeOverview =
                layer.flowParticles(layer.m_sceneBBox, overviewScale, layer.frameRevision());
            QVERIFY(beforeOverview);
            for (int i = 0; i < 60; ++i) {
                layer.advanceFlowStreaks(1. / 60.);
                QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, overviewScale));
            }
            const auto overviewFrame =
                layer.flowParticles(layer.m_sceneBBox, overviewScale, layer.frameRevision());
            QVERIFY(overviewFrame);
            int moving = 0;
            for (const auto &p : overviewFrame->particles)
                for (const auto &old : beforeOverview->particles)
                    moving += p.id == old.id && std::hypot((p.position - old.position).x(),
                                                           (p.position - old.position).y()) > 1e-6;
            QFile evidence(folder + "/" + name + "-overview.json");
            QVERIFY(evidence.open(QIODevice::WriteOnly));
            evidence.write(
                QJsonDocument(QJsonObject{{"captureMs", captureMs},
                                          {"residentTriangles", int(overview->triangles.size())},
                                          {"particles", int(overviewFrame->particles.size())},
                                          {"movingParticles", moving},
                                          {"fieldWorkingUpperBoundBytes",
                                           qint64(layer.flowParticleFieldBytes())},
                                          {"status", layer.flowStreakStatus()}})
                    .toJson());
            QVERIFY(moving > 0);
            QVERIFY(!layer.flowStreakStatus().contains("Zoom in"));
        }
    }
    /**
     *
     * @brief Record a saved-result particle zoom-out/in regeneration timeline without a window.
     * Skips without FLOW_STREAK_RESULT; requires a readable nonempty saved HDF5 mesh.
     * Models a 1920-by-1080 viewport, five factor-1.5 notches each way, animation requests
     * and ribbon synchronization. Each settle phase stops at 90-percent visible target
     * and idle worker or ten seconds; reaching that target is recorded, not asserted.
     * Writes zoom-FLOW_STREAK_ZOOM_LABEL.json (default label run) under
     * FLOW_STREAK_ZOOM_OUTPUT or the dated verification default, including GUI request
     * cost, captures, seed/field builds and node uploads. Requires writable output;
     * the report is diagnostic and supplies no asserted timing/performance threshold.
     *
     */
    void zoomOutRegenerationTimeline()
    {
        const QString path = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (path.isEmpty())
            QSKIP(
                "Set FLOW_STREAK_RESULT to the Bellinge saved result for the zoom regeneration timeline");
        const QString folder = qEnvironmentVariable(
            "FLOW_STREAK_ZOOM_OUTPUT", "tests/verification/flow_streak_zoom_2026-10-17");
        const QString label = qEnvironmentVariable("FLOW_STREAK_ZOOM_LABEL", "run");
        QVERIFY(QDir().mkpath(folder));
        SWMM2DResultsLayer layer;
        layer.setAsyncSurface(true);
        auto source = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(source->open(path));
        layer.setSource(std::move(source));
        layer.setCurrentTimeIndex(layer.source()->timeCount() - 1);
        for (auto *s : layer.sublayers())
            s->setVisible(false);
        layer.smoothDepthFillSublayer()->setVisible(true);
        layer.flowStreakSublayer()->setVisible(true);
        const auto *style = layer.flowStreakSublayer()->streakStyle();
        auto *capture = layer.findChild<FlowParticleLayerController *>();
        QVERIFY(capture);
        const auto *published = layer.findChild<FlowParticleController *>();
        QVERIFY(published);
        // Headless replica of the native item: the same frame() request per
        // rendered frame and animation tick, plus a ribbon sync at each scale.
        // Start on a sixteenth of the model area, wheel out five notches with
        // the canvas wheel factor 100 ms apart, settle, then wheel back in.
        const QSizeF viewport(1920, 1080);
        const auto bounds = layer.m_sceneBBox;
        const double wheelFactor = 1.5;
        const int notches = 5;
        double width = bounds.width() / 4, height = width * viewport.height() / viewport.width();
        if (height > bounds.height() / 4) {
            height = bounds.height() / 4;
            width = height * viewport.width() / viewport.height();
        }
        const QPointF center = bounds.center();
        const auto viewAt = [&](double factor) {
            return QRectF(center.x() - width * factor / 2, center.y() - height * factor / 2,
                          width * factor, height * factor);
        };
        const auto activeHeads = [](const std::shared_ptr<const FlowParticleFrame> &frame,
                                    const QRectF &view) {
            int active = 0;
            if (frame)
                for (const auto &p : frame->particles)
                    active += p.stop == FlowParticleStop::None && !p.waitingForData &&
                              view.contains(p.position);
            return active;
        };
        const auto present = [&](double factor) {
            const auto view = viewAt(factor);
            const double scale = viewport.width() / view.width();
            QElapsedTimer stall;
            stall.start();
            layer.flowParticles(view, scale, layer.frameRevision());
            return stall.nsecsElapsed() / 1e6;
        };
        const auto animate = [&](double factor, int ticks) {
            for (int i = 0; i < ticks; ++i) {
                layer.advanceFlowStreaks(1. / 60.);
                present(factor);
                QTest::qWait(16);
            }
        };
        FlowParticleNode node;
        QJsonArray steps;
        const auto record = [&](const QString &phase, int step, double factor, double stallMs) {
            const auto view = viewAt(factor);
            const double scale = viewport.width() / view.width();
            const auto frame = published->frame(layer.frameRevision());
            FlowParticleAppearance appearance;
            appearance.anchor = bounds.center();
            appearance.color = style->color();
            const double stroke = 2 * std::max(style->widthPx(), style->headWidthPx()) / scale;
            appearance.viewport = view.adjusted(-stroke, -stroke, stroke, stroke);
            appearance.revision =
                qHash(QJsonDocument(style->toJson()).toJson(QJsonDocument::Compact));
            const int uploads = node.uploads();
            node.sync(frame, layer.flowParticleInterpolation(frame), scale,
                      style->trailDurationSeconds(), style->widthPx(), style->headWidthPx(),
                      style->lifetimeFadeSeconds(), 0, appearance);
            const auto d = layer.flowParticleDiagnostics();
            steps.append(QJsonObject{{"phase", phase},
                                     {"step", step},
                                     {"scale", scale},
                                     {"viewWidth", view.width()},
                                     {"guiStallMs", stallMs},
                                     {"captures", capture->captures()},
                                     {"lastCaptureMs", capture->lastCaptureMs()},
                                     {"captureMsTotal", capture->captureMs()},
                                     {"fieldBuilds", qint64(d.fieldBuilds)},
                                     {"lastFieldBuildMs", d.lastFieldBuildMs},
                                     {"seedBuilds", qint64(d.seedBuilds)},
                                     {"lastSeedBuildMs", d.lastSeedBuildMs},
                                     {"targetPopulation", d.targetPopulation},
                                     {"particles", frame ? qint64(frame->particles.size()) : 0},
                                     {"activeVisibleHeads", activeHeads(frame, view)},
                                     {"workerRibbonMs", frame ? frame->ribbon.buildMs : 0.},
                                     {"nodeRibbonRebuilt", node.uploads() > uploads},
                                     {"nodeRibbonMs", node.geometryBuildMs()},
                                     {"status", layer.flowStreakStatus()}});
        };
        const auto settle = [&](const QString &phase, double factor) {
            QElapsedTimer clock;
            clock.start();
            const auto view = viewAt(factor);
            QJsonArray trace;
            while (clock.elapsed() < 10000) {
                layer.advanceFlowStreaks(1. / 60.);
                present(factor);
                QTest::qWait(16);
                const auto frame = published->frame(layer.frameRevision());
                const auto d = layer.flowParticleDiagnostics();
                const int active = activeHeads(frame, view);
                if (clock.elapsed() / 100 >= trace.size())
                    trace.append(QJsonObject{{"seconds", clock.elapsed() / 1000.},
                                             {"activeVisibleHeads", active},
                                             {"targetPopulation", d.targetPopulation}});
                if (d.targetPopulation > 0 && active >= .9 * d.targetPopulation &&
                    !layer.flowStreakBusy())
                    break;
            }
            steps.append(QJsonObject{{"phase", phase + "-settle"},
                                     {"settleSeconds", clock.elapsed() / 1000.},
                                     {"trace", trace}});
        };
        {
            const auto view = viewAt(1);
            const double scale = viewport.width() / view.width();
            QVERIFY2(layer.prepareFlowStreaksForCapture(view, scale),
                     qPrintable(layer.flowStreakStatus()));
        }
        animate(1, 180);
        record("start", 0, 1, present(1));
        double factor = 1;
        for (int step = 1; step <= notches; ++step) {
            factor *= wheelFactor;
            const double stallMs = present(factor);
            animate(factor, 5);
            record("zoom-out", step, factor, stallMs);
        }
        settle("zoom-out", factor);
        for (int step = 1; step <= notches; ++step) {
            factor /= wheelFactor;
            const double stallMs = present(factor);
            animate(factor, 5);
            record("zoom-in", step, factor, stallMs);
        }
        settle("zoom-in", factor);
        QFile report(folder + "/zoom-" + label + ".json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write(
            QJsonDocument(QJsonObject{{"result", path},
                                      {"label", label},
                                      {"viewport", QJsonArray{viewport.width(), viewport.height()}},
                                      {"wheelFactor", wheelFactor},
                                      {"meshTriangles", layer.source()->triangleCount()},
                                      {"steps", steps}})
                .toJson());
    }
    /**
     *
     * @brief Require overview scale changes to replace seed patches without animation ticks.
     * Checks fewer positive seed weights at scale 0.1 than 10, nonempty remaining seeds
     * and shared-input reuse when requesting the unchanged smaller scale again.
     *
     */
    void overviewScaleChangesRefreshTheSeedPatchesWithoutATick()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<GridFlowSource>(700, 200));
        layer.flowStreakSublayer()->setVisible(true);
        const auto view = layer.m_sceneBBox;
        const auto first = layer.captureFlowParticleInput(view, 10);
        QVERIFY(first);
        const auto resized = layer.captureFlowParticleInput(view, .1);
        QVERIFY(resized);
        QVERIFY(resized != first);
        const auto seeds = [](const auto &input) {
            return std::count_if(input->mesh->seedWeights.begin(), input->mesh->seedWeights.end(),
                                 [](double weight) { return weight > 0; });
        };
        QVERIFY(seeds(first) > seeds(resized));
        QVERIFY(seeds(resized) > 0);
        QCOMPARE(layer.captureFlowParticleInput(view, .1), resized);
    }
    /**
     *
     * @brief Require regional coverage to include the view with a motion-dependent margin.
     * Checks non-overview seed policy and fewer than 14000 triangles initially, then
     * more resident triangles after increasing the speed multiplier to eight.
     *
     */
    void regionalSeedCoverageUsesMotionDistance()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<GridFlowSource>(400, 200));
        layer.flowStreakSublayer()->setVisible(true);
        const QRectF view(150, -100, 60, 40);
        const auto input = layer.captureFlowParticleInput(view, 32);
        QVERIFY2(input, qPrintable(layer.flowStreakStatus()));
        QVERIFY(input->mesh->seedWeights.empty());
        QVERIFY(input->mesh->coverage.contains(view));
        QVERIFY2(input->triangles.size() < 14000,
                 "A small zoomed view loads a half-viewport margin unrelated to motion");
        layer.flowStreakSublayer()->streakStyle()->setSpeedMultiplier(8);
        const auto fast = layer.captureFlowParticleInput(view, 32);
        QVERIFY(fast);
        QVERIFY(fast->triangles.size() > input->triangles.size());
    }
    /**
     *
     * @brief Require a backward saved-frame seek to discard the old larger residency extent.
     * Prepares frame two, seeks to frame one and a tighter view, and checks resident
     * triangle count drops below half of the preceding request.
     *
     */
    void backwardSeekDropsThePreviousResidencyExtent()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<GridFlowSource>(400, 200));
        layer.flowStreakSublayer()->setVisible(true);
        layer.setCurrentTimeIndex(2);
        const QRectF view(150, -100, 60, 40);
        QVERIFY(layer.prepareFlowStreaksForCapture(view, 32));
        const auto before = layer.captureFlowParticleInput(view, 32);
        QVERIFY(before);
        layer.setCurrentTimeIndex(1);
        const QRectF zoom(175, -85, 5, 5);
        QVERIFY(layer.prepareFlowStreaksForCapture(zoom, 128));
        const auto after = layer.captureFlowParticleInput(zoom, 128);
        QVERIFY(after);
        QVERIFY2(after->triangles.size() < before->triangles.size() / 2,
                 "A backward seek keeps the previous view's now-obsolete field extent");
    }
    /**
     *
     * @brief Require pending seek coherence before expanding the replacement history budget.
     * Creates a dense population with first-particle history capacity at most 256,
     * requests a lower-density backward seek and deliberately mismatches source content.
     * Rejected capture must retain the old budget; restoring content then allows the
     * new first-particle capacity to reach at least 512. Uses synthetic source mutations.
     *
     */
    void pendingSeekKeepsOldHistoryLimitsUntilItsPacketIsReady()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<GridFlowSource>(16, 8);
        auto *raw = source.get();
        layer.setSource(std::move(source));
        layer.setCurrentTimeIndex(2);
        layer.flowStreakSublayer()->setVisible(true);
        auto *style = layer.flowStreakSublayer()->streakStyle();
        style->setSpacingPx(2);
        const auto view = layer.m_sceneBBox;
        QVERIFY(layer.prepareFlowStreaksForCapture(view, 128));
        const auto before = layer.flowParticles(view, 128, layer.frameRevision());
        QVERIFY(before && !before->particles.empty());
        QVERIFY(before->particles.front().history.capacity() <= 256);
        style->setSpacingPx(40);
        layer.setCurrentTimeIndex(1);
        QTRY_VERIFY(layer.frameRevision() != before->revision);
        // The source has a newer packet, but the displayed surface is still
        // from the preceding content revision. Capture must wait for coherence.
        ++raw->revision;
        layer.advanceFlowStreaks(1. / 30.);
        QVERIFY(!layer.prepareFlowStreaksForCapture(view, 128));
        const auto waiting = layer.flowParticles(view, 128, before->revision, false);
        QVERIFY(waiting && !waiting->particles.empty());
        QVERIFY2(
            waiting->particles.front().history.capacity() <= 256,
            "New history limits expand the abandoned population before its reset packet arrives");
        --raw->revision;
        QVERIFY(layer.prepareFlowStreaksForCapture(view, 128));
        const auto after = layer.flowParticles(view, 128, layer.frameRevision());
        QVERIFY(after && !after->particles.empty());
        QVERIFY(after->particles.front().history.capacity() >= 512);
    }
    /**
     *
     * @brief Register four bool quad/overview rows for triangle/quad region/overview residency.
     *
     */
    void liveResidencyExtendsFromTheDisplayedHydraulicSnapshot_data()
    {
        QTest::addColumn<bool>("quad");
        QTest::addColumn<bool>("overview");
        QTest::newRow("triangle-region") << false << false;
        QTest::newRow("triangle-overview") << false << true;
        QTest::newRow("quad-region") << true << false;
        QTest::newRow("quad-overview") << true << true;
    }
    /**
     *
     * @brief Require new residency to use displayed hydraulic values while live updates wait.
     * Consumes quad/overview data. Mutates live speed/depth/content before asynchronous
     * surface publication, then compares extended topology/depth/flux/units/availability
     * with an unchanged reference. No new regional read or displayed revision is allowed;
     * a history-generation change must reject further extension of that snapshot.
     *
     */
    void liveResidencyExtendsFromTheDisplayedHydraulicSnapshot()
    {
        QFETCH(bool, quad);
        QFETCH(bool, overview);
        const auto source = [&]() -> std::unique_ptr<GridFlowSource> {
            if (quad)
                return std::make_unique<QuadGridFlowSource>(400, 200);
            return std::make_unique<GridFlowSource>(400, 200);
        };
        SWMM2DResultsLayer layer, reference;
        layer.setAsyncSurface(true);
        reference.setAsyncSurface(true);
        auto live = source();
        auto *raw = live.get();
        layer.setSource(std::move(live));
        reference.setSource(source());
        layer.flowStreakSublayer()->setVisible(true);
        reference.flowStreakSublayer()->setVisible(true);
        const QRectF view = overview ? layer.m_sceneBBox : QRectF(60, -160, 16, 12);
        const QRectF nextView = overview ? view.translated(10, 0) : view.translated(80, 0);
        const double scale = overview ? 4 : 16, nextScale = scale * 1.25;
        const auto initial = layer.captureFlowParticleInput(view, scale);
        QVERIFY(initial);
        const auto expected = reference.captureFlowParticleInput(nextView, nextScale);
        QVERIFY(expected);
        const auto displayedRevision = layer.frameRevision();
        const int subsetReads = raw->regionalReads;
        // A mutable live packet advances before its replacement surface is
        // published. New residency must use the same old values as the water.
        raw->speed = -3;
        raw->depths[0] = .75f;
        ++raw->revision;
        layer.refreshCurrentFrame();
        const auto extended = layer.captureFlowParticleInput(nextView, nextScale);
        QVERIFY2(extended, "A newer live packet prevents extending the displayed native field");
        QCOMPARE(layer.frameRevision(), displayedRevision);
        QCOMPARE(extended->revision, displayedRevision);
        QCOMPARE(raw->regionalReads, subsetReads);
        QCOMPARE(extended->mesh->sourceTriangles, expected->mesh->sourceTriangles);
        QCOMPARE(extended->triangles.size(), expected->triangles.size());
        for (size_t i = 0; i < expected->triangles.size(); ++i) {
            const auto &a = extended->triangles[i], &b = expected->triangles[i];
            QCOMPARE(a.meanDepth, b.meanDepth);
            QVERIFY(a.depth == b.depth);
            QVERIFY(a.flux == b.flux);
            QVERIFY(a.metres == b.metres);
            QCOMPARE(a.available, b.available);
        }
        ++raw->generation;
        QVERIFY(!layer.captureFlowParticleInput(nextView.translated(5, 0), nextScale * 1.1));
    }
    /**
     *
     * @brief Require two-pixel cull padding and float-round-tripped scale to reuse overview
     * input.
     * Checks shared-input identity and unchanged field-read count, then requires a
     * distinct input after an eight-pixel pan outside that tolerance.
     *
     */
    void overviewCullPaddingDoesNotReloadTheField()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<GridFlowSource>(700, 200));
        layer.flowStreakSublayer()->setVisible(true);
        const auto view = layer.m_sceneBBox.adjusted(20, 20, -20, -20);
        const double scale = 4.1;
        const auto first = layer.captureFlowParticleInput(view, scale);
        QVERIFY(first);
        const int reads = layer.flowParticleFieldReads();
        // The native renderer culls with two extra pixels. Observation/capture
        // uses the exact viewport, and its scale may have passed through float.
        const auto padded = view.adjusted(-2 / scale, -2 / scale, 2 / scale, 2 / scale);
        QCOMPARE(layer.captureFlowParticleInput(padded, double(float(scale))), first);
        QCOMPARE(layer.captureFlowParticleInput(view, scale), first);
        QCOMPARE(layer.flowParticleFieldReads(), reads);
        const auto panned = layer.captureFlowParticleInput(view.translated(8 / scale, 0), scale);
        QVERIFY(panned);
        QVERIFY(panned != first);
    }
    /**
     *
     * @brief Require failed regional/overview probes to preserve the accepted working input.
     * Simulates missing and mismatched packets, checks rejected distant captures and
     * unchanged prior-input identity, then accepts valid replacement and bounded seeded
     * overview without a Zoom in status. Uses synthetic flags; no dataset IO.
     *
     */
    void missingOrMismatchedResidencyPacketsRetainTheAcceptedField()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<GridFlowSource>(700, 200);
        auto *raw = source.get();
        layer.setSource(std::move(source));
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setSpeedMultiplier(1);
        style->setMinimumLifetimeSeconds(60);
        style->setMaximumLifetimeSeconds(90);
        const QRectF view(300, -40, 30, 20);
        QVERIFY(layer.prepareFlowStreaksForCapture(view, 16));
        const auto accepted = layer.captureFlowParticleInput(view, 16);
        QVERIFY(accepted);
        raw->regionalMissing = true;
        const QRectF outside(100, -20, 30, 20);
        QVERIFY(!layer.captureFlowParticleInput(outside, 16));
        QCOMPARE(layer.captureFlowParticleInput(view, 16), accepted);
        raw->regionalMissing = false;
        raw->regionalMismatch = true;
        QVERIFY(!layer.captureFlowParticleInput(outside, 16));
        QCOMPARE(layer.captureFlowParticleInput(view, 16), accepted);
        raw->regionalMismatch = false;
        const auto replacement = layer.captureFlowParticleInput(outside, 16);
        QVERIFY(replacement);
        QVERIFY(replacement != accepted);
        // Missing overview probes keep the working regional snapshot too.
        raw->regionalMissing = true;
        QVERIFY(!layer.captureFlowParticleInput(layer.m_sceneBBox, 1));
        QCOMPARE(layer.captureFlowParticleInput(outside, 16), replacement);
        QVERIFY(!layer.flowStreakStatus().contains("Zoom in"));
        raw->regionalMissing = false;
        const auto overview = layer.captureFlowParticleInput(layer.m_sceneBBox, 1);
        QVERIFY(overview);
        QVERIFY(!overview->mesh->seedWeights.empty());
        QVERIFY(overview->triangles.size() < 40000);
    }
    /**
     *
     * @brief Distinguish live history-window trimming from a genuine backward time seek.
     * Advances a live particle, shifts the source origin/generation while retaining its
     * displayed instant and checks ID, clock, age and position preservation. Reversing
     * the origin then changing content must reset to prewarmed clock two with formed
     * history and zero transition opacity.
     *
     */
    void liveHistoryTrimPreservesAgeButAnActualBackwardSeekResetsIt()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        auto *raw = source.get();
        raw->live = true;
        layer.setSource(std::move(source));
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setSpeedMultiplier(.1);
        layer.setCurrentTimeIndex(2);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        layer.advanceFlowStreaks(1);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        const auto old = layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        QVERIFY(!old->particles.empty());
        const auto selected =
            std::find_if(old->particles.begin(), old->particles.end(),
                         [](const auto &p) { return p.stop == FlowParticleStop::None; });
        QVERIFY(selected != old->particles.end());
        raw->timeOrigin += 120;
        ++raw->generation;
        layer.setCurrentTimeIndex(0);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        const auto trimmed = layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        QVERIFY(trimmed);
        const auto retained = std::find_if(trimmed->particles.begin(), trimmed->particles.end(),
                                           [&](const auto &p) { return p.id == selected->id; });
        QVERIFY(retained != trimmed->particles.end());
        QCOMPARE(trimmed->clock, old->clock);
        QCOMPARE(retained->age, selected->age);
        QCOMPARE(retained->position, selected->position);
        raw->timeOrigin -= 120;
        ++raw->revision;
        layer.refreshCurrentFrame();
        QTRY_VERIFY(layer.frameRevision() != trimmed->revision);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        const auto sought = layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        QVERIFY(sought);
        QVERIFY(!sought->particles.empty());
        QVERIFY(std::abs(sought->clock - 2) < 1e-8);
        QVERIFY(sought->particles.front().history.size() > 1);
        QCOMPARE(layer.flowParticleOpacity(), 0.);
    }
    /**
     *
     * @brief Require staggered lifetimes and bounded active population during continuous births.
     * Advances two simulated seconds after prewarm using nearly stationary flow and
     * 20..40-s lifetimes. Checks active count within one of target, births and only
     * lifetime deaths, lifetime spread over ten seconds, zero failed births and exact
     * clock advancement. Fading particles may occupy separate bounded slots.
     *
     */
    void continuousBirthsAreDistributedAcrossTheSelectedLifetimeRange()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        source->speed = .001f;
        layer.setSource(std::move(source));
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setSpeedMultiplier(.001 * 4 / 48);
        style->setMinimumLifetimeSeconds(20);
        style->setMaximumLifetimeSeconds(40);
        style->setLifetimeFadeSeconds(1);
        style->setSpacingPx(30);
        const auto view = layer.m_sceneBBox;
        QVERIFY(layer.prepareFlowStreaksForCapture(view, 4));
        auto frame = layer.flowParticles(view, 4, layer.frameRevision());
        QVERIFY(frame);
        const double startClock = frame->clock;
        QVERIFY(std::abs(startClock - 2) < 1e-8);
        const auto checkPopulation = [&] {
            int active = 0;
            for (const auto &p : frame->particles)
                active += p.stop == FlowParticleStop::None;
            const int target = layer.flowParticleDiagnostics().targetPopulation;
            // A head dying on the final integration tick is replaced on the
            // next tick. Fading trails remain in their own bounded slots.
            QVERIFY(active <= target);
            QVERIFY(active >= target - 1);
        };
        checkPopulation();
        for (int i = 0; i < 120; ++i) {
            layer.advanceFlowStreaks(1. / 60);
            QVERIFY(layer.prepareFlowStreaksForCapture(view, 4));
        }
        frame = layer.flowParticles(view, 4, layer.frameRevision());
        QVERIFY(frame);
        QVERIFY(frame->births > 10);
        QVERIFY(frame->deaths > 0);
        QCOMPARE(frame->deaths, frame->stopCounts[size_t(FlowParticleStop::Lifetime)]);
        double minLife = INFINITY, maxLife = 0;
        for (const auto &p : frame->particles) {
            minLife = std::min(minLife, p.lifetime);
            maxLife = std::max(maxLife, p.lifetime);
        }
        QVERIFY(maxLife - minLife > 10);
        checkPopulation();
        QCOMPARE(layer.flowParticleDiagnostics().failedBirths, quint64(0));
        QVERIFY(std::abs(frame->clock - startClock - 2) < 1e-8);
    }
    /**
     *
     * @brief Reuse the bool quad triangle/quad rows from continuousRegionalLargeMesh_data().
     *
     */
    void continuousResidencyThroughRandomizedDeaths_data() { continuousRegionalLargeMesh_data(); }
    /**
     *
     * @brief Track regional trajectories and assigned lifetime deaths through 1000 0.1-s ticks.
     * Consumes bool quad; FLOW_PARTICLE_6M chooses six-million-cell fixtures instead
     * of a 500-by-80 grid. Pans away for 100 steps and compares x motion with normalized
     * display gain/reference speed. Requires lifetime/age coherence, only lifetime
     * stops, no waiting/history limits, at most 4096 history samples, field working
     * bound 128 MiB, trajectory error below 1e-4 and bounded regional reads/captures.
     * Writes triangle/quad lifetime JSON under FLOW_PARTICLE_NATIVE_OUTPUT or the dated
     * residency default. Large fixtures require substantial memory; report short writes
     * are not checked. The elapsed wall time is diagnostic, with no timing gate.
     *
     */
    void continuousResidencyThroughRandomizedDeaths()
    {
        QFETCH(bool, quad);
        const bool large = qEnvironmentVariableIsSet("FLOW_PARTICLE_6M");
        const int cols = large ? 2000 : 500, rows = large ? (quad ? 3000 : 1500) : 80;
        SWMM2DResultsLayer layer;
        std::unique_ptr<GridFlowSource> source =
            quad ? std::unique_ptr<GridFlowSource>(new QuadGridFlowSource(cols, rows))
                 : std::make_unique<GridFlowSource>(cols, rows);
        auto *raw = source.get();
        raw->speed = 1;
        layer.setSource(std::move(source));
        for (auto *sub : layer.sublayers())
            sub->setVisible(false);
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setSpeedMultiplier((large && !quad ? 2. : 1.) / 3);
        style->setSpacingPx(50);
        style->setMinimumLifetimeSeconds(60);
        style->setMaximumLifetimeSeconds(90);
        style->setTrailDurationSeconds(10);
        const QRectF view(large ? 1000 : 50, -rows * .5, 30, 20),
            distant(large ? 100 : 10, -rows * .2, 30, 20);
        const double scale = 16;
        QVERIFY(layer.prepareFlowStreaksForCapture(view, scale));
        const int fullReads = raw->reads;
        struct Trace {
            QPointF start;
            double born, life, lastAge, expectedX;
        };
        std::map<quint64, Trace> traces;
        QSet<quint64> deaths;
        int witnessedDeaths = 0, maxWaiting = 0, aliveAtEnd = 0;
        double maxError = 0;
        QElapsedTimer timer;
        timer.start();
        for (int step = 0; step < 1000; ++step) {
            const QRectF requested = step >= 300 && step < 400 ? distant : view;
            QVERIFY(layer.prepareFlowStreaksForCapture(requested, scale));
            layer.advanceFlowStreaks(.1);
            QVERIFY(layer.prepareFlowStreaksForCapture(requested, scale));
            const auto frame = layer.flowParticles(requested, scale, layer.frameRevision());
            QVERIFY(frame);
            int waiting = 0;
            const auto motion = layer.flowParticleDiagnostics();
            QVERIFY(motion.referenceSpeedSI > 0);
            for (const auto &p : frame->particles) {
                auto [it, inserted] =
                    traces.try_emplace(p.id, Trace{p.position, frame->clock - p.age, p.lifetime,
                                                   p.age, p.position.x()});
                auto &trace = it->second;
                QCOMPARE(p.lifetime, trace.life);
                waiting += p.waitingForData;
                const double nativeSpeed = !quad && trace.start.x() >= cols * .5 ? 2. : 1.;
                // Panning changes the normalized reference and its smoothed
                // gain. Retained particles follow that display mapping too.
                const double displayedSpeed =
                    motion.motionGain * motion.referenceSpeedSI *
                    std::pow(nativeSpeed / motion.referenceSpeedSI, motion.speedExponent);
                trace.expectedX += displayedSpeed * (p.age - trace.lastAge);
                trace.lastAge = p.age;
                maxError = std::max(maxError, std::abs(p.position.x() - trace.expectedX));
                if (p.stop == FlowParticleStop::None) {
                    QVERIFY2(std::abs(p.age - (frame->clock - trace.born)) < 1e-6,
                             "A cache boundary froze the assigned life");
                    QVERIFY(!p.waitingForData);
                } else {
                    QCOMPARE(p.stop, FlowParticleStop::Lifetime);
                    QVERIFY(std::abs(p.age - p.lifetime) < 1e-6);
                    if (!deaths.contains(p.id)) {
                        deaths.insert(p.id);
                        ++witnessedDeaths;
                    }
                }
                QVERIFY(!p.historyLimited);
                QVERIFY(p.history.size() <= 4096);
            }
            maxWaiting = std::max(maxWaiting, waiting);
            aliveAtEnd = int(frame->particles.size());
            QVERIFY(layer.flowParticleFieldBytes() <= 128 * 1024 * 1024);
        }
        const auto final = layer.flowParticles(view, scale, layer.frameRevision());
        QVERIFY(final);
        QVERIFY(witnessedDeaths > 5);
        QVERIFY(final->deaths > 5);
        QVERIFY(aliveAtEnd > 10);
        QVERIFY2(maxError < 1e-4, qPrintable(QString("trajectory error %1").arg(maxError)));
        QCOMPARE(raw->reads, fullReads);
        QVERIFY(layer.flowParticleFieldCaptures() > 3);
        QVERIFY(layer.flowParticleFieldCaptures() < 450);
        QVERIFY(layer.flowParticleFieldReads() < 450);
        QJsonObject report{{"nativeCells", raw->triangleCount()},
                           {"cellType", quad ? "quad" : "triangle"},
                           {"animationSeconds", final->clock},
                           {"testWallMs", timer.elapsed()},
                           {"births", qint64(final->births)},
                           {"assignedLifetimeDeaths", qint64(final->deaths)},
                           {"witnessedDeaths", witnessedDeaths},
                           {"maximumWaiting", maxWaiting},
                           {"trajectoryError", maxError},
                           {"particlesAtEnd", aliveAtEnd},
                           {"fieldCaptures", layer.flowParticleFieldCaptures()},
                           {"fieldSubsetReads", layer.flowParticleFieldReads()},
                           {"fieldWorkingUpperBoundBytes", qint64(layer.flowParticleFieldBytes())},
                           {"fullReadsAfterLoad", raw->reads - fullReads}};
        const QString folder =
            qEnvironmentVariable("FLOW_PARTICLE_NATIVE_OUTPUT",
                                 "workplans/artifacts/flow_particle_residency_2026-10-13");
        QDir().mkpath(folder);
        QFile output(folder + QString(large ? "/6m-lifetimes-%1.json" : "/lifetimes-%1.json")
                                  .arg(quad ? "quads" : "triangles"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(report).toJson());
    }
    /**
     *
     * @brief Set process-wide fixture settings identity/path and register three QML renderers.
     * Uses IniFormat under the current directory's flow_streaks/settings verification
     * folder. Organization/application names, QSettings defaults/path and registrations
     * remain active for the test process; they are not restored by this setup slot.
     *
     */
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("OpenSWMMFlowTests");
        QCoreApplication::setApplicationName("FlowStreaks");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
            QSettings::IniFormat, QSettings::UserScope,
            QDir::current().absoluteFilePath("tests/verification/flow_streaks/settings"));
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
    }
    /**
     *
     * @brief Round-trip flow sublayer order, visibility, alpha and full style JSON independently.
     * Checks default hidden/animated/live state, explicit saved values, independent
     * second-layer edits, nonempty legend and old empty configuration retaining hidden
     * flow. Uses in-memory sublayer JSON without creating a project file.
     *
     */
    void layerAndStylesPersistIndependently()
    {
        SWMM2DResultsLayer a, b;
        auto *sub = a.flowStreakSublayer();
        QVERIFY(a.sublayers().contains(sub));
        QCOMPARE(sub->id(), QString("results2d.flowStreaks"));
        QVERIFY(!sub->isVisible());
        QVERIFY(sub->streakStyle()->animated());
        QVERIFY(a.liveRenderEnabled());
        sub->setVisible(true);
        sub->setOpacity(.4);
        sub->streakStyle()->setWidthPx(3.5);
        sub->streakStyle()->setHeadWidthPx(8);

        sub->streakStyle()->setMinimumLifetimeSeconds(20);
        sub->streakStyle()->setMaximumLifetimeSeconds(30);
        sub->streakStyle()->setLifetimeFadeSeconds(1.5);
        sub->streakStyle()->setTrailDurationSeconds(3.5);
        sub->streakStyle()->setColor(QColor(20, 40, 80, 170));
        sub->streakStyle()->setSpeedMultiplier(12);
        // Explicit saved settings, including the previous defaults, must win
        // over the calmer defaults when an existing project is reopened.

        QVERIFY(a.moveSublayer(a.sublayers().indexOf(sub), 0));
        const auto json = ISublayerHost::saveSublayersToJson(a);
        ISublayerHost::loadSublayersFromJson(b, json);
        QCOMPARE(b.sublayers().first()->id(), sub->id());
        QVERIFY(b.flowStreakSublayer()->isVisible());
        QCOMPARE(b.flowStreakSublayer()->opacity(), .4);
        QCOMPARE(b.flowStreakSublayer()->streakStyle()->toJson(), sub->streakStyle()->toJson());
        b.flowStreakSublayer()->streakStyle()->setWidthPx(7);
        QCOMPARE(sub->streakStyle()->widthPx(), 3.5);
        QVERIFY(!sub->legendSymbolItems().empty());
        SWMM2DResultsLayer old;
        ISublayerHost::loadSublayersFromJson(old, {});
        QVERIFY(!old.flowStreakSublayer()->isVisible());
    }

    /**
     *
     * @brief Require extreme requested trail duration to retain bounded history and pool size.
     * Advances 120 s with 600..3600-s lifetimes, checks at most 4096 samples, retained
     * starting ages and reduced segments, survivor age progress and lifetime-only stops.
     * The requested numeric trail duration must remain unchanged while particle count
     * respects the explicit 96-MiB-derived worst-case history/ribbon allocation formula.
     *
     */
    void continuousLongTrailKeepsHistoryAndPopulationBounded()
    {
        SWMM2DResultsLayer layer;
        auto source = std::make_unique<FlowSource>();
        source->speed = .0001f;
        layer.setSource(std::move(source));
        auto *sub = layer.flowStreakSublayer();
        sub->setVisible(true);
        auto *style = sub->streakStyle();
        style->setSpeedMultiplier(.0001 / 48);
        style->setMinimumLifetimeSeconds(600);
        style->setMaximumLifetimeSeconds(3600);
        // Far beyond int range after conversion to samples: must clamp BEFORE
        // casting, keeping the requested duration rather than wrapping it.
        style->setTrailDurationSeconds(FlowStreakStyle::maximumNumericValue());
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        const auto initial = layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        QVERIFY(initial);
        layer.advanceFlowStreaks(120);
        QVERIFY(layer.prepareFlowStreaksForCapture(layer.m_sceneBBox, 1));
        const auto frame = layer.flowParticles(layer.m_sceneBBox, 1, layer.frameRevision());
        QVERIFY(frame);
        QVERIFY(!frame->particles.empty());
        QCOMPARE(frame->deaths, frame->stopCounts[size_t(FlowParticleStop::Lifetime)]);
        int survivors = 0;
        for (const auto &p : frame->particles) {
            QVERIFY(p.lifetime >= 600 && p.lifetime <= 3600);
            QVERIFY(p.history.size() <= 4096);
            QVERIFY(!p.historyLimited);
            const auto before = std::find_if(initial->particles.begin(), initial->particles.end(),
                                             [&](const auto &old) { return old.id == p.id; });
            if (before == initial->particles.end()) {
                QCOMPARE(p.history.front().age, 0.);
                continue;
            }
            QCOMPARE(p.history.front().age, before->history.front().age);
            if (p.stop == FlowParticleStop::None) {
                ++survivors;
                QVERIFY(std::abs(p.age - before->age - 120) < 1e-7);
            } else {
                QCOMPARE(p.stop, FlowParticleStop::Lifetime);
                QCOMPARE(p.age, p.lifetime);
            }
            bool reduced = false;
            for (const auto &sample : p.history)
                reduced |= sample.error >= 0;
            QVERIFY(reduced);
        }
        QVERIFY(survivors > 0);
        QCOMPARE(style->trailDurationSeconds(), FlowStreakStyle::maximumNumericValue());
        // The long-tail history allocation still reduces the maximum pool.
        QVERIFY(
            frame->particles.size() <=
            size_t((96 * 1024 * 1024) /
                   ((6 * sizeof(FlowParticleSample) + 6 * 2 * sizeof(FlowParticleVertex)) * 4096)));
    }

    /**
     *
     * @brief Require reconstructed x speed and magnitude to remain 2 m/s across three depths.
     * Checks every prepared display triangle at each synthetic frame within 1e-5.
     *
     */
    void velocitiesHaveCorrectDepthConversion()
    {
        SWMM2DResultsLayer layer;
        layer.setSource(std::make_unique<FlowSource>());
        for (int t = 0; t < 3; ++t) {
            layer.setCurrentTimeIndex(t);
            QVERIFY(!layer.m_sceneTris.empty());
            for (const auto &tri : layer.m_sceneTris) {
                QVERIFY(std::abs(tri.vx - 2) < 1e-5);
                QVERIFY(std::abs(tri.vmag - 2) < 1e-5);
            }
        }
    }
    /**
     *
     * @brief Exercise a queued style-panel refresh after its associated layer is destroyed.
     * Changes the scheme, deletes the owned layer, then processes events. Success is
     * completion without a stale-model dereference; no rendered-pixel assertion is made.
     *
     */
    void queuedStyleRefreshSurvivesLayerRemoval()
    {
        auto layer = std::make_unique<SWMM2DResultsLayer>();
        openswmmvis::ui::Swmm2DResultsStylePanel panel(layer.get());
        auto *style = layer->flowStreakSublayer()->streakStyle();
        auto scheme = style->scheme();
        scheme.setRangeMax(4);
        style->setScheme(scheme);
        layer.reset();
        QCoreApplication::
            processEvents(); // queued editor refresh must not dereference the deleted model
    }

    /**
     *
     * @brief Check compact live subset packets, revision changes and invalid request rejection.
     * Reads one cell's depth/four flux slots, updates flux and requires a new revision
     * with stable depth values. Rejects unsorted/duplicate/out-of-range cell IDs and
     * unavailable time; verifies the first packet reports 24 payload bytes.
     *
     */
    void compactLiveHydraulicSnapshots()
    {
        FlowSource fixture;
        std::vector<double> x, y, z;
        std::vector<std::array<int, 3>> tri;
        fixture.readMeshGeometry(x, y, z, tri);
        EngineMesh2DSource live(x, y, z, tri);
        std::vector<float> flux;
        fixture.readEdgeFluxAt(0, flux);
        live.pushDepths({.25f, .5f}, fixture.simTimeAt(0), 0);
        live.pushFlux(flux, fixture.simTimeAt(0), 0);
        const auto first = live.readHydraulicCellsAt(0, {1});
        QVERIFY(first);
        QCOMPARE(first->depths, std::vector<float>{.5f});
        QCOMPARE(first->flux, std::vector<float>(flux.begin() + 4, flux.end()));
        fixture.speed = -2;
        fixture.readEdgeFluxAt(0, flux);
        live.pushFlux(flux, fixture.simTimeAt(0), 0);
        const auto next = live.readHydraulicCellsAt(0, {1});
        QVERIFY(next);
        QVERIFY(first->revision != next->revision);
        QCOMPARE(first->depths, std::vector<float>{.5f});
        QCOMPARE(next->depths, std::vector<float>{.5f});
        QCOMPARE(next->flux, std::vector<float>(flux.begin() + 4, flux.end()));
        QVERIFY(!live.readHydraulicCellsAt(0, {1, 0}));
        QVERIFY(!live.readHydraulicCellsAt(0, {0, 0}));
        QVERIFY(!live.readHydraulicCellsAt(0, {2}));
        QVERIFY(!live.readHydraulicCellsAt(1, {0}));
        QCOMPARE(first->bytes(), size_t(24));
    }

    /**
     *
     * @brief Check regional surface reuse, latest live revisions and full-field analysis
     * fallback.
     * Pans between disjoint views without full reads, compares corner depths with the
     * original full fit, then coalesces live updates and checks final depth/flow sign.
     * An analysis query outside display residency must reconstruct/cache the full field;
     * overview and subsequent regional transitions preserve current values. A null CRS
     * change retains geometry/residency, and replacing the source clears regional state.
     *
     */
    void regionalSurfacePreservesAnalysisAndPan()
    {
        auto source = std::make_unique<GridFlowSource>();
        auto *raw = source.get();
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(source));
        layer.setAsyncSurface(true);
        // Automatic halo and manifold rings stay within the regional budget.

        layer.setLiveMinFitIntervalMs(0);
        const QRectF a(295, -205, 10, 10), b(95, -205, 10, 10);
        const auto reference = layer.m_sceneTris;
        layer.setHydraulicView(a, 8);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(), 10000);
        QVERIFY(layer.regionalHydraulicsPublished());
        QVERIFY(layer.hydraulicRegionCellCount() < 50000);
        const int reads = raw->reads;
        for (int i : layer.m_triGrid.query(a)) {
            const auto &t = layer.m_sceneTris[i];
            const auto &expected = reference[i];
            QVERIFY(std::abs(t.dv0 - expected.dv0) < 1e-5);
            QVERIFY(std::abs(t.dv1 - expected.dv1) < 1e-5);
            QVERIFY(std::abs(t.dv2 - expected.dv2) < 1e-5);
            QCOMPARE(t.vmag, expected.vmag);
        }
        layer.setHydraulicView(b, 8);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(), 10000);
        QCOMPARE(raw->reads, reads);
        QVERIFY(layer.regionalHydraulicsPublished());
        const auto velocity = layer.captureVelocityField();
        QVERIFY(velocity.depths.size() < 50000);
        float vx = 0, vy = 0;
        QVERIFY(layer.velocityAtScene(b.center(), vx, vy, velocity));
        QVERIFY(std::abs(vx - 2) < 1e-5);
        const auto oldRevision = layer.frameRevision();
        raw->depths[0] = .4f;
        raw->speed = -3;
        ++raw->revision;
        layer.refreshCurrentFrame();
        QTRY_VERIFY_WITH_TIMEOUT(layer.frameRevision() > oldRevision, 10000);
        QCOMPARE(raw->reads, reads);
        QVERIFY(raw->regionalReads >= 3);
        // Replace pending work repeatedly while one fit is active. Only a
        // coherent latest revision may become the final displayed region.
        for (int tick = 0; tick < 8; ++tick) {
            ++raw->revision;
            raw->speed = -3 - float(tick);
            layer.refreshCurrentFrame();
        }
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(), 10000);
        QCOMPARE(raw->reads, reads);
        for (int i : layer.m_triGrid.query(b)) {
            QCOMPARE(layer.m_sceneTris[i].depth, .4f);
            QVERIFY(layer.m_sceneTris[i].vx < 0);
        }
        // Analysis outside the display region still reconstructs the full field.
        QVERIFY(std::abs(layer.depthAtSceneInterp(a.center()) - .4) < 1e-5);
        QVERIFY(raw->reads > reads);
        const int analysisReads = raw->reads;
        QVERIFY(std::abs(layer.depthAtSceneInterp(a.center()) - .4) < 1e-5);
        QCOMPARE(raw->reads, analysisReads);
        layer.setHydraulicView(a, 8);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(), 10000);
        QCOMPARE(raw->reads, analysisReads);
        layer.setHydraulicView(layer.m_sceneBBox, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(), 10000);
        QVERIFY(!layer.regionalHydraulicsPublished());
        QCOMPARE(layer.m_sceneTris[0].depth, .4f);
        layer.setHydraulicView(a, 8);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(), 10000);
        const auto geometry = layer.geomRevision();
        layer.onCanvasCRSChanged(nullptr);
        QCOMPARE(layer.geomRevision(), geometry);
        QVERIFY(layer.regionalHydraulicsPublished());
        QVERIFY(std::abs(layer.depthAtSceneInterp(a.center()) - .4) < 1e-5);
        layer.closeSource();
        layer.setSource(std::make_unique<FlowSource>());
        QVERIFY(!layer.regionalHydraulicsPublished());
    }

    /**
     *
     * @brief Require a complex saved neighborhood to reject an inaccurate regional fit.
     * Skips without FLOW_STREAK_RESULT; needs a readable wet/moving HDF5 fixture whose
     * chosen region exceeds the 0.002-m error gate. Checks one fallback, finite-mask
     * preservation, retained full-fit corner error below 0.002 m and scientific query
     * agreement within 1e-6 m. Logs rejected error/RMS. Sets OPENSWMM_REGIONAL_SURFACE=1
     * then unsets it on normal completion; prior environment is not preserved and an
     * early assertion can leave the flag set.
     *
     */
    void regionalSavedSurfaceError()
    {
        const QString path = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (path.isEmpty())
            QSKIP("Set FLOW_STREAK_RESULT for saved regional/full reconstruction comparison");
        qputenv("OPENSWMM_REGIONAL_SURFACE", "1");
        auto file = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(file->open(path));
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(file));
        // fixed accuracy-comparison footprint
        const auto full = layer.m_sceneTris;
        int center = -1;
        for (int i = 0; i < full.size(); ++i)
            if (full[i].depth > .1 && full[i].vmag > .1) {
                center = i;
                break;
            }
        QVERIFY(center >= 0);
        const QRectF view(full[center].centroid - QPointF(15, 15), QSizeF(30, 30));
        layer.setHydraulicView(view, 8);
        QTRY_VERIFY_WITH_TIMEOUT(!layer.surfaceBusy(), 10000);
        // This complex wet neighborhood fails the regional error gate. Keep
        // the full-domain surface rather than accepting a faster inaccurate fit.
        QVERIFY(!layer.regionalHydraulicsPublished());
        QCOMPARE(layer.regionalSurfaceFallbackCount(), 1);
        QVERIFY(layer.regionalSurfaceError() > .002);
        double maxError = 0, rms = 0;
        int count = 0, maskChanges = 0;
        for (int i : layer.m_triGrid.query(view)) {
            const auto &a = full[i];
            const auto &b = layer.m_sceneTris[i];
            const float before[] = {a.dv0, a.dv1, a.dv2}, after[] = {b.dv0, b.dv1, b.dv2};
            for (int k = 0; k < 3; ++k) {
                if (std::isfinite(before[k]) != std::isfinite(after[k])) {
                    ++maskChanges;
                    continue;
                }
                if (!std::isfinite(before[k]))
                    continue;
                const double error = std::abs(before[k] - after[k]);
                maxError = std::max(maxError, error);
                rms += error * error;
                ++count;
            }
        }
        qInfo() << "Regional saved surface fallback; rejected error" << layer.regionalSurfaceError()
                << "corners" << count << "max error m" << maxError << "RMS m"
                << std::sqrt(rms / std::max(1, count)) << "mask changes" << maskChanges;
        QVERIFY(count > 0);
        QCOMPARE(maskChanges, 0);
        QVERIFY(maxError < .002);
        // Scientific profile query must return the full fit, not the approximation.
        const auto &t = full[center];
        const double expected = (double(t.dv0) + t.dv1 + t.dv2) / 3;
        QVERIFY(std::abs(layer.signedDepthAtDisplayTriangle(center, t.centroid) - expected) < 1e-6);
        qunsetenv("OPENSWMM_REGIONAL_SURFACE");
    }

    /**
     *
     * @brief Save and reopen a real-result project with spaced paths and retained flow style.
     * Skips without FLOW_STREAK_RESULT. Requires the current directory to contain the
     * userflags INP fixture, a readable saved HDF5 result and a functioning GUI. Writes
     * flow.inp when absent and overwrites flow.oswp under flow_streaks/project with spaces.
     * Uses EPSG:3857, checks sublayer visibility/alpha/full style after asynchronous
     * reopen, and verifies that the persisted motion style remains editable.
     * Marks reopened windows unchanged to avoid save prompts; leaves fixture files.
     *
     */
    void projectReopensFlowStyle()
    {
        const QString result = qEnvironmentVariable("FLOW_STREAK_RESULT");
        if (result.isEmpty())
            QSKIP("Optional real engine HDF5 result fixture");
        const QString folder =
            QDir::current().absoluteFilePath("tests/verification/flow_streaks/project with spaces");
        QVERIFY(QDir().mkpath(folder));
        const QString deck = folder + "/flow.inp", project = folder + "/flow.oswp";
        if (!QFile::exists(deck))
            QVERIFY(QFile::copy("tests/gui/data/userflags_fixture.inp", deck));
        SWMMVisProjectWindow original(nullptr, deck);
        // This fixture tests persistence, using projected coordinates in the
        // map rather than interpreting the result's large easting as degrees.
        original.modelLayer()->setSRS(SpatialReferenceSystem::fromAuthCode("EPSG", 3857), true);
        auto *layer = new SWMM2DResultsLayer;
        layer->setProperty("snoopy_h5_path", QFileInfo(result).absoluteFilePath());
        auto *sub = layer->flowStreakSublayer();
        sub->setVisible(true);
        sub->setOpacity(.65);
        sub->streakStyle()->setColor(Qt::cyan);
        sub->streakStyle()->setWidthPx(2.7);
        sub->streakStyle()->setHeadWidthPx(6.2);

        sub->streakStyle()->setMinimumLifetimeSeconds(10);
        sub->streakStyle()->setMaximumLifetimeSeconds(25);
        sub->streakStyle()->setLifetimeFadeSeconds(1.2);
        sub->streakStyle()->setTrailDurationSeconds(4.5);
        sub->streakStyle()->setSpacingPx(22);
        sub->streakStyle()->setAnimated(false);
        original.canvas()->addLayer(layer, false);
        QString error;
        QVERIFY2(ProjectSerializer::saveToFile(project, &original, &error), qPrintable(error));
        SWMMVis app;
        QVERIFY(QMetaObject::invokeMethod(&app, "onOpenProject", Qt::DirectConnection,
                                          Q_ARG(QString, project)));
        auto restored = [&]() -> SWMM2DResultsLayer * {
            for (auto *window : app.findChildren<SWMMVisProjectWindow *>())
                if (window->active2DResultsLayer())
                    return window->active2DResultsLayer();
            return nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(restored(), 30000);
        const auto *reopened = restored()->flowStreakSublayer();
        QVERIFY(reopened->isVisible());
        QCOMPARE(reopened->opacity(), sub->opacity());
        QCOMPARE(reopened->streakStyle()->toJson(), sub->streakStyle()->toJson());
        QVERIFY(!app.findChild<QCheckBox *>("flowStreakMotion"));
        reopened->streakStyle()->setAnimated(true);
        QVERIFY(reopened->streakStyle()->animated());
        for (auto *window : app.findChildren<SWMMVisProjectWindow *>())
            window->setHasChanges(false);
    }
};
QTEST_MAIN(FlowStreaksTest)
#include "test_flowstreaks.moc"
