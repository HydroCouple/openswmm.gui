#ifndef OPENSWMMVIS_PROFILESECTIONSERIES_H
#define OPENSWMMVIS_PROFILESECTIONSERIES_H

#include "io/mesh2dresultvariable.h"
#include <QDateTime>
#include <QJsonObject>
#include <QPen>
#include <QPointF>
#include <QStringList>
#include <QVector>
#include <functional>
#include <limits>

class IMesh2DSource;
namespace ProfileSection {
struct Section;
enum class SeriesRole { Elevation, Scalar };
enum class TimePolicy { Exact, Hold };
struct SourceReference {
    QString id, path, verticalDatum;
};
struct SeriesDefinition {
    QString id, sourceId, variableKey, label;
    SeriesRole role = SeriesRole::Scalar;
    TimePolicy timePolicy = TimePolicy::Hold;
    bool visible = true;
    QPen pen{QColor(0x24,0x70,0xa0),1.5};
    double opacity = 1.0;
    bool customRange = false;
    double minimum = 0.0, maximum = 1.0;
};
struct Definition {
    QString id, title;
    QVector<QPointF> scenePolyline; // scene Y is negative map Y; stored explicitly as scene coordinates
    QString sceneCRS, horizontalUnits, elevationUnits = QStringLiteral("m"), verticalDatum;
    QString primarySourceId;
    QVector<SourceReference> sources;
    QVector<SeriesDefinition> series;
    QJsonObject displayOptions; // viewer-owned versioned display-property values
};
struct SeriesStation {
    double chainage = 0.0;
    QPointF scenePt;
    int cellId = -1;
    bool breakBefore = false;
};
struct SeriesPoint : SeriesStation {
    double value = std::numeric_limits<double>::quiet_NaN();
    openswmmvis::io::Mesh2DValueStatus status = openswmmvis::io::Mesh2DValueStatus::Missing;
};
struct SampledSeries {
    SeriesDefinition definition;
    openswmmvis::io::Mesh2DResultVariable descriptor;
    QVector<SeriesPoint> points; // independent exact intervals for this source
    QString units, sourcePath, error;
    bool unitsKnown = false;
    QDateTime requestedTime, effectiveTime;
    int frame = -1; // static/envelope series have no report frame
};
struct SourceBinding {
    QString sourceId, sceneCRS, verticalDatum;
    IMesh2DSource *source = nullptr; // synchronous call only; owner guarantees lifetime
    QDateTime surfaceTime; // primary legacy surface geometry timestamp; never inferred from requested cursor
    QVector<SeriesStation> stations; // exact path intervals, independently mapped in the shared scene CRS
    std::function<int(const QPointF &)> mapCell; // optional primary-source fallback for legacy samples
};
// Shared terrain/current-WSE rows for the table and CSV; legacy values already use elevationUnits.
QVector<SampledSeries> builtInSeries(const Section &, const Definition &);
// Pure time selection: no extrapolation outside coverage and no interpolation.
int selectFrame(const QVector<QDateTime> &times, const QDateTime &requested,
                TimePolicy policy, QString *error = nullptr);
// Atomic definition validation; unavailable source/key yields a retained series with error.
// Existing exact terrain/surface fields are untouched. Elevation roles accept the
// authoritative groundwater table/base quantities only, with checked length units.
bool sampleSeries(Section &section, const Definition &definition,
                  const QVector<SourceBinding> &sources, const QDateTime &requested,
                  QString *error = nullptr);
bool validateDefinition(const Definition &, QString *error = nullptr);
QJsonObject definitionToJson(const Definition &, const QString &baseDirectory);
bool definitionFromJson(const QJsonObject &, const QString &baseDirectory,
                        Definition &, QString *error = nullptr);
// Atomic current-section table export. Unknown-unit visible series are refused.
bool exportSectionCsv(const Section &, const Definition &, const QString &path,
                      QString *error = nullptr);
} // namespace ProfileSection
#endif
