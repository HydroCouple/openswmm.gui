/*!
 * \file   irunlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — abstract "data-source" that the ComparisonPlotModel
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
 */
#ifndef OPENSWMMVIS_PLOT_IRUNLAYER_H
#define OPENSWMMVIS_PLOT_IRUNLAYER_H

#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <vector>

namespace openswmmvis::plot {

/*! \brief Identifies what object a series is about. Kind is the
 *         discriminator; the active union member is implied by kind. */
struct ObjectRef {
    enum class Kind {
        Unknown    = 0,
        Node       = 1,    ///< name = SWMM node ID
        Link       = 2,    ///< name = SWMM link ID
        Subcatch   = 3,    ///< name = SWMM subcatchment ID
        Mesh2DCell = 4,    ///< triIdx valid; name unused (CF.3)
        Observed   = 5,    ///< name = observed series label (e.g. CSV header)
        System     = 6,    ///< single global series per attribute; name unused (AT.2)
        Mesh2DEdge = 7,    ///< triIdx + edgeLocal valid; name unused — for edge-flux series
        Mesh2DVertex = 8,  ///< triIdx holds the vertex index; depth/HGL interpolated from incident cells
    };

    Kind     kind      = Kind::Unknown;
    QString  name;          ///< SWMM ID for 1D kinds; observed-series label for Observed; unused for System.
    int      triIdx    = -1;   ///< Triangle index for Mesh2DCell / Mesh2DEdge; vertex index for Mesh2DVertex; -1 otherwise.
    int      edgeLocal = -1;   ///< Local edge index 0..2 for Mesh2DEdge; -1 otherwise.

    constexpr ObjectRef() = default;
    ObjectRef(Kind k, QString n) : kind(k), name(std::move(n)) {}
    static ObjectRef forNode(QString n)       { return ObjectRef(Kind::Node, std::move(n)); }
    static ObjectRef forLink(QString n)       { return ObjectRef(Kind::Link, std::move(n)); }
    static ObjectRef forSubcatch(QString n)   { return ObjectRef(Kind::Subcatch, std::move(n)); }
    static ObjectRef forMesh2DCell(int idx)   { ObjectRef r; r.kind = Kind::Mesh2DCell; r.triIdx = idx; return r; }
    static ObjectRef forMesh2DEdge(int tri, int edge)
    { ObjectRef r; r.kind = Kind::Mesh2DEdge; r.triIdx = tri; r.edgeLocal = edge; return r; }
    static ObjectRef forMesh2DVertex(int idx)
    { ObjectRef r; r.kind = Kind::Mesh2DVertex; r.triIdx = idx; return r; }
    static ObjectRef forObserved(QString lbl) { return ObjectRef(Kind::Observed, std::move(lbl)); }
    static ObjectRef forSystem()              { ObjectRef r; r.kind = Kind::System; return r; }

    bool isValid() const noexcept
    {
        switch (kind) {
        case Kind::Mesh2DCell:   return triIdx >= 0;
        case Kind::Mesh2DVertex: return triIdx >= 0;
        case Kind::Mesh2DEdge:   return triIdx >= 0 && edgeLocal >= 0 && edgeLocal <= 2;
        case Kind::System:       return true;   // name is unused
        case Kind::Unknown:      return false;
        default:                 return !name.isEmpty();
        }
    }

    bool operator==(const ObjectRef& other) const noexcept
    {
        return kind == other.kind && name == other.name
            && triIdx == other.triIdx && edgeLocal == other.edgeLocal;
    }
    bool operator!=(const ObjectRef& other) const noexcept { return !(*this == other); }
};

/*! \brief The canonical plottable-attribute list for \p k (presentation
 *  order), dispatching to the shared lists in plotattribute.h. Lives here
 *  rather than there because the nested ObjectRef::Kind cannot be named in
 *  plotattribute.h without an include cycle. Empty for kinds with no
 *  fixed list (mesh kinds are gated on layer capabilities; Observed and
 *  Unknown have none). */
inline const QVector<PlotAttribute> &attributesForKind(ObjectRef::Kind k)
{
    switch (k) {
    case ObjectRef::Kind::Node:     return nodePlotAttributes();
    case ObjectRef::Kind::Link:     return linkPlotAttributes();
    case ObjectRef::Kind::Subcatch: return subcatchPlotAttributes();
    case ObjectRef::Kind::System:   return systemPlotAttributes();
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
 *  free-function symbol). */
inline QVector<ResultDescriptor> resultDescriptorsForKind(
    ObjectRef::Kind kind, const QStringList &speciesNames)
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
    const bool speciesKind = kind == ObjectRef::Kind::Node ||
                             kind == ObjectRef::Kind::Link ||
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
    std::vector<double> timesJulian;   ///< SWMM DateTime; convert via openswmmvis::core::swmmDateTimeToQDateTime.
    std::vector<double> values;        ///< Same length as timesJulian.
    bool                ok = false;    ///< False if the source couldn't fulfil the request.
    QString             errorMessage;  ///< Populated when ok == false.
};

/*! \brief Abstract source backing one comparison-plot run. */
class IRunLayer {
public:
    virtual ~IRunLayer() = default;

    /*! \brief Human label shown in the SeriesPanel tree + legends. */
    virtual QString scenarioName() const = 0;

    /*! \brief Unit system the source advertises (drives axis labels). */
    virtual UnitSystem unitSystem() const = 0;

    /*! \brief Start date of the simulation as SWMM Julian, or NaN if unknown. */
    virtual double startDateJulian() const = 0;

    /*! \brief Total number of time periods available (grows during live mode). */
    virtual int periodCount() const = 0;

    /*! \brief Report step in seconds. 0 if unknown. */
    virtual int reportStepSeconds() const = 0;

    /*! \brief Resolve a series. Returns timesJulian + values for the spec.
     *  Implementations are responsible for resolving \p ref against their
     *  own indexing (e.g. SWMM ID → output-file index, triIdx → mesh face).
     *  When \p ref or \p attr is not supported, set `out.ok = false` and
     *  fill `out.errorMessage`. */
    virtual void getSeriesAt(const ObjectRef& ref,
                             PlotAttribute attr,
                             SeriesData& out) const = 0;

    /*! \brief Convenience: does this source carry the given attribute at all?
     *  Default impl returns true; subclasses can refine (e.g. hide velocity
     *  attributes when CF.2 edge-flux data is missing). */
    virtual bool supportsAttribute(PlotAttribute /*attr*/) const { return true; }

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
     *  vtable/typeinfo in test_plotvariablepickerdialog). */
    virtual QVector<ResultDescriptor> resultDescriptorsForKind(
        ObjectRef::Kind kind) const
    { return plot::resultDescriptorsForKind(kind, QStringList()); }

    /*! \brief Stable identity string for `.oswp` round-trip. Defaults to the
     *  scenario name; subclasses may override with a file path or layer GUID. */
    virtual QString persistenceKey() const { return scenarioName(); }
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_IRUNLAYER_H
