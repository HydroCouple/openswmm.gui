// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
#include <QPointer>
#include <QStringList>
#include <array>
#include <openswmm/engine/openswmm_engine.h>
class SWMMModelLayer;class SWMM2DMeshLayer;class MapCanvas;class SelectionManager;
class QComboBox;class QDoubleSpinBox;class QLabel;class QTableView;class QTableWidget;class QPushButton;
namespace openswmmvis::ui {
class MeshSnowModel;
class MeshSnowDialog final : public QDialog {
public:
    MeshSnowDialog(SWMMModelLayer*,SWMM2DMeshLayer*,MapCanvas*,SelectionManager*,QWidget* parent=nullptr);
private:
    void preview();void apply();void addAssignment();void editPacks();void invalidate();
    void fillFromLayer();
    QString contextError()const;
    QPointer<SWMMModelLayer> model_;QPointer<SWMM2DMeshLayer> mesh_;QPointer<MapCanvas> canvas_;
    QPointer<SelectionManager> selection_;SWMM_Engine engine_=nullptr;quint64 geometry_=0;
    MeshSnowModel* rows_=nullptr;QComboBox *mode_=nullptr,*scope_=nullptr,*tag_=nullptr,*pack_=nullptr;
    QDoubleSpinBox* step_=nullptr;QLabel* status_=nullptr;QTableView* table_=nullptr;QTableWidget* resolved_=nullptr;
    double loaded_step_=0,displayed_step_=0;
    QPushButton* apply_=nullptr;std::array<QString,3> before_;
    QStringList sampledSources_;
};
}
