#include "plot/profilesection.h"
#include "plot/profilesectionseries.h"
#include "layers/swmm2dresultslayer.h"
#include <QSet>
#include <algorithm>
#include <cmath>
#include <exception>

namespace ProfileSection {
using V=openswmmvis::io::Mesh2DResultVariable;
using S=openswmmvis::io::Mesh2DValueStatus;
namespace {
bool fail(QString *error,const QString &text){if(error)*error=text;return false;}
double metresPerUnit(const QString &unit) {
    const auto u=unit.trimmed().toLower();
    if(u=="m"||u=="metre"||u=="meter"||u=="metres"||u=="meters")return 1;
    if(u=="ft"||u=="foot"||u=="feet")return .3048;
    return std::numeric_limits<double>::quiet_NaN();
}
bool validStations(const QVector<SeriesStation>&stations) {
    double previous=-1;
    for(const auto&s:stations) {
        if(!std::isfinite(s.chainage)||s.chainage<previous||s.chainage<0
            ||!std::isfinite(s.scenePt.x())||!std::isfinite(s.scenePt.y())||s.cellId< -1)return false;
        previous=s.chainage;
    }
    return true;
}
}
QVector<SampledSeries> builtInSeries(const Section&section,const Definition&d) {
    QVector<SampledSeries> rows;
    for(int water=0;water<2;++water) {
        SampledSeries row;row.definition.id=water?"__surface_wse":"__terrain";
        row.definition.sourceId=d.primarySourceId;row.definition.variableKey=water?"surface_wse":"terrain_elevation";
        row.definition.label=water?"Surface water elevation":"Terrain elevation";row.definition.role=SeriesRole::Elevation;
        row.descriptor.dataset=row.definition.variableKey;row.descriptor.label=row.definition.label;
        row.descriptor.units=d.elevationUnits;row.descriptor.unitsKnown=true;
        row.descriptor.temporal=water?V::Temporal::Reported:V::Temporal::Static;
        row.units=d.elevationUnits;row.unitsKnown=true;row.requestedTime=section.requestedTime;
        if(water)row.effectiveTime=section.effectiveTime;
        for(const auto&ref:d.sources)if(ref.id==d.primarySourceId)row.sourcePath=ref.path;
        for(const auto&s:section.samples) {
            SeriesPoint p;p.chainage=s.chainage;p.scenePt=s.scenePt;p.cellId=s.triIdx;p.breakBefore=s.breakBefore;
            if(std::isfinite(s.ground)) {
                if(!water){p.value=s.ground;p.status=S::Valid;}
                else if(section.hasResults&&section.effectiveTime.isValid()) {
                    const double depth=section.exactWaterGeometry?s.signedDepthNow:s.depthNow;
                    if(std::isfinite(depth)){p.status=s.cellHasSurface&&depth>0?S::Valid:S::Waterless;p.value=s.ground+std::max(0.,depth);}
                }
            }
            row.points.push_back(p);
        }
        rows.push_back(row);
    }
    return rows;
}
int selectFrame(const QVector<QDateTime>&times,const QDateTime&requested,TimePolicy policy,QString*error) {
    if(error)error->clear();
    auto bad=[&](const QString&t){fail(error,t);return -1;};
    if(!requested.isValid()||times.isEmpty())return bad(QStringLiteral("No report time is available."));
    for(int i=0;i<times.size();++i)
        if(!times[i].isValid()||(i&&times[i]<=times[i-1]))return bad(QStringLiteral("Report times are invalid or not strictly increasing."));
    if(requested<times.first()||requested>times.last())return bad(QStringLiteral("The requested time is outside this source's report coverage."));
    const auto it=std::lower_bound(times.cbegin(),times.cend(),requested);
    if(it!=times.cend()&&*it==requested)return int(it-times.cbegin());
    if(policy==TimePolicy::Exact)return bad(QStringLiteral("This source has no report at the exact requested time."));
    return int(it-times.cbegin())-1;
}
bool validateDefinition(const Definition&d,QString*error) {
    if(error)error->clear();
    if(d.id.trimmed().isEmpty()||d.primarySourceId.trimmed().isEmpty()||d.sceneCRS.trimmed().isEmpty()
        ||d.horizontalUnits.trimmed().isEmpty()||!std::isfinite(metresPerUnit(d.elevationUnits)))
        return fail(error,QStringLiteral("Section identity, coordinate frame and length units are required."));
    if(d.scenePolyline.size()<2||d.scenePolyline.size()>100000||d.sources.isEmpty()||d.sources.size()>64||d.series.size()>128)
        return fail(error,QStringLiteral("Section path/source/series counts are invalid."));
    bool distinct=false;
    for(int i=0;i<d.scenePolyline.size();++i) {
        const auto&p=d.scenePolyline[i];if(!std::isfinite(p.x())||!std::isfinite(p.y()))return fail(error,QStringLiteral("Section path contains a nonfinite coordinate."));
        if(i&&(p.x()!=d.scenePolyline[0].x()||p.y()!=d.scenePolyline[0].y()))distinct=true;
    }
    if(!distinct)return fail(error,QStringLiteral("Section path has no length."));
    QSet<QString> sources,series;
    for(const auto&s:d.sources) {if(s.id.trimmed().isEmpty()||sources.contains(s.id))return fail(error,QStringLiteral("Source identities must be nonempty and unique."));sources.insert(s.id);}
    if(!sources.contains(d.primarySourceId))return fail(error,QStringLiteral("The primary source reference is missing."));
    for(const auto&s:d.series) {
        if(s.id.trimmed().isEmpty()||s.id.startsWith("__")||series.contains(s.id)||!sources.contains(s.sourceId)||s.variableKey.trimmed().isEmpty())return fail(error,QStringLiteral("Series identities or source references are invalid."));
        series.insert(s.id);
        if((s.role!=SeriesRole::Elevation&&s.role!=SeriesRole::Scalar)||(s.timePolicy!=TimePolicy::Exact&&s.timePolicy!=TimePolicy::Hold)
            ||!s.pen.color().isValid()||!std::isfinite(s.pen.widthF())||s.pen.widthF()<=0||s.pen.widthF()>32
            ||!std::isfinite(s.opacity)||s.opacity<0||s.opacity>1||!std::isfinite(s.minimum)||!std::isfinite(s.maximum)
            ||(s.customRange&&s.maximum<=s.minimum))return fail(error,QStringLiteral("Series style, sampling policy or range is invalid."));
    }
    return true;
}
bool sampleSeries(Section&section,const Definition&d,const QVector<SourceBinding>&bindings,const QDateTime&requested,QString*error) {
    if(!validateDefinition(d,error))return false;
    try {
    QVector<SampledSeries> result;result.reserve(d.series.size());
    for(const auto&def:d.series) {
        SampledSeries out;out.definition=def;out.requestedTime=requested;
        const SourceBinding *binding=nullptr;int matches=0;
        for(const auto&b:bindings)if(b.sourceId==def.sourceId){binding=&b;++matches;}
        QVector<SeriesStation> stations;
        if(binding)stations=binding->stations;
        if(stations.isEmpty()&&def.sourceId==d.primarySourceId)
            for(const auto&s:section.samples)stations.push_back({s.chainage,s.scenePt,binding&&binding->mapCell?binding->mapCell(s.scenePt):s.triIdx,s.breakBefore});
        for(const auto&s:stations){SeriesPoint p;static_cast<SeriesStation&>(p)=s;out.points.push_back(p);}
        auto reject=[&](const QString&t){out.error=t;for(auto&p:out.points){p.value=std::numeric_limits<double>::quiet_NaN();p.status=S::Missing;}result.push_back(out);};
        if(matches!=1||!binding||!binding->source){reject(QStringLiteral("Saved result source is unavailable or ambiguous."));continue;}
        if(binding->sceneCRS!=d.sceneCRS){reject(QStringLiteral("Source path mapping is not in the section coordinate frame."));continue;}
        if(stations.isEmpty()||!validStations(stations)){reject(QStringLiteral("An independent, valid source path mapping is required."));continue;}
        if(def.role==SeriesRole::Elevation&&def.sourceId!=d.primarySourceId
            &&(d.verticalDatum.isEmpty()||binding->verticalDatum.isEmpty()||d.verticalDatum!=binding->verticalDatum)) {
            reject(QStringLiteral("A compatible declared vertical datum is required for elevations from another source."));continue;
        }
        auto*source=binding->source;out.sourcePath=source->sourcePath();
        const int generation=source->historyGeneration(),cells=source->triangleCount(),timeCount=source->timeCount();
        int variableMatches=0;
        for(const auto&v:source->faceVariables())if(v.key()==def.variableKey){out.descriptor=v;++variableMatches;}
        if(variableMatches!=1){reject(QStringLiteral("Saved result variable is unavailable or ambiguous: %1").arg(def.variableKey));continue;}
        out.units=out.descriptor.units;out.unitsKnown=out.descriptor.unitsKnown;
        double factor=1;
        if(def.role==SeriesRole::Elevation) {
            if(out.descriptor.dataset!="Mesh2_face_gw_table_elev"&&out.descriptor.dataset!="Mesh2_face_gw_bed_elev") {
                reject(QStringLiteral("Only groundwater table or aquifer-base elevations belong on the shared elevation axis."));continue;
            }
            const double unit=metresPerUnit(out.units);
            if(!out.unitsKnown||!std::isfinite(unit)){reject(QStringLiteral("Elevation units are unresolved or unsupported."));continue;}
            factor=unit/metresPerUnit(d.elevationUnits);out.units=d.elevationUnits;
        }
        const bool independent=out.descriptor.temporal==V::Temporal::Static||out.descriptor.temporal==V::Temporal::Envelope;
        int frame=0;
        if(!independent) {
            QVector<QDateTime> times;times.reserve(timeCount);
            for(int t=0;t<timeCount;++t)times.push_back(source->simTimeAt(t));
            frame=selectFrame(times,requested,def.timePolicy,&out.error);
            if(frame<0||frame>=out.descriptor.frameCount){reject(out.error.isEmpty()?QStringLiteral("The variable has no report at this time."):out.error);continue;}
            out.frame=frame;out.effectiveTime=times[frame];
        }
        std::vector<float> values;std::vector<S> status;
        if(!source->readFaceVariableAt(out.descriptor,frame,values,status)||cells<=0||values.size()!=size_t(cells)||status.size()!=values.size()) {
            reject(QStringLiteral("Could not read a complete result frame."));continue;
        }
        if(source->historyGeneration()!=generation||source->triangleCount()!=cells||source->sourcePath()!=out.sourcePath
            ||source->timeCount()!=timeCount||(!independent&&source->simTimeAt(frame)!=out.effectiveTime)) {
            reject(QStringLiteral("The result source changed while the section was sampled."));continue;
        }
        for(auto&p:out.points) {
            if(p.cellId<0||p.cellId>=cells)continue;
            const auto state=status[size_t(p.cellId)];const double value=double(values[size_t(p.cellId)])*factor;
            if(state!=S::Valid&&state!=S::Missing&&state!=S::Waterless&&state!=S::NotApplicable)continue;
            if(state==S::Missing||!std::isfinite(value))continue;
            p.status=state;p.value=value;
        }
        result.push_back(std::move(out));
    }
    section.series=std::move(result);section.requestedTime=requested;section.effectiveTime={};
    for(const auto&binding:bindings)if(binding.sourceId==d.primarySourceId){section.effectiveTime=binding.surfaceTime;break;}
    return true;
    } catch(const std::exception &e) {return fail(error,QStringLiteral("Section sampling failed: %1").arg(QString::fromUtf8(e.what())));}
      catch(...) {return fail(error,QStringLiteral("Section sampling failed unexpectedly."));}
}
} // namespace ProfileSection
