// SPDX-License-Identifier: Apache-2.0
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "meshkit/job.hpp"
#include "meshkit/io.hpp"
#include "meshkit/raster.hpp"
#include "meshkit/constraints.hpp"
#include "meshkit/constraintaudit.hpp"
#include "mixed.hpp"
#include <thread>
#include <condition_variable>
#include <chrono>
#include <set>
namespace
{
using namespace meshkit;
struct PythonError {
};
struct Ref {
    PyObject *p = nullptr;
    Ref() = default;
    explicit Ref(PyObject *v) : p(v) {}
    ~Ref() { Py_XDECREF(p); }
    Ref(const Ref &) = delete;
    PyObject *release()
    {
        auto v = p;
        p = nullptr;
        return v;
    }
};
[[noreturn]] void invalid(const std::string &s)
{
    PyErr_SetString(PyExc_ValueError, s.c_str());
    throw PythonError{};
}
void checked(bool v)
{
    if (!v)
        throw PythonError{};
}
void put(PyObject *d, const char *k, PyObject *v)
{
    Ref value(v);
    checked(v && PyDict_SetItemString(d, k, v) == 0);
}
std::string string(PyObject *p)
{
    if (!PyUnicode_Check(p))
        invalid("Expected a string.");
    Py_ssize_t count = 0;
    const char *s = PyUnicode_AsUTF8AndSize(p, &count);
    if (!s)
        throw PythonError{};
    return std::string(s, count);
}
void keys(PyObject *d, std::initializer_list<const char *> allowed)
{
    if (!PyDict_Check(d))
        invalid("Expected a mapping.");
    std::set<std::string> valid;
    for (auto s : allowed)
        valid.insert(s);
    Py_ssize_t at = 0;
    PyObject *k, *v;
    while (PyDict_Next(d, &at, &k, &v))
        if (!valid.count(string(k)))
            invalid("Unknown field: " + string(k));
}
PyObject *field(PyObject *d, const char *key)
{
    return d ? PyDict_GetItemString(d, key) : nullptr;
}
double number(PyObject *p)
{
    double v = PyFloat_AsDouble(p);
    if (PyErr_Occurred())
        throw PythonError{};
    return v;
}
std::int64_t integer(PyObject *p)
{
    auto v = PyLong_AsLongLong(p);
    if (PyErr_Occurred())
        throw PythonError{};
    return v;
}
bool boolean(PyObject *p)
{
    int value = PyObject_IsTrue(p);
    if (value < 0)
        throw PythonError{};
    return value;
}
#define REAL(d, k, v)                                                                              \
    if (auto item = field(d, k))                                                                   \
    v = number(item)
template <class T> T integerField(PyObject *item)
{
    if constexpr (std::is_signed<T>::value) {
        auto value = PyLong_AsLongLong(item);
        if (PyErr_Occurred())
            throw PythonError{};
        if (value < std::numeric_limits<T>::lowest() || value > std::numeric_limits<T>::max())
            invalid("Integer is out of range.");
        return static_cast<T>(value);
    } else {
        auto value = PyLong_AsUnsignedLongLong(item);
        if (PyErr_Occurred())
            throw PythonError{};
        if (value > std::numeric_limits<T>::max())
            invalid("Integer is out of range.");
        return static_cast<T>(value);
    }
}
#define INT(d, k, v)                                                                               \
    if (auto item = field(d, k))                                                                   \
    v = integerField<std::decay_t<decltype(v)>>(item)
#define BOOL(d, k, v)                                                                              \
    if (auto item = field(d, k))                                                                   \
    v = boolean(item)
struct Buffer {
    Py_buffer b{};
    bool held = false;
    explicit Buffer(PyObject *p)
    {
        held = PyObject_CheckBuffer(p);
        if (held && PyObject_GetBuffer(p, &b, PyBUF_FORMAT | PyBUF_STRIDES) != 0)
            throw PythonError{};
    }
    ~Buffer()
    {
        if (held)
            PyBuffer_Release(&b);
    }
};
template <class T> T convertInteger(std::int64_t value)
{
    if constexpr (std::is_signed<T>::value) {
        if (value < std::int64_t(std::numeric_limits<T>::lowest()) ||
            value > std::int64_t(std::numeric_limits<T>::max()))
            invalid("Integer is out of range.");
    } else if (value < 0 || std::uint64_t(value) > std::uint64_t(std::numeric_limits<T>::max()))
        invalid("Integer is out of range.");
    return T(value);
}
std::vector<Point2> points(PyObject *p)
{
    Buffer view(p);
    std::vector<Point2> out;
    if (view.held) {
        auto &b = view.b;
        if (b.ndim != 2 || b.shape[1] != 2 || b.itemsize != 8 || !b.format ||
            std::string(b.format) != "d")
            invalid("points/ring must have native float64 format and shape (N, 2).");
        if (b.shape[0] > 20000000)
            invalid("Too many source points.");
        out.resize(b.shape[0]);
        for (Py_ssize_t i = 0; i < b.shape[0]; ++i) {
            double x, y;
            std::memcpy(&x, static_cast<char *>(b.buf) + i * b.strides[0], 8);
            std::memcpy(&y, static_cast<char *>(b.buf) + i * b.strides[0] + b.strides[1], 8);
            out[i] = {x, y};
        }
        return out;
    }
    Ref list(PySequence_Fast(p, "Expected point rows."));
    if (!list.p)
        throw PythonError{};
    auto n = PySequence_Fast_GET_SIZE(list.p);
    if (n > 20000000)
        invalid("Too many source points.");
    out.reserve(n);
    for (Py_ssize_t i = 0; i < n; ++i) {
        Ref row(PySequence_Fast(PySequence_Fast_GET_ITEM(list.p, i), "Expected [x,y]."));
        if (!row.p)
            throw PythonError{};
        if (PySequence_Fast_GET_SIZE(row.p) != 2)
            invalid("Point needs exactly two coordinates.");
        out.emplace_back(number(PySequence_Fast_GET_ITEM(row.p, 0)),
                         number(PySequence_Fast_GET_ITEM(row.p, 1)));
    }
    return out;
}
template <class T> std::vector<T> integers(PyObject *p)
{
    Buffer view(p);
    std::vector<T> out;
    if (view.held) {
        auto &b = view.b;
        if (b.ndim != 1 || !b.format || (b.itemsize != 4 && b.itemsize != 8))
            invalid("Integer arrays must be one dimensional with native 32/64-bit integer format.");
        const std::string format = b.format;
        bool signedValue = format == "i" || format == "l" || format == "q";
        bool unsignedValue = format == "I" || format == "L" || format == "Q";
        if (!signedValue && !unsignedValue)
            invalid("Expected native integer buffer format.");
        out.reserve(b.shape[0]);
        for (Py_ssize_t i = 0; i < b.shape[0]; ++i) {
            std::uint64_t raw = 0;
            std::memcpy(&raw, static_cast<char *>(b.buf) + i * b.strides[0], b.itemsize);
            if (signedValue) {
                std::int64_t value;
                if (b.itemsize == 4) {
                    std::int32_t small;
                    std::uint32_t bits = raw;
                    std::memcpy(&small, &bits, 4);
                    value = small;
                } else
                    std::memcpy(&value, &raw, 8);
                out.push_back(convertInteger<T>(value));
            } else {
                if (raw > std::uint64_t(std::numeric_limits<T>::max()))
                    invalid("Unsigned integer is out of range.");
                out.push_back(T(raw));
            }
        }
        return out;
    }
    Ref list(PySequence_Fast(p, "Expected integer sequence."));
    if (!list.p)
        throw PythonError{};
    auto n = PySequence_Fast_GET_SIZE(list.p);
    out.reserve(n);
    for (Py_ssize_t i = 0; i < n; ++i) {
        const auto v = integer(PySequence_Fast_GET_ITEM(list.p, i));
        out.push_back(convertInteger<T>(v));
    }
    return out;
}
std::vector<double> reals(PyObject *p)
{
    Buffer view(p);
    std::vector<double> out;
    if (view.held) {
        auto &b = view.b;
        if (b.ndim != 1 || !b.format ||
            (std::string(b.format) != "d" && std::string(b.format) != "f"))
            invalid("Expected native float32/64 one-dimensional buffer.");
        out.resize(b.shape[0]);
        for (Py_ssize_t i = 0; i < b.shape[0]; ++i) {
            const char *at = static_cast<char *>(b.buf) + i * b.strides[0];
            if (b.itemsize == 8)
                std::memcpy(&out[i], at, 8);
            else if (b.itemsize == 4) {
                float value;
                std::memcpy(&value, at, 4);
                out[i] = value;
            } else
                invalid("Unsupported float format.");
        }
        return out;
    }
    Ref list(PySequence_Fast(p, "Expected number sequence."));
    if (!list.p)
        throw PythonError{};
    out.reserve(PySequence_Fast_GET_SIZE(list.p));
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(list.p); ++i)
        out.push_back(number(PySequence_Fast_GET_ITEM(list.p, i)));
    return out;
}
template <class F> void each(PyObject *p, F f)
{
    Ref list(PySequence_Fast(p, "Expected sequence."));
    if (!list.p)
        throw PythonError{};
    for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(list.p); ++i)
        f(PySequence_Fast_GET_ITEM(list.p, i));
}
TerrainTolerancePolicy terrainPolicy(PyObject *d)
{
    keys(d, {"mode", "reference_size", "minimum_tolerance", "maximum_tolerance"});
    TerrainTolerancePolicy p;
    if (auto v = field(d, "mode")) {
        auto mode = string(v);
        if (mode == "constant")
            p.mode = TerrainToleranceMode::Constant;
        else if (mode == "target_size")
            p.mode = TerrainToleranceMode::TargetSize;
        else
            invalid("Unknown terrain policy mode.");
    }
    REAL(d, "reference_size", p.referenceSize);
    REAL(d, "minimum_tolerance", p.minimumTolerance);
    REAL(d, "maximum_tolerance", p.maximumTolerance);
    return p;
}
Limits limits(PyObject *d)
{
    keys(d, {"max_vertices", "max_cells", "max_edges", "max_workspace_bytes", "max_work",
             "max_metadata_bytes", "max_record_bytes"});
    Limits l;
    INT(d, "max_vertices", l.maxVertices);
    INT(d, "max_cells", l.maxCells);
    INT(d, "max_edges", l.maxEdges);
    INT(d, "max_workspace_bytes", l.maxWorkspaceBytes);
    INT(d, "max_work", l.maxWork);
    INT(d, "max_metadata_bytes", l.maxMetadataBytes);
    INT(d, "max_record_bytes", l.maxRecordBytes);
    return l;
}
JobOptions options(PyObject *d)
{
    keys(d, {"cells",
             "minimum_target",
             "maximum_size",
             "minimum_edge",
             "minimum_angle_degrees",
             "adjacent_ratio",
             "flat_elevation",
             "smoothing_passes",
             "coarsen_passes",
             "sizing_repair_passes",
             "repair_insertions",
             "coarsening_order",
             "coarsening_reference_size",
             "terrain_tolerance",
             "terrain_policy",
             "strict_sizing",
             "require_terrain_certification",
             "retain_input_mapping",
             "limits",
             "automatic_blocks",
             "maximum_patch_cells",
             "maximum_automatic_blocks"});
    JobOptions o;
    if (auto v = field(d, "cells")) {
        auto type = string(v);
        if (type == "triangles")
            o.cells = CellPlan::Triangles;
        else if (type == "mixed")
            o.cells = CellPlan::AutomaticMixed;
        else
            invalid("cells must be triangles or mixed.");
    }
    REAL(d, "minimum_target", o.minimumTarget);
    REAL(d, "maximum_size", o.maximumSize);
    REAL(d, "minimum_edge", o.minimumEdge);
    REAL(d, "minimum_angle_degrees", o.minimumAngleDegrees);
    REAL(d, "adjacent_ratio", o.adjacentRatio);
    REAL(d, "flat_elevation", o.flatElevation);
    INT(d, "smoothing_passes", o.smoothingPasses);
    INT(d, "coarsen_passes", o.coarsenPasses);
    INT(d, "sizing_repair_passes", o.sizingRepairPasses);
    INT(d, "repair_insertions", o.repairInsertions);
    REAL(d, "coarsening_reference_size", o.coarseningReferenceSize);
    REAL(d, "terrain_tolerance", o.terrainTolerance);
    BOOL(d, "strict_sizing", o.strictSizing);
    BOOL(d, "require_terrain_certification", o.requireTerrainCertification);
    BOOL(d, "retain_input_mapping", o.retainInputMapping);
    BOOL(d, "automatic_blocks", o.automaticBlocks);
    INT(d, "maximum_patch_cells", o.maximumPatchCells);
    INT(d, "maximum_automatic_blocks", o.maximumAutomaticBlocks);
    if (auto v = field(d, "coarsening_order")) {
        auto type = string(v);
        if (type == "legacy_flatness")
            o.coarseningOrder = CoarseningOrder::LegacyFlatness;
        else if (type == "resolution_aware")
            o.coarseningOrder = CoarseningOrder::ResolutionAware;
        else
            invalid("Unknown coarsening order.");
    }
    if (auto v = field(d, "terrain_policy"))
        o.terrainPolicy = terrainPolicy(v);
    if (auto v = field(d, "limits"))
        o.limits = limits(v);
    return o;
}
JobRequest request(PyObject *d)
{
    keys(d, {"points", "paths", "options", "metadata", "point_ids", "point_markers",
             "point_elevations", "quad_regions", "region_seeds", "labels", "size_field"});
    JobRequest r;
    auto source = field(d, "points");
    if (!source)
        invalid("points is required.");
    r.points = points(source);
    if (auto v = field(d, "options"))
        r.options = options(v);
    if (auto v = field(d, "point_elevations"))
        r.pointElevations = reals(v);
    if (auto v = field(d, "point_ids"))
        r.pointIds = integers<std::int64_t>(v);
    if (auto v = field(d, "point_markers"))
        r.pointMarkers = integers<std::int32_t>(v);
    if (auto paths = field(d, "paths"))
        each(paths, [&](PyObject *p) {
            keys(p, {"vertices", "kind", "marker", "id", "closed"});
            Path path;
            auto indexes = field(p, "vertices");
            if (!indexes)
                invalid("Path vertices required.");
            path.vertices = integers<Index>(indexes);
            if (auto kind = field(p, "kind")) {
                auto s = string(kind);
                if (s == "outer")
                    path.kind = PathKind::Outer;
                else if (s == "hole")
                    path.kind = PathKind::Hole;
                else if (s == "line")
                    path.kind = PathKind::Line;
                else if (s == "region")
                    path.kind = PathKind::Region;
                else
                    invalid("Unknown path kind.");
            }
            INT(p, "marker", path.marker);
            INT(p, "id", path.id);
            BOOL(p, "closed", path.closed);
            r.paths.push_back(std::move(path));
        });
    if (auto v = field(d, "metadata")) {
        keys(v, {"horizontal_metres_per_unit", "vertical_metres_per_unit", "units_known", "crs",
                 "description"});
        REAL(v, "horizontal_metres_per_unit", r.metadata.horizontalMetresPerUnit);
        REAL(v, "vertical_metres_per_unit", r.metadata.verticalMetresPerUnit);
        BOOL(v, "units_known", r.metadata.unitsKnown);
        if (auto p = field(v, "crs"))
            r.metadata.crs = string(p);
        if (auto p = field(v, "description"))
            r.metadata.description = string(p);
    }
    if (auto v = field(d, "labels"))
        each(v, [&](PyObject *p) {
            keys(p, {"id", "name"});
            Label l;
            if (auto value = field(p, "name"))
                l.name = string(value);
            INT(p, "id", l.id);
            r.labels.push_back(std::move(l));
        });
    if (auto v = field(d, "region_seeds"))
        each(v, [&](PyObject *p) {
            keys(p, {"point", "region"});
            auto xy = field(p, "point");
            if (!xy)
                invalid("Region point required.");
            Ref wrapper(PyTuple_Pack(1, xy));
            auto coordinate = points(wrapper.p);
            RegionSeed s;
            s.point = coordinate[0];
            INT(p, "region", s.region);
            r.regions.push_back(s);
        });
    if (auto v = field(d, "quad_regions"))
        each(v, [&](PyObject *p) {
            keys(p, {"ring", "cells", "spacing", "h_along", "h_across", "along_angle_degrees",
                     "aspect_max", "attribute", "tag", "corners", "holes"});
            plan::QuadRegion q;
            auto ring = field(p, "ring");
            if (!ring)
                invalid("Quad region ring required.");
            q.ring = points(ring);
            if (auto value = field(p, "cells")) {
                auto s = string(value);
                if (s == "triangles")
                    q.mode = plan::QuadRegionMode::TrianglesOnly;
                else if (s == "required_quads")
                    q.requireMapped = true;
                else if (s != "auto")
                    invalid("Unknown region cells policy.");
            }
            REAL(p, "spacing", q.spacing);
            REAL(p, "h_along", q.hAlong);
            REAL(p, "h_across", q.hAcross);
            REAL(p, "along_angle_degrees", q.mappedAlongAngleDeg);
            REAL(p, "aspect_max", q.aspectMax);
            INT(p, "attribute", q.attribute);
            q.directionalSpacing = q.hAlong > 0 || q.hAcross > 0;
            if (auto value = field(p, "tag"))
                q.tag = string(value);
            if (auto value = field(p, "corners"))
                q.corners = integers<int>(value);
            if (auto value = field(p, "holes"))
                each(value, [&](PyObject *hole) { q.holes.append(plan::Polygon(points(hole))); });
            r.quadRegions.push_back(std::move(q));
        });
    if (auto v = field(d, "size_field")) {
        keys(v, {"near_size", "gradation", "max_size", "max_grid_cells", "area_floor", "regions",
                 "terrain_regions"});
        plan::SizeFieldOptions o;
        o.footprintBounds = true;
        REAL(v, "near_size", o.nearSize);
        REAL(v, "gradation", o.gradation);
        REAL(v, "max_size", o.maxSize);
        REAL(v, "area_floor", o.areaFloor);
        INT(v, "max_grid_cells", o.maxGridCells);
        auto regions = [&](PyObject *rows, auto &target) {
            each(rows, [&](PyObject *p) {
                keys(p, {"ring", "size"});
                auto ring = field(p, "ring"), h = field(p, "size");
                if (!ring || !h)
                    invalid("Size field region needs ring and size.");
                target.append({plan::Polygon(points(ring)), number(h)});
            });
        };
        if (auto p = field(v, "regions"))
            regions(p, o.regions);
        if (auto p = field(v, "terrain_regions"))
            regions(p, o.terrainRegions);
        r.sizeField = std::move(o);
    }
    return r;
}
RasterSurface raster(PyObject *d)
{
    keys(d, {"heights", "origin", "spacing", "maximum_cells_per_query"});
    auto values = field(d, "heights");
    if (!values)
        invalid("Raster heights required.");
    RasterData out;
    Buffer view(values);
    if (!view.held || view.b.ndim != 2 || !view.b.format ||
        (std::string(view.b.format) != "d" && std::string(view.b.format) != "f"))
        invalid("Raster heights need a native float32/64 buffer of shape (rows, columns).");
    auto &b = view.b;
    if (b.shape[0] > INT32_MAX || b.shape[1] > INT32_MAX || b.shape[0] < 2 || b.shape[1] < 2 ||
        std::uint64_t(b.shape[0]) * b.shape[1] > 100000000)
        invalid("Raster dimensions exceed limits.");
    out.rows = b.shape[0];
    out.columns = b.shape[1];
    out.heights.resize(std::uint64_t(out.rows) * out.columns);
    for (Py_ssize_t y = 0; y < b.shape[0]; ++y)
        for (Py_ssize_t x = 0; x < b.shape[1]; ++x) {
            auto p = static_cast<char *>(b.buf) + y * b.strides[0] + x * b.strides[1];
            double value;
            if (b.itemsize == 8)
                std::memcpy(&value, p, 8);
            else if (b.itemsize == 4) {
                float v;
                std::memcpy(&v, p, 4);
                value = v;
            } else
                invalid("Invalid raster float width.");
            out.heights[y * out.columns + x] = value;
        }
    if (auto p = field(d, "origin")) {
        Ref list(PySequence_Fast(p, "origin needs x,y"));
        if (!list.p)
            throw PythonError{};
        if (PySequence_Fast_GET_SIZE(list.p) != 2)
            invalid("origin needs x,y");
        out.origin = {number(PySequence_Fast_GET_ITEM(list.p, 0)),
                      number(PySequence_Fast_GET_ITEM(list.p, 1))};
    }
    if (auto p = field(d, "spacing")) {
        Ref list(PySequence_Fast(p, "spacing needs dx,dy"));
        if (!list.p)
            throw PythonError{};
        if (PySequence_Fast_GET_SIZE(list.p) != 2)
            invalid("spacing needs dx,dy");
        out.xSpacing = number(PySequence_Fast_GET_ITEM(list.p, 0));
        out.ySpacing = number(PySequence_Fast_GET_ITEM(list.p, 1));
    }
    INT(d, "maximum_cells_per_query", out.maximumCellsPerQuery);
    return RasterSurface(std::move(out));
}
PyObject *basicReport(const OperationReport &r)
{
    Ref d(PyDict_New());
    if (!d.p)
        throw PythonError{};
    put(d.p, "status", PyUnicode_FromString(statusName(r.status)));
    put(d.p, "code", PyUnicode_FromString(r.code.c_str()));
    put(d.p, "message", PyUnicode_FromString(r.message.c_str()));
    put(d.p, "complete", PyBool_FromLong(r.complete()));
    put(d.p, "seconds", PyFloat_FromDouble(r.seconds));
    put(d.p, "work", PyLong_FromUnsignedLongLong(r.work));
    put(d.p, "vertices", PyLong_FromLongLong(r.counts.vertices));
    put(d.p, "triangles", PyLong_FromLongLong(r.counts.triangles));
    put(d.p, "quads", PyLong_FromLongLong(r.counts.quads));
    return d.release();
}
PyObject *report(const JobReport &r)
{
    Ref d(basicReport(r));
    put(d.p, "algorithm_version", PyUnicode_FromString(r.algorithmVersion.c_str()));
    put(d.p, "provider_identity", PyUnicode_FromString(r.providerIdentity.c_str()));
#define COUNT(k, v) put(d.p, k, PyLong_FromUnsignedLongLong(v))
    COUNT("inserted", r.inserted);
    COUNT("removed", r.removed);
    COUNT("removal_attempts", r.removalAttempts);
    COUNT("queue_peak", r.queuePeak);
    COUNT("below_minimum", r.belowMinimum);
    COUNT("edge_conflicts", r.edgeConflicts);
    COUNT("size_conflicts", r.sizeConflicts);
    COUNT("transition_conflicts", r.transitionConflicts);
    COUNT("terrain_conflicts", r.terrainConflicts);
    COUNT("terrain_unknown", r.terrainUnknown);
    COUNT("planned_patches", r.plannedPatches);
    COUNT("planning_retries", r.planningRetries);
    COUNT("shared_sides", r.sharedSides);
    COUNT("constraint_segments", r.constraintSegments);
    put(d.p, "constraint_audit_passed", PyBool_FromLong(r.constraintAuditPassed));
    put(d.p, "largest_adjacent_ratio", PyFloat_FromDouble(r.largestAdjacentRatio));
    put(d.p, "largest_terrain_ratio", PyFloat_FromDouble(r.largestTerrainRatio));
    Ref decisions(PyList_New(0));
    for (const auto &v : r.planning) {
        if (PyList_GET_SIZE(decisions.p) >= 256)
            break;
        Ref row(Py_BuildValue("{s:s,s:s,s:i,s:i,s:O}", "source", v.source.c_str(), "reason",
                              v.reason.c_str(), "region", v.region, "repair_rounds", v.repairRounds,
                              "required", v.required ? Py_True : Py_False));
        checked(row.p && PyList_Append(decisions.p, row.p) == 0);
    }
    checked(PyDict_SetItemString(d.p, "planning", decisions.p) == 0);
    Ref examples(PyList_New(0));
    for (const auto &e : r.examples) {
        Ref row(Py_BuildValue("{s:s,s:I,s:d,s:d}", "code", e.code.c_str(), "cell", e.cell,
                              "measured", e.measured, "allowed", e.allowed));
        checked(row.p && PyList_Append(examples.p, row.p) == 0);
    }
    checked(PyDict_SetItemString(d.p, "examples", examples.p) == 0);
    const auto &o = r.resolvedOptions;
    Ref resolved(Py_BuildValue(
        "{s:s,s:d,s:d,s:d,s:d,s:d,s:d,s:i,s:i,s:O,s:O}", "cells",
        o.cells == CellPlan::Triangles ? "triangles" : "mixed", "minimum_target", o.minimumTarget,
        "maximum_size", o.maximumSize, "minimum_edge", o.minimumEdge, "minimum_angle_degrees",
        o.minimumAngleDegrees, "adjacent_ratio", o.adjacentRatio, "terrain_tolerance",
        o.terrainTolerance, "coarsen_passes", o.coarsenPasses, "smoothing_passes",
        o.smoothingPasses, "strict_sizing", o.strictSizing ? Py_True : Py_False,
        "require_terrain_certification", o.requireTerrainCertification ? Py_True : Py_False));
    checked(resolved.p != nullptr);
    put(resolved.p, "flat_elevation", PyFloat_FromDouble(o.flatElevation));
    put(resolved.p, "sizing_repair_passes", PyLong_FromLong(o.sizingRepairPasses));
    put(resolved.p, "repair_insertions", PyLong_FromUnsignedLong(o.repairInsertions));
    put(resolved.p, "coarsening_reference_size", PyFloat_FromDouble(o.coarseningReferenceSize));
    put(resolved.p, "coarsening_order",
        PyUnicode_FromString(o.coarseningOrder == CoarseningOrder::ResolutionAware
                                 ? "resolution_aware"
                                 : "legacy_flatness"));
    put(resolved.p, "retain_input_mapping", PyBool_FromLong(o.retainInputMapping));
    put(resolved.p, "maximum_patch_cells", PyLong_FromUnsignedLongLong(o.maximumPatchCells));
    put(resolved.p, "maximum_automatic_blocks", PyLong_FromLong(o.maximumAutomaticBlocks));
    put(resolved.p, "automatic_blocks", PyBool_FromLong(o.automaticBlocks));
    const auto &t = o.terrainPolicy;
    put(resolved.p, "terrain_policy",
        Py_BuildValue("{s:s,s:d,s:d,s:d}", "mode",
                      t.mode == TerrainToleranceMode::TargetSize ? "target_size" : "constant",
                      "reference_size", t.referenceSize, "minimum_tolerance", t.minimumTolerance,
                      "maximum_tolerance", t.maximumTolerance));
    Ref limitValues(PyDict_New());
    put(limitValues.p, "max_vertices", PyLong_FromUnsignedLongLong(o.limits.maxVertices));
    put(limitValues.p, "max_cells", PyLong_FromUnsignedLongLong(o.limits.maxCells));
    put(limitValues.p, "max_edges", PyLong_FromUnsignedLongLong(o.limits.maxEdges));
    put(limitValues.p, "max_work", PyLong_FromUnsignedLongLong(o.limits.maxWork));
    put(limitValues.p, "max_workspace_bytes",
        PyLong_FromUnsignedLongLong(o.limits.maxWorkspaceBytes));
    put(limitValues.p, "max_metadata_bytes",
        PyLong_FromUnsignedLongLong(o.limits.maxMetadataBytes));
    put(limitValues.p, "max_record_bytes", PyLong_FromUnsignedLongLong(o.limits.maxRecordBytes));
    checked(PyDict_SetItemString(resolved.p, "limits", limitValues.p) == 0);
    checked(PyDict_SetItemString(d.p, "resolved_options", resolved.p) == 0);
    return d.release();
}
struct Export {
    PyObject_HEAD std::shared_ptr<void> *owner;
    void *data;
    Py_ssize_t rows, columns, item, stride;
    const char *format;
};
struct MeshObject {
    PyObject_HEAD std::shared_ptr<JobResult> *value;
};
struct State {
    JobRequest input;
    Providers providers;
    EventChannel events;
    std::atomic<bool> cancelled{false}, done{false};
    std::mutex mutex;
    std::condition_variable ready;
    std::shared_ptr<JobResult> result;
    std::string error;
    std::thread worker;
    ~State()
    {
        cancelled = true;
        if (worker.joinable()) {
            if (worker.get_id() == std::this_thread::get_id())
                worker.detach();
            else
                worker.join();
        }
    }
};
struct JobObject {
    PyObject_HEAD std::shared_ptr<State> *value;
};
PyTypeObject exportType = {PyVarObject_HEAD_INIT(nullptr, 0)},
             meshType = {PyVarObject_HEAD_INIT(nullptr, 0)},
             jobType = {PyVarObject_HEAD_INIT(nullptr, 0)};
struct BufferMeta {
    Py_ssize_t shape[2], strides[2];
};
int getBuffer(PyObject *obj, Py_buffer *view, int flags)
{
    auto self = reinterpret_cast<Export *>(obj);
    if (flags & PyBUF_WRITABLE) {
        PyErr_SetString(PyExc_BufferError, "Mesh buffers are read-only.");
        return -1;
    }
    auto info = new (std::nothrow) BufferMeta;
    if (!info) {
        PyErr_NoMemory();
        return -1;
    }
    info->shape[0] = self->rows;
    info->shape[1] = self->columns;
    info->strides[0] = self->stride;
    info->strides[1] = self->item;
    view->buf = self->data;
    view->obj = Py_NewRef(obj);
    view->len = self->rows * self->columns * self->item;
    view->readonly = 1;
    view->itemsize = self->item;
    view->format = (flags & PyBUF_FORMAT) ? const_cast<char *>(self->format) : nullptr;
    view->ndim = self->columns == 1 ? 1 : 2;
    view->shape = (flags & PyBUF_ND) ? info->shape : nullptr;
    view->strides = (flags & PyBUF_STRIDES) ? info->strides : nullptr;
    view->suboffsets = nullptr;
    view->internal = info;
    const bool contiguous = self->rows <= 1 || self->stride == self->columns * self->item;
    if ((!contiguous && (flags & PyBUF_STRIDES) != PyBUF_STRIDES) ||
        (!contiguous && (flags & PyBUF_C_CONTIGUOUS) == PyBUF_C_CONTIGUOUS) ||
        ((flags & PyBUF_F_CONTIGUOUS) == PyBUF_F_CONTIGUOUS && self->rows > 1 &&
         self->columns > 1) ||
        (!contiguous && (flags & PyBUF_ANY_CONTIGUOUS) == PyBUF_ANY_CONTIGUOUS)) {
        Py_DECREF(view->obj);
        delete info;
        PyErr_SetString(PyExc_BufferError,
                        "Use the strided view or explicitly copy it to a contiguous array.");
        return -1;
    }
    return 0;
}
void releaseBuffer(PyObject *, Py_buffer *view)
{
    delete static_cast<BufferMeta *>(view->internal);
}
void exportDealloc(Export *self)
{
    delete self->owner;
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject *>(self));
}
PyBufferProcs bufferMethods = {getBuffer, releaseBuffer};
PyObject *buffer(std::shared_ptr<void> owner, void *data, Py_ssize_t rows, Py_ssize_t columns,
                 Py_ssize_t item, Py_ssize_t stride, const char *format)
{
    auto self = PyObject_New(Export, &exportType);
    if (!self)
        throw PythonError{};
    self->owner = nullptr;
    try {
        self->owner = new std::shared_ptr<void>(std::move(owner));
    } catch (...) {
        Py_DECREF(self);
        throw;
    }
    self->data = data;
    self->rows = rows;
    self->columns = columns;
    self->item = item;
    self->stride = stride;
    self->format = format;
    Ref holder(reinterpret_cast<PyObject *>(self));
    auto out = PyMemoryView_FromObject(holder.p);
    if (!out)
        throw PythonError{};
    return out;
}
void meshDealloc(MeshObject *self)
{
    delete self->value;
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject *>(self));
}
PyObject *makeMesh(std::shared_ptr<JobResult> value)
{
    auto self = PyObject_New(MeshObject, &meshType);
    if (!self)
        throw PythonError{};
    self->value = nullptr;
    try {
        self->value = new std::shared_ptr<JobResult>(std::move(value));
    } catch (...) {
        Py_DECREF(self);
        throw;
    }
    return reinterpret_cast<PyObject *>(self);
}
PyObject *meshArray(MeshObject *self, void *which)
{
    try {
        auto owner = *self->value;
        auto &m = owner->mesh;
        const int kind = int(reinterpret_cast<std::intptr_t>(which));
        if (kind == 0)
            return buffer(owner, m.vertices.data(), m.vertices.size(), 3, 8, sizeof(Point3), "d");
        if (kind == 1)
            return buffer(owner, m.cells.empty() ? nullptr : m.cells[0].vertices.data(),
                          m.cells.size(), 4, 4, sizeof(Cell), "I");
        if (kind == 2)
            return buffer(owner, m.cells.empty() ? nullptr : &m.cells[0].count, m.cells.size(), 1,
                          1, sizeof(Cell), "B");
        if (kind == 3)
            return buffer(owner, m.cells.empty() ? nullptr : &m.cells[0].region, m.cells.size(), 1,
                          4, sizeof(Cell), "i");
        if (kind == 4)
            return buffer(owner, m.vertexIds.data(), m.vertexIds.size(), 1, 8, 8, "q");
        if (kind == 5)
            return buffer(owner, m.cellIds.data(), m.cellIds.size(), 1, 8, 8, "q");
        if (kind == 6)
            return buffer(owner, m.vertexMarkers.data(), m.vertexMarkers.size(), 1, 4, 4, "i");
        if (kind == 7)
            return buffer(owner, m.edges.empty() ? nullptr : &m.edges[0].a, m.edges.size(), 2, 4,
                          sizeof(Edge), "I");
        if (kind == 8)
            return buffer(owner, m.edges.empty() ? nullptr : &m.edges[0].marker, m.edges.size(), 1,
                          4, sizeof(Edge), "i");
        if (kind == 9)
            return buffer(owner, owner->inputVertexToOutput.data(),
                          owner->inputVertexToOutput.size(), 1, 4, 4, "I");
        if (kind == 10)
            return buffer(owner, m.aliases.empty() ? nullptr : &m.aliases[0].vertex,
                          m.aliases.size(), 1, 4, sizeof(VertexAlias), "I");
        if (kind == 11)
            return buffer(owner, m.aliases.empty() ? nullptr : &m.aliases[0].sourceId,
                          m.aliases.size(), 1, 8, sizeof(VertexAlias), "q");
        if (kind == 12)
            return buffer(owner, m.aliases.empty() ? nullptr : &m.aliases[0].marker,
                          m.aliases.size(), 1, 4, sizeof(VertexAlias), "i");
        Py_RETURN_NONE;
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject *meshReport(MeshObject *self, void *)
{
    try {
        return report((**self->value).report);
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject *meshMetadata(MeshObject *self, void *)
{
    const auto &m = (**self->value).mesh.metadata;
    return Py_BuildValue("{s:d,s:d,s:O,s:s#,s:s#}", "horizontal_metres_per_unit",
                         m.horizontalMetresPerUnit, "vertical_metres_per_unit",
                         m.verticalMetresPerUnit, "units_known", m.unitsKnown ? Py_True : Py_False,
                         "crs", m.crs.data(), Py_ssize_t(m.crs.size()), "description",
                         m.description.data(), Py_ssize_t(m.description.size()));
}
PyObject *meshPaths(MeshObject *self, void *)
{
    try {
        auto owner = *self->value;
        Ref list(PyList_New(0));
        for (auto &p : owner->mesh.paths) {
            Ref d(PyDict_New());
            put(d.p, "vertices", buffer(owner, p.vertices.data(), p.vertices.size(), 1, 4, 4, "I"));
            put(d.p, "id", PyLong_FromLongLong(p.id));
            put(d.p, "marker", PyLong_FromLong(p.marker));
            put(d.p, "closed", PyBool_FromLong(p.closed));
            put(d.p, "kind",
                PyUnicode_FromString(p.kind == PathKind::Outer    ? "outer"
                                     : p.kind == PathKind::Hole   ? "hole"
                                     : p.kind == PathKind::Region ? "region"
                                                                  : "line"));
            checked(PyList_Append(list.p, d.p) == 0);
        }
        return list.release();
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject *meshLabels(MeshObject *self, void *)
{
    try {
        Ref rows(PyList_New(0));
        for (const auto &label : (**self->value).mesh.labels) {
            Ref row(PyDict_New());
            put(row.p, "id", PyLong_FromLong(label.id));
            put(row.p, "name", PyUnicode_FromStringAndSize(label.name.data(), label.name.size()));
            checked(PyList_Append(rows.p, row.p) == 0);
        }
        return rows.release();
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
#define ARRAY(name, id)                                                                            \
    {const_cast<char *>(name), reinterpret_cast<getter>(meshArray), nullptr, nullptr,              \
     reinterpret_cast<void *>(id)}
PyGetSetDef meshGetSet[] = {
    ARRAY("vertices", 0),
    ARRAY("cells", 1),
    ARRAY("cell_types", 2),
    ARRAY("regions", 3),
    ARRAY("vertex_ids", 4),
    ARRAY("cell_ids", 5),
    ARRAY("vertex_markers", 6),
    ARRAY("edges", 7),
    ARRAY("edge_markers", 8),
    ARRAY("input_mapping", 9),
    ARRAY("alias_vertices", 10),
    ARRAY("alias_ids", 11),
    ARRAY("alias_markers", 12),
    {const_cast<char *>("report"), reinterpret_cast<getter>(meshReport), nullptr, nullptr, nullptr},
    {const_cast<char *>("metadata"), reinterpret_cast<getter>(meshMetadata), nullptr, nullptr,
     nullptr},
    {const_cast<char *>("paths"), reinterpret_cast<getter>(meshPaths), nullptr, nullptr, nullptr},
    {const_cast<char *>("labels"), reinterpret_cast<getter>(meshLabels), nullptr, nullptr, nullptr},
    {nullptr}};
void jobDealloc(JobObject *self)
{
    if (self->value)
        (*self->value)->cancelled = true;
    delete self->value;
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject *>(self));
}
PyObject *jobCancel(JobObject *self, PyObject *)
{
    (*self->value)->cancelled = true;
    Py_RETURN_NONE;
}
PyObject *jobDone(JobObject *self, void *)
{
    return PyBool_FromLong((*self->value)->done.load());
}
PyObject *jobPoll(JobObject *self, PyObject *)
{
    try {
        auto drained = (*self->value)->events.drain();
        Ref rows(PyList_New(0));
        for (const auto &e : drained.events) {
            Ref row(Py_BuildValue("{s:s,s:s,s:i,s:s,s:L,s:L,s:L}", "stage", e.stage.c_str(),
                                  "message", e.message.c_str(), "percent", e.percent, "status",
                                  statusName(e.status), "vertices", e.counts.vertices, "triangles",
                                  e.counts.triangles, "quads", e.counts.quads));
            checked(row.p != nullptr);
            put(row.p, "sequence", PyLong_FromUnsignedLongLong(e.sequence));
            put(row.p, "job", PyLong_FromUnsignedLongLong(e.job));
            put(row.p, "elapsed_ms", PyLong_FromUnsignedLongLong(e.elapsedMs));
            put(row.p, "dropped", PyLong_FromUnsignedLongLong(drained.dropped));
            put(row.p, "severity",
                PyUnicode_FromString(e.severity == Severity::Error     ? "error"
                                     : e.severity == Severity::Warning ? "warning"
                                                                       : "info"));
            checked(row.p && PyList_Append(rows.p, row.p) == 0);
        }
        return rows.release();
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject *jobResult(JobObject *self, PyObject *args)
{
    double timeout = -1;
    if (!PyArg_ParseTuple(args, "|d", &timeout))
        return nullptr;
    auto state = *self->value;
    bool ready;
    Py_BEGIN_ALLOW_THREADS
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (timeout < 0) {
            state->ready.wait(lock, [&] { return state->done.load(); });
            ready = true;
        } else
            ready = state->ready.wait_for(lock, std::chrono::duration<double>(timeout),
                                          [&] { return state->done.load(); });
    }
    Py_END_ALLOW_THREADS if (!ready)
    {
        PyErr_SetString(PyExc_TimeoutError, "Mesh job is still running.");
        return nullptr;
    }
    if (!state->error.empty()) {
        PyErr_SetString(PyExc_RuntimeError, state->error.c_str());
        return nullptr;
    }
    try {
        return makeMesh(state->result);
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyMethodDef jobMethods[] = {
    {"cancel", reinterpret_cast<PyCFunction>(jobCancel), METH_NOARGS, nullptr},
    {"poll", reinterpret_cast<PyCFunction>(jobPoll), METH_NOARGS, nullptr},
    {"result", reinterpret_cast<PyCFunction>(jobResult), METH_VARARGS, nullptr},
    {nullptr}};
PyGetSetDef jobGetSet[] = {
    {const_cast<char *>("done"), reinterpret_cast<getter>(jobDone), nullptr, nullptr, nullptr},
    {nullptr}};
PyObject *start(PyObject *, PyObject *args, PyObject *kw)
{
    PyObject *input, *surface = Py_None;
    const char *identity = "";
    static const char *names[] = {"request", "raster", "provider_identity", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O|Os", const_cast<char **>(names), &input, &surface,
                                     &identity))
        return nullptr;
    try {
        auto state = std::make_shared<State>();
        state->input = request(input);
        if (surface != Py_None)
            state->providers = raster(surface).providers(identity);
        else
            state->providers.identity = identity;
        state->providers.cancelled = [pointer = state.get()] { return pointer->cancelled.load(); };
        auto object = PyObject_New(JobObject, &jobType);
        if (!object)
            throw PythonError{};
        object->value = nullptr;
        Ref holder(reinterpret_cast<PyObject *>(object));
        object->value = new std::shared_ptr<State>(state);
        state->worker = std::thread([state] {
            try {
                auto result = std::make_shared<JobResult>(
                    runJob(state->input, state->providers, &state->events));
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->result = std::move(result);
                    state->done = true;
                }
            } catch (const std::exception &e) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->error = e.what();
                state->done = true;
            } catch (...) {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->error = "Unknown native worker failure.";
                state->done = true;
            }
            state->input = {};
            state->providers = {};
            state->ready.notify_all();
        });
        return holder.release();
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
std::filesystem::path path(PyObject *p)
{
    Ref fs(PyOS_FSPath(p));
    if (!fs.p)
        throw PythonError{};
#ifdef _WIN32
    if (PyUnicode_Check(fs.p)) {
        Py_ssize_t length = 0;
        auto value = PyUnicode_AsWideCharString(fs.p, &length);
        if (!value)
            throw PythonError{};
        std::wstring wide(value, length);
        PyMem_Free(value);
        if (wide.find(wchar_t(0)) != std::wstring::npos)
            invalid("Path contains NUL.");
        std::filesystem::path result(wide);
        return result;
    }
#endif
    if (PyBytes_Check(fs.p)) {
        std::string value(PyBytes_AS_STRING(fs.p), PyBytes_GET_SIZE(fs.p));
        if (value.find(char(0)) != std::string::npos)
            invalid("Path contains NUL.");
        return std::filesystem::path(value);
    }
    auto value = string(fs.p);
    if (value.find(char(0)) != std::string::npos)
        invalid("Path contains NUL.");
    return std::filesystem::u8path(value);
}
IOOptions ioOptions(PyObject *d)
{
    IOOptions o;
    if (!d || d == Py_None)
        return o;
    keys(d, {"overwrite", "durable", "allow_attribute_loss", "legacy_2dm", "ignore_unknown_records",
             "limits"});
    BOOL(d, "overwrite", o.overwrite);
    BOOL(d, "durable", o.durable);
    BOOL(d, "allow_attribute_loss", o.allowAttributeLoss);
    BOOL(d, "legacy_2dm", o.legacy2dm);
    if (auto p = field(d, "ignore_unknown_records"))
        o.unknownRecords = boolean(p) ? UnknownRecords::IgnoreWithReport : UnknownRecords::Reject;
    if (auto p = field(d, "limits"))
        o.limits = limits(p);
    return o;
}
PyObject *readFile(PyObject *, PyObject *args, PyObject *kw)
{
    PyObject *file, *options = Py_None;
    static const char *names[] = {"path", "options", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O|O", const_cast<char **>(names), &file, &options))
        return nullptr;
    try {
        auto p = path(file);
        auto o = ioOptions(options);
        ReadResult result;
        Py_BEGIN_ALLOW_THREADS result = p.extension() == ".mkb" ? readBinary(p, o) : read2dm(p, o);
        Py_END_ALLOW_THREADS auto value = std::make_shared<JobResult>();
        value->mesh = std::move(result.mesh);
        static_cast<OperationReport &>(value->report) = result.report;
        return makeMesh(std::move(value));
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject *writeFile(PyObject *, PyObject *args, PyObject *kw)
{
    PyObject *file, *mesh, *options = Py_None;
    static const char *names[] = {"path", "mesh", "options", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kw, "OO|O", const_cast<char **>(names), &file, &mesh,
                                     &options))
        return nullptr;
    if (!PyObject_TypeCheck(mesh, &meshType)) {
        PyErr_SetString(PyExc_TypeError, "mesh must be a native MeshResult.");
        return nullptr;
    }
    try {
        auto p = path(file);
        if (p.extension() != ".mkb" && p.extension() != ".2dm")
            invalid("Output extension must be .mkb or .2dm.");
        auto o = ioOptions(options);
        auto owner = *reinterpret_cast<MeshObject *>(mesh)->value;
        IOReport result;
        Py_BEGIN_ALLOW_THREADS result =
            p.extension() == ".mkb" ? writeBinary(p, owner->mesh, o) : write2dm(p, owner->mesh, o);
        Py_END_ALLOW_THREADS Ref d(basicReport(result)), losses(PyList_New(0));
        for (auto &s : result.losses) {
            Ref value(PyUnicode_FromString(s.c_str()));
            checked(value.p && PyList_Append(losses.p, value.p) == 0);
        }
        checked(PyDict_SetItemString(d.p, "losses", losses.p) == 0);
        put(d.p, "bytes", PyLong_FromUnsignedLongLong(result.bytes));
        return d.release();
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
struct Prepared {
    plan::ConstraintSimplificationResult result;
    std::vector<Point2> points;
    std::vector<double> elevations;
    std::vector<Path> paths;
};
plan::ConstraintSimplificationPolicy sourcePolicy(PyObject *d)
{
    keys(d, {"mode", "maximum_deviation", "maximum_z_deviation", "maximum_turn_degrees",
             "maximum_area_change_fraction", "maximum_area_change", "minimum_clearance"});
    plan::ConstraintSimplificationPolicy p;
    if (auto v = field(d, "mode")) {
        auto s = string(v);
        if (s == "simplify")
            p.mode = plan::ConstraintSimplificationPolicy::Mode::Simplify;
        else if (s != "preserve")
            invalid("Unknown source geometry policy.");
    }
    REAL(d, "maximum_deviation", p.maxDeviation);
    REAL(d, "maximum_z_deviation", p.maxZDeviation);
    REAL(d, "maximum_turn_degrees", p.maxTurnDegrees);
    REAL(d, "maximum_area_change_fraction", p.maxAreaChangeFraction);
    REAL(d, "maximum_area_change", p.maxAreaChange);
    REAL(d, "minimum_clearance", p.minimumClearance);
    return p;
}
PyObject *prepare(PyObject *, PyObject *args, PyObject *kw)
{
    PyObject *features, *settings = Py_None;
    static const char *names[] = {"features", "options", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, kw, "O|O", const_cast<char **>(names), &features,
                                     &settings))
        return nullptr;
    try {
        plan::Vector<plan::SimplificationFeature> source;
        each(features, [&](PyObject *d) {
            keys(d, {"id", "xy", "z", "fixed", "closed", "role", "marker", "policy"});
            plan::SimplificationFeature f;
            auto xy = field(d, "xy");
            if (!xy)
                invalid("Source xy required.");
            f.xy = points(xy);
            if (auto v = field(d, "id"))
                f.id = string(v);
            if (auto v = field(d, "z"))
                f.z = reals(v);
            if (auto v = field(d, "fixed")) {
                auto flags = integers<int>(v);
                for (auto flag : flags)
                    f.fixed.append(bool(flag));
            }
            BOOL(d, "closed", f.closed);
            INT(d, "marker", f.marker);
            if (auto v = field(d, "policy"))
                f.policy = sourcePolicy(v);
            if (auto v = field(d, "role")) {
                auto kind = string(v);
                if (kind == "outer") {
                    f.role = plan::SimplificationFeature::Role::Domain;
                    f.closed = true;
                } else if (kind == "hole") {
                    f.role = plan::SimplificationFeature::Role::Hole;
                    f.closed = true;
                } else if (kind == "protected")
                    f.role = plan::SimplificationFeature::Role::Protected;
                else if (kind != "line")
                    invalid("Unknown source role.");
            }
            source.append(std::move(f));
        });
        plan::ConstraintSimplificationOptions options;
        if (settings != Py_None) {
            keys(settings, {"protected_points", "max_work", "max_workspace_bytes"});
            if (auto v = field(settings, "protected_points"))
                options.protectedPoints = points(v);
            INT(settings, "max_work", options.workBudget);
            INT(settings, "max_workspace_bytes", options.maximumWorkspaceBytes);
        }
        auto owner = std::make_shared<Prepared>();
        plan::ConstraintAuditReport audit;
        Py_BEGIN_ALLOW_THREADS owner->result = plan::simplifyConstraints(source, options);
        if (owner->result.ok) {
            plan::ConstraintAuditOptions o;
            o.protectedPoints = options.protectedPoints;
            o.workBudget = options.workBudget;
            o.maximumWorkspaceBytes = options.maximumWorkspaceBytes;
            audit = plan::auditConstraintSimplification(source, owner->result, o);
        }
        Py_END_ALLOW_THREADS Ref out(PyDict_New());
        if (!owner->result.ok || !audit.ok) {
            put(out.p, "ok", PyBool_FromLong(0));
            put(out.p, "error",
                PyUnicode_FromString(
                    (owner->result.ok ? audit.error : owner->result.error).c_str()));
            return out.release();
        }
        std::unordered_map<native_detail::Coordinate, Index, native_detail::CoordinateHash> ids;
        Ref featureRows(PyList_New(0));
        for (std::size_t i = 0; i < owner->result.features.size(); ++i) {
            auto &f = owner->result.features[i];
            Path path;
            path.kind = f.role == plan::SimplificationFeature::Role::Domain ? PathKind::Outer
                        : f.role == plan::SimplificationFeature::Role::Hole ? PathKind::Hole
                                                                            : PathKind::Line;
            path.closed = f.closed;
            path.marker = f.marker;
            path.id = i + 1;
            for (std::size_t j = 0; j < f.xy.size(); ++j) {
                auto p = f.xy[j];
                const double z = f.z.empty() ? std::numeric_limits<double>::quiet_NaN() : f.z[j];
                auto [at, added] = ids.emplace(native_detail::coordinate(p), owner->points.size());
                if (added) {
                    owner->points.push_back(p);
                    owner->elevations.push_back(z);
                } else if (std::isfinite(z)) {
                    auto &existing = owner->elevations[at->second];
                    if (std::isfinite(existing) && existing != z)
                        invalid("Shared sources have conflicting authored heights.");
                    existing = z;
                }
                path.vertices.push_back(at->second);
            }
            if (path.closed && path.vertices.size() > 1 &&
                path.vertices.front() == path.vertices.back())
                path.vertices.pop_back();
            owner->paths.push_back(std::move(path));
        }
        for (std::size_t i = 0; i < owner->paths.size(); ++i) {
            auto &p = owner->paths[i];
            auto &f = owner->result.features[i];
            auto &change = owner->result.changes[i];
            Ref row(PyDict_New());
            put(row.p, "id", PyLong_FromLongLong(p.id));
            put(row.p, "source_id", PyUnicode_FromString(f.id.c_str()));
            put(row.p, "kind",
                PyUnicode_FromString(p.kind == PathKind::Outer  ? "outer"
                                     : p.kind == PathKind::Hole ? "hole"
                                                                : "line"));
            put(row.p, "closed", PyBool_FromLong(p.closed));
            put(row.p, "marker", PyLong_FromLong(p.marker));
            put(row.p, "vertices",
                buffer(owner, p.vertices.data(), p.vertices.size(), 1, 4, 4, "I"));
            put(row.p, "source_vertices",
                buffer(owner, change.sourceVertices.data(), change.sourceVertices.size(), 1, 4, 4,
                       "i"));
            put(row.p, "removed", PyLong_FromLongLong(change.removed));
            checked(PyList_Append(featureRows.p, row.p) == 0);
        }
        put(out.p, "ok", PyBool_FromLong(1));
        put(out.p, "point_elevations",
            buffer(owner, owner->elevations.data(), owner->elevations.size(), 1, 8, 8, "d"));
        put(out.p, "points",
            buffer(owner, owner->points.data(), owner->points.size(), 2, 8, sizeof(Point2), "d"));
        checked(PyDict_SetItemString(out.p, "features", featureRows.p) == 0);
        put(out.p, "shared_chains", PyLong_FromUnsignedLongLong(owner->result.sharedChains));
        return out.release();
    } catch (const PythonError &) {
        return nullptr;
    } catch (const std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError, e.what());
        return nullptr;
    }
}
PyObject *capabilities(PyObject *, PyObject *)
{
    return Py_BuildValue("{s:O,s:O,s:O,s:O,s:s,s:s}", "qt_free", Py_True, "triangles", Py_True,
                         "automatic_mixed", Py_True, "native_raster", Py_True, "license",
                         "Apache-2.0", "version", "0.2.0");
}
PyMethodDef methods[] = {
    {"start", reinterpret_cast<PyCFunction>(start), METH_VARARGS | METH_KEYWORDS, nullptr},
    {"read", reinterpret_cast<PyCFunction>(readFile), METH_VARARGS | METH_KEYWORDS, nullptr},
    {"write", reinterpret_cast<PyCFunction>(writeFile), METH_VARARGS | METH_KEYWORDS, nullptr},
    {"prepare_sources", reinterpret_cast<PyCFunction>(prepare), METH_VARARGS | METH_KEYWORDS,
     nullptr},
    {"capabilities", capabilities, METH_NOARGS, nullptr},
    {nullptr}};
PyModuleDef module = {PyModuleDef_HEAD_INIT, "_meshkit",
                      "Native MeshKit meshing and streaming I/O.", -1, methods};
}
PyMODINIT_FUNC PyInit__meshkit()
{
    exportType.tp_name = "meshkit._Buffer";
    exportType.tp_basicsize = sizeof(Export);
    exportType.tp_flags = Py_TPFLAGS_DEFAULT;
    exportType.tp_dealloc = reinterpret_cast<destructor>(exportDealloc);
    exportType.tp_as_buffer = &bufferMethods;
    meshType.tp_name = "meshkit.MeshResult";
    meshType.tp_basicsize = sizeof(MeshObject);
    meshType.tp_flags = Py_TPFLAGS_DEFAULT;
    meshType.tp_dealloc = reinterpret_cast<destructor>(meshDealloc);
    meshType.tp_getset = meshGetSet;
    jobType.tp_name = "meshkit.Job";
    jobType.tp_basicsize = sizeof(JobObject);
    jobType.tp_flags = Py_TPFLAGS_DEFAULT;
    jobType.tp_dealloc = reinterpret_cast<destructor>(jobDealloc);
    jobType.tp_methods = jobMethods;
    jobType.tp_getset = jobGetSet;
    if (PyType_Ready(&exportType) < 0 || PyType_Ready(&meshType) < 0 || PyType_Ready(&jobType) < 0)
        return nullptr;
    auto m = PyModule_Create(&module);
    if (!m)
        return nullptr;
    Py_INCREF(&meshType);
    PyModule_AddObject(m, "MeshResult", reinterpret_cast<PyObject *>(&meshType));
    return m;
}
