#include "assignment/heatassignment.h"
#include "layers/gisrasterlayer.h"
#include "map/spatialreferencesystem.h"
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <algorithm>

namespace OpenSWMMVis {
HeatRasterPreview sampleHeatRaster(SWMMModelLayer* model,GISRasterLayer* raster,int band,
    int attr,int scope,const QString& tag,int linkSamples,std::function<bool(int,int)> progress) {
    HeatRasterPreview result;
    if(!model||!model->engine()||!raster||!model->srs()||!raster->srs()
        ||model->srs()->toWkt().isEmpty()||raster->srs()->toWkt().isEmpty()) {
        result.error=QObject::tr("Assign a coordinate system to both the model and raster before sampling.");return result;
    }
    if(attr<0||attr>=14||band<1||band>raster->bandCount()||linkSamples<1
        ||(scope!=SWMM_HEAT_SCOPE_NODE&&scope!=SWMM_HEAT_SCOPE_LINK)
        ||(scope==SWMM_HEAT_SCOPE_NODE&&attr>=6)){result.error=QObject::tr("Choose a valid property, object type and raster band.");return result;}
    const auto engine=model->engine();const bool link=scope==SWMM_HEAT_SCOPE_LINK;
    const int count=link?swmm_link_count(engine):swmm_node_count(engine);
    for(int i=0;i<count;++i){
        if(progress&&!progress(i,count)){result.error=QObject::tr("Sampling cancelled.");result.edits.clear();return result;}
        if(model->engine()!=engine){result.error=QObject::tr("The model was closed.");result.edits.clear();return result;}
        if(link){int type=-1;if(swmm_link_get_type(engine,i,&type)!=SWMM_OK||type!=0)continue;}
        char objectTag[1024]{};const int rc=link?swmm_link_get_tag(engine,i,objectTag,sizeof objectTag):swmm_node_get_tag(engine,i,objectTag,sizeof objectTag);
        if(rc!=SWMM_OK){result.error=QObject::tr("Cannot read model tags.");return result;}
        if(!tag.isEmpty()&&tag!=QString::fromUtf8(objectTag))continue;
        const char* id=link?swmm_link_id(engine,i):swmm_node_id(engine,i);if(!id)continue;
        double value=0;bool valid=true,missingGeometry=false;
        if(!link){double x=0,y=0;if(!model->cachedNodeCoord(i,&x,&y)){valid=false;missingGeometry=true;}
            else value=raster->valueAt(x,y,model->srs(),band,&valid);
        }else {
            const auto points=model->cachedLinkPolyline(i);QVector<double> distances;distances.append(0.);
            for(int k=1;k<points.size();++k)distances.append(distances.last()+model->polylineLengthInModelUnits({points[k-1],points[k]}));
            if(points.size()<2||!std::isfinite(distances.last())||distances.last()<=0){valid=false;missingGeometry=true;}
            else for(int k=0;k<linkSamples;++k){const double distance=distances.last()*(k+.5)/linkSamples;
                const int segment=std::clamp(int(std::upper_bound(distances.begin(),distances.end(),distance)-distances.begin()),1,int(points.size()-1));
                const double fraction=(distance-distances[segment-1])/(distances[segment]-distances[segment-1]);
                const auto point=points[segment-1]+fraction*(points[segment]-points[segment-1]);bool ok=false;
                const double sample=raster->valueAt(point.x(),point.y(),model->srs(),band,&ok);
                if(!ok||!std::isfinite(sample)){valid=false;break;}value+=sample/linkSamples;
            }
        }
        if(!valid){++result.skipped;if(missingGeometry)++result.skippedGeometry;else ++result.skippedNoData;continue;}
        if(!heatPropertyValueValid(attr,value)){++result.skipped;++result.skippedRange;continue;}
        result.edits.append({attr,scope,QString::fromUtf8(id),value});
    }
    return result;
}
}
