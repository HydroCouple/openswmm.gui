#include "ui/dialogs/channelburndialog.h"

#include "core/unitsystem.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/channelmeshadoptioncommand.h"
#include "map/spatialreferencesystem.h"
#include "mesh/inpmeshwriter.h"
#include "mesh/meshcellgeom.h"
#include "mesh/channelburnselector.h"
#include "swmmvisprojectwindow.h"
#include "map/mapundostack.h"

#include <openswmm/engine/openswmm_engine.h>
#include <QPushButton>
#include <QProgressBar>
#include <QPromise>
#include <QRegularExpression>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFuture>
#include <QFutureWatcher>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <QScopeGuard>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QDir>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include "project/generatedmeshartifacts.h"
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
QSet<int> inflowNodes(SWMM_Engine eng)
{
    QSet<int> fed; if(!eng)return fed;
    char id[256]={0};
    for(int i=0,n=swmm_ext_inflow_count(eng);i<n;++i){int node=-1;double mf=0,sf=0,base=0;char ts[256]={0},type[64]={0},pat[256]={0};if(swmm_ext_inflow_get(eng,i,&node,id,sizeof(id),ts,sizeof(ts),type,sizeof(type),&mf,&sf,&base,pat,sizeof(pat))==SWMM_OK&&node>=0)fed.insert(node);}
    for(int i=0,n=swmm_dwf_count(eng);i<n;++i){int node=-1;double avg=0;char p1[128]={0},p2[128]={0},p3[128]={0},p4[128]={0};if(swmm_dwf_get(eng,i,&node,id,sizeof(id),&avg,p1,sizeof(p1),p2,sizeof(p2),p3,sizeof(p3),p4,sizeof(p4))==SWMM_OK&&node>=0)fed.insert(node);}
    for(int i=0,n=swmm_rdii_count(eng);i<n;++i){int node=-1;double area=0;if(swmm_rdii_get(eng,i,&node,id,sizeof(id),&area)==SWMM_OK&&node>=0)fed.insert(node);}
    for(int i=0,n=swmm_subcatch_count(eng);i<n;++i){int node=-1;if(swmm_subcatch_get_outlet(eng,i,&node)==SWMM_OK&&node>=0)fed.insert(node);}
    return fed;
}

}

ChannelBurnDialog::ChannelBurnDialog(SWMMVisProjectWindow *project,QWidget *parent):QDialog(parent),m_project(project)
{
    setWindowTitle(tr("Burn Channels into Mesh"));resize(600,700);
    if (project && project->canvas()) {
        int count = 0, activeCount = 0;
        SWMM2DMeshLayer *sole = nullptr;
        for (auto *layer : project->canvas()->layers()) {
            if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer)) {
                sole = mesh;
                ++count;
                if (mesh->isActiveMesh()) { m_mesh = mesh; ++activeCount; }
            }
        }
        if (activeCount > 1) m_mesh.clear();
        else if (activeCount == 0 && count == 1) m_mesh = sole;
    }
    auto *outer=new QVBoxLayout(this);auto *form=new QFormLayout;
    m_selectionMode=new QComboBox(this);m_selectionMode->addItem(tr("All open channels"),int(mesh::BurnSelector::Mode::AllOpen));m_selectionMode->addItem(tr("Filter by query"),int(mesh::BurnSelector::Mode::ByQuery));m_selectionMode->addItem(tr("Channel IDs"),int(mesh::BurnSelector::Mode::ExplicitList));form->addRow(tr("Channels:"),m_selectionMode);
    m_query=new QLineEdit(this);m_query->setPlaceholderText(tr("Example: link_tag LIKE '%creek%'"));form->addRow(tr("Query:"),m_query);
    m_ids=new QLineEdit(this);m_ids->setPlaceholderText(tr("Comma or space separated conduit IDs"));form->addRow(tr("Channel IDs:"),m_ids);
    auto spin=[&](double value,double lo,double hi,int decimals){auto *s=new QDoubleSpinBox(this);s->setRange(lo,hi);s->setDecimals(decimals);s->setValue(value);return s;};
    m_cellSize=spin(0,0,1e9,6);m_cellSize->setSpecialValueText(tr("Match current mesh"));form->addRow(tr("Channel cell size:"),m_cellSize);
    m_aspect=spin(4,1,100,2);form->addRow(tr("Minimum spacing divisor:"),m_aspect);
    m_halfWidth=spin(2,0,1e9,4);form->addRow(tr("Minimum burn half-width:"),m_halfWidth);
    m_quads=new QCheckBox(tr("Use quadrilaterals where they fit"),this);m_quads->setChecked(true);form->addRow(m_quads);
    m_removeFromModel=new QCheckBox(tr("Replace burned channel intervals in the 1D model"),this);m_removeFromModel->setChecked(true);form->addRow(m_removeFromModel);
    const QString lengthUnit = project && project->unitSystem() && project->unitSystem()->isSI() ? tr(" m") : tr(" ft");
    m_cellSize->setSuffix(lengthUnit);
    m_halfWidth->setSuffix(lengthUnit);
    m_aspect->setToolTip(tr("Minimum channel spacing = channel cell size / this value. Default 4. This limits narrow walls; it is not a guarantee for every final cell's aspect ratio."));
    m_maxIncision = spin(0, 0, 1e9, 4);
    m_maxIncision->setSuffix(lengthUnit);
    m_maxIncision->setSpecialValueText(tr("Unlimited"));
    form->addRow(tr("Maximum incision:"), m_maxIncision);
    m_tolerance = spin(.05, .000001, 1e6, 6);
    m_tolerance->setSuffix(tr(" m"));
    form->addRow(tr("Channel elevation tolerance:"), m_tolerance);
    m_maxCells = new QSpinBox(this);
    m_maxCells->setRange(1, 100000000);
    m_maxCells->setValue(2000000);
    form->addRow(tr("Maximum total mesh cells:"), m_maxCells);
    m_exportRaster = new QCheckBox(tr("Also prepare a burned DEM copy and report"), this);
    form->addRow(m_exportRaster);
    m_exportOptions = new QWidget(this);
    auto *exportForm = new QFormLayout(m_exportOptions);
    exportForm->setContentsMargins(0, 0, 0, 0);
    m_demPath = new QLineEdit(m_exportOptions);
    auto *demRow = new QHBoxLayout;
    demRow->addWidget(m_demPath);
    auto *chooseDem = new QPushButton(tr("Browse…"), m_exportOptions);
    demRow->addWidget(chooseDem);
    connect(chooseDem, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(this, tr("Choose a DEM"), m_demPath->text(), tr("Raster files (*.tif *.tiff *.vrt *.asc);;All files (*)"));
        if (!path.isEmpty()) m_demPath->setText(path);
    });
    exportForm->addRow(tr("Source DEM:"), demRow);
    m_demZToSI = spin(1, .000000001, 1e9, 9);
    m_demZToSI->setToolTip(tr("Metres per DEM elevation unit: 1 for metres, 0.3048 for feet."));
    exportForm->addRow(tr("Metres per DEM elevation unit:"), m_demZToSI);
    m_outputDirectory = new QLineEdit(m_exportOptions);
    m_outputDirectory->setPlaceholderText(tr("Default: terrain folder beside the project"));
    exportForm->addRow(tr("Output folder:"), m_outputDirectory);
    form->addRow(m_exportOptions);
    m_exportOptions->setEnabled(false);
    m_exportOptions->setVisible(false);
    connect(m_exportRaster, &QCheckBox::toggled, m_exportOptions, &QWidget::setEnabled);
    connect(m_exportRaster, &QCheckBox::toggled, m_exportOptions, &QWidget::setVisible);
    auto *options = new QWidget(this);
    options->setLayout(form);
    auto *optionsScroll = new QScrollArea(this);
    optionsScroll->setWidgetResizable(true);
    optionsScroll->setFrameShape(QFrame::NoFrame);
    optionsScroll->setWidget(options);
    outer->addWidget(optionsScroll, 1);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *statusScroll = new QScrollArea(this);
    statusScroll->setWidgetResizable(true);
    statusScroll->setWidget(m_status);
    statusScroll->setMinimumHeight(85);
    statusScroll->setMaximumHeight(170);
    outer->addWidget(statusScroll);
    m_progress = new QProgressBar(this);
    m_progress->setObjectName(QStringLiteral("channelBurnProgress"));
    m_progress->setAccessibleName(tr("Channel burn progress"));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->hide();
    outer->addWidget(m_progress);
    connect(&m_watcher, &QFutureWatcher<Result>::progressValueChanged, this, [this](int value) {
        if (m_busy && m_cancelled && !m_cancelled->load())
            m_progress->setValue(std::clamp(value, m_progress->value(), 95));
    });
    connect(&m_watcher, &QFutureWatcher<Result>::progressTextChanged, this, [this](const QString &stage) {
        if (m_busy && m_cancelled && !m_cancelled->load()) {
            m_status->setText(stage);
            m_progress->setAccessibleDescription(stage);
        }
    });
    m_quality = new QTableWidget(6, 2, this);
    m_quality->setHorizontalHeaderLabels({tr("Before"), tr("After")});
    m_quality->setVerticalHeaderLabels({tr("Affected cells"), tr("Quads"), tr("Minimum angle"),
        tr("Minimum edge"), tr("Cells below 25°"), tr("Cells below 10°")});
    m_quality->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_quality->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_quality->setSelectionMode(QAbstractItemView::NoSelection);
    m_quality->setMaximumHeight(210);
    m_quality->hide();
    outer->addWidget(m_quality);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Cancel,this);m_apply=buttons->addButton(tr("Burn Channels"),QDialogButtonBox::AcceptRole);m_cancel=buttons->button(QDialogButtonBox::Cancel);outer->addWidget(buttons);
    connect(m_apply,&QPushButton::clicked,this,&ChannelBurnDialog::startBurn);connect(m_cancel,&QPushButton::clicked,this,&ChannelBurnDialog::cancelBurn);connect(&m_watcher,&QFutureWatcher<Result>::finished,this,&ChannelBurnDialog::finishBurn);
    if(project){auto settings=project->channelBurnSettings();m_query->setText(settings.selector.query);m_ids->setText(settings.selector.conduitIds.join(QStringLiteral(", ")));m_selectionMode->setCurrentIndex(m_selectionMode->findData(int(settings.selector.mode)));m_cellSize->setValue(settings.options.channelCellSize);m_aspect->setValue(settings.options.channelAspectMax);m_halfWidth->setValue(settings.options.forceHalfWidth);m_quads->setChecked(settings.options.quadCorridor);m_removeFromModel->setChecked(settings.options.removeBurnedFrom1D);
        m_maxIncision->setValue(settings.options.maxIncision);
        m_tolerance->setValue(settings.options.geometryTolerance);
        m_maxCells->setValue(settings.maxMeshCells);
        m_exportRaster->setChecked(settings.exportRaster);
        m_demPath->setText(settings.exportDemPath);
        m_outputDirectory->setText(settings.exportDirectory);
        m_demZToSI->setValue(settings.exportRasterZToSI);
    }
    if(!m_mesh){m_status->setText(tr("No active 2D mesh is available. Generate or import a mesh first."));m_apply->setEnabled(false);}else m_status->setText(tr("The active mesh will be updated. The channel surface is built from model sections and the existing mesh; no DEM is needed."));
}
ChannelBurnDialog::~ChannelBurnDialog()
{
    if (m_cancelled) m_cancelled->store(true);
}

void ChannelBurnDialog::setBusy(bool busy)
{
    m_busy = busy;
    if (busy) {
        m_progress->setValue(0);
        m_progress->setFormat(QStringLiteral("%p%"));
        m_progress->setAccessibleDescription(tr("Preparing channel burn…"));
        m_progress->show();
        m_cancel->setText(tr("Cancel"));
    }
    for (QWidget *control : QList<QWidget *>{static_cast<QWidget *>(m_selectionMode), m_query, m_ids,
                             m_cellSize, m_aspect, m_halfWidth, m_quads, m_removeFromModel,
                             m_maxIncision, m_tolerance, m_maxCells, m_exportRaster})
        control->setEnabled(!busy);
    m_exportOptions->setEnabled(!busy && m_exportRaster->isChecked());
    m_apply->setEnabled(!busy && m_mesh);
    m_apply->setText(busy ? tr("Burning…") : tr("Burn Channels"));
    m_cancel->setEnabled(true);
}

void ChannelBurnDialog::cancelBurn()
{
    if (m_busy && m_cancelled) {
        m_cancelled->store(true);
        m_cancel->setEnabled(false);
        m_status->setText(tr("Stopping channel burn…"));
        m_progress->setFormat(tr("Stopping…"));
        m_progress->setAccessibleDescription(m_status->text());
        return;
    }
    reject();
}

void ChannelBurnDialog::reject()
{
    m_invalidated = true;
    if (m_cancelled) m_cancelled->store(true);
    QDialog::reject();
}

void ChannelBurnDialog::closeEvent(QCloseEvent *event)
{
    m_invalidated = true;
    if (m_cancelled) m_cancelled->store(true);
    QDialog::closeEvent(event);
}

void ChannelBurnDialog::clearGuard()
{
    for (const auto &connection : m_connections) disconnect(connection);
    m_connections.clear();
}

void ChannelBurnDialog::beginGuard()
{
    clearGuard();
    m_invalidated = false;
    m_model = m_project->modelLayer();
    m_engine = m_model->engine();
    m_modelRevision = m_model->editRevision();
    m_modelPath = m_model->modelFilePath();
    m_geomRevision = m_mesh->geomRevision();
    m_attrRevision = m_mesh->attrRevision();
    m_bcRevision = m_mesh->bcRevision();
    const auto invalidate = [this] {
        m_invalidated = true;
        if (m_cancelled) m_cancelled->store(true);
    };
    m_connections << connect(m_project, &SWMMVisProjectWindow::aboutToClose, this, invalidate)
        << connect(m_model, &SWMMModelLayer::modelEdited, this, invalidate)
        << connect(m_model, &SWMMModelLayer::attributeChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::geometryChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::optionsChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::dataObjectsChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::hydrographChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::controlRulesChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::transectChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::modelFilePathChanged, this, invalidate)
        << connect(m_model, &SWMMModelLayer::engineAboutToClose, this, invalidate)
        << connect(m_model, &OpenSWMMVisLayer::srsChanged, this, invalidate)
        << connect(m_project->canvas(), &MapCanvas::layerAdded, this, invalidate)
        << connect(m_project->canvas(), &MapCanvas::layerRemoved, this, invalidate);
    for (auto *layer : m_project->canvas()->layers()) {
        if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer))
            m_connections << connect(mesh, &SWMM2DMeshLayer::activeMeshChanged, this, invalidate);
    }
    m_connections << connect(m_mesh, &SWMM2DMeshLayer::attributeChanged, this, invalidate)
        << connect(m_mesh, &SWMM2DMeshLayer::meshEditsChanged, this, invalidate)
        << connect(m_mesh, &OpenSWMMVisLayer::srsChanged, this, invalidate);
}

bool ChannelBurnDialog::ownerIsCurrent() const
{
    return !m_invalidated && m_project && !m_project->isClosing() && m_mesh && m_model
        && m_project->modelLayer() == m_model && m_model->engine() == m_engine
        && m_model->editRevision() == m_modelRevision && m_model->modelFilePath() == m_modelPath
        && m_project->canvas() && m_project->canvas()->layers().contains(m_mesh)
        && m_mesh->geomRevision() == m_geomRevision && m_mesh->attrRevision() == m_attrRevision
        && m_mesh->bcRevision() == m_bcRevision;
}

bool ChannelBurnDialog::collectInputs(mesh::ChannelMeshBurnInputs *out, QString *error)
{
    const auto fail = [&](const QString &message) { if (error) *error = message; return false; };
    if (!m_project || m_project->isClosing() || !m_mesh || !out
        || !m_project->canvas() || !m_project->canvas()->layers().contains(m_mesh))
        return fail(tr("The active mesh or project is unavailable."));
    SWMM2DMeshLayer *selected = nullptr;
    int active = 0, count = 0;
    for (auto *layer : m_project->canvas()->layers()) {
        if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer)) {
            ++count;
            if (mesh->isActiveMesh()) { selected = mesh; ++active; }
        }
    }
    if (active > 1 || (active == 1 && selected != m_mesh) || (active == 0 && count != 1))
        return fail(tr("The active mesh changed. Reopen Burn Channels for the current mesh."));
    auto *model = m_project->modelLayer();
    auto eng = model ? model->engine() : nullptr;
    if (!eng) return fail(tr("The SWMM model is unavailable."));
    if (m_mesh->edgeBCs().size() != mesh::edgeSlotCount(m_mesh->mesh().triangles.size()))
        return fail(tr("Wait for the mesh to finish loading before burning channels."));

    // Inspect both the model and the actual imported snapshot. The imported
    // flag alone does not imply unsupported mesh-indexed data.
    QStringList guardPaths{model->modelFilePath(), m_mesh->sourcePath()};
    if (const auto &artifacts = m_mesh->generatedArtifacts())
        for (const auto &entry : artifacts->entries())
            if (entry.role == ProjectSaveOutputs::Mesh) guardPaths.append(entry.stagedPath);
    guardPaths.removeDuplicates();
    for (const auto &path : guardPaths) {
        if (path.isEmpty() || !QFileInfo::exists(path)) continue;
        QString why;
        if (!mesh::InpMeshWriter::validateTopologyReplacement(path, &why))
            return fail(tr("Channel burn cannot replace this mesh topology: %1").arg(why));
    }
    auto *meshSrs = m_mesh->srs();
    auto *modelSrs = model->srs();
    if (!meshSrs || !modelSrs || !meshSrs->planarLinearUnit().usable)
        return fail(tr("Assign a projected or local CRS with linear units to the model and mesh."));
    const double meshUnitToSI = meshSrs->planarLinearUnit().metresPerUnit;
    const bool si = m_project->unitSystem() && m_project->unitSystem()->isSI();
    const double modelToSI = si ? 1.0 : 0.3048;
    const double xyScale = modelToSI / meshUnitToSI;
    const double zScale = m_mesh->meshUnitsSI() ? modelToSI : 1.0;
    out->verticalUnitToSI = m_mesh->meshUnitsSI() ? 1.0 : modelToSI;
    out->edgeBCs = m_mesh->edgeBCs();
    out->options = m_project->channelBurnSettings().options;
    out->options.channelCellSize = m_cellSize->value();
    out->options.channelAspectMax = m_aspect->value();
    out->options.forceHalfWidth = m_halfWidth->value();
    out->options.quadCorridor = m_quads->isChecked();
    out->options.removeBurnedFrom1D = m_removeFromModel->isChecked();
    out->options.convertInterfaceNodes = out->options.removeBurnedFrom1D;
    out->options.truncateAtBoundary = true;
    out->options.sectionBlend = 0;
    out->options.maxIncision = m_maxIncision->value();
    out->options.geometryTolerance = m_tolerance->value();
    out->maxCells = m_maxCells->value();
    mesh::BurnSelector selector;
    selector.mode = static_cast<mesh::BurnSelector::Mode>(m_selectionMode->currentData().toInt());
    selector.query = m_query->text().trimmed();
    selector.conduitIds = m_ids->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);

    // Engine splits are parameterized in the original model coordinate space.
    // A nonuniform projection changes those fractions and can detach interfaces.
    if (out->options.removeBurnedFrom1D && !modelSrs->equals(*meshSrs))
        return fail(tr("Use the model CRS for the mesh when replacing 1D channel intervals, or turn off 1D replacement."));

    // One transformation per snapshot, shared by all link and node coordinates.
    std::unique_ptr<OGRCoordinateTransformation, decltype(&OCTDestroyCoordinateTransformation)>
        transform(nullptr, OCTDestroyCoordinateTransformation);
    if (!modelSrs->equals(*meshSrs)) {
        transform.reset(modelSrs->createTransformationTo(*meshSrs));
        if (!transform) return fail(tr("Cannot transform channel coordinates into the mesh CRS."));
    }
    const auto projectPoint = [&](double &x, double &y) {
        return (!transform || transform->Transform(1, &x, &y)) && std::isfinite(x) && std::isfinite(y);
    };
    QHash<QString, QVector<QPointF>> lines;
    QHash<QString, QVariantMap> rows;
    for (int r = 0; r < model->categoryCount(SWMMModelLayer::CatConduits); ++r) {
        const QString id = model->objectNameAt(SWMMModelLayer::CatConduits, r);
        const int idx = model->linkIndex(id);
        if (id.isEmpty() || idx < 0) continue;
        auto line = model->cachedLinkPolyline(idx);
        for (auto &point : line) {
            double x = point.x(), y = point.y();
            if (!projectPoint(x, y)) return fail(tr("Cannot transform coordinates for channel %1.").arg(id));
            point = {x, y};
        }
        lines.insert(id, line);
        if (selector.mode == mesh::BurnSelector::Mode::ByQuery) {
            QVariantMap row;
            row.insert(QStringLiteral("Name"), id);
            char tag[256] = {};
            if (swmm_link_get_tag(eng, idx, tag, sizeof(tag)) == SWMM_OK)
                row.insert(QStringLiteral("link_tag"), QString::fromUtf8(tag));
            double length = 0, roughness = 0;
            if (swmm_link_get_length(eng, idx, &length) == SWMM_OK) row.insert(QStringLiteral("link_length"), length);
            if (swmm_link_get_roughness(eng, idx, &roughness) == SWMM_OK) row.insert(QStringLiteral("link_roughness"), roughness);
            rows.insert(id, row);
        }
    }
    const auto candidates = mesh::resolveBurnSet(eng, selector, out->options, lines, si, rows, &out->warnings);
    for (double *value : {&out->options.forceHalfWidth, &out->options.maxHalfWidth, &out->options.bankPad,
                          &out->options.chainageStep, &out->options.lateralStep, &out->options.channelCellSize})
        *value *= xyScale;
    out->options.maxIncision *= zScale;
    auto preparation = out->options;
    preparation.chainageStep = 0;
    QSet<int> affectedNodes;
    QSet<QString> burning;
    for (const auto &candidate : candidates) {
        if (!candidate.accepted) continue;
        auto input = candidate.input;
        for (auto &value : input.section.station) value *= xyScale;
        input.section.leftBank *= xyScale;
        input.section.rightBank *= xyScale;
        for (auto &value : input.section.elevation) value *= zScale;
        input.zUp *= zScale;
        input.zDn *= zScale;
        QString why;
        auto profile = mesh::buildBurnProfile(input, preparation, &out->warnings, &why);
        if (!profile.isValid()) {
            out->warnings << tr("Channel %1 skipped: %2").arg(candidate.conduitId, why);
            continue;
        }
        burning.insert(profile.conduitId);
        const int index = swmm_link_index(eng, profile.conduitId.toUtf8().constData());
        int from = -1, to = -1;
        swmm_link_get_from_node(eng, index, &from);
        swmm_link_get_to_node(eng, index, &to);
        affectedNodes.insert(from);
        affectedNodes.insert(to);
        out->profiles.append(std::move(profile));
    }
    if (out->profiles.isEmpty()) return fail(tr("No selected channel has a usable open section."));

    const QSet<int> fed = inflowNodes(eng);
    QHash<int, int> local;
    for (int i = 0, count = swmm_node_count(eng); i < count; ++i) {
        const char *id = swmm_node_id(eng, i);
        if (!id || !*id) continue;
        mesh::BurnNetwork::Node node;
        node.id = QString::fromUtf8(id);
        int type = -1;
        swmm_node_get_type(eng, i, &type);
        node.isJunction = type == SWMM_NODE_JUNCTION;
        node.hasExternalInflow = fed.contains(i);
        double depth = 0;
        swmm_node_get_initial_depth(eng, i, &depth);
        node.preserve = depth > 0;
        if (affectedNodes.contains(i) && out->options.removeBurnedFrom1D) {
            SWMM_ImpactReport impact{};
            if (swmm_node_analyze_impact(eng, i, &impact) != SWMM_OK) node.preserve = true;
            for (int j = 0; j < impact.n_entries; ++j) {
                const auto type = impact.entries[j].obj_type;
                if (type != SWMM_REF_LINK && type != SWMM_REF_EXT_INFLOW && type != SWMM_REF_DWF_INFLOW
                    && type != SWMM_REF_RDII_ASSIGN && type != SWMM_REF_SUBCATCH) node.preserve = true;
            }
            swmm_impact_report_free(&impact);
        }
        local.insert(i, out->network.nodes.size());
        out->network.nodes.append(node);
        double x = 0, y = 0;
        const int modelNode = model->nodeIndex(node.id);
        if (modelNode >= 0 && model->cachedNodeCoord(modelNode, &x, &y)) {
            if (!projectPoint(x, y)) return fail(tr("Cannot transform coordinates for node %1.").arg(node.id));
            // Coordinates are a lookup; the kernel couples only interfaces and
            // already coupled nodes, never unrelated nodes in this collection.
            out->nodes.append({node.id, {x, y}});
        }
    }
    for (int i = 0, count = swmm_link_count(eng); i < count; ++i) {
        const char *raw = swmm_link_id(eng, i);
        if (!raw || !*raw) continue;
        int from = -1, to = -1;
        swmm_link_get_from_node(eng, i, &from);
        swmm_link_get_to_node(eng, i, &to);
        mesh::BurnNetwork::Link link{QString::fromUtf8(raw), local.value(from, -1), local.value(to, -1), {}};
        if (burning.contains(link.id) && out->options.removeBurnedFrom1D) {
            SWMM_ImpactReport impact{};
            if (swmm_link_analyze_impact(eng, i, &impact) != SWMM_OK || impact.n_entries)
                link.replacementError = tr("referenced by a control, structure or other model object");
            swmm_impact_report_free(&impact);
        }
        out->network.links.append(link);
    }
    return true;
}

void ChannelBurnDialog::startBurn()
{
    try {
        if (m_busy) return;
        mesh::ChannelMeshBurnInputs inputs;
        QString error;
        if (!collectInputs(&inputs, &error)) {
            QMessageBox::warning(this, tr("Channel Burn"), error);
            return;
        }
        mesh::ChannelBurnExportRequest output;
        const bool exportRaster = m_exportRaster->isChecked();
        if (exportRaster) {
            output.projectPath = m_project->modelLayer()->modelFilePath();
            output.sourcePath = m_demPath->text().trimmed();
            if (output.projectPath.isEmpty() || !QFileInfo::exists(output.projectPath) || !QFileInfo::exists(output.sourcePath)) {
                QMessageBox::warning(this, tr("Channel Burn"), tr("Save the project and choose a readable DEM before requesting raster output."));
                return;
            }
            output.sourcePath = QFileInfo(output.sourcePath).absoluteFilePath();
            output.outputDirectory = m_outputDirectory->text().trimmed();
            if (!output.outputDirectory.isEmpty())
                output.outputDirectory = QFileInfo(output.projectPath).absoluteDir().absoluteFilePath(output.outputDirectory);
            output.meshCRSWkt = m_mesh->srs()->toWkt();
            output.rasterZToSI = m_demZToSI->value();
            output.meshZToSI = inputs.verticalUnitToSI;
            output.options = inputs.options;
            output.pendingArtifacts = m_mesh->generatedArtifacts();
        }
        auto settings = m_project->channelBurnSettings();
        settings.enabled = true;
        settings.selector.mode = static_cast<mesh::BurnSelector::Mode>(m_selectionMode->currentData().toInt());
        settings.selector.query = m_query->text().trimmed();
        settings.selector.conduitIds = m_ids->text().split(QRegularExpression(QStringLiteral("[,;\\s]+")), Qt::SkipEmptyParts);
        // Persist model-unit values, never the worker's converted mesh-unit values.
        settings.options.channelCellSize = m_cellSize->value();
        settings.options.channelAspectMax = m_aspect->value();
        settings.options.forceHalfWidth = m_halfWidth->value();
        settings.options.quadCorridor = m_quads->isChecked();
        settings.options.removeBurnedFrom1D = m_removeFromModel->isChecked();
        settings.options.convertInterfaceNodes = m_removeFromModel->isChecked();
        settings.options.maxIncision = m_maxIncision->value();
        settings.options.geometryTolerance = m_tolerance->value();
        settings.maxMeshCells = m_maxCells->value();
        settings.exportRaster = exportRaster;
        settings.exportDemPath = m_demPath->text().trimmed();
        settings.exportDirectory = m_outputDirectory->text().trimmed();
        settings.exportRasterZToSI = m_demZToSI->value();
        m_project->setChannelBurnSettings(settings);
        beginGuard();
        auto meshCopy = m_mesh->mesh();
        m_cancelled = std::make_shared<std::atomic_bool>(false);
        auto cancelled = m_cancelled;
        setBusy(true);
        m_status->setText(tr("Planning and stitching channel cells into the active mesh…"));
        m_watcher.setFuture(QtConcurrent::run([meshCopy = std::move(meshCopy), inputs = std::move(inputs),
            cancelled, exportRaster, output = std::move(output)](QPromise<Result> &promise) mutable {
            promise.setProgressRange(0, 100);
            Result result;
            try {
                const auto stop = [cancelled] { return cancelled->load(); };
                const auto progress = [&](int percent, const QString &stage) {
                    const int limit = exportRaster ? 80 : 95;
                    promise.setProgressValueAndText(limit * std::clamp(percent, 0, 100) / 100,
                        percent < 100 ? stage : QObject::tr("Preparing the channel burn result…"));
                };
                result.burn = mesh::burnChannelsIntoMesh(meshCopy, std::move(inputs), progress, stop);
                if (result.burn.ok && exportRaster && !stop()) {
                    result.output = mesh::prepareChannelBurnExport(result.burn, output, stop,
                        [&](int percent, const QString &stage) {
                            promise.setProgressValueAndText(80 + 15 * std::clamp(percent, 0, 100) / 100, stage);
                        });
                    if (!result.output.ok) {
                        result.burn.ok = false;
                        result.burn.error = result.output.error;
                    }
                }
            } catch (const std::exception &e) {
                result.burn.ok = false;
                result.burn.error = QObject::tr("Channel burn failed: %1").arg(QString::fromUtf8(e.what()));
            } catch (...) {
                result.burn.ok = false;
                result.burn.error = QObject::tr("Channel burn failed while preparing the mesh.");
            }
            if (result.burn.ok && !cancelled->load())
                promise.setProgressValueAndText(95, QObject::tr("Applying the channel burn result…"));
            promise.addResult(std::move(result));
        }));
    } catch (const std::exception &e) {
        if (m_cancelled) m_cancelled->store(true);
        clearGuard();
        setBusy(false);
        m_progress->setFormat(tr("Failed"));
        m_status->setText(QString::fromUtf8(e.what()));
        QMessageBox::critical(this, tr("Channel burn could not start"), m_status->text());
    } catch (...) {
        if (m_cancelled) m_cancelled->store(true);
        clearGuard();
        setBusy(false);
        m_progress->setFormat(tr("Failed"));
        m_status->setText(tr("Could not prepare the channel inputs."));
        QMessageBox::critical(this, tr("Channel burn could not start"), m_status->text());
    }

}

void ChannelBurnDialog::finishBurn()
{
    bool adopted = false;
    const auto release = qScopeGuard([this, &adopted] {
        setBusy(false);
        if (adopted) m_apply->setEnabled(false);
        m_progress->setAccessibleDescription(m_status->text());
    });
    const bool stopped = m_cancelled && m_cancelled->load();
    m_cancelled.reset();
    const bool current = ownerIsCurrent();
    clearGuard();
    if (!current) {
        m_progress->setFormat(tr("Discarded"));
        m_status->setText(tr("The project changed while the burn was running. The result was discarded."));
        return;
    }
    if (stopped) {
        m_progress->setFormat(tr("Cancelled"));
        m_status->setText(tr("Channel burn cancelled; the mesh and network are unchanged."));
        return;
    }
    try {
        auto job = m_watcher.result();
        auto &result = job.burn;
        if (result.cancelled) {
            m_progress->setFormat(tr("Cancelled"));
            m_status->setText(tr("Channel burn cancelled; the mesh and network are unchanged."));
            return;
        }
        if (!result.ok) {
            m_progress->setFormat(tr("Failed"));
            m_status->setText(result.error);
            QMessageBox::warning(this, tr("Channel Burn"), result.error);
            return;
        }
        m_progress->setValue(95);
        m_status->setText(tr("Applying the channel burn result…"));
        auto *canvas = m_project->canvas();
        if (!canvas->undoStack()) throw std::runtime_error("The undo stack is unavailable.");
        auto pending = std::make_unique<SWMM2DMeshLayer>(std::move(result.mesh), m_mesh->sourcePath(), nullptr, true);
        auto *layer = pending.get();
        layer->setExternalMesh(m_mesh->isExternalMesh());
        layer->setMeshUnitsSI(m_mesh->meshUnitsSI());
        layer->setOwnsGeneratedTopology(true);
        layer->setGeneratedArtifacts(job.output.artifacts ? job.output.artifacts : m_mesh->generatedArtifacts());
        layer->setImportNeedsSaveAsRebase(m_mesh->importNeedsSaveAsRebase());
        layer->copyDisplayStateFrom(*m_mesh);
        if (m_mesh->srs()) layer->setSRS(new SpatialReferenceSystem(*m_mesh->srs(), layer), true);
        layer->edgeBCsMutable() = std::move(result.edgeBCs);
        layer->setActiveMesh(false);
        auto adoption = std::make_unique<ChannelMeshAdoptionCommand>(m_project, layer, m_project->corridorSources(), m_mesh);
        adoption->setText(tr("Burn channels into mesh"));
        pending.release(); // The command now owns detached layers, including failure paths.
        QString error;
        if (!adoption->prepare(result.surgery, &error)) {
            m_progress->setFormat(tr("Failed"));
            m_status->setText(error);
            QMessageBox::warning(this, tr("Channel Burn"), error);
            return;
        }
        canvas->undoStack()->push(adoption.release());
        adopted = true;
        m_project->attachMeshLayer(layer);
        layer->finishSceneGeometryAsync();
        const auto showQuality = [&](int column, const mesh::ChannelMeshQuality &quality) {
            const QString unit = layer->srs() ? layer->srs()->linearUnitsName() : QString();
            const QStringList values{QString::number(quality.cells), QString::number(quality.quads),
                QString::number(quality.minAngle, 'g', 6) + QStringLiteral("°"),
                QString::number(quality.minEdge, 'g', 6) + QLatin1Char(' ') + unit,
                QString::number(quality.below25), QString::number(quality.below10)};
            for (int row = 0; row < values.size(); ++row)
                m_quality->setItem(row, column, new QTableWidgetItem(values[row]));
        };
        showQuality(0, result.before);
        showQuality(1, result.after);
        m_quality->show();
        QString status = tr("Burned channels into %1 cells (%2 quads). Cells below 25°: %3 before, %4 after. The operation is undoable.")
            .arg(layer->triangleCount()).arg(layer->quadCount()).arg(result.before.below25).arg(result.after.below25);
        if (!result.warnings.isEmpty()) status += QStringLiteral("\n") + result.warnings.join(QStringLiteral("\n"));
        if (job.output.ok) status += tr("\nDEM copy and report will be published when you save the project: %1").arg(job.output.rasterPath);
        if (!job.output.warnings.isEmpty()) status += QStringLiteral("\n") + job.output.warnings.join(QStringLiteral("\n"));
        m_status->setText(status);
        m_progress->setValue(100);
        m_progress->setFormat(tr("Complete — %p%"));
        m_apply->setEnabled(false);
        m_cancel->setText(tr("Close"));
    } catch (const std::exception &e) {
        m_progress->setFormat(adopted ? tr("Applied; display failed") : tr("Failed"));
        m_status->setText(QString::fromUtf8(e.what()));
        QMessageBox::critical(this, adopted ? tr("Channel burn applied; display failed") : tr("Channel burn was not applied"), m_status->text());
    } catch (...) {
        m_progress->setFormat(adopted ? tr("Applied; display failed") : tr("Failed"));
        m_status->setText(adopted ? tr("The burn is undoable. Reopen the project view to refresh the display.") : tr("Could not prepare the replacement mesh."));
        QMessageBox::critical(this, adopted ? tr("Channel burn applied; display failed") : tr("Channel burn was not applied"), m_status->text());
    }
}
