// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshsnowdialog.cpp
 * @brief     Configuration of snow packs and forcing for mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/meshsnowdialog.h"
#include "core/displaynumber.h"
#include "ui/models/meshsnowmodel.h"
#include "assignment/meshassignmentsampling.h"
#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "map/spatialreferencesystem.h"
#include "mesh/meshcellgeom.h"
#include "ui/dialogs/snowpackeditordialog.h"
#include "snowpack/snowpackregistry.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_model.h>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QHash>
#include <QSet>
#include <QStyledItemDelegate>
#include <QSignalBlocker>
#include <QTableView>
#include <QTableWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QCheckBox>
#include <QFutureWatcher>
#include <QProgressDialog>
#include <QEventLoop>
#include <QtConcurrent/QtConcurrentRun>
#include <QVBoxLayout>
#include <QUndoCommand>
#include <algorithm>
#include <cmath>
namespace openswmmvis::ui
{
namespace
{
class PackDelegate final : public QStyledItemDelegate
{
    MeshSnowModel *model_;

public:
    /**
     * @brief Bind a pack-choice delegate to the dialog's snow row model.
     * @param[in] model Non-null borrowed MeshSnowModel whose pack catalog must outlive the
     * delegate.
     * @param[in] parent Optional Qt owner; normally the assignment table.
     */
    PackDelegate(MeshSnowModel *model, QObject *parent) : QStyledItemDelegate(parent), model_(model)
    {
    }
    /**
     * @brief Create a pack combo from the current borrowed model catalog.
     * @param[in] p Optional Qt parent taking ownership of the new editor.
     * @return Newly allocated QComboBox containing copied pack labels. The unnamed style option
     * and index arguments are ignored; model_ must remain non-null.
     */
    QWidget *createEditor(QWidget *p, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *c = new QComboBox(p);
        c->addItems(model_->packs);
        return c;
    }
    /**
     * @brief Hydrate the pack combo from one row's edit-role text.
     * @param[in,out] w Non-null editor created by createEditor(); must be a QComboBox, enforced
     * only by static_cast.
     * @param[in] i Borrowed model index whose Qt::EditRole text becomes the combo's current
     * text.
     * @details Does not write model data and does not block editor signals.
     */
    void setEditorData(QWidget *w, const QModelIndex &i) const override
    {
        static_cast<QComboBox *>(w)->setCurrentText(i.data(Qt::EditRole).toString());
    }
    /**
     * @brief Write a pack combo's selected text to its target model cell.
     * @param[in] w Non-null QComboBox editor created by this delegate.
     * @param[in,out] m Non-null borrowed target model receiving setData() with its default edit
     * role.
     * @param[in] i Target index passed through without extra validity checks.
     * @details Does not retain arguments or inspect the model's boolean success result.
     */
    void setModelData(QWidget *w, QAbstractItemModel *m, const QModelIndex &i) const override
    {
        m->setData(i, static_cast<QComboBox *>(w)->currentText());
    }
};
class SnowUndo final : public QUndoCommand
{
    QPointer<SWMMModelLayer> model_;
    QPointer<SWMM2DMeshLayer> mesh_;
    SWMM_Engine engine_;
    quint64 geometry_;
    std::array<QString, 3> before_, after_;
    bool first_ = true;
    /**
     * @brief Restore one owned snow snapshot only if the expected opposite state still exists.
     * @param[in] after True installs after_, requiring before_ currently; false installs
     * before_, requiring after_ currently.
     * @details Rechecks guarded model/mesh, captured engine identity and geometry revision, then
     * compares all current section text before applying. Failure makes the command obsolete and
     * shows a warning; success marks the model dirty and emits optionsChanged. Does not merge
     * externally changed assignments or refresh the captured baseline.
     */
    void install(bool after)
    {
        std::array<QString, 3> current;
        QString error;
        if (!model_ || !mesh_ || model_->engine() != engine_ || mesh_->geomRevision() != geometry_)
            error = QObject::tr("The model or mesh has changed.");
        else {
            error = MeshSnowModel::readSections(engine_, current);
            if (error.isEmpty() && current != (after ? before_ : after_))
                error = QObject::tr("Snow assignments changed outside this undo operation.");
            if (error.isEmpty())
                error = MeshSnowModel::applySections(engine_, after ? after_ : before_);
        }
        if (!error.isEmpty()) {
            setObsolete(true);
            QMessageBox::warning(QApplication::activeWindow(), QObject::tr("Mesh snow"), error);
            return;
        }
        model_->markEdited();
        emit model_->optionsChanged({"2D_SNOW"});
    }

public:
    /**
     * @brief Own before/after snow configuration text and guard the originating context.
     * @param[in] m Non-null borrowed model; its current engine identity is captured and guarded
     * by QPointer.
     * @param[in] mesh Non-null borrowed mesh; its geometry revision is captured and guarded.
     * @param[in] before Owned three-section snapshot before the already-applied edit, moved into
     * the command.
     * @param[in] after Owned three-section snapshot after the edit, moved into the command.
     * @details Allocated for an undo stack after successful installation; first redo
     * intentionally does nothing. Section order follows MeshSnowModel::readSections().
     */
    SnowUndo(SWMMModelLayer *m, SWMM2DMeshLayer *mesh, std::array<QString, 3> before,
             std::array<QString, 3> after)
        : model_(m), mesh_(mesh), engine_(m->engine()), geometry_(mesh->geomRevision()),
          before_(std::move(before)), after_(std::move(after))
    {
        setText(QObject::tr("Assign mesh snow"));
    }
    /**
     * @brief Restore the owned before snapshot through guarded install(false).
     * @details Context or concurrency failure marks the command obsolete instead of overwriting
     * unrelated changes.
     */
    void undo() override { install(false); }
    /**
     * @brief Skip the stack's initial redo, then restore the owned after snapshot on later
     * redos.
     * @details The initial edit was installed before push(); subsequent calls use the same
     * guarded install(true) checks as undo.
     */
    void redo() override
    {
        if (first_) {
            first_ = false;
            return;
        }
        install(true);
    }
};
}
MeshSnowDialog::MeshSnowDialog(SWMMModelLayer *model, SWMM2DMeshLayer *mesh, MapCanvas *canvas,
                               SelectionManager *selection, QWidget *parent)
    : QDialog(parent), model_(model), mesh_(mesh), canvas_(canvas), selection_(selection),
      engine_(model ? model->engine() : nullptr), geometry_(mesh ? mesh->geomRevision() : 0)
{
    setWindowTitle(tr("Mesh snow assignment"));
    resize(980, 700);
    setObjectName("meshSnowDialog");
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    mode_ = new QComboBox(this);
    mode_->setObjectName("meshSnowMode");
    mode_->addItems({tr("Automatic — enabled when assigned"), tr("Enabled"), tr("Disabled")});
    form->addRow(tr("Snowmelt:"), mode_);
    step_ = new QDoubleSpinBox(this);
    step_->setObjectName("meshSnowStep");
    step_->setRange(0, 3600);
    step_->setDecimals(6);
    step_->setSpecialValueText(tr("Use global snow step"));
    step_->setSuffix(tr(" s"));
    form->addRow(tr("UEB update interval:"), step_);
    layout->addLayout(form);
    auto *note = new QLabel(
        tr("Cell override → region tag → whole-mesh default. Blank site parameters inherit; slope and aspect come from cell geometry unless both are set. Shared snowpacks retain their subcatchment assignments."),
        this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *controls = new QHBoxLayout;
    scope_ = new QComboBox(this);
    scope_->setObjectName("meshSnowScope");
    scope_->addItems({tr("Selected cells"), tr("Region tag"), tr("Whole mesh")});
    scope_->setAccessibleName(tr("Assignment scope"));
    controls->addWidget(scope_);
    tag_ = new QComboBox(this);
    if (mesh) {
        QStringList tags;
        for (const auto &cell : mesh->mesh().triangles)
            if (!cell.tag.isEmpty() && !tags.contains(cell.tag))
                tags.append(cell.tag);
        tags.sort();
        tag_->addItems(tags);
    }
    tag_->setAccessibleName(tr("Region tag"));
    controls->addWidget(tag_);
    pack_ = new QComboBox(this);
    pack_->setObjectName("meshSnowPack");
    pack_->setAccessibleName(tr("Shared snowpack"));
    controls->addWidget(pack_);
    auto *add = new QPushButton(tr("Add / replace assignment"), this);
    controls->addWidget(add);
    connect(add, &QPushButton::clicked, this, &MeshSnowDialog::addAssignment);
    auto *edit = new QPushButton(tr("Snowpacks…"), this);
    controls->addWidget(edit);
    connect(edit, &QPushButton::clicked, this, &MeshSnowDialog::editPacks);
    layout->addLayout(controls);
    rows_ = new MeshSnowModel(this);
    table_ = new QTableView(this);
    table_->setObjectName("meshSnowAssignments");
    table_->setModel(rows_);
    table_->setItemDelegateForColumn(2, new PackDelegate(rows_, table_));
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    layout->addWidget(table_, 2);
    auto *remove = new QPushButton(tr("Remove selected assignments"), this);
    layout->addWidget(remove);
    connect(remove, &QPushButton::clicked, this, [this] {
        auto selected = table_->selectionModel()->selectedRows();
        std::sort(selected.begin(), selected.end(),
                  [](auto a, auto b) { return a.row() > b.row(); });
        for (auto i : selected)
            rows_->removeRow(i.row());
        preview();
    });
    auto *fill = new QPushButton(tr("Fill from raster / layer field…"), this);
    fill->setObjectName("meshSnowFillFromLayer");
    fill->setAutoDefault(false);
    layout->addWidget(fill);
    connect(fill, &QPushButton::clicked, this, &MeshSnowDialog::fillFromLayer);
    auto *previewControls = new QHBoxLayout;
    auto *previewFilter = new QComboBox(this);
    previewFilter->setObjectName("meshSnowPreviewFilter");
    previewFilter->addItems(
        {tr("First cells"), tr("Selected cells"), tr("Assigned cells"), tr("Inspect cell number")});
    previewFilter->setAccessibleName(tr("Preview filter"));
    previewControls->addWidget(previewFilter);
    auto *previewCell = new QSpinBox(this);
    previewCell->setObjectName("meshSnowPreviewCell");
    previewCell->setRange(1, mesh ? std::max(1, int(mesh->mesh().triangles.size())) : 1);
    previewCell->setAccessibleName(tr("Cell number to inspect"));
    previewControls->addWidget(previewCell);
    auto *highlight = new QPushButton(tr("Highlight preview cell on map"), this);
    highlight->setAutoDefault(false);
    previewControls->addWidget(highlight);
    layout->addLayout(previewControls);
    connect(previewFilter, &QComboBox::currentIndexChanged, this, [this, previewCell](int index) {
        previewCell->setEnabled(index == 3);
        preview();
    });
    previewCell->setEnabled(false);
    connect(previewCell, &QSpinBox::valueChanged, this, [this] { preview(); });
    connect(highlight, &QPushButton::clicked, this, [this] {
        if (!selection_ || !mesh_ || resolved_->currentRow() < 0)
            return;
        const auto *item = resolved_->item(resolved_->currentRow(), 0);
        if (item)
            selection_->select(
                mesh::MeshObjectRef::cell(mesh_->sourcePath(), item->text().toInt() - 1));
    });
    if (selection_)
        connect(selection_, &SelectionManager::selectionChanged, this, [this] { preview(); });
    resolved_ = new QTableWidget(0, 5, this);
    resolved_->setObjectName("meshSnowPreview");
    resolved_->setHorizontalHeaderLabels({tr("Cell"), tr("Effective pack"), tr("Assignment source"),
                                          tr("Terrain"), tr("Snow model")});
    resolved_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resolved_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(resolved_, 1);
    status_ = new QLabel(this);
    status_->setObjectName("meshSnowStatus");
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, this);
    apply_ = buttons->button(QDialogButtonBox::Apply);
    apply_->setObjectName("meshSnowApply");
    layout->addWidget(buttons);
    connect(apply_, &QPushButton::clicked, this, &MeshSnowDialog::apply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(rows_, &QAbstractItemModel::dataChanged, this, [this] { preview(); });
    connect(mode_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { preview(); });
    connect(step_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { preview(); });
    connect(scope_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] { tag_->setEnabled(scope_->currentIndex() == 1); });
    tag_->setEnabled(false);
    if (model) {
        connect(model, &SWMMModelLayer::engineAboutToClose, this, &MeshSnowDialog::invalidate);
        connect(model, &QObject::destroyed, this, &MeshSnowDialog::invalidate);
    }
    QString error = rows_->load(engine_);
    if (error.isEmpty())
        error = MeshSnowModel::readSections(engine_, before_);
    if (!error.isEmpty()) {
        invalidate();
        status_->setText(error);
        return;
    }
    pack_->addItems(rows_->packs);
    loaded_step_ = rows_->step;
    {
        QSignalBlocker a(mode_), b(step_);
        mode_->setCurrentIndex(rows_->mode);
        step_->setValue(loaded_step_);
        displayed_step_ = step_->value();
    }
    preview();
}
QString MeshSnowDialog::contextError() const
{
    if (!model_ || !mesh_ || !canvas_ || !canvas_->undoStack() || !engine_ ||
        model_->engine() != engine_ || mesh_->geomRevision() != geometry_)
        return tr("The model or mesh changed. Reopen this editor.");
    int state = 0;
    if (swmm_engine_get_state(engine_, &state) != SWMM_OK ||
        (state != SWMM_STATE_OPENED && state != SWMM_STATE_BUILDING))
        return tr(
            "Snow assignments can be edited before initialization. Stop and reset the simulation to edit.");
    return {};
}
void MeshSnowDialog::invalidate()
{
    engine_ = nullptr;
    apply_->setEnabled(false);
    table_->setEnabled(false);
    status_->setText(tr("The model was closed. Reopen this editor to continue."));
}
void MeshSnowDialog::preview()
{
    if (!rows_ || !status_)
        return;
    rows_->mode = mode_->currentIndex();
    rows_->step = step_->value() == displayed_step_ ? loaded_step_ : step_->value();
    QString error = contextError();
    QStringList tags;
    if (mesh_)
        for (const auto &c : mesh_->mesh().triangles)
            tags.append(c.tag);
    if (error.isEmpty())
        error = rows_->validate(tags);
    resolved_->setRowCount(0);
    int active = 0, ueb = 0, degreeDay = 0;
    const auto *filter = findChild<QComboBox *>("meshSnowPreviewFilter");
    const int choice = filter ? filter->currentIndex() : 0;
    const auto *inspected = findChild<QSpinBox *>("meshSnowPreviewCell");
    QSet<int> selected;
    if (selection_ && mesh_)
        for (const auto &ref : selection_->selection()) {
            QString key;
            int cell = -1;
            if (mesh::MeshObjectRef::parseCell(ref, &key, &cell) &&
                key == mesh::MeshObjectRef::layerKey(mesh_->sourcePath()))
                selected.insert(cell);
        }
    int global = 0;
    if (engine_)
        swmm_snow_get_model(engine_, &global);
    QHash<QString, int> models;
    if (engine_)
        for (int p = 0; p < swmm_snowpack_count(engine_); ++p) {
            SWMM_UebSnowpack pack{};
            const auto *id = swmm_snowpack_id(engine_, p);
            if (id && swmm_snowpack_get_ueb(engine_, p, &pack) == SWMM_OK)
                models.insert(QString::fromUtf8(id).toCaseFolded(),
                              pack.model < 0 ? global : pack.model);
        }
    for (int i = 0; i < tags.size(); ++i) {
        QString origin;
        const auto row = rows_->effective(i, tags[i], &origin);
        const bool assigned =
            !row.pack.isEmpty() && row.pack.compare("NONE", Qt::CaseInsensitive) != 0;
        active += assigned;
        const int model = models.value(row.pack.toCaseFolded(), global);
        if (assigned) {
            ueb += model == 1;
            degreeDay += model == 0;
        }
        const bool show = choice == 1   ? selected.contains(i)
                          : choice == 2 ? assigned
                          : choice == 3 ? inspected && inspected->value() == i + 1
                                        : true;
        if (show && resolved_->rowCount() < 200) {
            const int out = resolved_->rowCount();
            resolved_->insertRow(out);
            const QString terrain =
                std::isnan(row.overrides[4])
                    ? tr("From cell geometry")
                    : tr("%1° slope · %2° aspect").arg(row.overrides[4]).arg(row.overrides[5]);
            const QStringList values = {QString::number(i + 1), row.pack, origin, terrain,
                                        assigned ? (model == 1 ? tr("UEB") : tr("Degree day"))
                                                 : tr("Unassigned")};
            for (int k = 0; k < 5; ++k)
                resolved_->setItem(out, k, new QTableWidgetItem(values[k]));
        }
    }
    step_->setEnabled(ueb > 0);
    step_->setToolTip(ueb > 0 ? tr("UEB update interval; zero inherits the global interval.")
                              : tr("Saved interval; no assigned snowpack currently uses UEB."));
    char ignored[80]{};
    if (engine_)
        swmm_options_get(engine_, "IGNORE_SNOWMELT", ignored, sizeof ignored);
    const QString activity =
        QString::fromUtf8(ignored) == "YES"
            ? tr("Inactive: global snowmelt is off; assignments are retained.")
        : rows_->mode == 2 ? tr("Inactive: mesh snowmelt is disabled; assignments are retained.")
        : active == 0
            ? tr("Inactive: assign a snowpack to mesh cells.")
            : tr("Snowmelt is configured for assigned cells. Weather and eligible surface coverage are checked when the run starts.");
    const int plannedActive = QString::fromUtf8(ignored) == "YES" || rows_->mode == 2 ? 0 : active;
    status_->setText(
        error.isEmpty()
            ? tr("%1 of %2 cells assigned; %3 planned active (eligible surface coverage checked at run start). %4 Preview shows up to %5 matching cells. Changes remain staged until Apply.")
                  .arg(active)
                  .arg(tags.size())
                  .arg(plannedActive)
                  .arg(activity + tr(" UEB: %1; degree day: %2.").arg(ueb).arg(degreeDay))
                  .arg(resolved_->rowCount())
            : error);
    apply_->setEnabled(error.isEmpty());
}
void MeshSnowDialog::addAssignment()
{
    if (!contextError().isEmpty())
        return;
    MeshSnowRow row;
    row.pack = pack_->currentText();
    if (scope_->currentIndex() == 2)
        rows_->upsert(row);
    else if (scope_->currentIndex() == 1) {
        row.tag = tag_->currentText();
        if (!row.tag.isEmpty())
            rows_->upsert(row);
    } else if (selection_)
        for (const auto &ref : selection_->selection()) {
            QString key;
            int cell = -1;
            if (mesh::MeshObjectRef::parseCell(ref, &key, &cell) &&
                key == mesh::MeshObjectRef::layerKey(mesh_->sourcePath())) {
                row.cell = cell;
                row.tag.clear();
                rows_->upsert(row);
            }
        }
    preview();
}
void MeshSnowDialog::editPacks()
{
    if (!model_ || !contextError().isEmpty())
        return;
    auto *registry = qobject_cast<snowpack::SnowpackRegistry *>(model_->ensureSnowpackRegistry());
    if (!registry)
        return;
    SnowpackEditorDialog dialog(registry, model_, this);
    dialog.exec();
    rows_->packs = {"NONE"};
    for (int i = 0; i < swmm_snowpack_count(engine_); ++i)
        if (const char *name = swmm_snowpack_id(engine_, i))
            rows_->packs.append(QString::fromUtf8(name));
    const auto selected = pack_->currentText();
    pack_->clear();
    pack_->addItems(rows_->packs);
    pack_->setCurrentText(selected);
    preview();
}
void MeshSnowDialog::apply()
{
    preview();
    if (!apply_->isEnabled())
        return;
    std::array<QString, 3> current;
    QString error = MeshSnowModel::readSections(engine_, current);
    if (error.isEmpty() && current != before_)
        error = tr("Snow assignments changed in another view. Reopen this editor before applying.");
    if (error.isEmpty())
        error = MeshSnowModel::applySections(engine_, rows_->sections());
    if (!error.isEmpty()) {
        status_->setText(error);
        return;
    }
    std::array<QString, 3> after;
    error = MeshSnowModel::readSections(engine_, after);
    if (!error.isEmpty()) {
        status_->setText(error);
        model_->markEdited();
        return;
    }
    if (after != before_) {
        auto *command = new SnowUndo(model_, mesh_, before_, after);
        if (!sampledSources_.isEmpty())
            command->setText(tr("Assign mesh snow from %1").arg(sampledSources_.join("; ")));
        canvas_->undoStack()->push(command);
    }
    sampledSources_.clear();
    before_ = after;
    model_->markEdited();
    emit model_->optionsChanged({"2D_SNOW"});
    status_->setText(tr("Snow assignments applied. Undo restores the previous configuration."));
}

void MeshSnowDialog::fillFromLayer()
{
    using Sampling = assignment::MeshAssignmentSampling;
    if (!contextError().isEmpty())
        return;
    QDialog dialog(this);
    dialog.setObjectName("meshSnowGisDialog");
    dialog.setWindowTitle(tr("Fill snow settings from GIS"));
    dialog.resize(820, 720);
    auto *layout = new QVBoxLayout(&dialog);
    auto *intro = new QLabel(
        tr("Use the assignment scope selected in the snow editor. Continuous values default to footprint area averages; snowpack classes use the largest covered share. Native units are shown beside each property. Values remain staged until Apply in the snow editor."),
        &dialog);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    auto *source = new QComboBox(&dialog);
    source->setObjectName("snowGisSource");
    for (auto *base : canvas_->layers())
        if (qobject_cast<GISRasterLayer *>(base) || qobject_cast<GISVectorLayer *>(base))
            source->addItem(base->name(), base->layerId());
    form->addRow(tr("Loaded layer:"), source);
    auto *target = new QComboBox(&dialog);
    target->setObjectName("snowGisTarget");
    target->addItem(tr("Snowpack from class lookup"), -1);
    for (int k = 0; k < 6; ++k)
        target->addItem(rows_->headerData(k + 3, Qt::Horizontal).toString(), k);
    form->addRow(tr("Property:"), target);
    auto *field = new QComboBox(&dialog);
    field->setObjectName("snowGisField");
    form->addRow(tr("Vector field:"), field);
    auto *band = new QSpinBox(&dialog);
    band->setObjectName("snowGisBand");
    band->setRange(1, 999);
    form->addRow(tr("Raster band:"), band);
    auto *filter = new QLineEdit(&dialog);
    form->addRow(tr("Vector filter:"), filter);
    auto *selected = new QCheckBox(tr("Use selected source features only"), &dialog);
    form->addRow(selected);
    auto *method = new QComboBox(&dialog);
    method->addItem(tr("Area-weighted mean (continuous)"),
                    int(Sampling::Sampling::AreaWeightedMean));
    method->addItem(tr("Majority (categorical)"), int(Sampling::Sampling::Majority));
    method->addItem(tr("Centroid"), int(Sampling::Sampling::Centroid));
    form->addRow(tr("Sampling:"), method);
    auto *lookup = new QTextEdit(&dialog);
    lookup->setObjectName("snowGisLookup");
    lookup->setPlaceholderText(tr(
        "One class = snowpack per line, for example:\n1 = Forest snow\n2 = Open snow\n3 = NONE"));
    lookup->setMaximumHeight(100);
    form->addRow(tr("Class lookup:"), lookup);
    layout->addLayout(form);
    auto *table = new QTableWidget(0, 4, &dialog);
    table->setHorizontalHeaderLabels({tr("Cell"), tr("Before"), tr("After"), tr("Source")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(table, 1);
    auto *status = new QLabel(&dialog);
    status->setObjectName("snowGisStatus");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *previewButton = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    previewButton->setObjectName("snowGisPreview");
    auto *stage = buttons->addButton(tr("Stage values"), QDialogButtonBox::AcceptRole);
    stage->setObjectName("snowGisStage");
    stage->setEnabled(false);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto invalidate = [stage, table, status] {
        stage->setEnabled(false);
        table->setRowCount(0);
        status->setText(tr("Preview the current choices before staging values."));
    };
    auto sourceLayer = [this, source]() -> OpenSWMMVisLayer * {
        if (canvas_)
            for (auto *base : canvas_->layers())
                if (base->layerId() == source->currentData().toString())
                    return base;
        return nullptr;
    };
    auto populate = [=] {
        auto *base = sourceLayer();
        auto *vector = qobject_cast<GISVectorLayer *>(base);
        auto *raster = qobject_cast<GISRasterLayer *>(base);
        field->clear();
        if (vector)
            field->addItems(vector->fieldNames());
        field->setEnabled(vector);
        filter->setEnabled(vector);
        selected->setEnabled(vector);
        band->setEnabled(raster);
        if (raster)
            band->setMaximum(std::max(1, raster->bandCount()));
        invalidate();
    };
    connect(source, &QComboBox::currentIndexChanged, &dialog, populate);
    populate();
    auto targetChanged = [=] {
        const bool categorical = target->currentData().toInt() < 0;
        lookup->setEnabled(categorical);
        method->setCurrentIndex(categorical ? 1 : 0);
        invalidate();
    };
    connect(target, &QComboBox::currentIndexChanged, &dialog, targetChanged);
    targetChanged();
    for (auto *combo : {field, method})
        connect(combo, &QComboBox::currentIndexChanged, &dialog, invalidate);
    connect(band, &QSpinBox::valueChanged, &dialog, invalidate);
    connect(filter, &QLineEdit::textChanged, &dialog, invalidate);
    connect(selected, &QCheckBox::toggled, &dialog, invalidate);
    connect(lookup, &QTextEdit::textChanged, &dialog, invalidate);
    Sampling::SampleResult sampled;
    QMap<QString, QString> mapping;
    QStringList tags;
    std::array<QString, 3> original;
    int property = -1;
    QString provenance;
    connect(previewButton, &QPushButton::clicked, &dialog, [&] {
        invalidate();
        if (!contextError().isEmpty()) {
            status->setText(contextError());
            return;
        }
        auto *base = sourceLayer();
        if (!base) {
            status->setText(tr("Load a raster or feature layer into the map first."));
            return;
        }
        Sampling::Job job;
        property = target->currentData().toInt();
        job.mode = property < 0 ? Sampling::Mode::SingleCategory : Sampling::Mode::SingleNumeric;
        job.sampling = Sampling::Sampling(method->currentData().toInt());
        job.strictCrs = true;
        job.meshCrsWkt = mesh_->srs() ? mesh_->srs()->toWkt() : QString();
        job.assignedSourceCrsWkt = base->srs() ? base->srs()->toWkt() : QString();
        if (job.assignedSourceCrsWkt.isEmpty() || job.meshCrsWkt.isEmpty()) {
            status->setText(
                tr("Assign coordinate systems to the mesh and source layer before sampling."));
            return;
        }
        if (property < 0 && job.sampling == Sampling::Sampling::AreaWeightedMean) {
            status->setText(tr("Use Majority or Centroid for snowpack classes."));
            return;
        }
        mapping.clear();
        if (property < 0)
            for (const auto &line : lookup->toPlainText().split('\n', Qt::SkipEmptyParts)) {
                const int sep = line.indexOf('=');
                const auto key = line.left(sep).trimmed(), pack = line.mid(sep + 1).trimmed();
                if (sep < 1 || mapping.contains(key) ||
                    !rows_->packs.contains(pack, Qt::CaseInsensitive)) {
                    status->setText(tr("Use unique class = existing snowpack entries (or NONE)."));
                    return;
                }
                mapping.insert(key, pack);
            }
        if (property < 0 && mapping.isEmpty()) {
            status->setText(tr("Enter at least one class-to-snowpack mapping."));
            return;
        }
        if (auto *raster = qobject_cast<GISRasterLayer *>(base)) {
            job.source = Sampling::Source::Raster;
            job.rasterPath = raster->filePath();
            job.bands = {band->value()};
            job.keyBand1 = band->value();
            provenance = tr("%1 · band %2").arg(raster->name()).arg(band->value());
        }
        if (auto *vector = qobject_cast<GISVectorLayer *>(base)) {
            job.source = Sampling::Source::Vector;
            job.vectorPath = vector->filePath();
            job.vectorLayerName = vector->ogrLayerName();
            job.fields = {field->currentText()};
            job.keyField1 = field->currentText();
            job.vectorFilterExpr =
                filter->text().isEmpty() ? vector->filterExpression() : filter->text();
            job.filterBySelection = selected->isChecked();
            job.selectedIds = vector->selectedFeatureIds();
            provenance = tr("%1:%2").arg(vector->name(), field->currentText());
            if (job.filterBySelection && job.selectedIds.isEmpty()) {
                status->setText(tr("Select source features first."));
                return;
            }
        }
        job.targetKeys = {"snow"};
        job.targetMin = {-std::numeric_limits<double>::max()};
        job.targetMax = {std::numeric_limits<double>::max()};
        tags.clear();
        QSet<int> chosen;
        if (scope_->currentIndex() == 0 && selection_)
            for (const auto &ref : selection_->selection()) {
                QString key;
                int cell = -1;
                if (mesh::MeshObjectRef::parseCell(ref, &key, &cell) &&
                    key == mesh::MeshObjectRef::layerKey(mesh_->sourcePath()))
                    chosen.insert(cell);
            }
        const auto &mesh = mesh_->mesh();
        for (int i = 0; i < mesh.triangles.size(); ++i) {
            const auto &cell = mesh.triangles[i];
            tags.append(cell.tag);
            if ((scope_->currentIndex() == 0 && !chosen.contains(i)) ||
                (scope_->currentIndex() == 1 && cell.tag != tag_->currentText()))
                continue;
            job.triangles.append(i);
            job.centroids.append(mesh::cellGeom(mesh.vertices, cell).centroid);
            for (int k = 0; k < 4; ++k)
                job.triVerts.append(mesh.vertices[cell.vertex(k < cell.vertexCount() ? k : 0)].xy);
        }
        if (job.triangles.isEmpty()) {
            status->setText(tr("No cells are in the chosen assignment scope."));
            return;
        }
        original = rows_->sections();
        QProgressDialog progress(tr("Sampling snow settings…"), tr("Cancel"), 0, 0, &dialog);
        progress.setWindowModality(Qt::WindowModal);
        QFutureWatcher<Sampling::SampleResult> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcher<Sampling::SampleResult>::finished, &loop,
                &QEventLoop::quit);
        const auto cancellation = connect(&progress, &QProgressDialog::canceled, &watcher,
                                          &QFutureWatcher<Sampling::SampleResult>::cancel);
        watcher.setFuture(QtConcurrent::run(assignment::runMeshAssignmentSampling, job));
        progress.show();
        loop.exec();
        disconnect(cancellation);
        progress.close();
        if (watcher.isCanceled()) {
            status->setText(tr("Sampling cancelled."));
            return;
        }
        sampled = watcher.result();
        if (!sampled.error.isEmpty()) {
            status->setText(sampled.error);
            return;
        }
        if (!contextError().isEmpty()) {
            status->setText(contextError());
            return;
        }
        MeshSnowModel candidate;
        candidate.rows = rows_->rows;
        candidate.packs = rows_->packs;
        int skipped = 0;
        const auto values = sampled.values.isEmpty() ? QVector<double>{} : sampled.values[0];
        const QString error = candidate.stageSamples(property, sampled.triangles, values,
                                                     sampled.keys, mapping, tags, &skipped);
        if (!error.isEmpty()) {
            status->setText(error);
            return;
        }
        int changed = 0;
        for (int i = 0; i < sampled.triangles.size(); ++i) {
            const int cell = sampled.triangles[i];
            const auto before = rows_->effective(cell, tags[cell]),
                       after = candidate.effective(cell, tags[cell]);
            QString old = property < 0 ? before.pack
                                       : openswmmvis::displayNumber(before.overrides[property]),
                    next = property < 0 ? after.pack
                                        : openswmmvis::displayNumber(after.overrides[property]);
            bool authored = false;
            for (const auto &row : rows_->rows)
                if (row.cell == cell) {
                    authored = property < 0 || !std::isnan(row.overrides[property]);
                    break;
                }
            if (!authored)
                old = tr("Inherited (%1)").arg(old == "nan" ? tr("pack / geometry") : old);
            if (old == next)
                continue;
            ++changed;
            if (table->rowCount() < 500) {
                const int r = table->rowCount();
                table->insertRow(r);
                const QStringList text = {QString::number(cell + 1),
                                          old == "nan" ? tr("Inherited") : old, next, provenance};
                for (int c = 0; c < 4; ++c)
                    table->setItem(r, c, new QTableWidgetItem(text[c]));
            }
        }
        status->setText(
            tr("%1 cells change. Skipped: %2 NoData / outside; %3 nonnumeric; %4 outside the valid range; %5 unmapped classes. First 500 changes shown.")
                .arg(changed)
                .arg(sampled.skippedNoData)
                .arg(sampled.skippedNonNumeric)
                .arg(sampled.skippedRange + (property >= 0 ? skipped : 0))
                .arg(property < 0 ? skipped : 0) +
            (property >= 4 ? tr(" Fill both slope and aspect before Apply.") : QString()));
        stage->setEnabled(changed > 0);
    });
    connect(stage, &QPushButton::clicked, &dialog, [&] {
        if (!stage->isEnabled())
            return;
        if (!contextError().isEmpty() || rows_->sections() != original) {
            status->setText(tr("Snow settings or mesh changed. Preview again."));
            invalidate();
            return;
        }
        const auto values = sampled.values.isEmpty() ? QVector<double>{} : sampled.values[0];
        const auto error =
            rows_->stageSamples(property, sampled.triangles, values, sampled.keys, mapping, tags);
        if (!error.isEmpty()) {
            status->setText(error);
            return;
        }
        sampledSources_.append(provenance);
        dialog.accept();
        preview();
    });
    dialog.exec();
}
}
