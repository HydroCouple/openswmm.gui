/**
 * @file      irunlayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BL — abstract "data-source" that the ComparisonPlotModel
 *         reads timeseries from.
 *
 * One `IRunLayer` instance represents one logical "run" in the comparison
 * plot — most commonly an open `.out` file backing a `SWMMResultsLayer`,
 * but the interface also fronts:
 *   - Standalone `.out` files loaded via *Tools → Comparison Plot → Load Run…*
 *     (a transient SwmmOutRunLayer owned by the dialog).
 *   - Observed-data CSV / TSV / XLSX (ObservedCsvRunLayer).
 *   - 2D mesh cell results — `SWMM2DResultsLayer` implements `IRunLayer`
 *     directly so a mesh layer is a first-class `RunSource` (Slice CF.3).
 *
 * The model never type-casts the pointer; every interaction goes through
 * the virtual methods below. New result-layer kinds plug in by adding a
 * new `ObjectRef` discriminator + handling it in `getSeriesAt`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_IRUNLAYER_H
#define OPENSWMMVIS_PLOT_IRUNLAYER_H

#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"
#include "plot/seriesstatistics.h"
#include <optional>
#include <atomic>

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <vector>

namespace openswmmvis::plot
{

/*! \brief Identifies what object a series is about. Kind is the
 *         discriminator; the active union member is implied by kind. */
struct ObjectRef {
    enum class Kind {
        Unknown = 0,
        Node = 1,       ///< name = SWMM node ID
        Link = 2,       ///< name = SWMM link ID
        Subcatch = 3,   ///< name = SWMM subcatchment ID
        Mesh2DCell = 4, ///< triIdx valid; name unused (CF.3)
        Observed = 5,   ///< name = observed series label (e.g. CSV header)
        System = 6,     ///< single global series per attribute; name unused (AT.2)
        Mesh2DEdge = 7, ///< triIdx + edgeLocal valid; name unused — for edge-flux series
        Mesh2DVertex =
            8, ///< triIdx holds the vertex index; depth/HGL interpolated from incident cells
    };

    Kind kind = Kind::Unknown;
    QString name; ///< SWMM ID for 1D kinds; observed-series label for Observed; unused for System.
    int triIdx =
        -1; ///< Triangle index for Mesh2DCell / Mesh2DEdge; vertex index for Mesh2DVertex; -1 otherwise.
    int edgeLocal = -1; ///< Local edge index 0..2 for Mesh2DEdge; -1 otherwise.

    /**
     * @brief Create an unresolved Unknown reference with empty name and negative mesh indices.
     */
    constexpr ObjectRef() = default;
    /**
     * @brief Store a kind and owned name without validating their compatibility.
     * @param[in] k Discriminator; mesh kinds still need their indices set separately.
     * @param[in] n ID/observed label moved into name; may be empty.
     */
    ObjectRef(Kind k, QString n) : kind(k), name(std::move(n)) {}
    /**
     * @brief Create a name-keyed node reference.
     * @param[in] n SWMM node ID, moved into the reference without normalization.
     * @return Value reference; an empty name fails isValid(). Existence in a source is not checked.
     */
    static ObjectRef forNode(QString n) { return ObjectRef(Kind::Node, std::move(n)); }
    /**
     * @brief Create a name-keyed link reference.
     * @param[in] n SWMM link ID, moved into the reference without normalization.
     * @return Value reference; an empty name fails isValid(). Existence in a source is not checked.
     */
    static ObjectRef forLink(QString n) { return ObjectRef(Kind::Link, std::move(n)); }
    /**
     * @brief Create a name-keyed subcatchment reference.
     * @param[in] n SWMM subcatchment ID, moved into the reference without normalization.
     * @return Value reference; an empty name fails isValid(). Existence in a source is not checked.
     */
    static ObjectRef forSubcatch(QString n) { return ObjectRef(Kind::Subcatch, std::move(n)); }
    /**
     * @brief Construct an index-keyed mesh triangle reference.
     * @param[in] idx Zero-based source triangle index; negative values remain invalid.
     * @return Reference without bounds/source checking; the index is stored in triIdx.
     */
    static ObjectRef forMesh2DCell(int idx)
    {
        ObjectRef r;
        r.kind = Kind::Mesh2DCell;
        r.triIdx = idx;
        return r;
    }
    /**
     * @brief Construct a triangle-local edge reference without resolving shared-edge orientation.
     * @param[in] tri Zero-based triangle index, expected nonnegative.
     * @param[in] edge Local edge index in [0,2].
     * @return Reference storing both indices; source bounds are not checked.
     */
    static ObjectRef forMesh2DEdge(int tri, int edge)
    {
        ObjectRef r;
        r.kind = Kind::Mesh2DEdge;
        r.triIdx = tri;
        r.edgeLocal = edge;
        return r;
    }
    /**
     * @brief Construct an index-keyed mesh vertex reference.
     * @param[in] idx Zero-based source vertex index; negative values remain invalid.
     * @return Reference without bounds/source checking; the index is stored in triIdx.
     */
    static ObjectRef forMesh2DVertex(int idx)
    {
        ObjectRef r;
        r.kind = Kind::Mesh2DVertex;
        r.triIdx = idx;
        return r;
    }
    /**
     * @brief Create a name-keyed observed reference.
     * @param[in] lbl Observed series label, moved into the reference without normalization.
     * @return Value reference; an empty name fails isValid(). Existence in a source is not checked.
     */
    static ObjectRef forObserved(QString lbl) { return ObjectRef(Kind::Observed, std::move(lbl)); }
    /**
     * @brief Construct a global system-results reference with no object name.
     * @return Structurally valid System reference, independent of source availability.
     */
    static ObjectRef forSystem()
    {
        ObjectRef r;
        r.kind = Kind::System;
        return r;
    }

    /**
     * @brief Check discriminator-specific presence and local index ranges.
     * @return Nonnegative mesh indices, edgeLocal in [0,2] for edges, true for System, false for Unknown, or nonempty name for other kinds. Does not check source membership/bounds.
     */
    bool isValid() const noexcept
    {
        switch (kind) {
        case Kind::Mesh2DCell:
            return triIdx >= 0;
        case Kind::Mesh2DVertex:
            return triIdx >= 0;
        case Kind::Mesh2DEdge:
            return triIdx >= 0 && edgeLocal >= 0 && edgeLocal <= 2;
        case Kind::System:
            return true; // name is unused
        case Kind::Unknown:
            return false;
        default:
            return !name.isEmpty();
        }
    }

    /**
     * @brief Compare every stored object-reference field.
     * @param[in] other Reference to compare.
     * @return Equality of kind, exact name, triIdx and edgeLocal, including fields unused by the active kind.
     */
    bool operator==(const ObjectRef &other) const noexcept
    {
        return kind == other.kind && name == other.name && triIdx == other.triIdx &&
               edgeLocal == other.edgeLocal;
    }
    /**
     * @brief Compare complete stored reference identity.
     * @param[in] other Reference to compare.
     * @return Negation of operator==.
     */
    bool operator!=(const ObjectRef &other) const noexcept { return !(*this == other); }
};

/*! \brief The canonical plottable-attribute list for \p k (presentation
 *  order), dispatching to the shared lists in plotattribute.h. Lives here
 *  rather than there because the nested ObjectRef::Kind cannot be named in
 *  plotattribute.h without an include cycle. Empty for kinds with no
 *  fixed list (Observed and Unknown). Mesh kinds return their full list;
 *  per-layer availability (edge flux, rainfall datasets) is gated by
 *  `IRunLayer::supportsAttribute`.
 * @param[in] k Object kind; observed/unknown/unsupported values have no fixed list.
 * @return Immutable static reference in picker order; availability in any particular run must be tested separately.
 */
inline const QVector<PlotAttribute> &attributesForKind(ObjectRef::Kind k)
{
    switch (k) {
    case ObjectRef::Kind::Node:
        return nodePlotAttributes();
    case ObjectRef::Kind::Link:
        return linkPlotAttributes();
    case ObjectRef::Kind::Subcatch:
        return subcatchPlotAttributes();
    case ObjectRef::Kind::System:
        return systemPlotAttributes();
    case ObjectRef::Kind::Mesh2DCell:
        return mesh2DCellPlotAttributes();
    case ObjectRef::Kind::Mesh2DEdge:
        return mesh2DEdgePlotAttributes();
    case ObjectRef::Kind::Mesh2DVertex:
        return mesh2DVertexPlotAttributes();
    default: {
        static const QVector<PlotAttribute> kEmpty;
        return kEmpty;
    }
    }
}

/*! \brief Y2b-1 (amendment D-Y4): the kind-keyed DESCRIPTOR list — the
 *  fixed `attributesForKind(kind)` set wrapped, plus one species
 *  descriptor per name in \p speciesNames for the element kinds that
 *  carry species columns (node/link/subcatch). Species ride BY NAME
 *  (D-G1). INLINE because IRunLayer's default implementation below calls
 *  it, and IRunLayer must stay a header-only interface — hundreds of
 *  tests stub it without linking any plot TU, so nothing reachable from
 *  a virtual's default may live in a .cpp (measured twice: an
 *  out-of-line default anchored the vtable in resultdescriptor.cpp, and
 *  an out-of-line list builder left the same tests with an undefined
 *  free-function symbol).
 * @param[in] kind Source object kind controlling the fixed list and whether species are appended.
 * @param[in] speciesNames Run-provided species order; only node/link/subcatch kinds append nonempty names. Duplicates are preserved.
 * @return Independent fixed-then-species list; mesh catalog variables require a source override and are not synthesized here.
 */
inline QVector<ResultDescriptor> resultDescriptorsForKind(ObjectRef::Kind kind,
                                                          const QStringList &speciesNames)
{
    QVector<ResultDescriptor> out;
    const QVector<PlotAttribute> &fixed = attributesForKind(kind);
    out.reserve(fixed.size() + speciesNames.size());
    for (const PlotAttribute a : fixed)
        out.append(ResultDescriptor::forAttribute(a));

    // Species columns exist for the element kinds the .out carries them
    // on (node/link/subcatch — Y2a's rule); System series and mesh kinds
    // have none. Order is the run's .out order — the engine's order — so
    // two pickers over the same run agree.
    const bool speciesKind = kind == ObjectRef::Kind::Node || kind == ObjectRef::Kind::Link ||
                             kind == ObjectRef::Kind::Subcatch;
    if (speciesKind)
        for (const QString &sp : speciesNames)
            if (!sp.isEmpty())
                out.append(ResultDescriptor::forSpecies(sp));
    return out;
}

/*! \brief Result of one series resolution. Times are SWMM DateTime doubles
 *         (OLE-Automation epoch, not astronomical Julian — see
 *         core/swmmdatetime.h); values are in the unit system the source
 *         advertises. */
struct SeriesData {
    std::vector<double>
        timesJulian; ///< SWMM DateTime; convert via openswmmvis::core::swmmDateTimeToQDateTime.
    std::vector<double> values; ///< Same length as timesJulian.
    bool ok = false;            ///< False if the source couldn't fulfil the request.
    QString errorMessage;       ///< Populated when ok == false.
    bool displayOnly = false; ///< Decimated data: never consume for statistics or export.
    bool allowPairing = true; ///< False until observation clock/statistic pairing is explicitly prepared.
    bool pointsOnly = false; ///< Irregular/calendar data require explicit continuity before connecting.
    QString displayNote; ///< Honest explanation of view reduction or temporal support.
    std::optional<SeriesStatistics> fullStatistics; ///< Exact full-resolution summary for the returned window.
    QDateTime statisticsFrom, statisticsTo; ///< Inclusive display window summarized above.

    /*! INPUT — first period (0-based) to resolve. A live chart that already
     *  holds periods [0, n) asks for n and gets only the tail; before this the
     *  comparison plot re-read every series in full on every live tick
     *  (O(run length) per tick, and a full-mesh copy per frame for 2D
     *  velocity / rainfall). Sources clamp it to [0, periodCount]; a source
     *  that ignores it simply returns the whole series (still correct — the
     *  chart de-duplicates by time). */
    int firstPeriod = 0;
    /*! OUTPUT — periods the source held when it answered (0 = not reported).
     *  The caller stores it as the next firstPeriod. */
    int periodCount = 0;
};

/*! A batch preserves request order and each live-series tail cursor. */
struct SeriesRequest {
    ObjectRef ref;
    ResultDescriptor descriptor;
    int firstPeriod = 0;
};

/*! \brief Abstract source backing one comparison-plot run. */
class IRunLayer
{
public:
    /**
     * @brief Destroy the concrete run source through the interface; callers own the lifetime unless a concrete QObject owner supplies it.
     */
    virtual ~IRunLayer() = default;

    /*! \brief Human label shown in the SeriesPanel tree + legends.
     * @return Display text chosen by the source; it need not be unique or stable.
     */
    virtual QString scenarioName() const = 0;

    /*! \brief Unit system the source advertises (drives axis labels).
     * @return Source-advertised unit family for interpreting values and labels; callers must not assume every variable follows it (some snow/species units are fixed).
     */
    virtual UnitSystem unitSystem() const = 0;

    /*! \brief Start date of the simulation as SWMM Julian, or NaN if unknown.
     * @return SWMM/OLE-Automation date, not astronomical Julian date; NaN means unknown.
     */
    virtual double startDateJulian() const = 0;

    /*! \brief Total number of time periods available (grows during live mode).
     * @return Currently available nonnegative period count, potentially increasing while a run is live.
     */
    virtual int periodCount() const = 0;

    /*! \brief Report step in seconds. 0 if unknown.
     * @return Report interval in seconds, or zero when unknown.
     */
    virtual int reportStepSeconds() const = 0;

    /*! \brief Resolve a series. Returns timesJulian + values for the spec.
     *  Implementations are responsible for resolving \p ref against their
     *  own indexing (e.g. SWMM ID → output-file index, triIdx → mesh face).
     *  When \p ref or \p attr is not supported, set `out.ok = false` and
     *  fill `out.errorMessage`.
     * @param[in] ref Source-local object identity; source performs any ID/index resolution.
     * @param[in] attr Fixed result quantity requested.
     * @param[in,out] out firstPeriod requests a zero-based tail; source returns times/values in its advertised units, ok/error and optionally periodCount. Implementations may ignore the cursor and return the full series; callers must inspect ok before using values.
     * @note This interface supplies no synchronization guarantee for concurrent access to a live source.
     */
    virtual void getSeriesAt(const ObjectRef &ref, PlotAttribute attr, SeriesData &out) const = 0;

    /*! Resolve related series together. Mesh sources share each frame read.
     * @param[in] requests Ordered object/descriptor/cursor requests, possibly empty.
     * @param[out] out Resized/replaced to one SeriesData per request. Each starts empty with its requested firstPeriod, then uses virtual descriptor resolution; one failure does not stop later requests.
     * @note Base implementation processes requests sequentially with no shared snapshot guarantee. Overrides may share frame reads.
     */
    virtual void getSeriesBatch(const QVector<SeriesRequest> &requests,
                                QVector<SeriesData> &out) const
    {
        out.resize(requests.size());
        for (int i = 0; i < requests.size(); ++i) {
            out[i] = SeriesData{};
            out[i].firstPeriod = requests[i].firstPeriod;
            getSeriesAt(requests[i].ref, requests[i].descriptor, out[i]);
        }
    }

    /*! \brief Convenience: does this source carry the given attribute at all?
     *  Default impl returns true; subclasses can refine (e.g. hide velocity
     *  attributes when CF.2 edge-flux data is missing).
     * @return Base implementation always true, including unknown attributes; subclasses provide availability filtering. This is a hint, not a guarantee that every object can resolve the quantity.
     */
    virtual bool supportsAttribute(PlotAttribute /*attr*/) const { return true; }

    /*! \brief Y2b-2 (amendment D-Y4): resolve a series by DESCRIPTOR.
     *  Fixed attributes forward to the enum overload above; a species
     *  refuses with a precise message — only sources that actually carry
     *  species columns override this (`SwmmOutRunLayer`).
     *  \note INLINE for the same header-only-interface reason as
     *  `resultDescriptorsForKind` below: nothing reachable from a
     *  default may live in a .cpp the stub tests do not link.
     * @param[in] ref Source-local object identity.
     * @param[in] descriptor Fixed, species or mesh-catalog identity. Base forwards fixed descriptors (including Unknown) to the enum overload.
     * @param[in,out] out Tail cursor and response. Unsupported dynamic branches set ok false and a source-named diagnostic but do not clear prior vectors/cursors; inspect ok.
     */
    virtual void getSeriesAt(const ObjectRef &ref, const ResultDescriptor &descriptor,
                             SeriesData &out) const
    {
        if (descriptor.isSpecies() || descriptor.isMeshVariable()) {
            out.ok = false;
            out.errorMessage =
                QStringLiteral("Source '%1' carries no %2 results")
                    .arg(scenarioName(), descriptor.isSpecies() ? QStringLiteral("species")
                                                                : QStringLiteral("2D variable"));
            return;
        }
        getSeriesAt(ref, descriptor.attr, out);
    }

    /*! \brief Y2b-1 (amendment D-Y4): everything plottable for \p kind on
     *  THIS run — the fixed attribute set as descriptors, plus any dynamic
     *  species the run carries. Base impl serves the fixed set only, which
     *  is exactly right for sources with no species (observed CSV, mesh
     *  layers, a legacy `.out` with no quality). `SwmmOutRunLayer`
     *  overrides to append the open run's species by name.
     *  \note INLINE on purpose: IRunLayer is a header-only interface —
     *  hundreds of tests stub it without linking plot TUs, and any
     *  out-of-line virtual would become the key function and anchor the
     *  vtable in a .cpp those tests do not link (measured: undefined
     *  vtable/typeinfo in test_plotvariablepickerdialog).
     * @param[in] kind Object kind whose picker choices are requested.
     * @return Base fixed list only, empty for observed/unknown kinds; it is not filtered through supportsAttribute(). Concrete sources may append species/catalog variables.
     */
    virtual QVector<ResultDescriptor> resultDescriptorsForKind(ObjectRef::Kind kind) const
    {
        return plot::resultDescriptorsForKind(kind, QStringList());
    }

    /*! \brief Stable identity string for `.oswp` round-trip. Defaults to the
     *  scenario name; subclasses may override with a file path or layer GUID.
     * @return Source identity token; base implementation returns scenarioName(), which alone provides no uniqueness guarantee.
     */
    virtual QString persistenceKey() const { return scenarioName(); }
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_IRUNLAYER_H
