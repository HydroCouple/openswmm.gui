#include "io/mesh2dvariablegisexport.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/meshspatialgrid.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <QFile>
#include <QFileInfo>
#include <QPolygonF>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <cmath>
#include <filesystem>
#include <limits>

namespace openswmmvis::io {
namespace {
using Status = Mesh2DValueStatus;
bool fail(QString *error, const QString &text) { if (error) *error=text;return false; }
QString statusName(Status s) {
    switch(s){case Status::Valid:return "valid";case Status::Missing:return "missing";
    case Status::Waterless:return "waterless";case Status::NotApplicable:return "not_applicable";}
    return {};
}
bool sameFile(const QString &a,const QString &b) {
    if(a.isEmpty()||b.isEmpty())return false;
    std::error_code ec;
    #ifdef Q_OS_WIN
    const auto path=[](const QString &s){return std::filesystem::path(s.toStdWString());};
    if(std::filesystem::equivalent(path(a),path(b),ec))return true;
#else
    if(std::filesystem::equivalent(std::filesystem::path(a.toStdString()),std::filesystem::path(b.toStdString()),ec))return true;
#endif
    ProjectSaveOutputs::DestinationState x,y;
    return ProjectSaveOutputs::captureDestination(a,&x)&&ProjectSaveOutputs::captureDestination(b,&y)&&x.resolvedPath==y.resolvedPath;
}
QPolygonF polygon(const Mesh2DVariableGisSnapshot &s,size_t cell) {
    QPolygonF p;for(int v:s.cells[cell])if(v>=0)p.append(s.vertices[v]);return p;
}
bool contains(const Mesh2DVariableGisSnapshot &s, size_t cell, const QPointF &point) {
    const auto &ids=s.cells[cell];const int n=ids[3]<0?3:4;int sign=0;
    for(int k=0;k<n;++k){const auto a=s.vertices[ids[k]],b=s.vertices[ids[(k+1)%n]];
        const auto u=b-a,v=point-a;const double z=u.x()*v.y()-u.y()*v.x();
        const double tolerance=16*std::numeric_limits<double>::epsilon()*(std::abs(u.x()*v.y())+std::abs(u.y()*v.x()));
        if(std::abs(z)<=tolerance)continue;const int current=z>0?1:-1;if(sign&&sign!=current)return false;sign=current;}
    return true;
}
bool validSnapshot(const Mesh2DVariableGisSnapshot &s,QString *error) {
    if(!s.scalar.descriptor.unitsKnown||s.scalar.descriptor.units.trimmed().isEmpty())
        return fail(error,"The variable has unresolved units. GIS export requires declared native units.");
    if(s.scalar.descriptor.key().isEmpty()||!s.scalar.error.isEmpty()||s.cells.empty()
        ||s.scalar.values.size()!=s.cells.size()||s.scalar.status.size()!=s.cells.size())
        return fail(error,"The variable snapshot is incomplete.");
    for(const auto &p:s.vertices)if(!std::isfinite(p.x())||!std::isfinite(p.y()))return fail(error,"Mesh coordinates are not finite.");
    for(size_t c=0;c<s.cells.size();++c){
        const auto &ids=s.cells[c];const int n=ids[3]==-1?3:4;
        for(int k=0;k<n;++k){if(ids[k]<0||ids[k]>=s.vertices.size())return fail(error,"Mesh connectivity is invalid.");
            for(int j=0;j<k;++j)if(ids[j]==ids[k])return fail(error,"A mesh cell repeats a vertex.");}
        const auto p=polygon(s,c);const auto o=p[0];double area=0;int sign=0;
        for(int k=0;k<n;++k){const auto a=p[k]-o,b=p[(k+1)%n]-o;area+=a.x()*b.y()-a.y()*b.x();
            const auto u=p[(k+1)%n]-p[k],v=p[(k+2)%n]-p[(k+1)%n];const double z=u.x()*v.y()-u.y()*v.x();
            if(!std::isfinite(z)||z==0)return fail(error,"A mesh cell is degenerate.");
            const int current=z>0?1:-1;if(sign&&sign!=current)return fail(error,"A mesh cell is not a simple convex polygon.");sign=current;}
        if(!std::isfinite(area)||area==0)return fail(error,"A mesh cell has no finite area.");
        if(statusName(s.scalar.status[c]).isEmpty())return fail(error,"The variable contains an unsupported status.");
        if(s.scalar.status[c]==Status::Valid&&!std::isfinite(s.scalar.values[c]))return fail(error,"A usable scalar value is not finite.");
    }
    OGRSpatialReference reference;reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if(s.crsWkt.isEmpty()||reference.SetFromUserInput(s.crsWkt.toUtf8().constData())!=OGRERR_NONE||!reference.IsProjected())
        return fail(error,"GIS export requires an explicit projected coordinate reference with known linear units.");
    return true;
}
bool safeSpatialIndex(const QVector<QRectF> &boxes,const QRectF &bounds,QString *error,const std::function<bool()> &cancelled) {
    // Mirror the shared grid's sizing calculation before any double-to-int
    // conversion. Pathological sparse/aspect-ratio data must fail explicitly.
    QVector<double> diagonal;diagonal.reserve(boxes.size());for(const auto &b:boxes)diagonal.append(std::hypot(b.width(),b.height()));
    std::nth_element(diagonal.begin(),diagonal.begin()+diagonal.size()/2,diagonal.end());
    double cell=std::max(diagonal[diagonal.size()/2]*16.,1e-6);
    double cols=std::max(1.,std::ceil(bounds.width()/cell)),rows=std::max(1.,std::ceil(bounds.height()/cell));
    const auto unsupported=[&]{return fail(error,"Mesh extent or density exceeds the safe raster spatial index. Export cell polygons instead.");};
    if(!std::isfinite(cell)||!std::isfinite(cols)||!std::isfinite(rows)||cols>std::numeric_limits<int>::max()-1||rows>std::numeric_limits<int>::max()-1)return unsupported();
    if(cols*rows>1024.*1024.){cell*=std::sqrt(cols*rows/(1024.*1024.));cols=std::max(1.,std::ceil(bounds.width()/cell));rows=std::max(1.,std::ceil(bounds.height()/cell));}
    if(!std::isfinite(cell)||cols*rows>4.*1024.*1024.)return unsupported();
    quint64 memberships=0;int index=0;
    for(const auto &b:boxes){if((index++%256)==0&&cancelled&&cancelled())return fail(error,"Export cancelled.");
        const auto nx=std::clamp(std::floor((b.right()-bounds.left())/cell),0.,cols-1)-std::clamp(std::floor((b.left()-bounds.left())/cell),0.,cols-1)+1;
        const auto ny=std::clamp(std::floor((b.bottom()-bounds.top())/cell),0.,rows-1)-std::clamp(std::floor((b.top()-bounds.top())/cell),0.,rows-1)+1;
        memberships+=quint64(nx)*quint64(ny);if(memberships>32000000)return unsupported();
    }
    return true;
}
QString domain(const Mesh2DResultVariable &d){return d.domain==Mesh2DResultVariable::Domain::Groundwater?"groundwater":"surface";}
QString zone(const Mesh2DResultVariable &d){using Z=Mesh2DResultVariable::Zone;return d.zone==Z::Saturated?"saturated":d.zone==Z::Unsaturated?"unsaturated":d.zone==Z::Sigma?"sigma":"none";}
QString temporal(const Mesh2DResultVariable &d){using T=Mesh2DResultVariable::Temporal;return d.temporal==T::Static?"static":d.temporal==T::Envelope?"envelope":d.temporal==T::Held?"held":"reported";}
void metadata(GDALMajorObject *object,const Mesh2DVariableGisSnapshot &s){
    const auto &d=s.scalar.descriptor;
    const std::pair<const char*,QString> rows[]={{"SOURCE",s.sourcePath},{"VARIABLE_KEY",d.key()},{"DATASET",d.dataset},{"SPECIES",d.species},
        {"DOMAIN",domain(d)},{"ZONE",zone(d)},{"NATIVE_UNITS",d.units},{"TIME",s.time.toString(Qt::ISODateWithMs)},
        {"FRAME_INDEX",QString::number(s.requestedFrame)},{"TEMPORAL",temporal(d)},{"SIGMA_LAYER_INDEX",QString::number(d.layer)},
        {"STATUS_CODES","0=valid;1=missing;2=waterless;3=not_applicable;255=outside"}};
    for(const auto &r:rows)object->SetMetadataItem(r.first,r.second.toUtf8().constData());
}
struct Dataset {GDALDataset *value=nullptr;~Dataset(){if(value)GDALClose(value);}bool close(){if(!value)return true;const auto flushed=value->FlushCache(false);const auto closed=GDALClose(value);value=nullptr;return flushed==CE_None&&closed==CE_None;}};
}
Mesh2DVariableGisSnapshotPtr captureMesh2DVariableGis(IMesh2DSource &source,const QString &key,int time,
    double scale,const QString &crs,QString *error) {
    if(error)error->clear();
    try {
        if(!std::isfinite(scale)||scale<=0){fail(error,"Coordinate conversion is unresolved.");return {};}
        auto *live=dynamic_cast<EngineMesh2DSource*>(&source);const bool pinned=live&&live->historyPinned();
        if(live)live->setHistoryPinned(true);const auto unpin=qScopeGuard([&]{if(live)live->setHistoryPinned(pinned);});
        auto s=std::make_shared<Mesh2DVariableGisSnapshot>();s->sourcePath=source.sourcePath();s->crsWkt=crs;s->requestedFrame=time;
        int matches=0;for(const auto &d:source.faceVariables())if(d.key()==key){s->scalar.descriptor=d;++matches;}
        const auto &d=s->scalar.descriptor;using T=Mesh2DResultVariable::Temporal;
        const bool independent=d.temporal==T::Static||d.temporal==T::Envelope;
        if(matches!=1||(!independent&&(time<0||time>=source.timeCount()||time>=d.frameCount))){fail(error,"The selected variable or frame is unavailable.");return {};}
        s->time=independent?QDateTime():source.simTimeAt(time);const int generation=source.historyGeneration(),revision=source.resultGeneration();
        std::vector<double> x,y,z;
        if(!source.readCells(x,y,z,s->cells)||x.size()!=y.size()||x.size()!=z.size()
            ||!source.readFaceVariableAt(d,independent?0:time,s->scalar.values,s->scalar.status)){fail(error,"Cannot acquire complete result geometry and values.");return {};}
        if(source.historyGeneration()!=generation||source.resultGeneration()!=revision||source.sourcePath()!=s->sourcePath){fail(error,"The result source changed during snapshot acquisition.");return {};}
        s->vertices.reserve(qsizetype(x.size()));for(size_t i=0;i<x.size();++i)s->vertices.append(QPointF(x[i]*scale,y[i]*scale));
        if(!validSnapshot(*s,error))return {};return s;
    }catch(const std::exception &e){fail(error,QString::fromUtf8(e.what()));return {};}
}
bool exportMesh2DVariableGis(const Mesh2DVariableGisSnapshot &s,const Mesh2DVariableGisOptions &options,
    const std::function<bool()> &cancelled,QString *error,const std::function<void(int)> &progress) {
    if(error)error->clear();
    try {
        const auto cancel=[&]{return cancelled&&cancelled();};
        if(cancel())return fail(error,"Export cancelled.");
        if(!validSnapshot(s,error))return false;
        if(options.path.isEmpty()||options.destination.resolvedPath.isEmpty())return fail(error,"An export destination baseline is required.");
        const auto unchanged=[&]{ProjectSaveOutputs::DestinationState now;return ProjectSaveOutputs::captureDestination(options.path,&now,error)
            &&now.resolvedPath==options.destination.resolvedPath&&now.fingerprint==options.destination.fingerprint;};
        const auto noCompanions=[&]{for(const auto &suffix:QStringList{".aux.xml",".ovr",".msk","-wal","-shm","-journal"})if(QFileInfo::exists(options.path+suffix)||QFileInfo(options.path+suffix).isSymLink())return false;return true;};
        if(!noCompanions())return fail(error,"The export destination has companion files. Choose a new destination so existing metadata, masks or database journals cannot change the exported result.");
        if(sameFile(s.sourcePath,options.path))return fail(error,"The export destination aliases its result source.");
        if(!unchanged())return fail(error,"The export destination changed; choose it again.");
        QTemporaryDir stage(QFileInfo(options.path).absoluteDir().filePath(".scalar-export-XXXXXX"));
        if(!stage.isValid())return fail(error,"Cannot create an adjacent export stage.");
        const bool raster=options.format==Mesh2DVariableGisFormat::GeoTiff;
        const QString path=stage.filePath(raster?"result.tif":"result.gpkg");
        GDALAllRegister();auto *driver=GetGDALDriverManager()->GetDriverByName(raster?"GTiff":"GPKG");
        if(!driver)return fail(error,"The required GIS format is unavailable in this installation.");
        OGRSpatialReference reference;reference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);reference.SetFromUserInput(s.crsWkt.toUtf8().constData());
        Dataset output;
        if(!raster){
            output.value=driver->Create(path.toUtf8().constData(),0,0,0,GDT_Unknown,nullptr);
            if(!output.value)return fail(error,"Cannot create GeoPackage export.");
            auto *layer=output.value->CreateLayer("scalar_cells",&reference,wkbPolygon,nullptr);if(!layer)return fail(error,"Cannot create scalar cell layer.");
            metadata(output.value,s);metadata(layer,s);
            const std::pair<const char*,OGRFieldType> fields[]={{"cell_index",OFTInteger64},{"value",OFTReal},{"status",OFTString},
                {"units",OFTString},{"variable_key",OFTString},{"species",OFTString},{"domain",OFTString},{"zone",OFTString},{"time",OFTString}};
            for(const auto &field:fields){OGRFieldDefn f(field.first,field.second);if(layer->CreateField(&f)!=OGRERR_NONE)return fail(error,"Cannot create scalar export fields.");}
            if(layer->StartTransaction()!=OGRERR_NONE)return fail(error,"Cannot start vector export transaction.");
            for(size_t c=0;c<s.cells.size();++c){
                if((c%256)==0){if(cancel())return fail(error,"Export cancelled.");if(progress)progress(int(80*c/s.cells.size()));}
                OGRFeature *f=OGRFeature::CreateFeature(layer->GetLayerDefn());const auto cleanup=qScopeGuard([&]{OGRFeature::DestroyFeature(f);});
                f->SetField("cell_index",GIntBig(c));if(s.scalar.status[c]!=Status::Missing&&std::isfinite(s.scalar.values[c]))f->SetField("value",double(s.scalar.values[c]));
                f->SetField("status",statusName(s.scalar.status[c]).toUtf8().constData());f->SetField("units",s.scalar.descriptor.units.toUtf8().constData());
                f->SetField("variable_key",s.scalar.descriptor.key().toUtf8().constData());f->SetField("species",s.scalar.descriptor.species.toUtf8().constData());
                f->SetField("domain",domain(s.scalar.descriptor).toUtf8().constData());f->SetField("zone",zone(s.scalar.descriptor).toUtf8().constData());
                f->SetField("time",s.time.toString(Qt::ISODateWithMs).toUtf8().constData());
                OGRLinearRing ring;for(const auto &p:polygon(s,c))ring.addPoint(p.x(),p.y());ring.closeRings();OGRPolygon geometry;geometry.addRing(&ring);f->SetGeometry(&geometry);
                if(layer->CreateFeature(f)!=OGRERR_NONE)return fail(error,"Writing a scalar cell failed.");
            }
            if(layer->CommitTransaction()!=OGRERR_NONE)return fail(error,"Cannot finish vector export transaction.");
        }else{
            QRectF bounds;QVector<QRectF> boxes;boxes.reserve(qsizetype(s.cells.size()));
            for(size_t c=0;c<s.cells.size();++c){const auto b=polygon(s,c).boundingRect();boxes.append(b);bounds=c?bounds.united(b):b;}
            const double pixel=options.pixelSize,w=std::ceil(bounds.width()/pixel),h=std::ceil(bounds.height()/pixel);
            if(!std::isfinite(pixel)||pixel<=0||!std::isfinite(w)||!std::isfinite(h)||w<1||h<1||w>1000000||h>1000000||w*h>64000000)
                return fail(error,"Choose a finite positive pixel size producing at most 64 million pixels and one million per side.");
            if(!safeSpatialIndex(boxes,bounds,error,cancelled))return false;
            const int width=int(w),height=int(h);char *creation[]={const_cast<char*>("COMPRESS=DEFLATE"),const_cast<char*>("TILED=YES"),nullptr};
            output.value=driver->Create(path.toUtf8().constData(),width,height,2,GDT_Float64,creation);
            if(!output.value)return fail(error,"Cannot create GeoTIFF export.");
            double transform[]={bounds.left(),pixel,0,bounds.bottom(),0,-pixel};
            if(output.value->SetGeoTransform(transform)!=CE_None||output.value->SetSpatialRef(&reference)!=CE_None)return fail(error,"Cannot write raster coordinate metadata.");
            metadata(output.value,s);auto *values=output.value->GetRasterBand(1),*statuses=output.value->GetRasterBand(2);
            values->SetDescription("Native scalar value (valid cells only)");values->SetUnitType(s.scalar.descriptor.units.toUtf8().constData());values->SetNoDataValue(std::numeric_limits<double>::quiet_NaN());
            statuses->SetDescription("Availability: 0 valid, 1 missing, 2 waterless, 3 not applicable, 255 outside");statuses->SetNoDataValue(std::numeric_limits<double>::quiet_NaN());
            MeshSpatialGrid grid;grid.rebuild(boxes);std::vector<double> row(static_cast<size_t>(width)),flags(static_cast<size_t>(width));
            for(int y=0;y<height;++y){if(cancel())return fail(error,"Export cancelled.");if(progress)progress(80*y/height);
                const double py=bounds.bottom()-(y+.5)*pixel;
                for(int x=0;x<width;++x){if(x%256==0&&cancel())return fail(error,"Export cancelled.");const double px=bounds.left()+(x+.5)*pixel;row[size_t(x)]=std::numeric_limits<double>::quiet_NaN();flags[size_t(x)]=255;
                    const int *begin=nullptr,*end=nullptr;grid.candidatesAtPoint(px,py,begin,end);int found=-1;
                    for(auto it=begin;it&&it!=end;++it)if(contains(s,size_t(*it),QPointF(px,py)))if(found<0||*it<found)found=*it;
                    if(found>=0){const auto state=s.scalar.status[size_t(found)];flags[size_t(x)]=int(state);if(state==Status::Valid)row[size_t(x)]=s.scalar.values[size_t(found)];}
                }
                if(values->RasterIO(GF_Write,0,y,width,1,row.data(),width,1,GDT_Float64,0,0,nullptr)!=CE_None
                    ||statuses->RasterIO(GF_Write,0,y,width,1,flags.data(),width,1,GDT_Float64,0,0,nullptr)!=CE_None)return fail(error,"Writing a scalar raster row failed.");
            }
        }
        if(!output.close())return fail(error,"The GIS driver could not flush and close the complete export.");
        if(QDir(stage.path()).entryList(QDir::Files|QDir::Hidden|QDir::NoDotAndDotDot).size()!=1)
            return fail(error,"The GIS driver created companion files; this export must be self-contained.");
        Dataset verify;verify.value=static_cast<GDALDataset*>(GDALOpenEx(path.toUtf8().constData(),raster?GDAL_OF_RASTER:GDAL_OF_VECTOR,nullptr,nullptr,nullptr));
        if(!verify.value||(raster?verify.value->GetRasterCount()!=2:verify.value->GetLayerCount()!=1))return fail(error,"The staged GIS result failed verification.");verify.close();
        if(cancel())return fail(error,"Export cancelled.");if(!noCompanions()||sameFile(s.sourcePath,options.path)||!unchanged())return fail(error,"The export destination changed before publication.");
        QFile input(path);QSaveFile destination(options.path);destination.setDirectWriteFallback(false);
        if(!input.open(QIODevice::ReadOnly)||!destination.open(QIODevice::WriteOnly))return fail(error,"Cannot prepare atomic GIS publication.");
        while(!input.atEnd()){if(cancel())return fail(error,"Export cancelled.");const auto bytes=input.read(1024*1024);if(bytes.isEmpty()&&input.error()!=QFileDevice::NoError)return fail(error,input.errorString());if(destination.write(bytes)!=bytes.size())return fail(error,destination.errorString());}
        if(cancel())return fail(error,"Export cancelled.");if(!noCompanions()||sameFile(s.sourcePath,options.path)||!unchanged())return fail(error,"The export destination changed before publication.");
        if(!destination.commit())return fail(error,destination.errorString());if(progress)progress(100);return true;
    }catch(const std::exception &e){return fail(error,QString::fromUtf8(e.what()));}
}
}
