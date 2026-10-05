// SPDX-License-Identifier: GPL-3.0-or-later
#include "assignment/surfaceownership.h"
#include "layers/swmm2dmeshlayer.h"
#include <openswmm/engine/openswmm_2d.h>
#include <cmath>
#include <limits>
namespace openswmmvis::assignment {
bool readSurfaceOwners(SWMM_Engine e,QVector<int>* rows,QString* error){
 int n=0;if(swmm_surface_owner_get(e,nullptr,0,&n)!=SWMM_OK){*error=QObject::tr("Surface ownership is available while editing a model.");return false;}
 rows->resize(n);if(n&&swmm_surface_owner_get(e,rows->data(),n,&n)!=SWMM_OK){*error=QObject::tr("Cannot read surface ownership records.");return false;}return true;
}
SurfaceOwnershipReview previewSurfaceOwners(SWMM_Engine e,const QVector<int>& rows,const std::function<bool(int,int)>& progress){
 SurfaceOwnershipReview p;p.after=rows;if(!readSurfaceOwners(e,&p.before,&p.error))return p;
 int on=0,sn=0,cn=0,valid=0;char token[17]={},diag[8192]={};
 auto cb=[](int d,int n,void* u){return (*static_cast<const std::function<bool(int,int)>*>(u))(d,n)?1:0;};
 auto call=[&](bool fill){return swmm_surface_owner_preview(e,rows.constData(),rows.size(),fill?p.objects.data():nullptr,fill?p.objects.size():0,&on,
  fill?p.shares.data():nullptr,fill?p.shares.size():0,&sn,fill?p.meshWeatherArea.data():nullptr,fill?p.meshWeatherArea.size():0,&cn,
  &valid,token,sizeof token,diag,sizeof diag,progress?+cb:nullptr,progress?const_cast<void*>(static_cast<const void*>(&progress)):nullptr);};
 if(call(false)!=SWMM_OK){p.error=QString::fromUtf8(diag);if(p.error.isEmpty())p.error=QObject::tr("Cannot preview surface ownership.");return p;}
 p.objects.resize(on);p.shares.resize(sn);p.meshWeatherArea.resize(cn);
 if(call(true)!=SWMM_OK){p.error=QString::fromUtf8(diag);if(p.error.isEmpty())p.error=QObject::tr("The model changed during preview.");return p;}
 p.token=QString::fromUtf8(token);p.error=QString::fromUtf8(diag);p.valid=valid;return p;
}
bool replaceSurfaceOwners(SWMM_Engine e,const QVector<int>& rows,const QString& token,QString* error){
 char diag[8192]={};const int result=swmm_surface_owner_replace(e,rows.constData(),rows.size(),token.toUtf8().constData(),diag,sizeof diag);
 if(result!=SWMM_OK){*error=QString::fromUtf8(diag);if(error->isEmpty())*error=QObject::tr("Cannot apply surface ownership in the current model state.");return false;}return true;
}
bool surfaceOwnershipMeshMatches(SWMM_Engine e,const SWMM2DMeshLayer* layer,QString* error){
 if(!e||!layer){*error=QObject::tr("Choose the model's active mesh first.");return false;}
 int nv=0,nc=0;if(swmm_2d_vertex_count(e,&nv)!=SWMM_OK||swmm_2d_triangle_count(e,&nc)!=SWMM_OK){*error=QObject::tr("No engine mesh is available.");return false;}
 const auto& mesh=layer->mesh();if(nv!=mesh.vertices.size()||nc!=mesh.triangles.size()){*error=QObject::tr("The active and engine meshes differ. Save/reload the mesh before reviewing ownership.");return false;}
 QVector<double> x(nv),y(nv),z(nv);if(swmm_2d_vertex_get_xyz_bulk(e,x.data(),y.data(),z.data())!=SWMM_OK){*error=QObject::tr("Cannot read mesh geometry while editing this model.");return false;}
 auto equal=[](double a,double b){return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=std::max(1e-10,8*std::numeric_limits<double>::epsilon()*std::max(std::abs(a),std::abs(b)));};
 for(int v=0;v<nv;++v)if(!equal(x[v],mesh.vertices[v].xy.x())||!equal(y[v],mesh.vertices[v].xy.y())||!equal(z[v],mesh.vertices[v].z)){*error=QObject::tr("Mesh coordinates or elevations differ from the engine. Save/reload before reviewing ownership.");return false;}
 for(int c=0;c<nc;++c){int v[4],n=0;if(swmm_2d_cell_get_vertices(e,c,v,&n)!=SWMM_OK||n!=mesh.triangles[c].vertexCount()){*error=QObject::tr("Mesh cell connectivity differs from the engine.");return false;}for(int k=0;k<n;++k)if(v[k]!=mesh.triangles[c].vertex(k)){*error=QObject::tr("Mesh cell IDs differ from the engine.");return false;}}
 return nc>0;
}
}
