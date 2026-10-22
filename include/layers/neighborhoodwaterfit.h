/* Shared surface fit for connected wet neighborhoods. GPL-3.0-or-later.
 * Included inside CellWaterGeometry after its Surface/CornerDepths definitions. */

/**
 * @file      neighborhoodwaterfit.h
 * @brief     Neighborhood reconstruction of wet-cell water surfaces.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_NEIGHBORHOODWATERFIT_H
#define OPENSWMM_NEIGHBORHOODWATERFIT_H

namespace NeighborhoodFit
{

// Integral and derivative of max(q,0) on a reference triangle. Clipping keeps
// barycentric coordinates, so the derivative is the wet-area shape-function
// integral, including partly wet triangles (not a finite-difference estimate).
/**
 * @brief Integrate the positive part of a linear depth over a normalized reference triangle.
 * @param[in] q Three finite signed corner depths in one vertical unit.
 * @param[out] derivative Replaced with normalized wet-area barycentric integrals, one per corner.
 * @return Area-normalized storage depth; multiply by physical triangle area for volume. Derivatives describe clipping-domain shape-function integrals, not a finite-difference estimate. Inputs are not changed.
 */
inline double triangleStorage(const std::array<double, 3> &q, std::array<double, 3> &derivative)
{
    struct Point {
        std::array<double, 3> w;
        double q;
    };
    derivative = {0, 0, 0};
    if (*std::max_element(q.begin(), q.end()) <= 0)
        return 0;
    if (*std::min_element(q.begin(), q.end()) >= 0) {
        derivative = {1.0 / 3, 1.0 / 3, 1.0 / 3};
        return (q[0] + q[1] + q[2]) / 3;
    }
    std::array<Point, 5> polygon{};
    int count = 0;
    for (int k = 0; k < 3; ++k) {
        const int j = (k + 1) % 3;
        Point a{{0, 0, 0}, q[k]}, b{{0, 0, 0}, q[j]};
        a.w[k] = 1;
        b.w[j] = 1;
        if (a.q >= 0)
            polygon[count++] = a;
        if ((a.q < 0) != (b.q < 0)) {
            const double t = a.q / (a.q - b.q);
            Point p{{0, 0, 0}, 0};
            for (int v = 0; v < 3; ++v)
                p.w[v] = a.w[v] + t * (b.w[v] - a.w[v]);
            polygon[count++] = p;
        }
    }
    double storage = 0;
    for (int k = 1; k + 1 < count; ++k) {
        const auto &a = polygon[0], &b = polygon[k], &c = polygon[k + 1];
        const double fraction =
            std::abs((b.w[1] - a.w[1]) * (c.w[2] - a.w[2]) - (b.w[2] - a.w[2]) * (c.w[1] - a.w[1]));
        storage += fraction * (a.q + b.q + c.q) / 3;
        for (int v = 0; v < 3; ++v)
            derivative[v] += fraction * (a.w[v] + b.w[v] + c.w[v]) / 3;
    }
    return storage;
}

struct Cell {
    int source = 0, component = 0, count = 3, halves = 1;
    std::array<int, 4> variable{};
    std::array<std::array<int, 3>, 2> local{};
    std::array<double, 2> fraction{1, 0};
    double area = 0, depth = 0, weight = 0;
};

/**
 * @brief Combine reference-triangle storage across one prepared fitted cell.
 * @param[in] cell Valid local/global variable mappings, one/two halves and normalized area fractions.
 * @param[in] field Finite signed-depth variables addressed by cell.variable.
 * @param[out] derivative Replaced with four local-corner wet-area integrals accumulated by half fraction; unused slots zero.
 * @param[in] offset Uniform depth offset added before clipping.
 * @param[in] cap Upper signed-depth clamp, default infinity, in the field unit.
 * @return Mean positive depth of the capped/offset piecewise-linear field. Derivative accumulation does not mask variables saturated by cap; callers needing that derivative distinction must handle it separately. No bounds validation occurs.
 */
inline double storage(const Cell &cell, const std::vector<double> &field,
                      std::array<double, 4> &derivative, double offset = 0,
                      double cap = std::numeric_limits<double>::infinity())
{
    derivative = {0, 0, 0, 0};
    double mean = 0;
    for (int s = 0; s < cell.halves; ++s) {
        std::array<double, 3> q{}, dq{};
        for (int k = 0; k < 3; ++k)
            q[k] = std::min(cap, field[size_t(cell.variable[cell.local[s][k]])] + offset);
        mean += cell.fraction[s] * triangleStorage(q, dq);
        for (int k = 0; k < 3; ++k)
            derivative[cell.local[s][k]] += cell.fraction[s] * dq[k];
    }
    return mean;
}

// Optional iteration counters for benchmarks. Null in production; counting
// never changes the arithmetic.
struct Stats {
    int gaussNewton = 0, cgRhs = 0, cgMass = 0, lineSearch = 0, bracket = 0, bisection = 0;
    bool fallback = false;
    size_t rows = 0, variables = 0, groups = 0;
};

struct Plane {
    double x = 0, y = 0, xx = 0, xy = 0, yy = 0, bx = 0, by = 0, gx = 0, gy = 0;
    bool wet = false;
};

// Owned by the serialized surface worker. Every field is reinitialized for
// each fit: re-use is allocation reuse, never a numerical warm start.
struct Workspace {
    std::vector<bool> eligible, connected;
    std::vector<int> parent, component, rootVariable, group, variableGroup;
    std::vector<Cell> rows;
    std::vector<Plane> planes;
    std::vector<std::array<double, 4>> jacobian;
    std::vector<double> pond, flow, weight, prior, pondCost, flowCost, target, reference, lower,
        upper, storageLower, storageUpper, minMass, maxMass, field, rhs, diagonal, delta, r, p,
        product, preconditioned, massGradient, massDirection, massResidual, numerator, denominator,
        candidate, meritResidual, lo, hi, scale, mass;
    /**
     * @return Approximate reserved scratch bytes from vector capacities (boolean capacities divided by eight), excluding vector/object/allocator overhead; not live payload size or process allocation. Reads only, with no synchronization.
     */
    [[nodiscard]] size_t bytes() const
    {
        size_t total = (eligible.capacity() + connected.capacity()) / 8;
        auto count = [&]<class T>(const std::vector<T> &v) { total += v.capacity() * sizeof(T); };
        count(parent);
        count(component);
        count(rootVariable);
        count(group);
        count(variableGroup);
        count(rows);
        count(planes);
        count(jacobian);
        for (const auto *v : {&pond,
                              &flow,
                              &weight,
                              &prior,
                              &pondCost,
                              &flowCost,
                              &target,
                              &reference,
                              &lower,
                              &upper,
                              &storageLower,
                              &storageUpper,
                              &minMass,
                              &maxMass,
                              &field,
                              &rhs,
                              &diagonal,
                              &delta,
                              &r,
                              &p,
                              &product,
                              &preconditioned,
                              &massGradient,
                              &massDirection,
                              &massResidual,
                              &numerator,
                              &denominator,
                              &candidate,
                              &meritResidual,
                              &lo,
                              &hi,
                              &scale,
                              &mass})
            count(*v);
        return total;
    }
};

// Small matrix-free Gauss-Newton fit. One variable belongs to one connected
// vertex fan, shared by BOTH traces of every supported edge. Storage rows use
// original volumes and the original quad diagonal. Local wet-cell envelopes
// bound the alternating vertex modes that cell volumes cannot identify.
/**
 * @brief Fit bounded shared water traces over hydraulically connected wet neighborhoods.
 * @param[in] cells Valid ordered triangle/quad splits, positive participating areas and consistent subtriangle membership.
 * @param[in] surfaces One reconstructed physical Surface per cell.
 * @param[in] x,y,z Stable finite vertex arrays with equal lengths and valid cell indices; this low-level helper does not validate those lengths/indices. Heights/depths share SI metres; XY use one consistent projected system.
 * @param[in] topology Shared-edge slots for exactly this mesh, each in range4*cells.size().
 * @param[in] depths One mean-depth value per cell, finite/positive for participating cells.
 * @param[in] flux Exactly four outward side-flux slots per cell; finite traces with opposite signs and relative residual<=1e-4 establish flow-supported contact. Additional still-water connections require positive shared-edge wet overlap.
 * @param[in,out] stats Optional exclusive counters incremented during solve; rows/variables/groups overwritten on a fitted path, fallback flag only set true. It is not reset on entry/early return.
 * @param[in,out] workspace Optional exclusive allocation reuse; every participating vector is reinitialized, with no numerical warm start. Null uses temporary scratch and releases setup allocations before solving.
 * @param[in] result Moved-by-value output capacity to reuse; previous elements are discarded.
 * @return One four-slot signed-depth array per cell, or empty for cell-array size mismatch or no flow-supported edge. Dry/invalid slots are NaN; disconnected wet cells retain isolated physical traces. Fitted components share corner variables and close total component volume within bounded numerical iterations, not each individual cell's volume.
 * @note Uses at most six Gauss-Newton passes, two matrix-free CG solves per pass (up to40 steps), backtracking and48 final volume bisections. Display policy argument is deliberately unused: thresholding belongs after fitting. No solver input is changed; distinct concurrent fits need distinct stats/workspace/output.
 */
inline std::vector<CornerDepths>
reconstruct(const std::vector<VertexDepthReconstruct::CellSplit> &cells,
            const std::vector<Surface> &surfaces, const std::vector<double> &x,
            const std::vector<double> &y, const std::vector<double> &z,
            const SmoothTopology &topology, const std::vector<float> &depths,
            const std::vector<float> &flux, VisibilityPolicy /*policy*/, Stats *stats = nullptr,
            Workspace *workspace = nullptr, std::vector<CornerDepths> result = {})
{
    const size_t n = cells.size();
    if (depths.size() != n || surfaces.size() != n || flux.size() != 4 * n)
        return {};
    const double nan = std::numeric_limits<double>::quiet_NaN();
    result.assign(n, CornerDepths{nan, nan, nan, nan});
    Workspace local;
    auto &w = workspace ? *workspace : local;
    auto &eligible = w.eligible;
    eligible.assign(n, false);
    auto &connected = w.connected;
    connected.assign(n, false);
    auto &parent = w.parent;
    parent.resize(4 * n);
    auto &component = w.component;
    component.resize(n);
    for (size_t i = 0; i < parent.size(); ++i)
        parent[i] = int(i);
    for (size_t i = 0; i < n; ++i)
        component[i] = int(i);
    auto root = [](std::vector<int> &p, int i) {
        while (p[size_t(i)] != i) {
            p[size_t(i)] = p[size_t(p[size_t(i)])];
            i = p[size_t(i)];
        }
        return i;
    };
    auto join = [&](std::vector<int> &p, int a, int b) {
        a = root(p, a);
        b = root(p, b);
        if (a != b)
            p[size_t(std::max(a, b))] = std::min(a, b);
    };
    double bound = 0;
    for (size_t c = 0; c < n; ++c) {
        const auto &cell = cells[c];
        // Visibility is applied AFTER fitting. A display threshold must not
        // add/remove hydraulic neighbours or change this objective's weights.
        if (surfaces[c].state != State::Wet)
            continue;
        const double area = cell.area[0] + (cell.nSub == 2 ? cell.area[1] : 0);
        if (!(area > 0) || !std::isfinite(area) || !(depths[c] > 0))
            continue;
        eligible[c] = true;
        for (int k = 0; k < cell.vertexCount(); ++k) {
            result[c][k] = surfaces[c].signedDepth(z[size_t(cell.v[k])]);
            bound = std::max(bound, result[c][k]);
            eligible[c] = eligible[c] && std::isfinite(flux[4 * c + k]);
        }
    }
    auto slot = [&](int a, int b) {
        const int nv = cells[size_t(a / 4)].vertexCount();
        const int start = (a % 4 + 1) % nv == b % 4 ? a % 4 : b % 4;
        return 4 * (a / 4) + (start + nv - 1) % nv;
    };
    bool any = false;
    for (const auto &e : topology.edges) {
        const int a = e.a0 / 4, b = e.a1 / 4;
        if (!eligible[size_t(a)] || !eligible[size_t(b)])
            continue;
        const double qa = flux[size_t(slot(e.a0, e.b0))], qb = flux[size_t(slot(e.a1, e.b1))];
        if (!(qa * qb < 0) || std::abs(qa + qb) > 1e-4 * std::max(std::abs(qa), std::abs(qb)))
            continue;
        join(parent, e.a0, e.a1);
        join(parent, e.b0, e.b1);
        join(component, a, b);
        connected[size_t(a)] = connected[size_t(b)] = true;
        any = true;
    }
    if (!any)
        return {};
    // Still-water connections use the existing positive-overlap rule. Flow
    // evidence additionally connects edges which the isolated VFR masks made
    // dry. A crest with neither support remains a separate vertex fan.
    for (const auto &e : topology.edges) {
        const size_t a = size_t(e.a0 / 4), b = size_t(e.a1 / 4);
        if (!eligible[a] || !eligible[b])
            continue;
        double lo = 0, hi = 1;
        auto wet = [&](double q0, double q1) {
            if (!(std::max(q0, q1) > 0))
                return false;
            if (q0 < 0)
                lo = std::max(lo, q0 / (q0 - q1));
            if (q1 < 0)
                hi = std::min(hi, q0 / (q0 - q1));
            return hi > lo;
        };
        if (!wet(result[a][e.a0 % 4], result[a][e.b0 % 4]) ||
            !wet(result[b][e.a1 % 4], result[b][e.b1 % 4]))
            continue;
        join(parent, e.a0, e.a1);
        join(parent, e.b0, e.b1);
        join(component, int(a), int(b));
        connected[a] = connected[b] = true;
    }

    auto &rootVariable = w.rootVariable;
    rootVariable.assign(4 * n, -1);
    auto &group = w.group;
    group.assign(n, -1);
    auto &rows = w.rows;
    rows.clear();
    rows.reserve(size_t(std::count(connected.begin(), connected.end(), true)));
    auto &pond = w.pond;
    pond.clear();
    auto &flow = w.flow;
    flow.clear();
    auto &weight = w.weight;
    weight.clear();
    auto &prior = w.prior;
    prior.clear();
    auto &variableGroup = w.variableGroup;
    variableGroup.clear();
    int groups = 0;
    for (size_t c = 0; c < n; ++c) {
        if (!connected[c])
            continue;
        const auto &source = cells[c];
        Cell row;
        row.source = int(c);
        row.count = source.vertexCount();
        row.halves = source.nSub;
        row.area = source.area[0] + (source.nSub == 2 ? source.area[1] : 0);
        row.depth = depths[c];
        const double scale = std::max(row.depth, 1e-6);
        row.weight = row.area / (scale * scale);
        const int comp = root(component, int(c));
        if (group[size_t(comp)] < 0)
            group[size_t(comp)] = groups++;
        row.component = group[size_t(comp)];
        for (int k = 0; k < row.count; ++k) {
            const int r = root(parent, 4 * int(c) + k);
            if (rootVariable[size_t(r)] < 0) {
                rootVariable[size_t(r)] = int(pond.size());
                pond.push_back(0);
                flow.push_back(0);
                weight.push_back(0);
                prior.push_back(0);
                variableGroup.push_back(row.component);
            }
            const int v = row.variable[k] = rootVariable[size_t(r)];
            const double w = row.area / row.count;
            pond[size_t(v)] += w * result[c][k];
            flow[size_t(v)] += w * row.depth;
            weight[size_t(v)] += w;
            prior[size_t(v)] += 0.0025 * row.weight / row.count;
        }
        for (int s = 0; s < row.halves; ++s) {
            row.fraction[s] = source.area[s] / row.area;
            for (int k = 0; k < 3; ++k)
                row.local[s][k] =
                    int(std::find(source.v.begin(), source.v.end(), source.sub[s][k]) -
                        source.v.begin());
        }
        rows.push_back(row);
    }
    for (size_t v = 0; v < pond.size(); ++v) {
        pond[v] /= weight[v];
        flow[v] /= weight[v];
    }
    auto &pondCost = w.pondCost;
    pondCost.assign(pond.size(), 0);
    auto &flowCost = w.flowCost;
    flowCost.assign(pond.size(), 0);
    auto &target = w.target;
    target.assign(size_t(groups), 0);
    for (const auto &row : rows) {
        std::array<double, 4> derivative;
        const double a = storage(row, pond, derivative) - row.depth,
                     b = storage(row, flow, derivative) - row.depth;
        for (int k = 0; k < row.count; ++k) {
            const size_t v = size_t(row.variable[k]);
            pondCost[v] += row.weight * a * a / row.count;
            flowCost[v] += row.weight * b * b / row.count;
        }
        target[size_t(row.component)] += row.area * row.depth;
    }
    // A level lake's clipped-volume residual is zero; a moving uniform sheet's
    // depth residual is zero. Blend the two neighborhood references by their
    // measured storage residuals on the incident cell fan. Continuous weights avoid a
    // mode switch when their relative quality changes between saved frames.
    auto &reference = w.reference;
    reference.resize(pond.size());
    for (size_t v = 0; v < reference.size(); ++v) {
        const double total = pondCost[v] + flowCost[v];
        const double moving = total > 0 ? pondCost[v] / total : 0;
        reference[v] = pond[v] + moving * (flow[v] - pond[v]);
    }

    // A fully wet cell supplies a local surface envelope, independent of the
    // large VFR pool depths on a nearby partly wet slope. Reconstruct its
    // stage gradient from supported, fully wet neighbours so a resolved slope
    // is retained. Work at local coordinate/elevation differences for datum
    // invariance. Partly wet cells still retain signed, negative dry corners.
    // Plane scratch belongs only to fitted cells. Dry regions never enter
    // the solve; re-use the completed component map for cell-to-row lookup.
    component.assign(n, -1);
    for (size_t i = 0; i < rows.size(); ++i)
        component[size_t(rows[i].source)] = int(i);
    auto &planes = w.planes;
    planes.assign(rows.size(), Plane{});
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        const auto &cell = cells[size_t(row.source)];
        auto &plane = planes[i];
        const int origin = cell.v[0];
        plane.wet = true;
        for (int s = 0; s < row.halves; ++s)
            for (int k = 0; k < 3; ++k) {
                const int v = cell.sub[s][k];
                plane.x += row.fraction[s] * (x[size_t(v)] - x[size_t(origin)]) / 3;
                plane.y += row.fraction[s] * (y[size_t(v)] - y[size_t(origin)]) / 3;
            }
        plane.x += x[size_t(origin)];
        plane.y += y[size_t(origin)];
        for (int k = 0; k < row.count; ++k)
            plane.wet = plane.wet && result[size_t(row.source)][k] > 0;
    }
    auto observe = [&](int a, int b) {
        const int ai = component[size_t(a)], bi = component[size_t(b)];
        if (ai < 0 || bi < 0)
            return;
        auto &p = planes[size_t(ai)];
        const auto &q = planes[size_t(bi)];
        if (!p.wet || !q.wet)
            return;
        const double dx = q.x - p.x, dy = q.y - p.y, d2 = dx * dx + dy * dy;
        if (!(d2 > 0))
            return;
        const auto &sa = surfaces[size_t(a)], &sb = surfaces[size_t(b)];
        const double dh = (sb.datum - sa.datum) + (sb.level - sa.level);
        p.xx += dx * dx / d2;
        p.xy += dx * dy / d2;
        p.yy += dy * dy / d2;
        p.bx += dx * dh / d2;
        p.by += dy * dh / d2;
    };
    for (const auto &e : topology.edges) {
        // Same endpoint fan means this edge has hydraulic support. Merely
        // sharing a vertex or belonging to a distant component is not enough.
        if (root(parent, e.a0) != root(parent, e.a1) || root(parent, e.b0) != root(parent, e.b1))
            continue;
        observe(e.a0 / 4, e.a1 / 4);
        observe(e.a1 / 4, e.a0 / 4);
    }
    for (auto &p : planes) {
        const double det = p.xx * p.yy - p.xy * p.xy;
        if (det > 1e-12 * (p.xx + p.yy) * (p.xx + p.yy)) {
            p.gx = (p.yy * p.bx - p.xy * p.by) / det;
            p.gy = (p.xx * p.by - p.xy * p.bx) / det;
        }
    }
    const double inf = std::numeric_limits<double>::infinity();
    auto &lower = w.lower;
    lower.assign(pond.size(), inf);
    auto &upper = w.upper;
    upper.assign(pond.size(), -inf);
    auto &storageLower = w.storageLower;
    storageLower.assign(pond.size(), inf);
    auto &storageUpper = w.storageUpper;
    storageUpper.assign(pond.size(), -inf);
    for (const auto &row : rows) {
        const auto c = size_t(row.source);
        const auto &cell = cells[c];
        const auto &plane = planes[size_t(component[c])];
        for (int k = 0; k < row.count; ++k) {
            const size_t v = size_t(row.variable[k]);
            const double q = result[c][k];
            // This wider envelope always brackets each original cell volume,
            // including disconnected shorelines and variable-depth quads.
            storageLower[v] = std::min({storageLower[v], q, row.depth});
            storageUpper[v] = std::max({storageUpper[v], q, row.depth});
            if (plane.wet) {
                const size_t vertex = size_t(cell.v[k]);
                const double projected = std::clamp(q + plane.gx * (x[vertex] - plane.x) +
                                                        plane.gy * (y[vertex] - plane.y),
                                                    0.0, bound);
                lower[v] = std::min({lower[v], row.depth, projected});
                upper[v] = std::max({upper[v], row.depth, projected});
            }
        }
    }
    // A signed dry trace is shoreline evidence even when another incident
    // cell contains a film. Do not force such a crest corner above the bed:
    // its extra storage would otherwise have to be taken from the wet plain.
    for (size_t v = 0; v < lower.size(); ++v)
        if (!std::isfinite(lower[v]) || storageLower[v] < 0) {
            lower[v] = storageLower[v];
            upper[v] = storageUpper[v];
        }
    // A wet-cell bound can conflict with the storage of an attached partial
    // pool. Detect feasibility per component before solving; only that
    // component uses the wider, storage-supported envelope in that case.
    auto &minMass = w.minMass;
    minMass.assign(size_t(groups), 0);
    auto &maxMass = w.maxMass;
    maxMass.assign(size_t(groups), 0);
    for (const auto &row : rows) {
        std::array<double, 4> derivative;
        minMass[size_t(row.component)] += row.area * storage(row, lower, derivative);
        maxMass[size_t(row.component)] += row.area * storage(row, upper, derivative);
    }
    for (size_t v = 0; v < lower.size(); ++v) {
        const size_t g = size_t(variableGroup[v]);
        if (minMass[g] > target[g] || maxMass[g] < target[g]) {
            lower[v] = storageLower[v];
            upper[v] = storageUpper[v];
            if (stats)
                stats->fallback = true;
        }
    }
    // Setup fields no longer participate in any arithmetic below. Releasing
    // them before solve allocation avoids overlapping full-domain scratch.
    auto release = []<class T>(std::vector<T> &values) { std::vector<T>().swap(values); };
    if (!workspace) {
        release(eligible);
        release(connected);
        release(parent);
        release(component);
        release(rootVariable);
        release(group);
        release(pond);
        release(flow);
        release(weight);
        release(pondCost);
        release(flowCost);
        release(planes);
        release(storageLower);
        release(storageUpper);
        release(minMass);
        release(maxMass);
    }
    auto bounded = [&](size_t v, double value) { return std::clamp(value, lower[v], upper[v]); };
    auto &field = w.field;
    field = reference;
    for (size_t v = 0; v < field.size(); ++v)
        field[v] = bounded(v, field[v]);
    auto &jacobian = w.jacobian;
    jacobian.resize(rows.size());
    const size_t nv = field.size();
    if (stats) {
        stats->rows = rows.size();
        stats->variables = nv;
        stats->groups = size_t(groups);
    }
    auto &rhs = w.rhs;
    rhs.resize(nv);
    auto &diagonal = w.diagonal;
    diagonal.resize(nv);
    auto &delta = w.delta;
    delta.resize(nv);
    auto &r = w.r;
    r.resize(nv);
    auto &p = w.p;
    p.resize(nv);
    auto &product = w.product;
    product.resize(nv);
    auto &preconditioned = w.preconditioned;
    preconditioned.resize(nv);
    auto &massGradient = w.massGradient;
    massGradient.resize(nv);
    auto &massDirection = w.massDirection;
    massDirection.resize(nv);
    auto &massResidual = w.massResidual;
    massResidual.assign(size_t(groups), 0);
    auto &numerator = w.numerator;
    numerator.assign(size_t(groups), 0);
    auto &denominator = w.denominator;
    denominator.assign(size_t(groups), 0);
    auto &candidate = w.candidate;
    candidate.resize(nv);
    auto &meritResidual = w.meritResidual;
    meritResidual.assign(size_t(groups), 0);
    auto dot = [](const std::vector<double> &a, const std::vector<double> &b) {
        double sum = 0;
        for (size_t i = 0; i < a.size(); ++i)
            sum += a[i] * b[i];
        return sum;
    };
    for (int iteration = 0; iteration < 6; ++iteration) {
        if (stats)
            ++stats->gaussNewton;
        diagonal = prior;
        std::fill(massGradient.begin(), massGradient.end(), 0);
        for (int g = 0; g < groups; ++g)
            massResidual[size_t(g)] = -target[size_t(g)];
        for (size_t v = 0; v < nv; ++v)
            rhs[v] = -prior[v] * (field[v] - reference[v]);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto &row = rows[i];
            const double residual = storage(row, field, jacobian[i]) - row.depth;
            massResidual[size_t(row.component)] += row.area * (residual + row.depth);
            for (int k = 0; k < row.count; ++k) {
                const size_t v = size_t(row.variable[k]);
                const double j = jacobian[i][k];
                rhs[v] -= row.weight * j * residual;
                diagonal[v] += row.weight * j * j;
                massGradient[v] += row.area * j;
            }
        }
        auto multiply = [&](const std::vector<double> &a, std::vector<double> &b) {
            for (size_t v = 0; v < nv; ++v)
                b[v] = prior[v] * a[v];
            for (size_t i = 0; i < rows.size(); ++i) {
                const auto &row = rows[i];
                double s = 0;
                for (int k = 0; k < row.count; ++k)
                    s += jacobian[i][k] * a[size_t(row.variable[k])];
                for (int k = 0; k < row.count; ++k)
                    b[size_t(row.variable[k])] += row.weight * jacobian[i][k] * s;
            }
        };
        auto solve = [&](const std::vector<double> &source, std::vector<double> &solution,
                         int *count) {
            std::fill(solution.begin(), solution.end(), 0);
            r = source;
            for (size_t v = 0; v < nv; ++v)
                preconditioned[v] = r[v] / diagonal[v];
            p = preconditioned;
            double residual = dot(r, preconditioned), initial = residual;
            for (int cg = 0; cg < 40 && residual > initial * 1e-12 && residual > 1e-24; ++cg) {
                if (count)
                    ++*count;
                multiply(p, product);
                const double denom = dot(p, product);
                if (!(denom > 0))
                    break;
                const double alpha = residual / denom;
                for (size_t v = 0; v < nv; ++v) {
                    solution[v] += alpha * p[v];
                    r[v] -= alpha * product[v];
                    preconditioned[v] = r[v] / diagonal[v];
                }
                const double next = dot(r, preconditioned), beta = next / residual;
                for (size_t v = 0; v < nv; ++v)
                    p[v] = preconditioned[v] + beta * p[v];
                residual = next;
            }
        };
        // Enforce the linearized component volume with a Lagrange multiplier.
        // A post-fit global offset alone would inundate shallow neighbours.
        solve(rhs, delta, stats ? &stats->cgRhs : nullptr);
        solve(massGradient, massDirection, stats ? &stats->cgMass : nullptr);
        numerator = massResidual;
        std::fill(denominator.begin(), denominator.end(), 0);
        for (size_t v = 0; v < nv; ++v) {
            const size_t g = size_t(variableGroup[v]);
            numerator[g] += massGradient[v] * delta[v];
            denominator[g] += massGradient[v] * massDirection[v];
        }
        for (size_t v = 0; v < nv; ++v) {
            const size_t g = size_t(variableGroup[v]);
            if (denominator[g] > 0)
                delta[v] -= numerator[g] / denominator[g] * massDirection[v];
        }
        // Include conservation in the line search: a necessary volume repair
        // can temporarily increase the unconstrained storage residual.
        // One storage pass gives the weighted residual and each component's
        // volume residual. Every sum keeps its original order of terms.
        auto merit = [&](const std::vector<double> &trial) {
            double value = 0;
            std::array<double, 4> derivative;
            std::fill(meritResidual.begin(), meritResidual.end(), 0);
            for (const auto &row : rows) {
                const double r = storage(row, trial, derivative) - row.depth;
                value += row.weight * r * r;
                meritResidual[size_t(row.component)] += row.area * r;
            }
            for (size_t v = 0; v < trial.size(); ++v)
                value += prior[v] * (trial[v] - reference[v]) * (trial[v] - reference[v]);
            for (int g = 0; g < groups; ++g)
                if (denominator[size_t(g)] > 0) {
                    const double m = meritResidual[size_t(g)], d = denominator[size_t(g)];
                    value += (2 * std::abs(numerator[size_t(g)] * m) + m * m) / d;
                }
            return value;
        };
        const double previous = merit(field);
        bool accepted = false;
        double acceptedMerit = previous;
        for (double step = 1; step >= 1.0 / 128; step *= 0.5) {
            if (stats)
                ++stats->lineSearch;
            for (size_t v = 0; v < nv; ++v)
                candidate[v] = bounded(v, field[v] + step * delta[v]);
            acceptedMerit = merit(candidate);
            if (acceptedMerit < previous) {
                field.swap(candidate);
                accepted = true;
                break;
            }
        }
        // The accepted trial's merit is the merit of the new field.
        if (!accepted || previous - acceptedMerit < 1e-10 * std::max(previous, 1e-10))
            break;
    }

    // Close component volume INSIDE the same envelope used by the solve.
    // A common monotone interpolation toward the lower/upper field retains
    // shared traces and cannot recreate a forbidden peak or dry pocket.
    auto scaled = [&](size_t v, double fraction) {
        return fraction < 0 ? field[v] + fraction * (field[v] - lower[v])
                            : field[v] + fraction * (upper[v] - field[v]);
    };
    auto massFor = [&](const std::vector<double> &scale, std::vector<double> &mass) {
        std::fill(mass.begin(), mass.end(), 0);
        for (const auto &row : rows) {
            double mean = 0;
            for (int s = 0; s < row.halves; ++s) {
                std::array<double, 3> q{}, derivative{};
                for (int k = 0; k < 3; ++k)
                    q[k] =
                        scaled(size_t(row.variable[row.local[s][k]]), scale[size_t(row.component)]);
                mean += row.fraction[s] * triangleStorage(q, derivative);
            }
            mass[size_t(row.component)] += row.area * mean;
        }
    };
    auto &lo = w.lo;
    lo.assign(size_t(groups), -1);
    auto &hi = w.hi;
    hi.assign(size_t(groups), 1);
    auto &scale = w.scale;
    scale.assign(size_t(groups), 0);
    auto &mass = w.mass;
    mass.assign(size_t(groups), 0);
    for (int iteration = 0; iteration < 48; ++iteration) {
        if (stats)
            ++stats->bisection;
        for (int g = 0; g < groups; ++g)
            scale[size_t(g)] = (lo[size_t(g)] + hi[size_t(g)]) / 2;
        massFor(scale, mass);
        for (int g = 0; g < groups; ++g) {
            if (mass[size_t(g)] < target[size_t(g)])
                lo[size_t(g)] = scale[size_t(g)];
            else
                hi[size_t(g)] = scale[size_t(g)];
        }
    }
    for (const auto &row : rows)
        for (int k = 0; k < row.count; ++k)
            result[size_t(row.source)][k] =
                scaled(size_t(row.variable[k]), scale[size_t(row.component)]);
    return result;
}
} // namespace NeighborhoodFit
#endif
