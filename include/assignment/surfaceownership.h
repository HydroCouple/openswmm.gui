// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMMVIS_SURFACEOWNERSHIP_H
#define OPENSWMMVIS_SURFACEOWNERSHIP_H
#include <openswmm/engine/openswmm_surface_ownership.h>
#include <QVector>
#include <QString>
#include <functional>
class SWMM2DMeshLayer;
namespace openswmmvis::assignment {
struct SurfaceOwnershipReview {
 QVector<int> before,after;
 QVector<SWMM_SurfaceOwnerObject> objects;
 QVector<SWMM_SurfaceOwnerShare> shares;
 QVector<double> meshWeatherArea;
 QString token,error;
 bool valid=false;
};
bool readSurfaceOwners(SWMM_Engine,QVector<int>*,QString*);
SurfaceOwnershipReview previewSurfaceOwners(SWMM_Engine,const QVector<int>&,const std::function<bool(int,int)>& progress={});
bool replaceSurfaceOwners(SWMM_Engine,const QVector<int>&,const QString& token,QString* error);
bool surfaceOwnershipMeshMatches(SWMM_Engine,const SWMM2DMeshLayer*,QString* error);
}
#endif
