#ifndef OPENSWMMVIS_CHANNELBURNDIALOG_H
#define OPENSWMMVIS_CHANNELBURNDIALOG_H

#include "mesh/channelburnmesh.h"
#include "mesh/channelburnexport.h"
#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>
#include <atomic>
#include <memory>

class SWMM2DMeshLayer;
class SWMMModelLayer;
class QCloseEvent;
class SWMMVisProjectWindow;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

class ChannelBurnDialog final : public QDialog
{
    Q_OBJECT
    friend class TestChannelBurnDialog;
    friend class TestMeshTerrainPipeline;
public:
    explicit ChannelBurnDialog(SWMMVisProjectWindow *project, QWidget *parent = nullptr);
    ~ChannelBurnDialog() override;

protected:
    void reject() override;
    void closeEvent(QCloseEvent *) override;

private slots:
    void startBurn();
    void finishBurn();
    void cancelBurn();

private:
    bool collectInputs(mesh::ChannelMeshBurnInputs *inputs, QString *error);
    void setBusy(bool busy);
    void beginGuard();
    void clearGuard();
    bool ownerIsCurrent() const;

    QPointer<SWMMModelLayer> m_model;
    void *m_engine = nullptr;
    quint64 m_modelRevision = 0, m_geomRevision = 0, m_attrRevision = 0, m_bcRevision = 0;
    QString m_modelPath;
    QList<QMetaObject::Connection> m_connections;
    bool m_invalidated = false;
    bool m_busy = false;

    QPointer<SWMMVisProjectWindow> m_project;
    QPointer<SWMM2DMeshLayer> m_mesh;
    struct Result {
        mesh::ChannelMeshBurnResult burn;
        mesh::ChannelBurnExportResult output;
    };
    QFutureWatcher<Result> m_watcher;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QComboBox *m_selectionMode = nullptr;
    QLineEdit *m_query = nullptr;
    QLineEdit *m_ids = nullptr;
    QDoubleSpinBox *m_cellSize = nullptr;
    QDoubleSpinBox *m_aspect = nullptr;
    QDoubleSpinBox *m_halfWidth = nullptr;
    QCheckBox *m_quads = nullptr;
    QCheckBox *m_removeFromModel = nullptr;
    QDoubleSpinBox *m_maxIncision = nullptr;
    QDoubleSpinBox *m_tolerance = nullptr;
    QSpinBox *m_maxCells = nullptr;
    QCheckBox *m_exportRaster = nullptr;
    QLineEdit *m_demPath = nullptr;
    QLineEdit *m_outputDirectory = nullptr;
    QDoubleSpinBox *m_demZToSI = nullptr;
    QWidget *m_exportOptions = nullptr;
    QTableWidget *m_quality = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_apply = nullptr;
    QPushButton *m_cancel = nullptr;
};

#endif
