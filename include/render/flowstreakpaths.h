// GPL-3.0-or-later
#pragma once

#include "layers/meshspatialgrid.h"
#include <QPointF>
#include <QRectF>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace OpenSWMM::Render {

// Immutable input from the displayed mesh. The private adjacency is prepared
// once by the serial flow worker; no worker accesses the owning layer.
struct FlowStreakMesh {
    // Capture is bounded before copying geometry. This is a regional working
    // limit, independent of the size of the source mesh.
    static constexpr int maxRegionTriangles = 250000;
    static std::shared_ptr<FlowStreakMesh> captureRegion(
        const QVector<QPointF> &vertices,
        const std::vector<std::array<int,3>> &triangles,
        const MeshSpatialGrid &grid, const QRectF &coverage);
    QRectF coverage;
    std::vector<int> sourceTriangles; // local -> global, ascending, includes dry cells
    QVector<QPointF> vertices;
    std::vector<std::array<int,3>> triangles;
    MeshSpatialGrid grid;
    std::vector<std::array<int,3>> neighbors;
    void prepare(const std::atomic_bool &cancelled);
    size_t bytes() const;
};

struct FlowStreakField {
    float vx = 0, vy = 0; // vector transformed to scene units / second
    float speed = 0;      // magnitude in model SI m/s, for styling
    std::array<float,3> depth{}; // signed displayed wet surface
    unsigned char exits = 0; // allowed outward crossings, opposite each corner
};

struct FlowStreakSample {
    int id = -1;
    std::array<QPointF,3> vertices;
    std::array<int,3> neighbors{{-1,-1,-1}};
    FlowStreakField field;
};
// Overview queries borrow the displayed frame on its owning thread. They are
// advanced in short slices, and must validate that frame before each slice.
struct FlowStreakSampler {
    struct Stats { int candidateTests=0; bool limited=false; };
    std::shared_ptr<Stats> stats;
    std::function<bool(QPointF &,FlowStreakSample &)> locate;
    std::function<bool(int,FlowStreakSample &)> cell;
    std::function<bool()> valid;
};

struct FlowStreakInput {
    std::shared_ptr<FlowStreakMesh> mesh;
    FlowStreakSampler sampler;
    std::vector<int> triangles; // ascending mesh-local ids, coverage + halo
    std::vector<FlowStreakField> fields;
    QRectF coverage;
    double spacing = 1; // scene units
    double maxLength = 1;
    double minDuration = 0; // extend beyond base length to support visual lifetime
    double step = 1;
    double cutoff = 0;
    double seedBudgetArea = 0; // unclipped padded view area: stable spacing while panning at mesh edges
    int maxPaths = 2000;
    int maxPoints = 64000;
};

struct FlowStreakPoint { QPointF position; float seconds = 0, speed = 0; };
struct FlowStreakPath {
    std::vector<FlowStreakPoint> points;
    double length = 0;
    float offset = 0; // deterministic phase in [0,1)
    quint64 seed = 0; // stable identity across hydraulic frames
    float appearedAt = -1; // presentation clock; negative means fully visible
    // A presentation anchor preserves a particle's head and remaining life
    // across compatible hydraulic frames. Phase ticks change uniforms only.
    float lifeClock = 0, lifePhase = 0, lifeHead = 0, lifeAge = 0, lifeRemaining = 0;
    quint32 lifeGeneration = 0;
};
struct FlowStreakBatch {
    std::vector<FlowStreakPath> paths;
    int seedAttempts = 0, cellVisits = 0, candidateTests = 0;
    bool limited = false;
    size_t bytes() const;
    int pointCount() const;
};

// Both the immutable regional worker and the cooperative native overview use
// the same edge-by-edge integration. No coarse vector can bridge a dry gap.
class FlowStreakTracer {
public:
    FlowStreakTracer(const FlowStreakInput &, FlowStreakSampler);
    bool advance(); // at most one seed and 512 native steps; true when complete
    FlowStreakBatch take() {
        if(sampler.stats) {out.candidateTests=sampler.stats->candidateTests;out.limited|=sampler.stats->limited;}
        return std::move(out);
    }
private:
    FlowStreakInput settings;
    FlowStreakSampler sampler;
    FlowStreakBatch out;
    double spacing=0, sx=0, sy=0;
    int cols=0, rows=0, seed=0, stride=1, points=0;
};

FlowStreakBatch buildFlowStreakPaths(const FlowStreakInput &input,
                                    const std::atomic_bool &cancelled);
struct FlowStreakMotion {
    double head = 0, age = 0, remaining = 0;
    quint32 generation = 0;
};
// A positive duration is a fixed window of model flow time: spatial length
// follows the local velocity. Zero retains the legacy mean-speed pixel fit.
double flowStreakTailSeconds(const FlowStreakPath &,double trailPx,double scale,double duration=0);
// Shared with the shader: bounded, deterministic random lifetimes in elapsed
// seconds, independent of redraw cadence and visual speed. No wrapped line.
float flowStreakLifetimeSeed(quint64 seed);
double flowStreakLifetime(float seed,quint32 generation,double minimum,double maximum);
FlowStreakMotion flowStreakMotion(const FlowStreakPath &,double tail,double phase,double clock,
                                 double minimum,double maximum,double speed);
double flowStreakOpacity(double pointTime,double tail,const FlowStreakMotion &,bool fade,double fadeSeconds);
double flowStreakAppearance(double appearedAt, double elapsed);
// One bounded presentation copy. Only phase/appearance metadata changes;
// every rendered position still comes from the newly validated wet paths.
class FlowStreakPlayback {
public:
    std::shared_ptr<const FlowStreakBatch> present(std::shared_ptr<const FlowStreakBatch>,
        double phase, double clock, double trailPx, double scale, bool fadeNew,
        double minimum=8,double maximum=16,double speed=2,double trailDuration=0);
    std::shared_ptr<const FlowStreakBatch> current() const { return m_current; }
    void clear() { m_raw.reset();m_current.reset(); }
private:
    std::shared_ptr<const FlowStreakBatch> m_raw,m_current;
    double m_trail=0,m_scale=0,m_trailDuration=0;
    double m_minimum=8,m_maximum=16,m_speed=2;
    bool m_fadeNew=false;
};

} // namespace OpenSWMM::Render
