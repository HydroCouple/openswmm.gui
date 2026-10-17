#pragma once
#include <QDialog>
class SWMMModelLayer;class MapCanvas;
namespace OpenSWMMVis {
class HeatRasterAssignDialog final:public QDialog {
public:
    HeatRasterAssignDialog(SWMMModelLayer*,MapCanvas*,QWidget* parent=nullptr);
};
}
