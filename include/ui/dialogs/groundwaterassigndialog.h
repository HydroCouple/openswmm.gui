#ifndef OPENSWMMVIS_GROUNDWATERASSIGNDIALOG_H
#define OPENSWMMVIS_GROUNDWATERASSIGNDIALOG_H
#include "assignment/groundwaterassignment.h"
#include "assignment/groundwatertransportassignment.h"
#include "assignment/meshassignmentsampling.h"
#include <QDialog>
#include <QPointer>
#include <QFutureWatcher>
#include <QJsonObject>
class SWMMModelLayer;class SWMM2DMeshLayer;class MapCanvas;class SelectionManager;class UnitSystem;
class QComboBox;class QLineEdit;class QDoubleSpinBox;class QSpinBox;class QLabel;class QPushButton;class QTableWidget;class QCheckBox;
namespace openswmmvis::ui {
class GroundwaterAssignmentEvents:public QObject {
 Q_OBJECT
public:
 explicit GroundwaterAssignmentEvents(QObject*parent=nullptr):QObject(parent){}
signals:
 void changed(const QJsonObject&recipe,bool installed);
};
// One nonmodal, staged assignment workflow. Existing map rectangle/lasso tools
// and the selection bus remain usable while this editor is open.
class GroundwaterAssignDialog:public QDialog {
 Q_OBJECT
public:
 GroundwaterAssignDialog(SWMMModelLayer*,SWMM2DMeshLayer*,MapCanvas*,SelectionManager*,const UnitSystem*,QWidget *parent=nullptr);
 ~GroundwaterAssignDialog() override;
 GroundwaterAssignmentEvents *assignmentEvents() const {return m_events;}
public slots:
 void reject() override;
signals:
 void applied();
 void recipeAccepted(const QJsonObject &recipe);
private slots:
 void invalidateContext();
 void invalidatePreview();
 void preview();
 void apply();
private:
 struct WorkResult {
    assignment::AquiferPreview preview;
    assignment::GroundwaterTransportPreview transport;
    bool isTransport=false;
    QVector<double> values;
    QMap<QString,QByteArray> fingerprints;
    QString error,summary;
    bool cancelled=false;
 };
 void updateTarget();
 void addSpeciesTerm();
 void refreshTermAccessibility();
 void populateSources();
 void sourceChanged();
 QVector<int> scope(QString *error) const;
 bool currentContext(QString *error) const;
 void commitVerified();
 void finishPreview();
 QPointer<GroundwaterAssignmentEvents> m_events;
 QPointer<SWMMModelLayer> m_model;
 QPointer<SWMM2DMeshLayer> m_mesh;
 QPointer<MapCanvas> m_canvas;
 QPointer<SelectionManager> m_selection;
 SWMM_Engine m_engine=nullptr;
 quint64 m_geometry=0,m_attributes=0,m_serial=0;
 QString m_meshPath,m_meshCrs,m_length,m_rate;
 QVector<int> m_scope;
 QJsonObject m_recipe;
 WorkResult m_reviewed;
 bool m_havePreview=false,m_committing=false;
 QFutureWatcher<WorkResult>*m_watcher=nullptr;
 QComboBox *m_target=nullptr,*m_route=nullptr,*m_scopeChoice=nullptr,*m_source=nullptr,*m_sampling=nullptr;
 QComboBox *m_species=nullptr,*m_zone=nullptr,*m_distribution=nullptr,*m_flowSeries=nullptr;
 QLineEdit *m_sourceName=nullptr;
 QWidget *m_transportControls=nullptr,*m_sourceControls=nullptr;
 QVector<assignment::TransportSpecies> m_speciesCatalog;
 QLineEdit *m_scopeText=nullptr,*m_path=nullptr,*m_layerName=nullptr,*m_field=nullptr,*m_filter=nullptr;
 QDoubleSpinBox *m_value=nullptr,*m_scale=nullptr,*m_offset=nullptr;
 QSpinBox *m_band=nullptr;
 QCheckBox *m_skip=nullptr,*m_selectedFeatures=nullptr;
 QLabel *m_status=nullptr,*m_units=nullptr;
 QPushButton *m_preview=nullptr,*m_apply=nullptr;
 QTableWidget *m_table=nullptr,*m_terms=nullptr;
};
}
#endif
