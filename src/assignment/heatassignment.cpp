#include "assignment/heatassignment.h"
#include "assignment/meshassignmentsampling.h"
#include "layers/gisrasterlayer.h"
#include "map/spatialreferencesystem.h"
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <algorithm>
#include <cmath>

namespace OpenSWMMVis {
HeatRasterPreview sampleHeatRaster(SWMMModelLayer* model,GISRasterLayer* raster,int band,
    int attr,int scope,const QString& tag,int linkSamples,std::function<bool(int,int)> progress,
    std::function<bool()> cancelled) {
    HeatRasterPreview result;
    if(!model||!model->engine()||!raster||!model->srs()||!raster->srs()
        ||model->srs()->toWkt().isEmpty()||raster->srs()->toWkt().isEmpty()) {
        result.error=QObject::tr("Assign a coordinate system to both the model and raster before sampling.");return result;
    }
    if(attr<0||attr>SWMM_HEAT_ATTR_MESH_POROSITY||band<1||band>raster->bandCount()||linkSamples<1
        ||(scope!=SWMM_HEAT_SCOPE_NODE&&scope!=SWMM_HEAT_SCOPE_LINK&&scope!=SWMM_HEAT_SCOPE_CELL2D)
        ||(scope==SWMM_HEAT_SCOPE_NODE&&attr>=6)
        ||(attr==SWMM_HEAT_ATTR_MESH_POROSITY&&scope!=SWMM_HEAT_SCOPE_CELL2D)
        ||(attr==SWMM_HEAT_ATTR_GROUND_SOURCE&&scope==SWMM_HEAT_SCOPE_CELL2D)){
        result.error=QObject::tr("Choose a valid property, object type and raster band.");return result;
    }
    const auto engine=model->engine();const bool link=scope==SWMM_HEAT_SCOPE_LINK;
    if(scope==SWMM_HEAT_SCOPE_CELL2D) {
        int cellCount=0;
        if(swmm_2d_cell_count(engine,&cellCount)!=SWMM_OK){result.error=QObject::tr("The model has no available 2D mesh.");return result;}
        openswmmvis::assignment::MeshAssignmentSampling::Job job;
        job.mode=openswmmvis::assignment::MeshAssignmentSampling::Mode::SingleNumeric;
        job.source=openswmmvis::assignment::MeshAssignmentSampling::Source::Raster;
        job.sampling=openswmmvis::assignment::MeshAssignmentSampling::Sampling::AreaWeightedMean;
        job.strictCrs=true;job.meshCrsWkt=model->srs()->toWkt();
        job.assignedSourceCrsWkt=raster->srs()->toWkt();job.rasterPath=raster->filePath();
        job.bands={band};job.targetKeys={QByteArray("heat")};
        job.targetMin={heatProperties()[attr].minimum};job.targetMax={heatProperties()[attr].maximum};
        for(int cell=0;cell<cellCount;++cell) {
            if((cancelled&&cancelled())||(progress&&!progress(cell,cellCount))){result.error=QObject::tr("Sampling cancelled.");return result;}
            char cellTag[1024]{};
            if(swmm_2d_get_triangle_tag(engine,cell,cellTag,sizeof cellTag)!=SWMM_OK){
                result.error=QObject::tr("Cannot read a 2D mesh region tag.");return result;
            }
            if(!tag.isEmpty()&&QString::fromUtf8(cellTag)!=tag)continue;
            int vertexCount=0,vertices[4]{-1,-1,-1,-1};
            if(swmm_2d_cell_vertex_count(engine,cell,&vertexCount)!=SWMM_OK||vertexCount<3||vertexCount>4
                ||swmm_2d_cell_get_vertices(engine,cell,vertices,&vertexCount)!=SWMM_OK){
                result.error=QObject::tr("Cannot read 2D cell vertices.");return result;
            }
            double twiceArea=0.0,centroidX=0.0,centroidY=0.0;
            QVector<QPointF> footprint;footprint.reserve(vertexCount);
            for(int i=0;i<vertexCount;++i){
                double x0=0.0,y0=0.0,z0=0.0,x1=0.0,y1=0.0,z1=0.0;
                if(swmm_2d_vertex_get_xyz(engine,vertices[i],&x0,&y0,&z0)!=SWMM_OK
                    ||swmm_2d_vertex_get_xyz(engine,vertices[(i+1)%vertexCount],&x1,&y1,&z1)!=SWMM_OK
                    ||!std::isfinite(x0)||!std::isfinite(y0)||!std::isfinite(x1)||!std::isfinite(y1)){
                    result.error=QObject::tr("Cannot read finite 2D cell vertices.");return result;
                }
                const double cross=x0*y1-x1*y0;
                twiceArea+=cross;
                centroidX+=(x0+x1)*cross;
                centroidY+=(y0+y1)*cross;
                footprint.append({x0,y0});
            }
            if(!std::isfinite(twiceArea)||twiceArea==0.0){
                result.error=QObject::tr("Cannot calculate a non-degenerate 2D cell centroid.");return result;
            }
            const double x=centroidX/(3.0*twiceArea),y=centroidY/(3.0*twiceArea);
            if(!std::isfinite(x)||!std::isfinite(y)){
                result.error=QObject::tr("Cannot calculate a finite 2D cell centroid.");return result;
            }
            job.triangles.append(cell);job.centroids.append(QPointF(x,y));
            for(const auto& point:footprint)job.triVerts.append(point);
            if(vertexCount==3)job.triVerts.append(footprint.front());
        }
        if(job.triangles.isEmpty()){result.error=QObject::tr("No 2D cells match the selected mesh region tag.");return result;}
        const auto sampled=openswmmvis::assignment::sampleMeshAssignment(job,
            cancelled,
            [&](int done,int total){if(progress)progress(done,total);});
        if(sampled.cancelled){result.error=QObject::tr("Sampling cancelled.");return result;}
        if(!sampled.error.isEmpty()){result.error=sampled.error;return result;}
        result.skippedNoData=sampled.skippedNoData;
        result.skippedRange=sampled.skippedRange;
        result.skipped=result.skippedNoData+result.skippedRange;
        if(sampled.values.isEmpty()){result.error=QObject::tr("The mesh raster sampler returned no values.");return result;}
        for(int i=0;i<sampled.triangles.size();++i){
            if(i>=sampled.values[0].size()||!heatPropertyValueValid(attr,sampled.values[0][i])){++result.skipped;++result.skippedRange;continue;}
            result.edits.append({attr,SWMM_HEAT_SCOPE_CELL2D,QString::number(sampled.triangles[i]+1),sampled.values[0][i]});
        }
        return result;
    }
    const int count=link?swmm_link_count(engine):swmm_node_count(engine);
    for(int i=0;i<count;++i){
        if((cancelled&&cancelled())||(progress&&!progress(i,count))){result.error=QObject::tr("Sampling cancelled.");result.edits.clear();return result;}
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
