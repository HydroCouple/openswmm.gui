// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMMVIS_SURFACEOWNERSHIPDIALOG_H
#define OPENSWMMVIS_SURFACEOWNERSHIPDIALOG_H
#include "assignment/surfaceownership.h"
#include <QDialog>
#include <QPointer>
class SWMMModelLayer;class SWMM2DMeshLayer;class MapCanvas;class SelectionManager;
class QComboBox;class QCheckBox;class QLineEdit;class QTableWidget;class QPushButton;class QLabel;
namespace openswmmvis::ui {
class SurfaceOwnershipDialog:public QDialog {
 Q_OBJECT
public:
 SurfaceOwnershipDialog(SWMMModelLayer*,SWMM2DMeshLayer*,MapCanvas*,SelectionManager*,QWidget* parent=nullptr);
public slots:
 void reject() override;
signals:
 void applied();
 void groundwaterEditorRequested();
 void sourceEditorRequested(const QString&);
private slots:
 void preview();void apply();void invalidate();void selectReceivers();void selectCellSources();
private:
 bool contextValid(QString*) const;
 QVector<int> scope(const assignment::SurfaceOwnershipReview&) const;
 QPointer<SWMMModelLayer> m_model;QPointer<SWMM2DMeshLayer> m_mesh;QPointer<MapCanvas> m_canvas;QPointer<SelectionManager> m_selection;
 SWMM_Engine m_engine=nullptr;assignment::SurfaceOwnershipReview m_review;
 quint64 m_geometry=0;QString m_crs,m_areaUnit;double m_areaFactor=1;bool m_busy=false,m_committing=false,m_ready=false;
 QComboBox *m_scope=nullptr,*m_representation=nullptr;QLineEdit* m_tag=nullptr;QCheckBox* m_uniform=nullptr;
 QTableWidget *m_objects=nullptr,*m_receivers=nullptr;QPushButton *m_preview=nullptr,*m_apply=nullptr;QLabel *m_status=nullptr,*m_parameters=nullptr;
};
}
#endif
