/**
 * @file      groundwaterassigndialog.cpp
 * @brief     Staged assignment of groundwater forcing to selected mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/groundwaterassigndialog.h"
#include "core/displaynumber.h"
#include "ui/hydrothermalstate.h"
#include <openswmm/engine/openswmm_gw2d.h>
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/gisrasterlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/meshcommands.h"
#include "map/spatialreferencesystem.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "core/unitsystem.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include "ui/util/fileopendialog.h"
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QHash>
#include <QHBoxLayout>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QScopeGuard>
#include <QSignalBlocker>
#include "ui/util/numerictablewidgetitem.h"
#include <QVBoxLayout>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>
#include <memory>

namespace openswmmvis::ui
{
using namespace openswmmvis::assignment;
namespace
{
/**
 * @brief Hash complete source-file contents for immutable preview/source verification.
 * @param[in] paths Borrowed paths opened read-only in order; output keys are their absolute
 * paths and duplicate keys overwrite.
 * @param[out] error Non-null failure text destination; written for open/read/cancellation
 * failure, unchanged on success.
 * @param[in] cancel Optional synchronous cancellation predicate tested between 1-MiB reads; must
 * be safe on the caller's worker thread and not throw.
 * @return Owned absolute-path-to-SHA-256 map on success; empty on failure, discarding any
 * partial hashes. Empty input also returns empty successfully. No files are modified or pointers
 * retained.
 */
QMap<QString, QByteArray> fingerprints(const QStringList &paths, QString *error,
                                       const std::function<bool()> &cancel = {})
{
    QMap<QString, QByteArray> result;
    for (const QString &path : paths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            *error = QObject::tr("Cannot snapshot source %1: %2").arg(path, file.errorString());
            return {};
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!file.atEnd()) {
            if (cancel && cancel()) {
                *error = QObject::tr("Cancelled.");
                return {};
            }
            const QByteArray data = file.read(1024 * 1024);
            if (data.isEmpty() && file.error() != QFile::NoError) {
                *error = file.errorString();
                return {};
            }
            hash.addData(data);
        }
        result.insert(QFileInfo(path).absoluteFilePath(), hash.result());
    }
    return result;
}
/**
 * @brief Open a GDAL source read-only and enumerate its backing dataset/sidecar paths.
 * @param[in] path Borrowed local source path opened as raster or vector using registered GDAL
 * drivers.
 * @param[out] error Non-null failure text destination, written when opening fails and unchanged
 * on success.
 * @return Owned deduplicated absolute file list including the requested primary path; empty on
 * open failure. Releases GDAL's returned file list and closes the dataset before returning; no
 * dataset ownership escapes.
 */
QStringList sourceFiles(const QString &path, QString *error)
{
    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(path.toUtf8().constData(), GDAL_OF_READONLY | GDAL_OF_RASTER | GDAL_OF_VECTOR,
                   nullptr, nullptr, nullptr));
    if (!ds) {
        *error = QObject::tr("Cannot open the assignment source.");
        return {};
    }
    QStringList result;
    char **list = ds->GetFileList();
    for (int i = 0; list && list[i]; ++i)
        result.append(QFileInfo(QString::fromUtf8(list[i])).absoluteFilePath());
    CSLDestroy(list);
    GDALClose(ds);
    const auto primary = QFileInfo(path).absoluteFilePath();
    if (!result.contains(primary))
        result.append(primary);
    result.removeDuplicates();
    return result;
}
/**
 * @brief Convert an engine-authored infiltration row to the GUI's value representation.
 * @param[in] row Borrowed native row; absent method becomes None and destination enum is copied.
 * @return Owned InfilRow with only method-used positional values copied; other slots retain
 * their NaN/unset defaults. Performs no unit conversion, enum validation or engine edit.
 */
mesh::InfilRow meshRow(const SWMM_Infil2DRow &row)
{
    mesh::InfilRow value;
    value.method = row.has_method ? mesh::InfilMethod(row.method) : mesh::InfilMethod::None;
    value.dest = mesh::InfilDest(row.dest);
    for (int k = 0; k < 5; ++k)
        if (mesh::infilUsesParam(value.method, k))
            value.p[k] = row.p[k];
    return value;
}
/**
 * @brief Compare a live mesh's authored infiltration collections with a reviewed row snapshot.
 * @param[in] layer Non-null borrowed mesh layer, read synchronously on its owner thread.
 * @param[in] snapshot Borrowed ownership snapshot; only rows are inspected, not its global
 * destination token.
 * @return True for equal ordered tag-default rows and equal cell-override maps after meshRow()
 * conversion, using InfilRow value equality; false otherwise. Duplicate cell keys collapse with
 * the later value. No mutation occurs.
 */
bool meshMatchesOwnership(SWMM2DMeshLayer *layer, const InfiltrationOwnershipSnapshot &snapshot)
{
    QVector<mesh::InfilDefaultRow> defaults;
    QHash<int, mesh::InfilRow> overrides;
    for (const auto &row : snapshot.rows)
        if (row.cell < 0)
            defaults.append({QString::fromUtf8(row.tag), meshRow(row.row)});
        else
            overrides[row.cell] = meshRow(row.row);
    const auto &mesh = layer->mesh();
    if (defaults.size() != mesh.infilDefaults.size() || overrides != mesh.infilOverrides)
        return false;
    for (int i = 0; i < defaults.size(); ++i)
        if (defaults[i].tag != mesh.infilDefaults[i].tag ||
            defaults[i].row != mesh.infilDefaults[i].row)
            return false;
    return true;
}
/**
 * @brief Replace GUI mesh infiltration collections from an already-applied ownership snapshot.
 * @param[in,out] layer Non-null borrowed mesh layer receiving replaceInfiltrationRows().
 * @param[in] snapshot Borrowed rows converted to ordered tag defaults and a cell-override map;
 * global destination token is not applied here.
 * @details Last duplicate cell key wins. Uses owned value copies, performs no engine write
 * itself and relies on the preceding engine ownership helper's checks.
 */
void installOwnershipRows(SWMM2DMeshLayer *layer, const InfiltrationOwnershipSnapshot &snapshot)
{
    QVector<mesh::InfilDefaultRow> defaults;
    QHash<int, mesh::InfilRow> overrides;
    for (const auto &row : snapshot.rows)
        if (row.cell < 0)
            defaults.append({QString::fromUtf8(row.tag), meshRow(row.row)});
        else
            overrides[row.cell] = meshRow(row.row);
    layer->replaceInfiltrationRows(defaults, overrides);
}
/**
 * @brief Copy engine-side tag changes into the mesh layer, which owns tags for saving.
 * @param[in,out] layer Mesh whose cells follow engine cell order.
 * @param[in] from Tags the change started from; cells equal in both lists are untouched.
 * @param[in] to Tags to install.
 */
void installTags(SWMM2DMeshLayer *layer, const QStringList &from, const QStringList &to)
{
    if (!layer || from.size() != to.size() || from == to)
        return;
    mesh::AttributeEditBatch batch(layer, to.size());
    for (int c = 0; c < to.size() && c < layer->mesh().triangles.size(); ++c)
        if (from[c] != to[c])
            layer->applyMeshTriangleTag(c, to[c]);
}
class ProcessUndo final : public QObject, public QUndoCommand
{
    QPointer<SWMMModelLayer> model;
    QPointer<SWMM2DMeshLayer> mesh;
    SWMM_Engine engine;
    quint64 geometry;
    AquiferProcessPreview preview;
    bool first = true;
    /**
     * @brief Replay reviewed groundwater process options against their expected opposite
     * snapshot.
     * @param[in] after True installs preview.after expecting before; false installs before
     * expecting after.
     * @details Requires guarded model/mesh, captured engine identity and unchanged mesh
     * geometry. applyAquiferProcesses() performs its own expected-state/atomic update checks;
     * failure marks obsolete and warns. Success marks edited, emits
     * GW_ET/LINK_SEEPAGE/WILTING_SUCTION optionsChanged and refreshes mesh ownership. No
     * callback exceptions are caught here.
     */
    void install(bool after)
    {
        QString error;
        if (!model || !mesh || model->engine() != engine || mesh->geomRevision() != geometry ||
            !applyAquiferProcesses(engine, after ? preview.before : preview.after,
                                   after ? preview.after : preview.before, preview.groundwater,
                                   &error)) {
            setObsolete(true);
            QMessageBox::warning(QApplication::activeWindow(), QObject::tr("Groundwater ET"),
                                 error.isEmpty() ? QObject::tr("The model changed.") : error);
            return;
        }
        model->markEdited();
        emit model->optionsChanged({"GW_ET", "LINK_SEEPAGE", "WILTING_SUCTION"});
        mesh->refreshInfiltrationOwnership();
    }

public:
    /**
     * @brief Own a reviewed shared process-option before/after proposal for later replay.
     * @param[in] m Non-null borrowed model; captured engine identity is retained, QObject
     * lifetime guarded.
     * @param[in] l Non-null borrowed mesh; geometry revision is captured and QObject lifetime
     * guarded.
     * @param[in] p Owned preview moved into the command, already installed by the original
     * Apply.
     * @details Intended for a canvas undo stack; first redo skips installation because it
     * already occurred. Does not own model/mesh or advance simulation.
     */
    ProcessUndo(SWMMModelLayer *m, SWMM2DMeshLayer *l, AquiferProcessPreview p)
        : model(m), mesh(l), engine(m->engine()), geometry(l->geomRevision()), preview(std::move(p))
    {
        setText(QObject::tr("Change reviewed groundwater ET and seepage"));
    }
    /**
     * @brief Restore the retained before proposal through install(false).
     * @details A changed/unavailable owner or helper failure makes the command obsolete and
     * reports a warning rather than silently merging unrelated state.
     */
    void undo() override { install(false); }
    /**
     * @brief Skip initial stack push, then replay the retained after proposal.
     * @details Later calls use install(true) and its owner/expected-state guards; first redo
     * performs no write.
     */
    void redo() override
    {
        if (first) {
            first = false;
            return;
        }
        install(true);
    }
};
class OwnershipUndo final : public QObject, public QUndoCommand
{
    QPointer<SWMMModelLayer> model;
    QPointer<SWMM2DMeshLayer> layer;
    SWMM_Engine engine;
    quint64 geometry;
    InfiltrationOwnershipPreview preview;
    bool first = true;
    /**
     * @brief Replay reviewed infiltration ownership in engine and mesh collections.
     * @param[in] after True expects before/installs after; false expects after/installs before.
     * @details Requires guarded model/mesh, captured engine identity, unchanged geometry and
     * matching expected GUI rows, then delegates expected engine-state checking/publication to
     * applyInfiltrationOwnership(). Failure makes the command obsolete and warns; success
     * installs converted GUI rows and marks model edited. Engine-helper restoration limitations
     * remain applicable; this wrapper supplies no extra transaction.
     */
    void install(bool after)
    {
        QString error;
        if (!model || !layer || model->engine() != engine || layer->geomRevision() != geometry ||
            !meshMatchesOwnership(layer, after ? preview.before : preview.after) ||
            !applyInfiltrationOwnership(engine, after ? preview.before : preview.after,
                                        after ? preview.after : preview.before, &error)) {
            setObsolete(true);
            QMessageBox::warning(
                QApplication::activeWindow(), QObject::tr("Infiltration ownership"),
                error.isEmpty() ? QObject::tr("The model or mesh changed.") : error);
            return;
        }
        installOwnershipRows(layer, after ? preview.after : preview.before);
        model->markEdited();
    }

public:
    /**
     * @brief Own a reviewed infiltration ownership before/after proposal for later replay.
     * @param[in] m Non-null borrowed model; captured engine identity is retained, QObject
     * lifetime guarded.
     * @param[in] l Non-null borrowed mesh; geometry revision is captured and QObject lifetime
     * guarded.
     * @param[in] p Owned preview moved into the command, already installed by the original
     * Apply.
     * @details Intended for a canvas undo stack; first redo skips installation because it
     * already occurred. Does not own model/mesh or advance simulation.
     */
    OwnershipUndo(SWMMModelLayer *m, SWMM2DMeshLayer *l, InfiltrationOwnershipPreview p)
        : model(m), layer(l), engine(m->engine()), geometry(l->geomRevision()),
          preview(std::move(p))
    {
        setText(QObject::tr("Migrate reviewed infiltration ownership"));
    }
    /**
     * @brief Restore the retained before proposal through install(false).
     * @details A changed/unavailable owner or helper failure makes the command obsolete and
     * reports a warning rather than silently merging unrelated state.
     */
    void undo() override { install(false); }
    /**
     * @brief Skip initial stack push, then replay the retained after proposal.
     * @details Later calls use install(true) and its owner/expected-state guards; first redo
     * performs no write.
     */
    void redo() override
    {
        if (first) {
            first = false;
            return;
        }
        install(true);
    }
};
class AssignmentUndo final : public QObject, public QUndoCommand
{
    QPointer<SWMMModelLayer> model;
    QPointer<SWMM2DMeshLayer> mesh;
    SWMM_Engine engine;
    quint64 geometry;
    AquiferPreview preview;
    GroundwaterTransportPreview transport;
    RowAssignmentPreview rows;
    bool isTransport = false, isRows = false, first = true, valid = true;
    std::function<void(bool)> changed;
    std::function<void(const QString &)> partial;
    /**
     * @brief Check that replay's captured model/engine/mesh identity remains usable.
     * @return True only while the permanent valid flag remains set, guarded model/mesh survive,
     * engine pointer matches and mesh geometry revision is unchanged. Does not itself compare
     * assignment contents, source hashes or attribute revisions; replay helpers check expected
     * authoring state.
     */
    bool check()
    {
        return valid && model && mesh && model->engine() == engine &&
               mesh->geomRevision() == geometry;
    }
    /**
     * @brief Mark this undo command obsolete and display a replay failure.
     * @param[in] error Borrowed failure text copied into a Groundwater assignment warning on the
     * active window.
     * @details Does not itself restore data or emit dirty/audit callbacks; callers report
     * changed transport outcomes separately.
     */
    void failed(const QString &error)
    {
        setObsolete(true);
        QMessageBox::warning(QApplication::activeWindow(), QObject::tr("Groundwater assignment"),
                             error);
    }

public:
    /**
     * @brief Own an already-installed aquifer property proposal and replay notification.
     * @param[in] m Non-null borrowed model; guarded, with permanent invalidation when
     * engineAboutToClose emits.
     * @param[in] layer Non-null borrowed mesh; guarded with captured geometry revision.
     * @param[in] p Owned AquiferPreview moved into the command, containing expected before/after
     * authoring state and affected cells.
     * @param[in] notify Optional owned callback moved into the command; receives false after
     * successful undo and true after successful later redo. Must remain callable on the GUI
     * thread without throwing/reentering this command.
     * @details Captures the current engine pointer but does not own it. First redo skips the
     * already-completed original write; callback lifetime must not depend on the dialog
     * remaining alive.
     */
    AssignmentUndo(SWMMModelLayer *m, SWMM2DMeshLayer *layer, AquiferPreview p,
                   std::function<void(bool)> notify)
        : model(m), mesh(layer), engine(m->engine()), geometry(layer->geomRevision()),
          preview(std::move(p)), changed(std::move(notify))
    {
        setText(QObject::tr("Assign aquifer values to %n cell(s)", nullptr, preview.cells.size()));
        connect(m, &SWMMModelLayer::engineAboutToClose, this, [this] { valid = false; });
    }
    /**
     * @brief Own an already-installed transport proposal with success and partial-write
     * reporting.
     * @param[in] m Non-null borrowed model, guarded with engine-close invalidation.
     * @param[in] layer Non-null borrowed mesh, guarded with captured geometry revision.
     * @param[in] p Owned GroundwaterTransportPreview moved into the command, including expected
     * before/after records.
     * @param[in] notify Optional owned callback receiving false/true after successful undo/later
     * redo; GUI-thread nonthrowing callable.
     * @param[in] dirty Optional owned callback receiving an error when transport replay fails
     * with changed=true, enabling an audit/dirty notification even without full success; must
     * not throw or require a live editor.
     * @details Delegates owner capture to the property constructor, then selects transport
     * replay. First redo skips installation already performed by Apply; transport helpers
     * control their own rollback and report partial outcomes.
     */
    AssignmentUndo(SWMMModelLayer *m, SWMM2DMeshLayer *layer, GroundwaterTransportPreview p,
                   std::function<void(bool)> notify, std::function<void(const QString &)> dirty)
        : AssignmentUndo(m, layer, AquiferPreview{}, std::move(notify))
    {
        transport = std::move(p);
        isTransport = true;
        partial = std::move(dirty);
        setText(QObject::tr("Assign groundwater transport to %n cell(s)", nullptr,
                            transport.cells.size()));
    }
    /**
     * @brief Own an already-applied scoped-row proposal (Phase 3b surface / groundwater rows).
     * @param[in] p Preview whose before/after rows and cell tags are replayed; the mesh layer's
     * tags follow the engine's on every replay.
     */
    AssignmentUndo(SWMMModelLayer *m, SWMM2DMeshLayer *layer, RowAssignmentPreview p,
                   std::function<void(bool)> notify, std::function<void(const QString &)> dirty)
        : AssignmentUndo(m, layer, AquiferPreview{}, std::move(notify))
    {
        rows = std::move(p);
        isRows = true;
        partial = std::move(dirty);
        setText(QObject::tr("Assign %1 rows to %n cell(s)", nullptr, rows.cells.size())
                    .arg(ui::Mesh2DSectionRowModel(rows.before.section).sectionName()));
    }
    /**
     * @brief Undo the retained property or transport proposal while its owner still matches.
     * @details Invalid owner or helper failure marks obsolete and warns. A failed transport
     * result with changed=true invokes the partial callback before reporting failure; successful
     * replay invokes changed(false). Property replay failure has only its helper error contract.
     * This wrapper does not add an independent rollback or catch callback exceptions.
     */
    void undo() override
    {
        QString error;
        if (!check()) {
            failed(QObject::tr("The model or mesh changed; assignment undo was refused."));
            return;
        }
        if (isRows) {
            auto r = undoRowAssignment(engine, rows);
            if (!r.success) {
                if (r.changed && partial)
                    partial(r.error);
                failed(r.error);
                return;
            }
            installTags(mesh, rows.after.cellTags, rows.before.cellTags);
        } else if (isTransport) {
            auto r = undoGroundwaterTransport(engine, transport);
            if (!r.success) {
                if (r.changed && partial)
                    partial(r.error);
                failed(r.error);
                return;
            }
            installTags(mesh, transport.after.cellTags, transport.before.cellTags);
        } else if (!undoAquiferPreview(engine, preview, &error)) {
            failed(error);
            return;
        } else {
            installTags(mesh, preview.tagsAfter, preview.tagsBefore);
        }
        model->markEdited();
        if (changed)
            changed(false);
    }
    /**
     * @brief Skip initial push, then reinstall the retained property or transport proposal.
     * @details Later replay validates captured owner, delegates expected-state/write handling to
     * the appropriate helper and invokes changed(true) only on success. A transport failure with
     * changed=true invokes the partial callback and marks the command obsolete; first redo never
     * writes/notifies because Apply already did.
     */
    void redo() override
    {
        if (first) {
            first = false;
            return;
        }
        QString error;
        if (!check()) {
            failed(QObject::tr("The model or mesh changed; assignment redo was refused."));
            return;
        }
        if (isRows) {
            auto r = applyRowAssignment(engine, rows);
            if (!r.success) {
                if (r.changed && partial)
                    partial(r.error);
                failed(r.error);
                return;
            }
            installTags(mesh, rows.before.cellTags, rows.after.cellTags);
        } else if (isTransport) {
            auto r = applyGroundwaterTransport(engine, transport);
            if (!r.success) {
                if (r.changed && partial)
                    partial(r.error);
                failed(r.error);
                return;
            }
            installTags(mesh, transport.before.cellTags, transport.after.cellTags);
        } else if (!applyAquiferPreview(engine, preview, &error)) {
            failed(error);
            return;
        } else {
            installTags(mesh, preview.tagsBefore, preview.tagsAfter);
        }
        model->markEdited();
        if (changed)
            changed(true);
    }
};
}
GroundwaterAssignDialog::GroundwaterAssignDialog(SWMMModelLayer *model, SWMM2DMeshLayer *mesh,
                                                 MapCanvas *canvas, SelectionManager *selection,
                                                 const UnitSystem *units, QWidget *parent)
    : QDialog(parent), m_model(model), m_mesh(mesh), m_canvas(canvas), m_selection(selection)
{
    m_events = new GroundwaterAssignmentEvents(model ? static_cast<QObject *>(model) : this);
    setWindowTitle(tr("Assign spatial values"));
    setWindowModality(Qt::NonModal);
    resize(780, 790);
    m_engine = model ? model->engine() : nullptr;
    m_length = units ? units->lengthLabel() : tr("project length");
    m_rate = units ? (units->isSI() ? tr("mm/hr") : tr("in/hr")) : tr("project rate");
    if (m_mesh)
        m_mesh->setInfiltrationModel(model);
    auto *outer = new QVBoxLayout(this);
    auto *intro = new QLabel(
        tr("Use the existing map rectangle or polygon cell selection, or enter cells below. Groundwater properties are written as explicit cell overrides that stop inheriting later property changes. Surface-quality and initial-quality rows are written as CELL rows, or as one TAG row for cells tagged for it."),
        this);
    intro->setObjectName("gwAssignmentIntro");
    intro->setWordWrap(true);
    outer->addWidget(intro);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *fields = new QWidget(scroll);
    auto *form = new QFormLayout(fields);
    form->setObjectName("gwAssignmentForm");
    scroll->setObjectName("gwAssignmentFields");
    scroll->setWidget(fields);
    outer->addWidget(scroll, 2);
    m_target = new QComboBox(this);
    m_target->setObjectName("gwAssignmentTarget");
    for (const auto &t : aquiferTargets()) {
        if (t.key == "FLOW")
            continue;
        m_target->addItem(t.label, t.key);
        const int i = m_target->count() - 1;
        m_target->setItemData(i, t.unavailableReason, Qt::ToolTipRole);
        if (!t.supported)
            static_cast<QStandardItemModel *>(m_target->model())->item(i)->setEnabled(false);
    }
    m_target->addItem(tr("Review / migrate infiltration ownership"), "INFILTRATION_OWNERSHIP");
    m_target->addItem(tr("Review ET and seepage defaults"), "ET_POLICY");
    m_target->addItem(tr("Initial species concentration"), "INITIAL_QUALITY");
    m_target->addItem(tr("Add injection / extraction sources"), "FLOW");
    // Phase 3b: the mesh surface-quality rows share the same selection, sampling and review.
    m_target->addItem(tr("Land-use coverage [2D_COVERAGES]"), "SQ_COVERAGE");
    m_target->addItem(tr("Initial buildup [2D_LOADINGS]"), "SQ_LOADING");
    m_target->addItem(tr("Curb length [2D_CURB_LENGTH]"), "SQ_CURB");
    m_target->addItem(tr("Surface initial concentration [2D_INITIAL_QUALITY]"), "SQ_INITIAL");
    {
        Mesh2DSectionRowModel coverages(Mesh2DSectionRowModel::Section::Coverages),
            loadings(Mesh2DSectionRowModel::Section::Loadings),
            initial(Mesh2DSectionRowModel::Section::SurfaceInitialQuality);
        coverages.load(m_engine);
        loadings.load(m_engine);
        initial.load(m_engine);
        m_landUses = coverages.landUses();
        m_loadingSpecies = loadings.names();
        m_surfaceIqSpecies = initial.names();
    }
    form->addRow(tr("&Target:"), m_target);
    m_units = new QLabel(this);
    m_units->setWordWrap(true);
    form->addRow(m_units);
    m_processControls = new QWidget(this);
    auto *processForm = new QFormLayout(m_processControls);
    m_processEt = new QComboBox(m_processControls);
    m_processEt->setObjectName("gwProcessEt");
    for (const auto &pair : QList<QPair<QString, QString>>{{tr("Automatic"), "AUTO"},
                                                           {tr("None"), "NONE"},
                                                           {tr("Boundary ET"), "BOUNDARY_ET"},
                                                           {tr("Capillary rise"), "CAPILLARY_RISE"},
                                                           {tr("Both"), "BOTH"}})
        m_processEt->addItem(pair.first, pair.second);
    processForm->addRow(tr("Groundwater ET:"), m_processEt);
    m_processLink = new QComboBox(m_processControls);
    m_processLink->setObjectName("gwProcessLink");
    for (const auto &pair : QList<QPair<QString, QString>>{{tr("Automatic"), "DEFAULT"},
                                                           {tr("One way"), "ONE_WAY"},
                                                           {tr("Two way"), "TWO_WAY"},
                                                           {tr("Off"), "NONE"}})
        m_processLink->addItem(pair.first, pair.second);
    processForm->addRow(tr("Link seepage:"), m_processLink);
    m_wiltingAuto = new QCheckBox(tr("Automatic wilting suction (150 m)"), m_processControls);
    m_wiltingAuto->setObjectName("gwProcessWiltingAuto");
    processForm->addRow(m_wiltingAuto);
    m_wilting = new QDoubleSpinBox(m_processControls);
    m_wilting->setObjectName("gwProcessWilting");
    m_wilting->setDecimals(9);
    m_wilting->setRange(1e-9, 1e9);
    m_wilting->setSuffix(" " + m_length);
    processForm->addRow(tr("Wilting suction:"), m_wilting);
    form->addRow(m_processControls);
    AquiferSnapshot processSnapshot;
    QString processError;
    if (readAquiferSnapshot(m_engine, &processSnapshot, &processError)) {
        const auto &o = processSnapshot.options;
        m_processEt->setCurrentIndex(m_processEt->findData(o.value("GW_ET")));
        m_processLink->setCurrentIndex(m_processLink->findData(o.value("LINK_SEEPAGE")));
        m_wiltingAuto->setChecked(o.value("WILTING_SUCTION") == "AUTO");
        m_wilting->setValue(m_wiltingAuto->isChecked() ? (m_length == "ft" ? 150 / .3048 : 150)
                                                       : o.value("WILTING_SUCTION").toDouble());
    }
    m_wilting->setEnabled(!m_wiltingAuto->isChecked());
    auto refreshEt = [this] {
        char mode[64]{};
        if (m_engine)
            swmm_gw2d_option_get(m_engine, "MODE", mode, sizeof mode);
        const auto state = groundwaterEtState(m_processEt->currentData().toString(),
                                              QString::fromUtf8(mode), true, true);
        m_wiltingAuto->setEnabled(state.boundary);
        m_wilting->setEnabled(state.boundary && !m_wiltingAuto->isChecked());
    };
    refreshEt();
    connect(m_processEt, &QComboBox::currentIndexChanged, this, refreshEt);
    connect(m_wiltingAuto, &QCheckBox::toggled, this, refreshEt);
    connect(m_processEt, &QComboBox::currentIndexChanged, this,
            &GroundwaterAssignDialog::invalidatePreview);
    connect(m_processLink, &QComboBox::currentIndexChanged, this,
            &GroundwaterAssignDialog::invalidatePreview);
    connect(m_wilting, &QDoubleSpinBox::valueChanged, this,
            &GroundwaterAssignDialog::invalidatePreview);
    connect(m_wiltingAuto, &QCheckBox::toggled, this, [this](bool automatic) {
        char mode[64]{};
        if (m_engine)
            swmm_gw2d_option_get(m_engine, "MODE", mode, sizeof mode);
        const auto state = groundwaterEtState(m_processEt->currentData().toString(),
                                              QString::fromUtf8(mode), true, true);
        m_wilting->setEnabled(!automatic && state.boundary);
        invalidatePreview();
    });
    m_scopeChoice = new QComboBox(this);
    m_scopeChoice->setObjectName("gwAssignmentScope");
    m_scopeChoice->addItems({tr("Selected mesh cells"), tr("All mesh cells"), tr("Mesh tag"),
                             tr("Cell numbers"), tr("Polygon WKT in mesh CRS")});
    form->addRow(tr("&Scope:"), m_scopeChoice);
    m_scopeText = new QLineEdit(this);
    m_scopeText->setObjectName("gwAssignmentScopeText");
    m_scopeText->setPlaceholderText(
        tr("Tag, 1-based cell numbers separated by commas, or POLYGON ((x y, ...))"));
    form->addRow(tr("Scope &details:"), m_scopeText);
    auto *selectionRow = new QWidget(this);
    auto *selectionLayout = new QHBoxLayout(selectionRow);
    selectionLayout->setContentsMargins(0, 0, 0, 0);
    m_selectionCount = new QLabel(selectionRow);
    m_selectionCount->setObjectName("gwAssignmentSelectionCount");
    m_selectionCount->setWordWrap(true);
    selectionLayout->addWidget(m_selectionCount, 1);
    auto *pickCells = new QPushButton(tr("Select cells on map"), selectionRow);
    pickCells->setObjectName("gwAssignmentPickCells");
    pickCells->setAutoDefault(false);
    pickCells->setToolTip(
        tr("Turns on the mesh cell selection tool. Drag a box, or press L and click a polygon "
           "(double-click closes it). Shift adds, Alt subtracts, Ctrl toggles; cells whose "
           "centroid lies inside are selected."));
    selectionLayout->addWidget(pickCells);
    form->addRow(tr("Selection:"), selectionRow);
    connect(pickCells, &QPushButton::clicked, this, [] {
        for (QWidget *window : QApplication::topLevelWidgets())
            if (auto *action = window->findChild<QAction *>("actionPick2DCells")) {
                action->setChecked(true);
                return;
            }
    });
    m_route = new QComboBox(this);
    m_route->setObjectName("gwAssignmentRoute");
    m_route->addItems({tr("Manual constant"), tr("Feature layer"), tr("Raster")});
    form->addRow(tr("Input &method:"), m_route);
    m_value = new QDoubleSpinBox(this);
    m_value->setObjectName("gwAssignmentValue");
    m_value->setDecimals(12);
    m_value->setRange(-1e15, 1e15);
    m_value->setValue(.5);
    form->addRow(tr("&Value:"), m_value);
    m_transportControls = new QWidget(this);
    auto *transportForm = new QFormLayout(m_transportControls);
    transportForm->setContentsMargins(0, 0, 0, 0);
    m_species = new QComboBox(m_transportControls);
    m_species->setObjectName("gwAssignmentSpecies");
    QString speciesError;
    m_speciesCatalog = groundwaterTransportSpecies(m_engine, &speciesError);
    for (const auto &sp : m_speciesCatalog)
        m_species->addItem(tr("%1 (%2)").arg(sp.id, sp.nativeConcUnits.isEmpty()
                                                        ? tr("units unresolved")
                                                        : sp.nativeConcUnits),
                           sp.id);
    transportForm->addRow(tr("Species:"), m_species);
    m_zone = new QComboBox(m_transportControls);
    m_zone->setObjectName("gwAssignmentZone");
    m_zone->addItem(tr("Saturated (SAT)"), 0);
    m_zone->addItem(tr("Unsaturated (UNSAT)"), 1);
    m_zone->addItem(tr("Soil layer"), 2);
    transportForm->addRow(tr("Initial zone:"), m_zone);
    m_qualityLayer = new QSpinBox(m_transportControls);
    m_qualityLayer->setObjectName("gwAssignmentQualityLayer");
    m_qualityLayer->setRange(1, 128);
    m_qualityLayer->setToolTip(tr(
        "One-based layer, counted from the surface. Requires separate soil-layer chemistry and a SIGMA column."));
    transportForm->addRow(tr("Soil layer:"), m_qualityLayer);
    form->addRow(m_transportControls);
    m_sourceControls = new QWidget(this);
    auto *sourceForm = new QFormLayout(m_sourceControls);
    sourceForm->setContentsMargins(0, 0, 0, 0);
    m_sourceName = new QLineEdit("Assigned", m_sourceControls);
    m_sourceName->setObjectName("gwAssignmentSourceName");
    sourceForm->addRow(tr("Source name prefix:"), m_sourceName);
    m_distribution = new QComboBox(m_sourceControls);
    m_distribution->setObjectName("gwAssignmentDistribution");
    m_distribution->addItems(
        {tr("Value for each selected cell"), tr("Total across selected cells (area weighted)")});
    sourceForm->addRow(tr("Flow and MASS distribution:"), m_distribution);
    m_flowSeries = new QComboBox(m_sourceControls);
    m_flowSeries->setObjectName("gwAssignmentFlowSeries");
    m_flowSeries->addItem(tr("Constant value"), QString());
    GroundwaterTransportSnapshot catalog;
    if (readGroundwaterTransportSnapshot(m_engine, &catalog, &speciesError))
        for (auto it = catalog.timeSeries.cbegin(); it != catalog.timeSeries.cend(); ++it)
            m_flowSeries->addItem(it.key(), it.key());
    sourceForm->addRow(tr("Flow time series:"), m_flowSeries);
    m_pointPlacement = new QComboBox(m_sourceControls);
    m_pointPlacement->setObjectName("gwAssignmentPointPlacement");
    m_pointPlacement->addItem(tr("In the cell that contains the point (CELL)"), "CELL");
    m_pointPlacement->addItem(tr("At the point (XY; placed in the nearest cell centroid)"), "XY");
    sourceForm->addRow(tr("Point features:"), m_pointPlacement);
    m_pointName = new QLineEdit(m_sourceControls);
    m_pointName->setObjectName("gwAssignmentPointNameField");
    m_pointName->setPlaceholderText(tr("Optional field naming each source"));
    sourceForm->addRow(tr("Point name field:"), m_pointName);
    auto *explanation = new QLabel(
        tr("Each application adds new named sources alongside existing forcing. Undo removes that batch. Flow is signed m³/s: positive injects; negative extracts at in-situ concentration. CONC uses the species' native concentration units; MASS uses native mass/s. Region totals split flow and MASS by cell area; CONC is unchanged. Layer values require separate soil-layer chemistry in Groundwater Options. Hydraulic edge chemistry is specified with its hydraulic boundary. Feature values and default raster sampling are per-cell rates. Choose raster water flux density to integrate flow over pixel–cell intersections. Species terms remain per-cell values; only water flux is area-integrated. A point feature layer adds one source per point: flow from the numeric field, species values from each term's feature field (blank keeps the constant), and points outside the mesh or scope are listed, not assigned. Writing one TAG row needs the total across selected cells; the engine spreads it over the tagged cells by area."),
        m_sourceControls);
    explanation->setWordWrap(true);
    sourceForm->addRow(explanation);
    m_terms = new QTableWidget(0, 6, m_sourceControls);
    m_terms->setObjectName("gwAssignmentTerms");
    m_terms->setHorizontalHeaderLabels({tr("Species"), tr("Term"), tr("Constant"),
                                        tr("Time series"), tr("Native units"),
                                        tr("Feature field (points)")});
    m_terms->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_terms->setMaximumHeight(135);
    sourceForm->addRow(m_terms);
    auto *termButtons = new QWidget(m_sourceControls);
    auto *termLayout = new QHBoxLayout(termButtons);
    termLayout->setContentsMargins(0, 0, 0, 0);
    auto *addTerm = new QPushButton(tr("Add species term"), termButtons);
    auto *removeTerm = new QPushButton(tr("Remove selected term"), termButtons);
    termLayout->addWidget(addTerm);
    termLayout->addWidget(removeTerm);
    sourceForm->addRow(termButtons);
    form->addRow(m_sourceControls);
    m_surfaceControls = new QWidget(this);
    auto *surfaceForm = new QFormLayout(m_surfaceControls);
    surfaceForm->setContentsMargins(0, 0, 0, 0);
    m_surfaceSpecies = new QComboBox(m_surfaceControls);
    m_surfaceSpecies->setObjectName("gwAssignmentSurfaceSpecies");
    surfaceForm->addRow(tr("Surface species:"), m_surfaceSpecies);
    m_landUse = new QComboBox(m_surfaceControls);
    m_landUse->setObjectName("gwAssignmentLandUse");
    for (const QString &use : m_landUses)
        m_landUse->addItem(use, use);
    surfaceForm->addRow(tr("Land use (manual value is its percent):"), m_landUse);
    m_classLookup = new QLineEdit(m_surfaceControls);
    m_classLookup->setObjectName("gwAssignmentClassLookup");
    m_classLookup->setPlaceholderText(tr("e.g. 1=Urban, 2=Grass (blank: class values are "
                                         "land-use names)"));
    surfaceForm->addRow(tr("Class to land use:"), m_classLookup);
    form->addRow(m_surfaceControls);
    m_strategyControls = new QWidget(this);
    auto *strategyForm = new QFormLayout(m_strategyControls);
    strategyForm->setContentsMargins(0, 0, 0, 0);
    m_strategy = new QComboBox(m_strategyControls);
    m_strategy->setObjectName("gwAssignmentStrategy");
    m_strategy->addItem(tr("Automatic: one TAG row when every cell gets the same value"), "AUTO");
    m_strategy->addItem(tr("One CELL row per cell"), "CELL");
    m_strategy->addItem(tr("Tag the cells and write one TAG row"), "TAG");
    strategyForm->addRow(tr("Write as:"), m_strategy);
    m_tagName = new QLineEdit(m_strategyControls);
    m_tagName->setObjectName("gwAssignmentTagName");
    m_tagName->setPlaceholderText(tr("Blank: the scope's own tag, or a new unused name"));
    strategyForm->addRow(tr("Tag name:"), m_tagName);
    m_allowRetag = new QCheckBox(
        tr("Allow moving cells out of another tag (the preview lists what they stop inheriting)"),
        m_strategyControls);
    m_allowRetag->setObjectName("gwAssignmentAllowRetag");
    strategyForm->addRow(m_allowRetag);
    form->addRow(m_strategyControls);
    for (auto *combo : {m_surfaceSpecies, m_landUse, m_strategy, m_pointPlacement})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &GroundwaterAssignDialog::invalidatePreview);
    connect(m_allowRetag, &QCheckBox::toggled, this, &GroundwaterAssignDialog::invalidatePreview);
    addTerm->setAutoDefault(false);
    removeTerm->setAutoDefault(false);
    m_terms->setAccessibleName(tr("Groundwater source species terms"));
    m_terms->setAccessibleDescription(tr(
        "Each row adds one species concentration or mass-rate term. Native units are shown in the last column."));
    connect(addTerm, &QPushButton::clicked, this, &GroundwaterAssignDialog::addSpeciesTerm);
    connect(removeTerm, &QPushButton::clicked, this, [this] {
        if (m_terms->currentRow() >= 0) {
            m_terms->removeRow(m_terms->currentRow());
            refreshTermAccessibility();
            invalidatePreview();
        }
    });
    connect(m_terms, &QTableWidget::itemChanged, this, &GroundwaterAssignDialog::invalidatePreview);
    connect(m_qualityLayer, qOverload<int>(&QSpinBox::valueChanged), this,
            &GroundwaterAssignDialog::invalidatePreview);
    for (auto *combo : {m_species, m_zone, m_distribution, m_flowSeries})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
            updateTarget();
            sourceChanged();
        });
    m_source = new QComboBox(this);
    m_source->setObjectName("gwAssignmentSource");
    form->addRow(tr("Loaded &source:"), m_source);
    auto *pathRow = new QWidget(this);
    auto *pathLayout = new QHBoxLayout(pathRow);
    pathLayout->setContentsMargins(0, 0, 0, 0);
    m_path = new QLineEdit(pathRow);
    m_path->setObjectName("gwAssignmentPath");
    pathLayout->addWidget(m_path);
    auto *browse = new QPushButton(tr("Browse…"), pathRow);
    pathLayout->addWidget(browse);
    form->addRow(tr("Source &file:"), pathRow);
    if (auto *caption = qobject_cast<QLabel *>(form->labelForField(pathRow)))
        caption->setBuddy(m_path);
    m_path->setAccessibleName(tr("Source file"));
    m_path->setAccessibleDescription(
        tr("Feature or raster source file for the selected input method."));
    browse->setAutoDefault(false);
    browse->setAccessibleName(tr("Browse assignment source file"));
    m_layerName = new QLineEdit(this);
    form->addRow(tr("Vector layer name:"), m_layerName);
    m_field = new QLineEdit(this);
    m_field->setObjectName("gwAssignmentField");
    form->addRow(tr("Numeric field:"), m_field);
    m_filter = new QLineEdit(this);
    form->addRow(tr("Feature attribute filter:"), m_filter);
    m_selectedFeatures = new QCheckBox(tr("Use selected source features only"), this);
    form->addRow(m_selectedFeatures);
    m_band = new QSpinBox(this);
    m_band->setRange(1, 65535);
    form->addRow(tr("Raster band / time slice:"), m_band);
    m_sampling = new QComboBox(this);
    m_sampling->setObjectName("gwAssignmentRasterMeaning");
    m_sampling->addItem(tr("Per-cell value at centroid"), "cell-rate");
    m_sampling->addItem(tr("Water flux density (area integral)"), "flux-density");
    form->addRow(tr("Raster &values:"), m_sampling);
    m_cellSampling = new QComboBox(this);
    m_cellSampling->setObjectName("gwAssignmentCellSampling");
    m_cellSampling->addItem(tr("Value at the cell centroid"), "centroid");
    m_cellSampling->addItem(tr("Area-weighted mean over the cell"), "area");
    m_cellSampling->setToolTip(
        tr("For intensive values. Raster means use exact pixel–cell overlap and need the raster "
           "in the mesh's projected CRS; feature means weight each polygon by its overlap."));
    form->addRow(tr("Cell value from &source:"), m_cellSampling);
    connect(m_cellSampling, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GroundwaterAssignDialog::invalidatePreview);
    m_densityUnits = new QComboBox(this);
    m_densityUnits->setObjectName("gwAssignmentDensityUnits");
    m_densityUnits->addItem(tr("Choose density units…"), QString());
    for (const QString &unit : {QString("m/s"), QString("mm/h"), QString("m/day"), QString("in/h")})
        m_densityUnits->addItem(unit, unit);
    form->addRow(tr("Density &units:"), m_densityUnits);
    m_coverage = new QComboBox(this);
    m_coverage->setObjectName("gwAssignmentCoverage");
    m_coverage->addItem(tr("Require complete valid coverage"), "complete");
    m_coverage->addItem(tr("Integrate valid covered area only"), "valid-area");
    form->addRow(tr("Raster &coverage:"), m_coverage);
    m_sampling->setToolTip(tr(
        "Area integration uses original raster pixels and exact pixel–cell overlap in an equivalent projected CRS. Different projections and geographic CRSs must be prepared in the mesh CRS first."));
    m_densityUnits->setToolTip(tr(
        "Water flux per unit area. Positive adds water; negative withdraws it. Source scale and offset are applied to raw pixel values in these units before integration; raster metadata scale and offset are not applied automatically."));
    m_coverage->setToolTip(tr(
        "Complete coverage refuses missing pixels and NoData. Valid-area integration does not renormalize partial coverage; cells with no valid coverage are preserved."));
    m_scale = new QDoubleSpinBox(this);
    m_scale->setDecimals(12);
    m_scale->setRange(-1e15, 1e15);
    m_scale->setValue(1);
    form->addRow(tr("Source scale:"), m_scale);
    m_offset = new QDoubleSpinBox(this);
    m_offset->setDecimals(12);
    m_offset->setRange(-1e15, 1e15);
    form->addRow(tr("Source offset:"), m_offset);
    m_skip = new QCheckBox(
        tr("Preserve cells with NoData / no coverage (otherwise refuse the batch)"), this);
    form->addRow(m_skip);
    m_removeInfilOverrides = new QCheckBox(
        tr("Remove the reviewed explicit surface overrides that conflict with aquifer ownership"),
        this);
    m_removeInfilOverrides->setObjectName("gwOwnershipRemoveOverrides");
    m_removeInfilOverrides->setVisible(false);
    outer->addWidget(m_removeInfilOverrides);
    connect(m_removeInfilOverrides, &QCheckBox::toggled, this,
            &GroundwaterAssignDialog::invalidatePreview);
    m_status = new QLabel(
        tr("Choose a target and scope, then Preview. No model values change during preview."),
        this);
    m_status->setObjectName("gwAssignmentStatus");
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(60);
    m_status->setTextFormat(Qt::PlainText);
    outer->addWidget(m_status);
    m_table = new QTableWidget(0, 3, this);
    m_table->setObjectName("gwAssignmentPreview");
    m_table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    m_table->setSortingEnabled(true);
    m_table->setHorizontalHeaderLabels({tr("Cell"), tr("Old effective value"), tr("New value")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    outer->addWidget(m_table, 1);
    m_table->setAccessibleName(tr("Reviewed groundwater assignments"));
    m_table->setAccessibleDescription(tr(
        "Preview of affected cell numbers and reviewed values. Apply reviewed values commits this batch."));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_preview = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    m_preview->setObjectName("gwAssignmentPreviewButton");
    m_apply = buttons->addButton(tr("Apply reviewed values"), QDialogButtonBox::ApplyRole);
    m_apply->setObjectName("gwAssignmentApplyButton");
    m_apply->setEnabled(false);
    outer->addWidget(buttons);
    connect(m_preview, &QPushButton::clicked, this, &GroundwaterAssignDialog::preview);
    connect(m_apply, &QPushButton::clicked, this, &GroundwaterAssignDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &GroundwaterAssignDialog::reject);
    // Form buddies provide keyboard focus; explicit descriptions retain the
    // field identity where native combo accessibility uses the selected value.
    for (auto *layout : {form, transportForm, sourceForm, surfaceForm, strategyForm})
        for (int row = 0; row < layout->rowCount(); ++row) {
            auto *field = layout->itemAt(row, QFormLayout::FieldRole);
            auto *labelItem = layout->itemAt(row, QFormLayout::LabelRole);
            auto *editor = field ? field->widget() : nullptr;
            auto *caption = labelItem ? qobject_cast<QLabel *>(labelItem->widget()) : nullptr;
            if (!editor || !caption ||
                (!qobject_cast<QLineEdit *>(editor) && !qobject_cast<QComboBox *>(editor) &&
                 !qobject_cast<QAbstractSpinBox *>(editor)))
                continue;
            QString name = caption->text();
            name.remove('&');
            if (name.endsWith(':'))
                name.chop(1);
            editor->setAccessibleName(name);
            editor->setAccessibleDescription(name);
        }
    connect(m_target, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GroundwaterAssignDialog::updateTarget);
    connect(m_route, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        populateSources();
        sourceChanged();
    });
    connect(m_source, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GroundwaterAssignDialog::sourceChanged);
    connect(m_sampling, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GroundwaterAssignDialog::sourceChanged);
    for (auto *combo : {m_densityUnits, m_coverage})
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &GroundwaterAssignDialog::invalidatePreview);
    for (auto *edit : findChildren<QLineEdit *>())
        connect(edit, &QLineEdit::textChanged, this, &GroundwaterAssignDialog::invalidatePreview);
    for (auto *spin : findChildren<QDoubleSpinBox *>())
        connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                &GroundwaterAssignDialog::invalidatePreview);
    connect(m_scopeChoice, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GroundwaterAssignDialog::invalidatePreview);
    connect(m_band, qOverload<int>(&QSpinBox::valueChanged), this,
            &GroundwaterAssignDialog::invalidatePreview);
    connect(m_skip, &QCheckBox::toggled, this, &GroundwaterAssignDialog::invalidatePreview);
    connect(m_selectedFeatures, &QCheckBox::toggled, this,
            &GroundwaterAssignDialog::invalidatePreview);
    connect(browse, &QPushButton::clicked, this, [this] {
        QPointer<GroundwaterAssignDialog> self = this;
        const QString path = openswmmvis::ui::FileOpenDialog::getOpenFileName(
            QStringLiteral("assignment-sources"), this, tr("Assignment source"));
        if (!self || path.isEmpty() || !m_engine)
            return;
        m_source->setCurrentIndex(0);
        m_path->setText(path);
        invalidatePreview();
    });
    if (model) {
        connect(model, &SWMMModelLayer::engineAboutToClose, this,
                &GroundwaterAssignDialog::invalidateContext);
        connect(model, &QObject::destroyed, this, &GroundwaterAssignDialog::invalidateContext);
    }
    if (mesh)
        connect(mesh, &QObject::destroyed, this, &GroundwaterAssignDialog::invalidateContext);
    if (selection) {
        connect(selection, &SelectionManager::selectionChanged, this,
                &GroundwaterAssignDialog::invalidatePreview);
        connect(selection, &SelectionManager::selectionChanged, this,
                &GroundwaterAssignDialog::refreshSelectionCount);
    }
    if (units)
        connect(units, &UnitSystem::unitsChanged, this,
                &GroundwaterAssignDialog::invalidateContext);
    populateSources();
    updateTarget();
    sourceChanged();
    refreshSelectionCount();
    if (!model || !mesh || !canvas || !canvas->undoStack() || !m_engine)
        invalidateContext();
}
void GroundwaterAssignDialog::selectTarget(const QString &key)
{
    const int index = m_target->findData(key);
    if (index >= 0)
        m_target->setCurrentIndex(index);
}
void GroundwaterAssignDialog::refreshSelectionCount()
{
    int cells = 0;
    if (m_selection && m_mesh) {
        const auto key = mesh::MeshObjectRef::layerKey(m_mesh->sourcePath());
        for (const auto &ref : m_selection->selection()) {
            QString layer;
            int cell = -1;
            if (mesh::MeshObjectRef::parseCell(ref, &layer, &cell) && layer == key)
                ++cells;
        }
    }
    m_selectionCount->setText(
        tr("%n cell(s) selected on this mesh. Shift adds, Alt subtracts, Ctrl toggles.", nullptr,
           cells));
}
GroundwaterAssignDialog::~GroundwaterAssignDialog()
{
    if (m_watcher)
        m_watcher->cancel();
}
void GroundwaterAssignDialog::invalidateContext()
{
    m_engine = nullptr;
    ++m_serial;
    if (m_watcher)
        m_watcher->cancel();
    m_havePreview = false;
    setEnabled(false);
    QDialog::reject();
}
void GroundwaterAssignDialog::reject()
{
    m_haveProcess = false;
    m_haveOwnership = false;
    m_apply->setEnabled(false);
    if (m_watcher) {
        m_watcher->cancel();
        ++m_serial;
        m_status->setText(tr("Cancelling; the model remains unchanged."));
        return;
    }
    QDialog::reject();
}
void GroundwaterAssignDialog::invalidatePreview()
{
    m_haveProcess = false;
    m_haveOwnership = false;
    ++m_serial;
    m_havePreview = false;
    m_apply->setEnabled(false);
    if (m_watcher)
        m_watcher->cancel();
}
void GroundwaterAssignDialog::updateTarget()
{
    invalidatePreview();
    const QString key = m_target->currentData().toString();
    const bool quality = key == "INITIAL_QUALITY", source = key == "FLOW";
    const bool ownership = key == "INFILTRATION_OWNERSHIP", process = key == "ET_POLICY",
               review = ownership || process;
    m_removeInfilOverrides->setVisible(ownership);
    if (auto *form = findChild<QFormLayout *>("gwAssignmentForm"))
        for (int row = 2; row < form->rowCount(); ++row)
            form->setRowVisible(row, row == 2 ? process : !review);
    if (auto *intro = findChild<QLabel *>("gwAssignmentIntro"))
        intro->setVisible(!review);
    if (auto *fields = findChild<QScrollArea *>("gwAssignmentFields"))
        fields->setMaximumHeight(review ? (process ? 250 : 130) : QWIDGETSIZE_MAX);
    m_scopeChoice->setEnabled(!ownership);
    m_scopeText->setEnabled(!ownership);
    m_route->setEnabled(!ownership);
    m_value->setEnabled(!ownership);
    m_transportControls->setVisible(quality);
    m_sourceControls->setVisible(source);
    m_qualityLayer->setEnabled(quality && m_zone->currentData().toInt() == 2);
    const bool surface = key.startsWith(QLatin1String("SQ_")), coverage = key == "SQ_COVERAGE";
    m_surfaceControls->setVisible(surface);
    if (auto *surfaceForm = qobject_cast<QFormLayout *>(m_surfaceControls->layout())) {
        surfaceForm->setRowVisible(m_surfaceSpecies, key == "SQ_LOADING" || key == "SQ_INITIAL");
        surfaceForm->setRowVisible(m_landUse, coverage);
        surfaceForm->setRowVisible(m_classLookup, coverage);
    }
    // Aquifer properties and sources keep their Phase 35 default of exact per-cell rows;
    // surface and initial-quality rows default to Automatic (TAG when every value is equal).
    const bool aquifer = !surface && !quality && !source && !review;
    m_strategyControls->setVisible(surface || quality || source || aquifer);
    if (key != m_lastTarget) {
        m_lastTarget = key;
        const QSignalBlocker block(m_strategy);
        m_strategy->setCurrentIndex(m_strategy->findData(source || aquifer ? "CELL" : "AUTO"));
    }
    if (surface) {
        const QStringList names = key == "SQ_INITIAL" ? m_surfaceIqSpecies : m_loadingSpecies;
        const QString current = m_surfaceSpecies->currentData().toString();
        const QSignalBlocker block(m_surfaceSpecies);
        m_surfaceSpecies->clear();
        for (const QString &name : names)
            m_surfaceSpecies->addItem(name, name);
        m_surfaceSpecies->setCurrentIndex(std::max(0, m_surfaceSpecies->findData(current)));
    }
    if (process) {
        m_units->setText(tr(
            "One mesh-area atmospheric demand. Surface evaporation uses it first; soil ET uses the remainder with one stress factor. Capillary rise is internal transfer. Automatic resolves to Both and Two way for an active mesh aquifer. Subcatchment and LID area partitioning requires their own reviewed configuration."));
        return;
    }
    if (ownership) {
        m_units->setText(tr(
            "Whole-mesh ownership review. Capacities are computed by the aquifer; no second infiltration rate is assigned. Explicit conflicts block Apply until their removal is reviewed."));
        return;
    }
    const bool us = m_length == QLatin1String("ft");
    if (coverage) {
        m_units->setText(tr(
            "Percent of the cell's area per land use (0–100; a cell's percents add to at most 100). Polygon and raster sources give each class's share of the cell's area; the class lookup maps class values to land uses."));
    } else if (key == "SQ_LOADING") {
        m_units->setText(
            tr("Initial buildup: %1 (project units), divided among the cell's covered land uses.")
                .arg(us ? tr("lb/ac") : tr("kg/ha")));
    } else if (key == "SQ_CURB") {
        m_units->setText(tr("Curb length: %1 (project length units).").arg(m_length));
    } else if (key == "SQ_INITIAL") {
        m_units->setText(tr(
            "Initial concentration in the species' units (water age in hours, temperature in °C) in water standing on the cell at the start. This sets an initial value, not a sustained load."));
    } else if (source) {
        m_units->setText(tr(
            "Flow: m³/s, independent of project length units. Series values keep their authored times; preview lists only the currently authored constant or series reference."));
    } else if (quality) {
        QString unit;
        for (const auto &sp : m_speciesCatalog)
            if (sp.id == m_species->currentData().toString())
                unit = sp.nativeConcUnits;
        m_units->setText(
            unit.isEmpty()
                ? tr("Species units are unresolved. Initial quality cannot be assigned until a native unit mapping is available.")
                : tr("Initial concentration: %1. This sets an initial value; it does not supply a sustained water or mass flux.")
                      .arg(unit));
    } else
        for (const auto &t : aquiferTargets())
            if (t.key == key) {
                QString unit = t.unitKind;
                if (unit == "length")
                    unit = m_length;
                else if (unit == "rate")
                    unit = m_rate;
                else if (unit == "inverse-length")
                    unit = tr("1/%1").arg(m_length);
                m_units->setText(
                    tr("Units: %1. Values are authored in project units; source units are not inferred or converted.")
                        .arg(unit));
                break;
            }
    sourceChanged();
}
void GroundwaterAssignDialog::addSpeciesTerm()
{
    const int row = m_terms->rowCount();
    m_terms->insertRow(row);
    auto *species = new QComboBox(m_terms);
    for (const auto &sp : m_speciesCatalog)
        species->addItem(sp.id, sp.id);
    auto *kind = new QComboBox(m_terms);
    kind->addItems({"CONC", "MASS"});
    auto *series = new QComboBox(m_terms);
    series->addItem(tr("Constant"), QString());
    for (int i = 1; i < m_flowSeries->count(); ++i)
        series->addItem(m_flowSeries->itemText(i), m_flowSeries->itemData(i));
    m_terms->setCellWidget(row, 0, species);
    m_terms->setCellWidget(row, 1, kind);
    m_terms->setItem(row, 2, new QTableWidgetItem("0"));
    m_terms->setItem(row, 5, new QTableWidgetItem(QString()));
    m_terms->setCellWidget(row, 3, series);
    auto *unit = new QLabel(m_terms);
    m_terms->setCellWidget(row, 4, unit);
    const auto refresh = [this, species, kind, unit] {
        QString text = tr("unresolved");
        for (const auto &sp : m_speciesCatalog)
            if (sp.id == species->currentData().toString()) {
                const bool mass = kind->currentText() == "MASS";
                text = mass ? (sp.massSupported ? sp.nativeMassRateUnits : tr("unsupported"))
                            : sp.nativeConcUnits;
                break;
            }
        unit->setText(text);
        refreshTermAccessibility();
        invalidatePreview();
    };
    connect(species, qOverload<int>(&QComboBox::currentIndexChanged), this, refresh);
    connect(kind, qOverload<int>(&QComboBox::currentIndexChanged), this, refresh);
    connect(series, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &GroundwaterAssignDialog::invalidatePreview);
    refresh();
}
void GroundwaterAssignDialog::refreshTermAccessibility()
{
    for (int row = 0; row < m_terms->rowCount(); ++row)
        for (int column = 0; column < m_terms->columnCount(); ++column) {
            const QString name = tr("Species term row %1, %2")
                                     .arg(row + 1)
                                     .arg(m_terms->horizontalHeaderItem(column)->text());
            if (auto *editor = m_terms->cellWidget(row, column)) {
                editor->setAccessibleName(name);
                QString description = name;
                if (auto *unit = qobject_cast<QLabel *>(editor))
                    description += QStringLiteral(": ") + unit->text();
                editor->setAccessibleDescription(description);
            } else if (auto *item = m_terms->item(row, column))
                item->setData(Qt::AccessibleDescriptionRole, name);
        }
}
void GroundwaterAssignDialog::populateSources()
{
    m_source->clear();
    m_source->addItem(tr("Choose a file…"), QString());
    if (!m_canvas)
        return;
    for (auto *base : m_canvas->layers()) {
        if (m_route->currentIndex() == 1)
            if (auto *layer = qobject_cast<GISVectorLayer *>(base))
                m_source->addItem(layer->name(), layer->layerId());
        if (m_route->currentIndex() == 2)
            if (auto *layer = qobject_cast<GISRasterLayer *>(base))
                m_source->addItem(layer->name(), layer->layerId());
    }
}
void GroundwaterAssignDialog::sourceChanged()
{
    invalidatePreview();
    const bool manual = m_route->currentIndex() == 0, vector = m_route->currentIndex() == 1;
    m_value->setEnabled(manual && !(m_target->currentData() == "FLOW" &&
                                    !m_flowSeries->currentData().toString().isEmpty()));
    m_source->setEnabled(!manual);
    m_path->setEnabled(!manual);
    m_layerName->setEnabled(vector);
    m_field->setEnabled(vector);
    m_filter->setEnabled(vector);
    m_selectedFeatures->setEnabled(vector);
    m_band->setEnabled(!manual && !vector);
    m_scale->setEnabled(!manual);
    m_offset->setEnabled(!manual);
    const bool rasterFlow = m_route->currentIndex() == 2 && m_target->currentData() == "FLOW",
               flux = rasterFlow && m_sampling->currentData() == "flux-density";
    m_sampling->setEnabled(rasterFlow);
    m_densityUnits->setEnabled(flux);
    m_coverage->setEnabled(flux);
    m_skip->setEnabled(!flux);
    const QString key = m_target->currentData().toString();
    const bool coverage = key == "SQ_COVERAGE";
    m_cellSampling->setEnabled(!manual && !coverage && key != "FLOW");
    m_landUse->setEnabled(coverage && manual);
    m_classLookup->setEnabled(coverage && !manual);
    m_scale->setEnabled(!manual && !coverage);
    m_offset->setEnabled(!manual && !coverage);
    if (auto *form = findChild<QFormLayout *>("gwAssignmentForm"))
        if (auto *caption = qobject_cast<QLabel *>(form->labelForField(m_field)))
            caption->setText(coverage ? tr("Land-use class field:") : tr("Numeric field:"));
    m_field->setAccessibleName(coverage ? tr("Land-use class field") : tr("Numeric field"));
    if (!m_canvas)
        return;
    for (auto *base : m_canvas->layers())
        if (base->layerId() == m_source->currentData().toString()) {
            if (auto *layer = qobject_cast<GISVectorLayer *>(base)) {
                m_path->setText(layer->filePath());
                m_layerName->setText(layer->ogrLayerName());
            }
            if (auto *layer = qobject_cast<GISRasterLayer *>(base))
                m_path->setText(layer->filePath());
        }
}
QVector<int> GroundwaterAssignDialog::scope(QString *error) const
{
    if (m_target->currentData() == "INFILTRATION_OWNERSHIP" ||
        m_target->currentData() == "ET_POLICY") {
        QVector<int> cells;
        if (m_mesh)
            for (int i = 0; i < m_mesh->mesh().triangles.size(); ++i)
                cells.append(i);
        return cells;
    }
    QVector<int> cells;
    if (!m_mesh) {
        *error = tr("No mesh.");
        return cells;
    }
    const auto &m = m_mesh->mesh();
    const int mode = m_scopeChoice->currentIndex();
    QSet<int> chosen;
    if (mode == 0 && m_selection) {
        const auto key = mesh::MeshObjectRef::layerKey(m_mesh->sourcePath());
        for (const auto &ref : m_selection->selection()) {
            QString layer;
            int cell = -1;
            if (mesh::MeshObjectRef::parseCell(ref, &layer, &cell) && layer == key)
                chosen.insert(cell);
        }
    } else if (mode == 1 || mode == 2) {
        for (int i = 0; i < m.triangles.size(); ++i)
            if (mode == 1 || m.triangles[i].tag == m_scopeText->text())
                chosen.insert(i);
    } else if (mode == 3) {
        for (const auto &text : m_scopeText->text().split(',', Qt::SkipEmptyParts)) {
            bool ok = false;
            int n = text.trimmed().toInt(&ok);
            if (!ok || n < 1) {
                *error = tr("Enter 1-based cell numbers separated by commas.");
                return {};
            }
            chosen.insert(n - 1);
        }
    } else if (mode == 4) {
        OGRGeometry *g = nullptr;
        const auto bytes = m_scopeText->text().toUtf8();
        if (OGRGeometryFactory::createFromWkt(bytes.constData(), nullptr, &g) != OGRERR_NONE ||
            !g) {
            *error = tr("Enter valid polygon WKT in the mesh CRS.");
            return {};
        }
        std::unique_ptr<OGRGeometry, decltype(&OGRGeometryFactory::destroyGeometry)> polygon(
            g, &OGRGeometryFactory::destroyGeometry);
        if (wkbFlatten(g->getGeometryType()) != wkbPolygon || !g->IsValid() || g->IsEmpty()) {
            *error = tr("The scope must be a valid nonempty polygon; holes are allowed.");
            return {};
        }
        for (int i = 0; i < m.triangles.size(); ++i) {
            const auto p = mesh::cellGeom(m.vertices, m.triangles[i]).centroid;
            OGRPoint point(p.x(), p.y());
            if (g->Intersects(&point))
                chosen.insert(i);
        }
    }
    for (int cell : chosen)
        if (cell < 0 || cell >= m.triangles.size()) {
            *error = tr("A selected cell no longer exists.");
            return {};
        } else
            cells.append(cell);
    std::sort(cells.begin(), cells.end());
    if (cells.isEmpty())
        *error = tr("No mesh cells are in scope.");
    return cells;
}
bool GroundwaterAssignDialog::currentContext(QString *error) const
{
    if (!m_engine || !m_model || !m_mesh || m_model->engine() != m_engine) {
        *error = tr("The model is no longer available.");
        return false;
    }
    if (m_mesh->geomRevision() != m_geometry || m_mesh->attrRevision() != m_attributes ||
        m_mesh->sourcePath() != m_meshPath ||
        (m_mesh->srs() ? m_mesh->srs()->toWkt() : QString()) != m_meshCrs) {
        *error = tr("The mesh changed since preview. Preview again.");
        return false;
    }
    QString why;
    const auto selected = scope(&why);
    if (!why.isEmpty() || selected != m_scope) {
        *error = tr("The assignment scope changed. Preview again.");
        return false;
    }
    return true;
}
void GroundwaterAssignDialog::preview()
{
    if (m_watcher || !m_model || !m_mesh || !m_engine)
        return;
    invalidatePreview();
    QString error;
    if (m_target->currentData() == "ET_POLICY") {
        m_process = previewAquiferProcesses(m_engine, m_processEt->currentData().toString(),
                                            m_processLink->currentData().toString(),
                                            m_wiltingAuto->isChecked()
                                                ? QString("AUTO")
                                                : QString::number(m_wilting->value(), 'g', 17));
        m_geometry = m_mesh->geomRevision();
        m_attributes = m_mesh->attrRevision();
        m_scope = scope(&error);
        m_meshPath = m_mesh->sourcePath();
        m_meshCrs = m_mesh->srs() ? m_mesh->srs()->toWkt() : QString();
        m_haveProcess = true;
        const bool sorted = m_table->isSortingEnabled();
        m_table->setSortingEnabled(false);
        const auto guard = qScopeGuard([this, sorted] { m_table->setSortingEnabled(sorted); });
        m_table->setColumnCount(3);
        m_table->setHorizontalHeaderLabels(
            {tr("Cell"), tr("Before: authored / effective"), tr("After: authored / effective")});
        m_table->setRowCount(std::min(qsizetype(500), m_scope.size()));
        for (int i = 0; i < m_table->rowCount(); ++i) {
            m_table->setItem(i, 0, new NumericTableWidgetItem(QString::number(m_scope[i] + 1)));
            m_table->setItem(i, 1, new QTableWidgetItem(m_process.beforeEffective));
            m_table->setItem(i, 2, new QTableWidgetItem(m_process.afterEffective));
        }
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        for (int j = 1; j < 3; ++j)
            m_table->horizontalHeader()->setSectionResizeMode(j, QHeaderView::Stretch);
        m_table->resizeRowsToContents();
        m_status->setText(
            m_process.error.isEmpty()
                ? tr("Reviewed global settings for %1 cells, including implicit whole-mesh coverage. Wilting values use %2. Apply changes one shared configuration; Undo restores authored choices. %3")
                      .arg(m_scope.size())
                      .arg(m_length)
                      .arg(m_process.forcing)
                : m_process.error);
        m_apply->setEnabled(m_process.error.isEmpty() && m_process.changed);
        return;
    }
    if (m_target->currentData() == "INFILTRATION_OWNERSHIP") {
        QStringList tags;
        for (const auto &cell : m_mesh->mesh().triangles)
            tags.append(cell.tag);
        m_ownership =
            previewInfiltrationOwnership(m_engine, tags, m_removeInfilOverrides->isChecked());
        if (m_ownership.error.isEmpty() && !meshMatchesOwnership(m_mesh, m_ownership.before))
            m_ownership.error = tr(
                "Save pending mesh infiltration edits before reviewing a migration. The preview has changed no values.");
        m_geometry = m_mesh->geomRevision();
        m_attributes = m_mesh->attrRevision();
        m_scope = scope(&error);
        m_meshPath = m_mesh->sourcePath();
        m_meshCrs = m_mesh->srs() ? m_mesh->srs()->toWkt() : QString();
        m_haveOwnership = true;
        const bool sorted = m_table->isSortingEnabled();
        m_table->setSortingEnabled(false);
        const auto sortGuard = qScopeGuard([this, sorted] { m_table->setSortingEnabled(sorted); });
        m_table->setColumnCount(4);
        m_table->setHorizontalHeaderLabels(
            {tr("Cell"), tr("Owner"), tr("Inherited source"), tr("Reviewed action / reason")});
        m_table->setRowCount(std::min(qsizetype(500), m_ownership.cells.size()));
        for (int i = 0; i < m_table->rowCount(); ++i) {
            const auto &c = m_ownership.cells[i];
            m_table->setItem(i, 0, new NumericTableWidgetItem(QString::number(c.cell + 1)));
            m_table->setItem(i, 1,
                             new QTableWidgetItem(c.owner == 2   ? tr("Aquifer")
                                                  : c.owner == 0 ? tr("Process disabled")
                                                                 : tr("Surface bank")));
            m_table->setItem(i, 2, new QTableWidgetItem(c.source));
            m_table->setItem(i, 3, new QTableWidgetItem(c.reason));
            if (c.conflict)
                m_table->item(i, 3)->setBackground(QColor(255, 230, 190));
        }
        for (int column = 0; column < 3; ++column)
            m_table->horizontalHeader()->setSectionResizeMode(column,
                                                              QHeaderView::ResizeToContents);
        m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        m_table->resizeRowsToContents();
        m_status->setText(
            m_ownership.error.isEmpty()
                ? tr("Reviewed %1 cells. %2 Original rows are preserved by Undo; Cancel changes nothing.")
                      .arg(tags.size())
                      .arg(m_ownership.summaries.join("; "))
                : m_ownership.error);
        m_apply->setEnabled(m_ownership.error.isEmpty() && m_ownership.changed);
        return;
    }
    m_table->setColumnCount(3);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_haveOwnership = false;
    AquiferRequest request;
    GroundwaterTransportRequest transport;
    RowAssignmentRequest rowRequest;
    const QString key = m_target->currentData().toString();
    const bool surface = key.startsWith(QLatin1String("SQ_")), coverage = key == "SQ_COVERAGE";
    const bool gwQuality = key == "INITIAL_QUALITY";
    const QString strategy = m_strategy->currentData().toString();
    const bool isTransport = key == "FLOW" || gwQuality;
    bool isRows = surface || (gwQuality && strategy != "CELL");
    const int route = m_route->currentIndex();
    const bool fluxMode = key == "FLOW" && route == 2 && m_sampling->currentData() == "flux-density";
    const bool areaMean =
        route && !coverage && key != "FLOW" && m_cellSampling->currentData() == "area";
    const bool zonal = route == 2 && (coverage || areaMean); // exact pixel overlap
    const bool pointCapable = key == "FLOW" && route == 1;    // point layers: one source each
    if (fluxMode && m_densityUnits->currentData().toString().isEmpty()) {
        m_status->setText(tr("Choose explicit water flux density units before preview."));
        m_densityUnits->setFocus();
        return;
    }
    if (isTransport) {
        if (!readGroundwaterTransportSnapshot(m_engine, &transport.before, &error)) {
            m_status->setText(error);
            return;
        }
    } else if (!surface && !readAquiferSnapshot(m_engine, &request.before, &error)) {
        m_status->setText(error);
        return;
    }
    m_scope = scope(&error);
    if (!error.isEmpty()) {
        m_status->setText(error);
        return;
    }
    request.cells = m_scope;
    request.target = m_target->currentData().toString();
    request.skipNoData = fluxMode ? m_coverage->currentData() == "valid-area" : m_skip->isChecked();
    m_geometry = m_mesh->geomRevision();
    m_attributes = m_mesh->attrRevision();
    m_meshPath = m_mesh->sourcePath();
    m_meshCrs = m_mesh->srs() ? m_mesh->srs()->toWkt() : QString();
    const auto &meshData = m_mesh->mesh();
    MeshAssignmentSampling::Job job;
    job.strictCrs = true;
    job.rejectOverlaps = true;
    job.meshCrsWkt = m_meshCrs;
    job.triangles = m_scope;
    job.targetKeys = {request.target.toUtf8()};
    job.targetMin = {-std::numeric_limits<double>::max()};
    job.targetMax = {std::numeric_limits<double>::max()};
    QVector<double> areas;
    QVector<QVector<QPointF>> footprints;
    for (int cell : m_scope) {
        const auto &t = meshData.triangles[cell];
        request.cellTags.append(t.tag);
        job.centroids.append(mesh::cellGeom(meshData.vertices, t).centroid);
        areas.append(std::abs(mesh::cellSignedArea(meshData.vertices, t)));
        QVector<QPointF> polygon;
        for (int k = 0; k < t.vertexCount(); ++k) {
            const int vertex = t.vertex(k);
            if (vertex < 0 || vertex >= meshData.vertices.size()) {
                m_status->setText(tr("A selected cell has an invalid vertex."));
                return;
            }
            polygon.append(meshData.vertices[vertex].xy);
        }
        footprints.append(polygon);
        for (int k = 0; k < 4; ++k) // overlay footprints: a triangle repeats its first corner
            job.triVerts.append(polygon[k < polygon.size() ? k : 0]);
    }
    if (coverage) {
        job.mode = MeshAssignmentSampling::Mode::CategoryShare;
        job.keyField1 = m_field->text();
        job.sampling = MeshAssignmentSampling::Sampling::AreaWeightedMean;
    } else if (areaMean) {
        job.sampling = MeshAssignmentSampling::Sampling::AreaWeightedMean;
    }
    // Layer tags are what Save writes; TAG rows need them equal to the model's.
    QStringList layerTags;
    for (const auto &t : meshData.triangles)
        layerTags.append(t.tag);
    if (isTransport) {
        transport.cells = request.cells;
        transport.skipNoData = request.skipNoData;
        transport.target = key == "FLOW" ? GroundwaterTransportTarget::Source
                                         : GroundwaterTransportTarget::InitialQuality;
        transport.species = m_species->currentData().toString();
        transport.zone = m_zone->currentData().toInt();
        transport.layer = m_qualityLayer->value();
        transport.sourceName = m_sourceName->text().trimmed();
        transport.flowSeries = m_flowSeries->currentData().toString();
        transport.distribution = m_distribution->currentIndex() == 1
                                     ? GroundwaterSourceDistribution::RegionTotal
                                     : GroundwaterSourceDistribution::PerCell;
        if (gwQuality) {
            bool known = false;
            for (const auto &sp : m_speciesCatalog)
                if (sp.id == transport.species && !sp.nativeConcUnits.isEmpty())
                    known = true;
            if (!known) {
                m_status->setText(tr("Choose a species with verified native concentration units."));
                return;
            }
        }
        if (key == "FLOW") {
            if (route && (transport.distribution == GroundwaterSourceDistribution::RegionTotal ||
                          !transport.flowSeries.isEmpty())) {
                m_status->setText(tr(
                    "Region-total or time-series flow requires the manual input method. Use per-cell distribution with raster density integration; it already computes each cell’s m³/s without further area weighting."));
                return;
            }
            transport.areaWeights = areas;
            for (int row = 0; row < m_terms->rowCount(); ++row) {
                GroundwaterSpeciesTerm term;
                term.species = qobject_cast<QComboBox *>(m_terms->cellWidget(row, 0))
                                   ->currentData()
                                   .toString();
                term.kind = qobject_cast<QComboBox *>(m_terms->cellWidget(row, 1))->currentText();
                term.series = qobject_cast<QComboBox *>(m_terms->cellWidget(row, 3))
                                  ->currentData()
                                  .toString();
                bool ok = false;
                term.value = m_terms->item(row, 2)->text().toDouble(&ok);
                if (!ok || !std::isfinite(term.value)) {
                    m_status->setText(tr("Enter a finite constant for each species term."));
                    return;
                }
                transport.terms.append(term);
            }
            if (strategy == "TAG") {
                if (layerTags != transport.before.cellTags) {
                    m_status->setText(tr(
                        "One TAG source cannot be written: mesh tag edits since the last save are not in the model yet. Save first, or write CELL rows."));
                    return;
                }
                transport.tag = m_tagName->text().trimmed();
                for (int n = 1; transport.tag.isEmpty(); ++n)
                    if (!layerTags.contains(QStringLiteral("SOURCE_%1").arg(n)))
                        transport.tag = QStringLiteral("SOURCE_%1").arg(n);
                transport.allowRetag = m_allowRetag->isChecked();
            }
        }
    }
    QStringList strategyNotes;
    if (!isTransport && !surface && strategy != "CELL") {
        // Aquifer TAG rows resolve against the engine tags, which must match what Save writes.
        request.allCellTags = engineCellTags(m_engine, &error);
        if (!error.isEmpty() || request.allCellTags != layerTags) {
            if (strategy == "TAG") {
                m_status->setText(tr("One TAG row cannot be written: mesh tag edits since the "
                                     "last save are not in the model yet."));
                return;
            }
            error.clear();
            strategyNotes << tr("Automatic: one CELL override per cell, because mesh tag edits "
                                "since the last save are not in the model yet.");
        } else {
            request.strategy = strategy == "TAG" ? AquiferWriteStrategy::TagRow
                                                 : AquiferWriteStrategy::Automatic;
            request.tag = m_tagName->text().trimmed();
            request.allowRetag = m_allowRetag->isChecked();
        }
    }
    if (isRows) {
        using Section = Mesh2DSectionRowModel::Section;
        const Section section = coverage                ? Section::Coverages
                                : key == "SQ_LOADING"   ? Section::Loadings
                                : key == "SQ_CURB"      ? Section::CurbLength
                                : key == "SQ_INITIAL"   ? Section::SurfaceInitialQuality
                                                        : Section::GwInitialQuality;
        if (!readRowAssignmentSnapshot(m_engine, section, &rowRequest.before, &error)) {
            m_status->setText(error);
            return;
        }
        rowRequest.cells = m_scope;
        rowRequest.skipNoData = request.skipNoData;
        rowRequest.name =
            surface ? m_surfaceSpecies->currentData().toString() : transport.species;
        rowRequest.zone = gwQuality ? transport.zone : 0;
        rowRequest.strategy = strategy == "TAG"    ? RowWriteStrategy::TagRow
                              : strategy == "CELL" ? RowWriteStrategy::CellRows
                                                   : RowWriteStrategy::Automatic;
        rowRequest.tag = m_tagName->text().trimmed();
        rowRequest.allowRetag = m_allowRetag->isChecked();
        if (rowRequest.strategy != RowWriteStrategy::CellRows) {
            QString refuse;
            if (layerTags != rowRequest.before.cellTags)
                refuse = tr("mesh tag edits since the last save are not in the model yet");
            else if (gwQuality && !transport.before.qualityFile.isEmpty())
                refuse = tr("[GW_INITIAL_QUALITY] rows come from a FILE reference");
            else if (gwQuality && transport.zone == 2)
                refuse = tr("soil-layer values are written per cell");
            if (!refuse.isEmpty()) {
                if (rowRequest.strategy == RowWriteStrategy::TagRow) {
                    m_status->setText(tr("One TAG row cannot be written: %1.").arg(refuse));
                    return;
                }
                rowRequest.strategy = RowWriteStrategy::CellRows;
                strategyNotes << tr("Automatic: one CELL row per cell, because %1.").arg(refuse);
                isRows = !gwQuality; // groundwater CELL rows use the per-cell transport path
            } else {
                rowRequest.tagUsage = tagReferences(m_engine);
                for (const auto &row : meshData.infilDefaults)
                    if (!rowRequest.tagUsage.value(row.tag).contains("[2D_INFILTRATION_DEFAULTS]"))
                        rowRequest.tagUsage[row.tag] << QStringLiteral("[2D_INFILTRATION_DEFAULTS]");
            }
        }
    }
    QMap<QString, QString> lookup;
    QString manualLandUse;
    if (coverage) {
        if (route == 0) {
            manualLandUse = m_landUse->currentData().toString();
            if (manualLandUse.isEmpty()) {
                m_status->setText(tr("The model has no land uses; add them under [LANDUSES]."));
                return;
            }
        } else {
            lookup = parseClassLookup(m_classLookup->text(), rowRequest.before.landUses, &error);
            if (!error.isEmpty()) {
                m_status->setText(error);
                return;
            }
            if (route == 2 && lookup.isEmpty()) {
                m_status->setText(tr("Raster classes are numbers: enter a class lookup such as "
                                     "1=Urban, 2=Grass."));
                m_classLookup->setFocus();
                return;
            }
            if (route == 1 && m_field->text().trimmed().isEmpty()) {
                m_status->setText(tr("Choose the field that holds each polygon's land-use class."));
                return;
            }
        }
    }
    const QString path = m_path->text();
    job.source = route == 1 ? MeshAssignmentSampling::Source::Vector
                            : MeshAssignmentSampling::Source::Raster;
    job.rasterPath = path;
    job.vectorPath = path;
    job.vectorLayerName = m_layerName->text();
    job.fields = {m_field->text()};
    job.vectorFilterExpr = m_filter->text();
    job.bands = {m_band->value()};
    job.scale = m_scale->value();
    job.offset = m_offset->value();
    if (route && path.isEmpty()) {
        m_status->setText(tr("Choose a source file."));
        return;
    }
    if (m_canvas)
        for (auto *base : m_canvas->layers())
            if (base->layerId() == m_source->currentData().toString()) {
                QString loadedPath;
                if (auto *layer = qobject_cast<GISVectorLayer *>(base))
                    loadedPath = layer->filePath();
                if (auto *layer = qobject_cast<GISRasterLayer *>(base))
                    loadedPath = layer->filePath();
                if (QFileInfo(loadedPath).absoluteFilePath() != QFileInfo(path).absoluteFilePath())
                    continue;
                if (base->srs())
                    job.assignedSourceCrsWkt = base->srs()->toWkt();
                if (auto *layer = qobject_cast<GISVectorLayer *>(base)) {
                    job.filterBySelection = m_selectedFeatures->isChecked();
                    job.selectedIds = layer->selectedFeatureIds();
                }
            }
    if (route == 1 && m_selectedFeatures->isChecked() &&
        (!job.filterBySelection || job.selectedIds.isEmpty())) {
        m_status->setText(tr("Select source features in a loaded feature layer first."));
        return;
    }
    RasterFluxRequest fluxRequest;
    if (fluxMode) {
        fluxRequest.path = path;
        fluxRequest.band = m_band->value();
        fluxRequest.meshCrsWkt = m_meshCrs;
        fluxRequest.assignedSourceCrsWkt = job.assignedSourceCrsWkt;
        fluxRequest.cells = m_scope;
        fluxRequest.densityUnit = m_densityUnits->currentData().toString();
        fluxRequest.scale = job.scale;
        fluxRequest.offset = job.offset;
        fluxRequest.coverage =
            request.skipNoData ? RasterFluxCoverage::ValidAreaOnly : RasterFluxCoverage::Complete;
        fluxRequest.footprints = footprints;
    }
    RasterZonalRequest zonalRequest;
    if (zonal) {
        zonalRequest.path = path;
        zonalRequest.band = m_band->value();
        zonalRequest.meshCrsWkt = m_meshCrs;
        zonalRequest.assignedSourceCrsWkt = job.assignedSourceCrsWkt;
        zonalRequest.cells = m_scope;
        zonalRequest.footprints = footprints;
        zonalRequest.scale = job.scale;
        zonalRequest.offset = job.offset;
        zonalRequest.categorical = coverage;
    }
    PointSourceJob pointJob;
    if (pointCapable) {
        pointJob.path = path;
        pointJob.layerName = job.vectorLayerName;
        pointJob.filter = job.vectorFilterExpr;
        pointJob.assignedSourceCrsWkt = job.assignedSourceCrsWkt;
        pointJob.meshCrsWkt = m_meshCrs;
        pointJob.filterBySelection = job.filterBySelection && m_selectedFeatures->isChecked();
        pointJob.selectedIds = job.selectedIds;
        pointJob.nameField = m_pointName->text().trimmed();
        pointJob.flowField = m_field->text().trimmed();
        for (int row = 0; row < m_terms->rowCount(); ++row)
            pointJob.termFields << (m_terms->item(row, 5) ? m_terms->item(row, 5)->text().trimmed()
                                                          : QString());
        pointJob.scale = job.scale;
        pointJob.offset = job.offset;
        pointJob.vertices = meshData.vertices;
        pointJob.cells = meshData.triangles;
        for (int cell : m_scope)
            pointJob.scope.insert(cell);
        pointJob.byXy = m_pointPlacement->currentData() == "XY";
    }
    request.values.fill(m_value->value(), m_scope.size());
    QVector<QMap<QString, double>> manualPercents;
    if (coverage && route == 0)
        manualPercents.fill({{manualLandUse, m_value->value()}}, m_scope.size());
    m_recipe = {{"operationId", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                {"recordKind", "snapshot-assignment-audit"},
                {"version", 1},
                {"target", request.target},
                {"sourceRoute", route},
                {"sourcePath", path},
                {"layerName", job.vectorLayerName},
                {"field", m_field->text()},
                {"filter", job.vectorFilterExpr},
                {"band", m_band->value()},
                {"scale", job.scale},
                {"offset", job.offset},
                {"sampling", zonal       ? "pixel-cell-area-weighted"
                             : areaMean  ? "polygon-area-weighted"
                             : coverage  ? "polygon-area-share"
                                         : "centroid"},
                {"skipNoData", request.skipNoData},
                {"meshCrsWkt", m_meshCrs},
                {"assignedSourceCrsWkt", job.assignedSourceCrsWkt},
                {"constant", m_value->value()},
                {"scopeMode", m_scopeChoice->currentIndex()},
                {"scopeText", m_scopeText->text()},
                {"meshLayerId", m_mesh->layerId()},
                {"writeStrategy", strategy},
                {"tag", m_tagName->text().trimmed()},
                {"refreshPolicy", "snapshot-only"}};
    if (surface) {
        m_recipe["species"] = rowRequest.name;
        m_recipe["landUse"] = manualLandUse;
        m_recipe["classLookup"] = m_classLookup->text();
    }
    if (fluxMode) {
        m_recipe["sampling"] = "pixel-cell-area-integral";
        m_recipe["densityUnits"] = fluxRequest.densityUnit;
        m_recipe["coveragePolicy"] = m_coverage->currentData().toString();
        m_recipe["flowUnits"] = "m3/s";
        m_recipe["areaUnits"] = "m2";
        m_recipe["areaRelativeTolerance"] = 1e-10;
    }
    if (isTransport) {
        m_recipe["species"] = transport.species;
        m_recipe["zone"] = transport.zone;
        m_recipe["layer"] = transport.layer;
        m_recipe["distribution"] = int(transport.distribution);
        m_recipe["flowSeries"] = transport.flowSeries;
        m_recipe["sourceName"] = transport.sourceName;
        QJsonArray terms;
        for (int i = 0; i < transport.terms.size(); ++i) {
            const auto &t = transport.terms[i];
            terms.append(QJsonObject{{"species", t.species},
                                     {"kind", t.kind},
                                     {"series", t.series},
                                     {"value", t.value},
                                     {"field", pointJob.termFields.value(i)}});
        }
        m_recipe["terms"] = terms;
        if (pointCapable) {
            m_recipe["pointNameField"] = pointJob.nameField;
            m_recipe["pointPlacement"] = pointJob.byXy ? "XY" : "CELL";
        }
    }
    QJsonArray ids;
    for (int cell : m_scope)
        ids.append(cell);
    m_recipe["cells"] = ids;
    QJsonArray fids;
    for (auto id : job.selectedIds)
        fids.append(QString::number(id));
    m_recipe["featureIds"] = fids;
    const auto serial = m_serial;
    const bool us = m_length == QLatin1String("ft");
    const QString sourcePrefix = transport.sourceName.isEmpty() ? QStringLiteral("Assigned")
                                                                : transport.sourceName;
    m_preview->setEnabled(false);
    m_status->setText(tr("Preparing an immutable preview…"));
    m_watcher = new QFutureWatcher<WorkResult>(this);
    connect(m_watcher, &QFutureWatcher<WorkResult>::finished, this, [this, serial] {
        if (serial != m_serial) {
            m_apply->setEnabled(false);
            if (m_watcher)
                m_watcher->cancel();
        }
        finishPreview();
    });
    m_watcher->setFuture(QtConcurrent::run(
        [request, transport, rowRequest, isTransport, isRows, gwQuality, coverage, zonal,
         pointCapable, job, route, path, fluxMode, fluxRequest, zonalRequest, pointJob, lookup,
         manualPercents, areas, strategyNotes, us, sourcePrefix,
         key](QPromise<WorkResult> &promise) mutable {
            WorkResult result;
            const auto stop = [&] { return promise.isCanceled(); };
            QStringList notes = strategyNotes;
            QVector<QMap<QString, double>> percents = manualPercents;
            bool points = false;
            try {
                if (route) {
                    auto files = sourceFiles(path, &result.error);
                    if (result.error.isEmpty())
                        result.fingerprints = fingerprints(files, &result.error, stop);
                    if (result.error.isEmpty() && pointCapable) {
                        const auto sample = samplePointSources(pointJob, stop);
                        result.error = sample.error;
                        result.cancelled = sample.cancelled;
                        if (sample.pointLayer && result.error.isEmpty() && !result.cancelled) {
                            points = true;
                            transport.target = GroundwaterTransportTarget::PointSources;
                            for (const auto &f : sample.features) {
                                if (!f.problem.isEmpty()) {
                                    result.unmatched << QObject::tr("feature %1: %2")
                                                            .arg(f.fid)
                                                            .arg(f.problem);
                                    continue;
                                }
                                GroundwaterSourceRow row;
                                row.name = f.name.isEmpty()
                                               ? QStringLiteral("%1_pt%2").arg(sourcePrefix).arg(f.fid)
                                               : f.name;
                                row.cell = pointJob.byXy ? f.xyCell : f.cell;
                                row.flow = f.flow;
                                row.byXy = pointJob.byXy;
                                row.x = f.xy.x();
                                row.y = f.xy.y();
                                for (int k = 0; k < transport.terms.size(); ++k) {
                                    auto term = transport.terms[k];
                                    if (!std::isnan(f.termValues.value(k, std::nan(""))))
                                        term.value = f.termValues[k];
                                    row.terms.append(term);
                                }
                                if (f.sharedEdge)
                                    notes << QObject::tr("Feature %1 lies on a shared cell edge; "
                                                         "it is placed in cell %2.")
                                                 .arg(f.fid)
                                                 .arg(f.cell + 1);
                                if (pointJob.byXy && f.xyCell != f.cell)
                                    notes << QObject::tr("Feature %1 lies in cell %2; its XY row "
                                                         "resolves to the nearest centroid, cell "
                                                         "%3.")
                                                 .arg(f.fid)
                                                 .arg(f.cell + 1)
                                                 .arg(f.xyCell + 1);
                                transport.points.append(row);
                            }
                        }
                    }
                    if (result.error.isEmpty() && !result.cancelled && !points) {
                        if (fluxMode) {
                            result.isFlux = true;
                            result.flux = integrateRasterFlux(fluxRequest, stop);
                            result.error = result.flux.error;
                            result.cancelled = result.flux.cancelled;
                            request.values = result.flux.flows;
                        } else if (zonal) {
                            const auto z = integrateRasterZonal(zonalRequest, stop);
                            result.error = z.error;
                            result.cancelled = z.cancelled;
                            if (result.error.isEmpty() && !result.cancelled) {
                                request.values.fill(std::numeric_limits<double>::quiet_NaN(),
                                                    request.cells.size());
                                percents.fill({}, request.cells.size());
                                double mapped = 0, unmapped = 0, total = 0;
                                for (int i = 0; i < request.cells.size(); ++i) {
                                    total += z.cellAreas[i];
                                    if (!coverage) {
                                        if (z.validAreas[i] > 0)
                                            request.values[i] = z.means[i];
                                        continue;
                                    }
                                    QMap<QString, double> fractions;
                                    for (auto it = z.classAreas[i].cbegin();
                                         it != z.classAreas[i].cend(); ++it)
                                        fractions.insert(QString::number(it.key()),
                                                         it.value() / z.cellAreas[i]);
                                    double lost = 0;
                                    percents[i] = sharesToPercents(fractions, lookup,
                                                                   rowRequest.before.landUses,
                                                                   &lost);
                                    unmapped += lost * z.cellAreas[i];
                                    mapped += z.validAreas[i] - lost * z.cellAreas[i];
                                }
                                if (coverage && total > 0)
                                    notes << QObject::tr("Land-use shares: %1 % of the selected "
                                                         "area maps to land uses, %2 % lies in "
                                                         "classes without a land use, %3 % has "
                                                         "no raster value.")
                                                 .arg(displayNumber(100 * mapped / total),
                                                      displayNumber(100 * unmapped / total),
                                                      displayNumber(
                                                          100 * (total - mapped - unmapped) /
                                                          total));
                            }
                        } else {
                            const auto sampled = sampleMeshAssignment(job, stop);
                            result.error = sampled.error;
                            result.cancelled = sampled.cancelled;
                            request.values.fill(std::numeric_limits<double>::quiet_NaN(),
                                                request.cells.size());
                            percents.fill({}, request.cells.size());
                            if (sampled.skippedRange || sampled.skippedNonNumeric)
                                result.error = QObject::tr(
                                    "The source contains invalid numeric values; no assignment was prepared.");
                            double mapped = 0, unmapped = 0, total = 0;
                            for (double a : areas)
                                total += a;
                            if (result.error.isEmpty() && !result.cancelled)
                                for (int i = 0; i < sampled.triangles.size(); ++i) {
                                    const auto at = std::lower_bound(request.cells.begin(),
                                                                     request.cells.end(),
                                                                     sampled.triangles[i]);
                                    if (at == request.cells.end() || *at != sampled.triangles[i])
                                        continue;
                                    const int index = int(at - request.cells.begin());
                                    if (coverage) {
                                        double lost = 0, covered = 0;
                                        for (double f : sampled.shares[i])
                                            covered += f;
                                        percents[index] = sharesToPercents(
                                            sampled.shares[i], lookup, rowRequest.before.landUses,
                                            &lost);
                                        unmapped += lost * areas[index];
                                        mapped += (covered - lost) * areas[index];
                                    } else if (!sampled.values.isEmpty()) {
                                        request.values[index] =
                                            route == 1 ? sampled.values[0][i] * job.scale + job.offset
                                                       : sampled.values[0][i];
                                    }
                                }
                            if (coverage && total > 0)
                                notes << QObject::tr("Land-use shares: %1 % of the selected area "
                                                     "maps to land uses, %2 % lies in classes "
                                                     "without a land use, %3 % has no polygon.")
                                             .arg(displayNumber(100 * mapped / total),
                                                  displayNumber(100 * unmapped / total),
                                                  displayNumber(100 * (total - mapped - unmapped) /
                                                                total));
                        }
                    }
                    if (result.error.isEmpty() && !result.cancelled &&
                        fingerprints(files, &result.error, stop) != result.fingerprints &&
                        result.error.isEmpty())
                        result.error =
                            QObject::tr("The source changed while sampling; preview again.");
                }
                if (result.error.isEmpty() && !result.cancelled && !promise.isCanceled()) {
                    result.values = request.values;
                    if (points) {
                        result.isTransport = true;
                        result.transport = previewGroundwaterTransport(transport, stop);
                    } else if (isRows) {
                        rowRequest.values = request.values;
                        rowRequest.percents = percents;
                        result.rows = previewRowAssignment(rowRequest, stop);
                        result.isRows = true;
                        notes << result.rows.notes;
                        if (gwQuality && result.rows.error.isEmpty() && !result.rows.tagRow) {
                            // Automatic chose CELL rows: those use the per-cell transport path.
                            result.isRows = false;
                            result.isTransport = true;
                            notes = strategyNotes + result.rows.notes;
                            transport.values = request.values;
                            result.transport = previewGroundwaterTransport(transport, stop);
                        } else if (key == "SQ_LOADING" && result.rows.error.isEmpty()) {
                            double mass = 0;
                            for (int i = 0; i < request.cells.size(); ++i)
                                if (std::isfinite(request.values[i]) &&
                                    (result.rows.cells.contains(request.cells[i]) ||
                                     result.rows.unchangedCells.contains(request.cells[i])))
                                    mass += request.values[i] * areas[i] / (us ? 43560.0 : 1e4);
                            notes << QObject::tr("Integrated initial buildup over the assigned "
                                                 "cells: %1 %2.")
                                         .arg(displayNumber(mass), us ? QObject::tr("lb")
                                                                      : QObject::tr("kg"));
                        }
                    } else if (isTransport) {
                        result.isTransport = true;
                        transport.values = request.values;
                        result.transport = previewGroundwaterTransport(transport, stop);
                    } else {
                        result.preview = previewAquiferAssignment(request, stop);
                        notes << result.preview.notes;
                    }
                }
            } catch (const std::exception &e) {
                result.error = QString::fromUtf8(e.what());
            } catch (...) {
                result.error = QObject::tr("Assignment preview failed.");
            }
            if (!notes.isEmpty())
                result.summary += QStringLiteral(" ") + notes.join(QStringLiteral(" "));
            result.cancelled = result.cancelled || promise.isCanceled();
            promise.addResult(result);
        }));
}
void GroundwaterAssignDialog::finishPreview()
{
    auto *watcher = m_watcher;
    m_watcher = nullptr;
    m_preview->setEnabled(m_engine);
    if (!watcher)
        return;
    WorkResult result;
    try {
        if (watcher->isCanceled())
            result.cancelled = true;
        else
            result = watcher->result();
    } catch (...) {
        result.error = tr("Assignment worker failed.");
    }
    watcher->deleteLater();
    if (result.cancelled) {
        m_status->setText(tr("Cancelled; the model was not changed."));
        return;
    }
    QString error;
    if (!currentContext(&error)) {
        m_status->setText(error);
        return;
    }
    const QString previewError = result.isRows        ? result.rows.error
                                 : result.isTransport ? result.transport.error
                                                      : result.preview.error;
    if (!result.error.isEmpty() || !previewError.isEmpty()) {
        m_status->setText(result.error.isEmpty() ? previewError : result.error);
        return;
    }
    if (result.isFlux) {
        const auto &f = result.flux;
        m_recipe["selectedAreaM2"] = f.selectedArea;
        m_recipe["validAreaM2"] = f.validArea;
        m_recipe["uncoveredAreaM2"] = f.uncoveredArea;
        m_recipe["integratedFlowM3PerS"] = f.totalFlow;
        m_recipe["partialCells"] = f.partialCells;
        m_recipe["emptyCells"] = f.emptyCells;
        result.summary.prepend(
            tr(" Raster coverage: %1 m² valid, %2 m² uncovered; %3 partial cells. Integrated water flow: %4 m³/s.")
                .arg(openswmmvis::displayNumber(f.validArea),
                     openswmmvis::displayNumber(f.uncoveredArea))
                .arg(f.partialCells)
                .arg(openswmmvis::displayNumber(f.totalFlow)));
    }
    m_table->setSortingEnabled(false);
    const auto restoreSorting = qScopeGuard([this] { m_table->setSortingEnabled(true); });
    if (result.isRows) {
        m_reviewed = result;
        m_havePreview = true;
        const auto &p = result.rows;
        m_table->setHorizontalHeaderLabels(
            {tr("Cell"), tr("Old effective value"), tr("New value")});
        m_table->setRowCount(std::min(qsizetype(500), p.cells.size()));
        for (int i = 0; i < m_table->rowCount(); ++i) {
            m_table->setItem(i, 0, new NumericTableWidgetItem(QString::number(p.cells[i] + 1)));
            m_table->setItem(i, 1, new QTableWidgetItem(p.oldValues.value(i)));
            m_table->setItem(i, 2, new QTableWidgetItem(p.newValues.value(i)));
        }
        m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        for (int column = 1; column < 3; ++column) // coverage sets need the width
            m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Stretch);
        const QString section = Mesh2DSectionRowModel(p.before.section).sectionName();
        m_status->setText(
            tr("Preview %1: %2 cell(s) change, written as %3. %4 cell(s) already have this value; %5 cell(s) without a source value are preserved. The table shows up to 500 cells.%6")
                .arg(section)
                .arg(p.cells.size())
                .arg(p.tagRow ? tr("one TAG row for tag %1").arg(p.tag) : tr("CELL rows"))
                .arg(p.unchangedCells.size())
                .arg(p.skippedCells.size())
                .arg(result.summary));
        m_apply->setEnabled(!(p.before == p.after));
        return;
    }
    if (result.isTransport) {
        m_reviewed = result;
        m_havePreview = true;
        const auto &p = result.transport;
        m_table->setRowCount(std::min(qsizetype(500), p.cells.size()));
        m_table->setHorizontalHeaderLabels(
            {tr("Cell"), tr("Previous records"), tr("Reviewed value / series")});
        QHash<int, QStringList> sourceValues;
        const bool sources = p.target != GroundwaterTransportTarget::InitialQuality;
        if (sources)
            for (int i = p.before.sources.size(); i < p.after.sources.size(); ++i) {
                const auto &r = p.after.sources[i];
                const QString flow =
                    (r.series.isEmpty() ? openswmmvis::displayNumber(r.flow) : r.series) +
                    tr(" × %1").arg(openswmmvis::displayNumber(r.scale));
                if (r.scope == 1) { // one TAG source: the engine area-weights its total
                    for (int cell : p.cells)
                        sourceValues[cell] << tr("TAG %1 source %2: %3 m³/s total, area-weighted")
                                                  .arg(r.tag, r.name, flow);
                } else if (p.target == GroundwaterTransportTarget::PointSources) {
                    sourceValues[r.cell] << tr("%1%2: %3 m³/s")
                                                .arg(r.name, r.byXy ? tr(" (XY)") : QString(), flow);
                } else {
                    sourceValues[r.cell] << flow;
                }
            }
        for (int i = 0; i < m_table->rowCount(); ++i) {
            const int cell = p.cells[i];
            const int at =
                int(std::lower_bound(m_scope.begin(), m_scope.end(), cell) - m_scope.begin());
            QString value = at < result.values.size()
                                ? openswmmvis::displayNumber(result.values[at])
                                : QString();
            if (sources)
                value = sourceValues.value(cell).join(QStringLiteral("; "));
            if (result.isFlux && at < result.flux.validAreas.size())
                value += tr(" m³/s; %1 m² valid, %2 m² uncovered")
                             .arg(openswmmvis::displayNumber(result.flux.validAreas[at]),
                                  openswmmvis::displayNumber(result.flux.uncoveredAreas[at]));
            m_table->setItem(
                i, 0, new openswmmvis::ui::NumericTableWidgetItem(QString::number(cell + 1)));
            m_table->setItem(
                i, 1,
                new openswmmvis::ui::NumericTableWidgetItem(tr("Preserved / checked upsert")));
            m_table->setItem(i, 2, new openswmmvis::ui::NumericTableWidgetItem(value));
        }
        QString totals;
        if (sources) {
            double flow = 0;
            bool series = false;
            QMap<QString, double> mass;
            for (int i = p.before.sources.size(); i < p.after.sources.size(); ++i) {
                const auto &r = p.after.sources[i];
                flow += r.flow * r.scale;
                series |= !r.series.isEmpty();
                for (const auto &t : r.terms)
                    if (t.kind == QLatin1String("MASS")) {
                        mass[t.species] += t.value * r.scale;
                        series |= !t.series.isEmpty();
                    }
            }
            totals =
                series
                    ? tr(" Flow series and area scales are preserved; no single instantaneous total is implied.")
                    : tr(" Total authored constant flow: %1 m³/s.")
                          .arg(openswmmvis::displayNumber(flow));
            if (!series)
                for (auto it = mass.cbegin(); it != mass.cend(); ++it)
                    totals += tr(" Total %1 MASS rate: %2 (species mass units per second).")
                                  .arg(it.key(), openswmmvis::displayNumber(it.value()));
            // A region total is split by normalized area weights; check the split sums back.
            const bool regionTotal = p.target == GroundwaterTransportTarget::Source &&
                                     !result.values.isEmpty() && !series &&
                                     p.after.sources.size() > p.before.sources.size() &&
                                     m_distribution->currentIndex() == 1;
            if (regionTotal) {
                const double requested = result.values.first();
                totals += std::abs(flow - requested) <= 1e-12 * std::max(1.0, std::abs(requested))
                              ? tr(" The split sums to the requested total.")
                              : tr(" Warning: the split sums to %1, not the requested %2.")
                                    .arg(openswmmvis::displayNumber(flow),
                                         openswmmvis::displayNumber(requested));
            }
            if (!result.unmatched.isEmpty())
                totals += tr(" %n point feature(s) not assigned: %1.", nullptr,
                             result.unmatched.size())
                              .arg(result.unmatched.mid(0, 10).join(QStringLiteral("; ")) +
                                   (result.unmatched.size() > 10 ? tr("; …") : QString()));
            const int retagged = [&] {
                int n = 0;
                for (int c = 0; c < p.after.cellTags.size() && c < p.before.cellTags.size(); ++c)
                    n += p.after.cellTags[c] != p.before.cellTags[c];
                return n;
            }();
            if (retagged)
                totals += tr(" %n cell(s) are tagged for the TAG source.", nullptr, retagged);
        }
        m_status->setText(
            tr("Preview: %1 cells; %2 NoData cells preserved. Existing unrelated transport records are retained.%3")
                .arg(p.cells.size())
                .arg(p.skippedCells.size())
                .arg(totals + result.summary));
        m_apply->setEnabled(p.before != p.after);
        return;
    }
    m_table->setHorizontalHeaderLabels({tr("Cell"), tr("Old effective value"), tr("New value")});
    m_reviewed = result;
    m_havePreview = true;
    m_table->setRowCount(std::min(qsizetype(500), result.preview.cells.size()));
    for (int i = 0; i < m_table->rowCount(); ++i) {
        m_table->setItem(i, 0,
                         new openswmmvis::ui::NumericTableWidgetItem(
                             QString::number(result.preview.cells[i] + 1)));
        m_table->setItem(i, 1,
                         new openswmmvis::ui::NumericTableWidgetItem(
                             openswmmvis::displayNumber(result.preview.oldValues[i])));
        m_table->setItem(i, 2,
                         new openswmmvis::ui::NumericTableWidgetItem(
                             openswmmvis::displayNumber(result.preview.newValues[i])));
    }
    m_status->setText(
        (result.preview.tagsAfter.isEmpty()
             ? tr("Preview: %1 changed cells; %2 cells preserved for NoData / no coverage. Original aquifer rows and mesh tags are retained. The table shows up to 500 changed cells. This property/initial-state assignment adds no sustained water or mass rate.")
             : tr("Preview: %1 cells; %2 cells preserved for NoData / no coverage. Original aquifer rows are retained; one TAG row is appended. This property/initial-state assignment adds no sustained water or mass rate."))
            .arg(result.preview.cells.size())
            .arg(result.preview.skippedCells.size()) +
        result.summary);
    m_apply->setEnabled(!result.preview.appended.isEmpty());
}
void GroundwaterAssignDialog::apply()
{
    if (m_haveProcess) {
        QString error;
        if (!m_canvas || !m_canvas->undoStack() || !currentContext(&error) ||
            !m_process.error.isEmpty()) {
            m_status->setText(error);
            return;
        }
        m_committing = true;
        const auto guard = qScopeGuard([this] { m_committing = false; });
        if (!applyAquiferProcesses(m_engine, m_process.before, m_process.after,
                                   m_process.groundwater, &error)) {
            m_status->setText(error);
            invalidatePreview();
            return;
        }
        m_canvas->undoStack()->push(new ProcessUndo(m_model, m_mesh, m_process));
        m_model->markEdited();
        emit m_model->optionsChanged({"GW_ET", "LINK_SEEPAGE", "WILTING_SUCTION"});
        m_mesh->refreshInfiltrationOwnership();
        m_status->setText(
            tr("Reviewed ET and seepage settings applied as one undoable operation."));
        m_haveProcess = false;
        m_apply->setEnabled(false);
        emit applied();
        return;
    }

    if (m_haveOwnership) {
        QString error;
        if (!m_canvas || !m_canvas->undoStack() || !currentContext(&error) ||
            !m_ownership.error.isEmpty()) {
            m_status->setText(error);
            return;
        }
        m_committing = true;
        const auto guard = qScopeGuard([this] { m_committing = false; });
        if (!applyInfiltrationOwnership(m_engine, m_ownership.before, m_ownership.after, &error)) {
            m_status->setText(error);
            invalidatePreview();
            return;
        }
        installOwnershipRows(m_mesh, m_ownership.after);
        m_canvas->undoStack()->push(new OwnershipUndo(m_model, m_mesh, m_ownership));
        m_model->markEdited();
        m_status->setText(
            tr("Reviewed ownership migration applied. Undo restores the original configuration."));
        m_haveOwnership = false;
        m_apply->setEnabled(false);
        emit applied();
        return;
    }
    if (!m_havePreview || m_watcher)
        return;
    QString error;
    if (!currentContext(&error)) {
        m_status->setText(error);
        invalidatePreview();
        return;
    }
    if (m_reviewed.fingerprints.isEmpty()) {
        commitVerified();
        return;
    }
    const auto expected = m_reviewed.fingerprints;
    const auto serial = m_serial;
    m_apply->setEnabled(false);
    m_preview->setEnabled(false);
    m_status->setText(tr("Checking that source files still match the reviewed values…"));
    m_watcher = new QFutureWatcher<WorkResult>(this);
    connect(m_watcher, &QFutureWatcher<WorkResult>::finished, this, [this, serial, expected] {
        auto *w = m_watcher;
        m_watcher = nullptr;
        m_preview->setEnabled(m_engine);
        WorkResult r;
        try {
            if (w->isCanceled())
                r.cancelled = true;
            else
                r = w->result();
        } catch (...) {
            r.error = tr("Source verification failed.");
        }
        w->deleteLater();
        if (serial != m_serial || r.cancelled) {
            m_status->setText(tr("Assignment cancelled or edited; preview again."));
            return;
        }
        if (!r.error.isEmpty() || r.fingerprints != expected) {
            m_status->setText(r.error.isEmpty()
                                  ? tr("Source files changed since preview; preview again.")
                                  : r.error);
            invalidatePreview();
            return;
        }
        commitVerified();
    });
    m_watcher->setFuture(QtConcurrent::run([expected](QPromise<WorkResult> &p) {
        WorkResult r;
        r.fingerprints = fingerprints(expected.keys(), &r.error, [&] { return p.isCanceled(); });
        r.cancelled = p.isCanceled();
        p.addResult(r);
    }));
}
void GroundwaterAssignDialog::commitVerified()
{
    QString error;
    if (!m_havePreview || !currentContext(&error)) {
        m_status->setText(error);
        invalidatePreview();
        return;
    }
    if (!m_canvas || !m_canvas->undoStack()) {
        m_status->setText(tr("The undo owner is no longer available."));
        invalidatePreview();
        return;
    }
    if (m_reviewed.isRows || m_reviewed.isTransport) {
        // Both report the same success / changed / rollbackComplete contract.
        bool success = false, changed = false, rollbackComplete = true;
        if (m_reviewed.isRows) {
            const auto result = applyRowAssignment(m_engine, m_reviewed.rows);
            success = result.success;
            changed = result.changed;
            rollbackComplete = result.rollbackComplete;
            error = result.error;
        } else {
            const auto result = applyGroundwaterTransport(m_engine, m_reviewed.transport);
            success = result.success;
            changed = result.changed;
            rollbackComplete = result.rollbackComplete;
            error = result.error;
        }
        if (!success) {
            if (changed && m_events) {
                auto event = m_recipe;
                event["event"] = "failed-after-write";
                event["error"] = error;
                event["rollbackComplete"] = rollbackComplete;
                emit m_events->changed(event, true);
                emit applied();
            }
            m_status->setText(error);
            invalidatePreview();
            return;
        }
        // The mesh layer owns the tags that Save writes; follow the engine's.
        if (m_reviewed.isRows)
            installTags(m_mesh, m_reviewed.rows.before.cellTags, m_reviewed.rows.after.cellTags);
        else
            installTags(m_mesh, m_reviewed.transport.before.cellTags,
                        m_reviewed.transport.after.cellTags);
    } else if (!applyAquiferPreview(m_engine, m_reviewed.preview, &error)) {
        m_status->setText(error);
        invalidatePreview();
        return;
    } else {
        installTags(m_mesh, m_reviewed.preview.tagsBefore, m_reviewed.preview.tagsAfter);
    }
    QJsonObject hashes;
    for (auto it = m_reviewed.fingerprints.begin(); it != m_reviewed.fingerprints.end(); ++it)
        hashes[it.key()] = QString::fromLatin1(it.value().toHex());
    m_recipe["sourceSha256"] = hashes;
    QPointer<GroundwaterAssignmentEvents> events = m_events;
    const auto recipe = m_recipe;
    const auto changed = [events, recipe,
                          mesh = QPointer<SWMM2DMeshLayer>(m_mesh)](bool installed) {
        if (mesh)
            mesh->refreshInfiltrationOwnership();
        if (events) {
            auto event = recipe;
            event["event"] = installed ? "redone" : "undone";
            emit events->changed(event, installed);
        }
    };
    const auto partial = [events, recipe](const QString &error) {
        if (events) {
            auto event = recipe;
            event["event"] = "failed-after-write";
            event["error"] = error;
            emit events->changed(event, true);
        }
    };
    AssignmentUndo *command =
        m_reviewed.isRows ? new AssignmentUndo(m_model, m_mesh, m_reviewed.rows, changed, partial)
        : m_reviewed.isTransport
            ? new AssignmentUndo(m_model, m_mesh, m_reviewed.transport, changed, partial)
            : new AssignmentUndo(m_model, m_mesh, m_reviewed.preview, changed);
    m_canvas->undoStack()->push(command);
    m_mesh->refreshInfiltrationOwnership();
    m_model->markEdited(); // refreshes groundwater markers and other engine-derived views
    if (events) {
        auto event = recipe;
        event["event"] = "applied";
        emit events->changed(event, true);
    }
    emit applied();
    emit recipeAccepted(m_recipe);
    m_status->setText(tr("Applied %1 cell overrides as one undoable operation.")
                          .arg(m_reviewed.isRows        ? m_reviewed.rows.cells.size()
                               : m_reviewed.isTransport ? m_reviewed.transport.cells.size()
                                                        : m_reviewed.preview.cells.size()));
    invalidatePreview();
}
} // namespace
