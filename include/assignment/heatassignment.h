#pragma once
#include "ui/heatpropertyedit.h"
#include <functional>
class GISRasterLayer;
namespace OpenSWMMVis {
struct HeatRasterPreview {
    QVector<HeatPropertyEdit> edits;
    int skipped=0,skippedNoData=0,skippedGeometry=0,skippedRange=0;
    QString error;
};
HeatRasterPreview sampleHeatRaster(SWMMModelLayer*,GISRasterLayer*,int band,int attr,
    int scope,const QString& tag={},int linkSamples=20,
    std::function<bool(int,int)> progress={},std::function<bool()> cancelled={});
}
