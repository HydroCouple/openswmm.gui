#ifndef OPENSWMMVIS_GROUNDWATERTRANSPORTASSIGNMENT_H
#define OPENSWMMVIS_GROUNDWATERTRANSPORTASSIGNMENT_H
#include <openswmm/engine/openswmm_engine.h>
#include "assignment/groundwaterassignment.h"
#include <QMap>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <functional>

namespace openswmmvis::assignment {
struct TransportSpecies {
    QString id, nativeConcUnits, nativeMassRateUnits;
    bool massSupported=false, signedConcentration=false;
    bool operator==(const TransportSpecies &) const = default;
};
struct GroundwaterQualityRow {
    int scope=0,cell=-1,zone=0,layer=0;
    QString tag,species;
    double value=0;
    bool operator==(const GroundwaterQualityRow &) const = default;
};
struct GroundwaterSpeciesTerm {
    QString species,kind=QStringLiteral("CONC"),series;
    double value=0;
    bool operator==(const GroundwaterSpeciesTerm &) const = default;
};
struct GroundwaterSourceRow {
    QString name,tag,series;
    int scope=2,cell=-1;
    double flow=0,scale=1;
    QVector<GroundwaterSpeciesTerm> terms;
    bool operator==(const GroundwaterSourceRow &) const = default;
};
struct GroundwaterTransportSnapshot {
    int cellCount=0;
    AquiferSnapshot aquifers;
    QStringList cellTags;
    QVector<bool> activeCells;
    QVector<GroundwaterQualityRow> quality;
    QString qualityFile;
    QVector<GroundwaterSourceRow> sources;
    QVector<TransportSpecies> species;
    QMap<QString,QString> options;
    QMap<QString,QVector<QPointF>> timeSeries;
    bool operator==(const GroundwaterTransportSnapshot &) const;
};
QVector<TransportSpecies> groundwaterTransportSpecies(SWMM_Engine,QString *error=nullptr);
bool readGroundwaterTransportSnapshot(SWMM_Engine,GroundwaterTransportSnapshot *,QString *error=nullptr);
enum class GroundwaterTransportTarget { InitialQuality, Source };
enum class GroundwaterSourceDistribution { PerCell, RegionTotal };
struct GroundwaterTransportRequest {
    GroundwaterTransportSnapshot before;
    GroundwaterTransportTarget target=GroundwaterTransportTarget::InitialQuality;
    QVector<int> cells;
    QVector<double> values; // initial values or per-cell constant source flow (m3/s)
    bool skipNoData=false;
    QString species;
    int zone=0; // SAT=0 / UNSAT=1 only
    QString sourceName=QStringLiteral("Assigned"),flowSeries;
    QVector<GroundwaterSpeciesTerm> terms;
    GroundwaterSourceDistribution distribution=GroundwaterSourceDistribution::PerCell;
    QVector<double> areaWeights; // positive cell areas; normalized for RegionTotal
};
struct GroundwaterTransportPreview {
    GroundwaterTransportSnapshot before,after;
    GroundwaterTransportTarget target=GroundwaterTransportTarget::InitialQuality;
    QVector<int> cells,skippedCells;
    QString error;
};
GroundwaterTransportPreview previewGroundwaterTransport(const GroundwaterTransportRequest &);
struct GroundwaterTransportResult {
    bool success=false,changed=false,rollbackComplete=true;
    QString error;
};
// Full preflight and readback. A failed write attempts bounded restoration;
// changed remains true when restoration cannot be confirmed (owner must dirty).
GroundwaterTransportResult applyGroundwaterTransport(SWMM_Engine,const GroundwaterTransportPreview &,
                                                     std::function<bool(int)> beforeWrite={});
GroundwaterTransportResult undoGroundwaterTransport(SWMM_Engine,const GroundwaterTransportPreview &);
}
#endif
