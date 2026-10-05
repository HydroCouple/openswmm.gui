#ifndef OPENSWMMVIS_GROUNDWATERASSIGNMENT_H
#define OPENSWMMVIS_GROUNDWATERASSIGNMENT_H
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infil2d.h>
#include <QMap>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <functional>
#include <limits>

namespace openswmmvis::assignment {
struct InfiltrationOwnershipSnapshot {
    QVector<SWMM_Infil2DAuthoredRow> rows;
    QString destination;
};
struct InfiltrationOwnershipCell {
    int cell=0,owner=0,conflict=0;
    QString source,reason;
};
struct InfiltrationOwnershipPreview {
    InfiltrationOwnershipSnapshot before,after;
    QVector<InfiltrationOwnershipCell> cells;
    QStringList summaries;
    QString error;
    bool changed=false;
};
InfiltrationOwnershipPreview previewInfiltrationOwnership(SWMM_Engine,const QStringList& tags,bool removeExplicit);
bool applyInfiltrationOwnership(SWMM_Engine,const InfiltrationOwnershipSnapshot& expected,
                               const InfiltrationOwnershipSnapshot& desired,QString* error);
struct AquiferRow {
    int scope=0,cell=-1;
    QString tag;
    double ks=0,zs=0,thetaS=0,thetaR=0,alpha=0;
    QMap<QString,double> optional;
    bool operator==(const AquiferRow &) const = default;
};
struct AquiferSnapshot {
    QVector<AquiferRow> rows;
    QMap<QString,QString> options;
    bool operator==(const AquiferSnapshot &) const = default;
};
struct AquiferTarget {
    QString key,label,unitKind;
    double minimum=0,maximum=std::numeric_limits<double>::max();
    bool minimumExclusive=false,supported=true;
    QString unavailableReason;
};
struct AquiferProcessPreview {
    AquiferSnapshot before,after;
    QString groundwater,beforeEffective,afterEffective,forcing,error;
    bool changed=false;
};
QString groundwaterEtForcingStatus(SWMM_Engine,const QString& meshMode={});
AquiferProcessPreview previewAquiferProcesses(SWMM_Engine,const QString& et,const QString& link,const QString& wilting);
bool applyAquiferProcesses(SWMM_Engine,const AquiferSnapshot& expected,const AquiferSnapshot& desired,const QString& groundwater,QString* error);
QVector<AquiferTarget> aquiferTargets();
bool readAquiferSnapshot(SWMM_Engine,AquiferSnapshot *,QString *error);
struct AquiferRequest {
    AquiferSnapshot before;
    QVector<int> cells;
    QStringList cellTags; // Parallel to cells; engine cell indices, not display triangles.
    QString target;
    QVector<double> values; // One sampled value per cell; NaN is explicit NoData.
    bool skipNoData=false;
};
struct AquiferPreview {
    AquiferSnapshot before;
    QVector<AquiferRow> appended;
    QVector<int> cells,skippedCells;
    QVector<double> oldValues,newValues;
    QString target,error;
};
AquiferPreview previewAquiferAssignment(const AquiferRequest &);
// Append-only transaction: original authored records remain byte-for-byte in
// engine storage. Undo removes only a verified, unchanged appended suffix.
// The optional hook injects a failure before a numbered write for regression.
bool applyAquiferPreview(SWMM_Engine,const AquiferPreview &,QString *error,
                        std::function<bool(int)> beforeWrite={});
bool undoAquiferPreview(SWMM_Engine,const AquiferPreview &,QString *error);
}
#endif
