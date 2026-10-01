// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QVector>
#include <memory>
#include <openswmm/engine/openswmm_trace.h>

class SWMMModelLayer;
namespace openswmmvis::trace
{
struct Node
{
    QString id;
    int type = 0, flags = 0;
    QPointF point;
    bool hasGeometry = false;
};
struct Link
{
    QString id;
    int from = -1, to = -1, type = 0;
    double length = 0;
    QVector<QPointF> points;
};
struct Snapshot
{
    QVector<Node> nodes;
    QVector<Link> links;
    QString wkt, modelFingerprint;
    QJsonObject toJson() const;
    static Snapshot fromJson(const QJsonObject &);
    static Snapshot capture(SWMMModelLayer *, QString *error);
    bool valid(QString *error = nullptr) const;
};
struct Dataset
{
    Snapshot snapshot;
    QString sourceId, runId, label, outputPath, fingerprint, packagePath;
    QString provenance =
        QStringLiteral("User-supplied model pairing; original topology unverified");
    SWMM_TraceOptions options{};
    SWMM_TraceInfo info{};
    QVector<SWMM_TraceNodeAverage> nodes;
    QVector<SWMM_TraceLinkAverage> links;
    Dataset() { swmm_trace_default_options(&options); }
};
struct Result
{
    std::shared_ptr<const Dataset> dataset;
    QString id;
    int seed = 0, direction = 0;
    bool saved = false;
    QVector<SWMM_TraceValue> nodes, links;
    SWMM_TraceSummary summary{};
    QJsonObject style;
    QString title() const;
};
QString statusText(int flags);
QJsonObject infoJson(const SWMM_TraceInfo &);
SWMM_TraceInfo infoFromJson(const QJsonObject &);
QJsonObject optionsJson(const SWMM_TraceOptions &);
SWMM_TraceOptions optionsFromJson(const QJsonObject &);
QJsonObject summaryJson(const SWMM_TraceSummary &);
SWMM_TraceSummary summaryFromJson(const QJsonObject &);
SWMM_Trace createHandle(const Snapshot &, const SWMM_TraceOptions &, QString *error);
} // namespace openswmmvis::trace
