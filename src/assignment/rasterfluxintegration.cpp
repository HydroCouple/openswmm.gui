#include "assignment/rasterfluxintegration.h"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <QSet>
#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace openswmmvis::assignment {
namespace {
struct Sum {double value=0,correction=0;void add(double v){const double y=v-correction,t=value+y;correction=(t-value)-y;value=t;}};
using Polygon=QVector<QPointF>;
double cross(const QPointF&a,const QPointF&b){return a.x()*b.y()-a.y()*b.x();}
double area(const Polygon&p){if(p.size()<3)return 0;Sum s;for(int i=1;i+1<p.size();++i)s.add(cross(p[i]-p[0],p[i+1]-p[0]));return std::abs(s.value)*.5;}
Polygon clip(const Polygon&input,int axis,double edge,bool minimum){
 Polygon out;if(input.isEmpty())return out;out.reserve(input.size()+1);
 auto coord=[axis](const QPointF&p){return axis?p.y():p.x();};
 QPointF previous=input.last();double pv=coord(previous);bool pin=minimum?pv>=edge:pv<=edge;
 for(const auto&current:input){const double cv=coord(current);const bool cin=minimum?cv>=edge:cv<=edge;
  if(cin!=pin){const double t=(edge-pv)/(cv-pv);QPointF intersection=previous+(current-previous)*t;if(axis)intersection.setY(edge);else intersection.setX(edge);out.append(intersection);}
  if(cin)out.append(current);previous=current;pv=cv;pin=cin;
 }return out;
}
double pixelArea(const Polygon&cell,int x,int y){return area(clip(clip(clip(clip(cell,0,x,true),0,x+1.,false),1,y,true),1,y+1.,false));}
bool convex(const Polygon&p){
 if(p.size()!=3&&p.size()!=4)return false;double sign=0;
 for(int i=0;i<p.size();++i){const auto a=p[(i+1)%p.size()]-p[i],b=p[(i+2)%p.size()]-p[(i+1)%p.size()];const double c=cross(a,b);
  if(!std::isfinite(c)||c==0)return false;if(sign&&std::signbit(sign)!=std::signbit(c))return false;sign=c;
 }return std::isfinite(area(p))&&area(p)>0;
}
RasterFluxResult failure(const QString&message){RasterFluxResult r;r.error=message;return r;}
RasterFluxResult cancelledResult(){auto r=failure(QObject::tr("Cancelled."));r.cancelled=true;return r;}
RasterFluxResult integrate(const RasterFluxRequest&j,const std::function<bool()>&cancel){
 auto stopped=[&]{return cancel&&cancel();};if(stopped())return cancelledResult();
 double factor=0;if(j.densityUnit=="m/s")factor=1;else if(j.densityUnit=="mm/h")factor=.001/3600;else if(j.densityUnit=="m/day")factor=1./86400;else if(j.densityUnit=="in/h")factor=.0254/3600;
 if(!factor)return failure(QObject::tr("Choose explicit water flux density units (m/s, mm/h, m/day or in/h)."));
 if(!std::isfinite(j.scale)||!std::isfinite(j.offset))return failure(QObject::tr("Source scale and offset must be finite."));
 if(j.cells.isEmpty()||j.cells.size()!=j.footprints.size())return failure(QObject::tr("Each selected cell needs its own footprint."));
 if(j.coverage!=RasterFluxCoverage::Complete&&j.coverage!=RasterFluxCoverage::ValidAreaOnly)return failure(QObject::tr("Choose a supported coverage policy."));
 QSet<int> ids;for(int i=0;i<j.cells.size();++i){if(stopped())return cancelledResult();if(j.cells[i]<0||ids.contains(j.cells[i]))return failure(QObject::tr("Selected cell IDs must be distinct and nonnegative."));ids.insert(j.cells[i]);for(const auto&p:j.footprints[i])if(!std::isfinite(p.x())||!std::isfinite(p.y()))return failure(QObject::tr("Cell coordinates must be finite."));if(!convex(j.footprints[i]))return failure(QObject::tr("Cell %1 needs a nondegenerate triangle or convex quadrilateral for flux integration.").arg(j.cells[i]+1));}
 std::unique_ptr<GDALDataset,decltype(&GDALClose)> ds(static_cast<GDALDataset*>(GDALOpenEx(j.path.toUtf8().constData(),GDAL_OF_RASTER|GDAL_OF_READONLY,nullptr,nullptr,nullptr)),&GDALClose);
 if(!ds)return failure(QObject::tr("Cannot open the flux raster."));
 if(j.band<1||j.band>ds->GetRasterCount())return failure(QObject::tr("The flux raster has no band %1.").arg(j.band));
 OGRSpatialReference mesh,source;mesh.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
 const QByteArray mw=j.meshCrsWkt.toUtf8(),sw=j.assignedSourceCrsWkt.isEmpty()?QByteArray(ds->GetProjectionRef()):j.assignedSourceCrsWkt.toUtf8();
 if(mw.isEmpty()||sw.isEmpty()||mesh.importFromWkt(mw.constData())!=OGRERR_NONE||source.importFromWkt(sw.constData())!=OGRERR_NONE||!mesh.IsProjected()||!source.IsProjected()||!mesh.IsSame(&source))return failure(QObject::tr("Flux integration requires the raster and mesh to share an equivalent projected CRS. Prepare the raster in the mesh CRS; geographic and different projections are unsupported."));
 const double units=mesh.GetLinearUnits();if(!std::isfinite(units)||units<=0)return failure(QObject::tr("The projected CRS must declare valid linear units."));
 double gt[6];if(ds->GetGeoTransform(gt)!=CE_None)return failure(QObject::tr("Flux integration requires an explicit raster geotransform."));
 for(double v:gt)if(!std::isfinite(v))return failure(QObject::tr("The raster geotransform must be finite."));
 double inverse[6];if(!GDALInvGeoTransform(gt,inverse))return failure(QObject::tr("The raster geotransform is singular."));
 const double squareMetres=std::abs(gt[1]*gt[5]-gt[2]*gt[4])*units*units;
 if(!std::isfinite(squareMetres)||squareMetres<=0)return failure(QObject::tr("The raster pixel area cannot be represented in square metres."));
 auto*band=ds->GetRasterBand(j.band);int hasNoData=0;const double noData=band->GetNoDataValue(&hasNoData);const bool needsMask=(band->GetMaskFlags()&GMF_ALL_VALID)==0;auto*mask=needsMask?band->GetMaskBand():nullptr;
 const int nx=ds->GetRasterXSize(),ny=ds->GetRasterYSize();constexpr int chunk=4096;QVector<double> buffer(chunk);QVector<unsigned char> maskBuffer(chunk);
 RasterFluxResult r;Sum selected,valid,missing,flow;int polls=0;
 for(int i=0;i<j.cells.size();++i){if(stopped())return cancelledResult();Polygon cell;cell.reserve(j.footprints[i].size());
  double xmin=std::numeric_limits<double>::infinity(),xmax=-xmin,ymin=xmin,ymax=-xmin;
  for(const auto&p:j.footprints[i]){const double dx=p.x()-gt[0],dy=p.y()-gt[3];QPointF q(dx*inverse[1]+dy*inverse[2],dx*inverse[4]+dy*inverse[5]);if(!std::isfinite(q.x())||!std::isfinite(q.y()))return failure(QObject::tr("A cell cannot be represented in raster pixel coordinates."));cell.append(q);xmin=std::min(xmin,q.x());xmax=std::max(xmax,q.x());ymin=std::min(ymin,q.y());ymax=std::max(ymax,q.y());}
  const double cellArea=area(cell)*squareMetres;if(!std::isfinite(cellArea)||cellArea<=0)return failure(QObject::tr("A cell area cannot be represented in square metres."));
  const int x0=int(std::clamp(std::floor(xmin),0.,double(nx))),x1=int(std::clamp(std::ceil(xmax),0.,double(nx))),y0=int(std::clamp(std::floor(ymin),0.,double(ny))),y1=int(std::clamp(std::ceil(ymax),0.,double(ny)));
  Sum cellValid,cellFlow;bool hasInjection=false,hasExtraction=false;
  for(int y=y0;y<y1;++y)for(int x=x0;x<x1;){if(stopped())return cancelledResult();const int count=std::min(chunk,x1-x);
   if(band->RasterIO(GF_Read,x,y,count,1,buffer.data(),count,1,GDT_Float64,0,0)!=CE_None||(mask&&mask->RasterIO(GF_Read,x,y,count,1,maskBuffer.data(),count,1,GDT_Byte,0,0)!=CE_None))return failure(QObject::tr("Cannot read flux raster values or its validity mask."));
   for(int k=0;k<count;++k){if((++polls%128)==0&&stopped())return cancelledResult();const double v=buffer[k];if(!std::isfinite(v)||(hasNoData&&v==noData)||(mask&&!maskBuffer[k]))continue;
    const double a=pixelArea(cell,x+k,y)*squareMetres;if(a<=0)continue;const double density=(v*j.scale+j.offset)*factor,contribution=density*a;
    if(!std::isfinite(a)||!std::isfinite(density)||!std::isfinite(contribution))return failure(QObject::tr("A flux value or integrated flow is not finite; review source scale, offset and units."));
    hasInjection|=density>0;hasExtraction|=density<0;
    if(hasInjection&&hasExtraction)return failure(QObject::tr("Cell %1 intersects both injection and extraction flux. Netting them would lose water-quality exchange. Split or reprocess the source into separate same-sign assignments.").arg(j.cells[i]+1));
    cellValid.add(a);cellFlow.add(contribution);
   }x+=count;
  }
  const double tolerance=cellArea*1e-10;
  if(!std::isfinite(cellValid.value)||!std::isfinite(cellFlow.value)||cellValid.value>cellArea+tolerance)return failure(QObject::tr("Integrated area or flow is numerically invalid."));
  const double uncovered=std::max(0.,cellArea-cellValid.value);const bool incomplete=uncovered>tolerance;
  if(j.coverage==RasterFluxCoverage::Complete&&incomplete)return failure(QObject::tr("Cell %1 has %2 m² without valid raster coverage. Choose valid-area integration explicitly or provide complete coverage.").arg(j.cells[i]+1).arg(uncovered,0,'g',17));
  if(cellValid.value<=0)++r.emptyCells;else if(incomplete)++r.partialCells;
  r.flows.append(cellValid.value>0?cellFlow.value:std::numeric_limits<double>::quiet_NaN());r.validAreas.append(std::min(cellArea,cellValid.value));r.uncoveredAreas.append(uncovered);
  selected.add(cellArea);valid.add(r.validAreas.last());missing.add(uncovered);flow.add(cellFlow.value);
 }
 if(stopped())return cancelledResult();
 r.selectedArea=selected.value;r.validArea=valid.value;r.uncoveredArea=missing.value;r.totalFlow=flow.value;
 for(double total:{r.selectedArea,r.validArea,r.uncoveredArea,r.totalFlow})if(!std::isfinite(total))return failure(QObject::tr("The selected area or total flow exceeds the supported numeric range."));
 return r;
}
}
RasterFluxResult integrateRasterFlux(const RasterFluxRequest&j,std::function<bool()> cancelled){
 try{return integrate(j,cancelled);}catch(const std::exception&e){return failure(QObject::tr("Flux integration failed: %1").arg(QString::fromUtf8(e.what())));}catch(...){return failure(QObject::tr("Flux integration failed."));}
}
}
