#pragma once
#include "io/mesh2dvariablegisexport.h"
#include <QDialog>
#include <memory>
#include <atomic>
class QComboBox;class QDoubleSpinBox;class QLineEdit;class QLabel;class QPushButton;class QProgressBar;
namespace openswmmvis::ui {
// The worker receives only immutable copied result data. Closing the owner
// requests cancellation without retaining any model/source QObject pointer.
class ResultVariableExportDialog:public QDialog {
 Q_OBJECT
public:
 explicit ResultVariableExportDialog(io::Mesh2DVariableGisSnapshotPtr snapshot,QWidget *parent=nullptr);
 ~ResultVariableExportDialog() override;
public slots:
 void reject() override;
 void startExport();
signals:
 void exportFinished(bool success,const QString &message);
private:
 io::Mesh2DVariableGisSnapshotPtr snapshot_;
 QComboBox *format_;QDoubleSpinBox *pixel_;QLineEdit *path_;QLabel *status_;QPushButton *export_;QPushButton *browse_;QProgressBar *progress_;
 std::shared_ptr<std::atomic_bool> cancelled_;
 bool running_=false;
};
}
