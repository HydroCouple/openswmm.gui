#include "ui/dialogs/resultvariableexportdialog.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QFutureWatcher>
#include <QTimer>
#include <QPointer>
#include <QMessageBox>
#include <QFileInfo>
#include <QtConcurrent>
namespace openswmmvis::ui {
ResultVariableExportDialog::ResultVariableExportDialog(io::Mesh2DVariableGisSnapshotPtr snapshot,QWidget *parent)
 :QDialog(parent),snapshot_(std::move(snapshot)) {
 setWindowTitle(tr("Export result variable to GIS"));setAttribute(Qt::WA_DeleteOnClose);resize(580,330);
 auto *form=new QFormLayout(this);
 using Temporal=io::Mesh2DResultVariable::Temporal;
 const auto &descriptor=snapshot_->scalar.descriptor;
 const QString timing=descriptor.temporal==Temporal::Static?tr("Static result (independent of report time)"):
  descriptor.temporal==Temporal::Envelope?tr("Run envelope (values need not occur simultaneously)"):
  tr("Captured report frame %1 — %2%3").arg(snapshot_->requestedFrame+1).arg(snapshot_->time.isValid()?snapshot_->time.toString(Qt::ISODateWithMs):tr("timestamp unavailable"),descriptor.temporal==Temporal::Held?tr(" (held result)"):QString());
 auto *description=new QLabel(tr("Exports an immutable snapshot of %1 [%2].\n%3\nRaster values use containing cells without interpolation; a separate status band distinguishes gaps.")
  .arg(descriptor.label.isEmpty()?descriptor.key():descriptor.label,descriptor.units,timing),this);description->setObjectName("scalarGisSnapshot");description->setWordWrap(true);description->setTextFormat(Qt::PlainText);form->addRow(description);
 format_=new QComboBox(this);format_->setObjectName("scalarGisFormat");format_->setAccessibleName(tr("GIS export format"));format_->addItems({tr("GeoPackage cell polygons"),tr("GeoTIFF native values and status")});form->addRow(tr("&Format:"),format_);
 pixel_=new QDoubleSpinBox(this);pixel_->setObjectName("scalarGisPixelSize");pixel_->setAccessibleName(tr("Raster pixel size in projected coordinate units"));pixel_->setDecimals(6);pixel_->setRange(.000001,1e12);pixel_->setValue(1);pixel_->setEnabled(false);pixel_->setToolTip(tr("Pixel size in the declared projected coordinate reference's native linear units."));form->addRow(tr("&Pixel size:"),pixel_);
 auto *row=new QWidget(this);auto *layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);path_=new QLineEdit(row);path_->setObjectName("scalarGisPath");path_->setAccessibleName(tr("Export destination"));browse_=new QPushButton(tr("Browse…"),row);browse_->setAutoDefault(false);layout->addWidget(path_,1);layout->addWidget(browse_);auto *destinationLabel=new QLabel(tr("&Destination:"),this);destinationLabel->setBuddy(path_);form->addRow(destinationLabel,row);
 status_=new QLabel(this);status_->setObjectName("scalarGisStatus");status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);status_->setAccessibleName(tr("Export status"));form->addRow(status_);
 progress_=new QProgressBar(this);progress_->setRange(0,100);progress_->setAccessibleName(tr("Export progress"));form->addRow(progress_);
 auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close,this);export_=buttons->addButton(tr("Export"),QDialogButtonBox::ActionRole);export_->setObjectName("scalarGisStart");form->addRow(buttons);
 connect(buttons,&QDialogButtonBox::rejected,this,&ResultVariableExportDialog::reject);connect(export_,&QPushButton::clicked,this,&ResultVariableExportDialog::startExport);
 connect(format_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int i){pixel_->setEnabled(i==1&&!running_);});
 connect(browse_,&QPushButton::clicked,this,[this]{QPointer<ResultVariableExportDialog> self=this;const auto path=QFileDialog::getSaveFileName(this,tr("Export result variable"),path_->text(),format_->currentIndex()==0?tr("GeoPackage (*.gpkg)"):tr("GeoTIFF (*.tif)"));if(self&&!path.isEmpty())path_->setText(path);});
}
ResultVariableExportDialog::~ResultVariableExportDialog(){if(cancelled_)cancelled_->store(true);}
void ResultVariableExportDialog::reject(){if(cancelled_)cancelled_->store(true);QDialog::reject();}
void ResultVariableExportDialog::startExport(){
 if(running_)return;
 io::Mesh2DVariableGisOptions options;options.path=path_->text().trimmed();options.pixelSize=pixel_->value();options.format=format_->currentIndex()==0?io::Mesh2DVariableGisFormat::GeoPackage:io::Mesh2DVariableGisFormat::GeoTiff;
 QString error;if(options.path.isEmpty()||!ProjectSaveOutputs::captureDestination(options.path,&options.destination,&error)){status_->setText(error.isEmpty()?tr("Choose an export destination."):error);return;}
 if(QFileInfo::exists(options.path)) {
  QPointer<ResultVariableExportDialog> self=this;
  auto *prompt=new QMessageBox(QMessageBox::Question,tr("Replace export?"),tr("Replace the existing file %1?").arg(options.path),QMessageBox::Yes|QMessageBox::No,this);
  QPointer<QMessageBox> safePrompt=prompt;const int answer=prompt->exec();if(safePrompt)prompt->deleteLater();if(!self||answer!=QMessageBox::Yes)return;
  ProjectSaveOutputs::DestinationState now;
  if(!ProjectSaveOutputs::captureDestination(options.path,&now,&error)||now.resolvedPath!=options.destination.resolvedPath||now.fingerprint!=options.destination.fingerprint){status_->setText(tr("The destination changed during confirmation. Choose it again."));return;}
 }
 cancelled_=std::make_shared<std::atomic_bool>(false);auto progress=std::make_shared<std::atomic_int>(0);const auto cancel=cancelled_;const auto snapshot=snapshot_;
 running_=true;browse_->setEnabled(false);format_->setEnabled(false);pixel_->setEnabled(false);path_->setEnabled(false);export_->setEnabled(false);status_->setText(tr("Exporting the captured frame. Closing this dialog cancels publication."));
 auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,[this,progress]{progress_->setValue(progress->load());});timer->start(100);
 using Result=QPair<bool,QString>;auto *watcher=new QFutureWatcher<Result>(this);
 connect(watcher,&QFutureWatcher<Result>::finished,this,[this,watcher,timer]{timer->stop();timer->deleteLater();const auto result=watcher->result();watcher->deleteLater();running_=false;browse_->setEnabled(true);format_->setEnabled(true);pixel_->setEnabled(format_->currentIndex()==1);path_->setEnabled(true);export_->setEnabled(true);status_->setText(result.second);if(result.first)progress_->setValue(100);emit exportFinished(result.first,result.second);});
 watcher->setFuture(QtConcurrent::run([snapshot,options,cancel,progress]{QString error;const bool ok=io::exportMesh2DVariableGis(*snapshot,options,[cancel]{return cancel->load();},&error,[progress](int value){progress->store(value);});return Result(ok,ok?QObject::tr("Exported %1").arg(options.path):error);}));
}
}
