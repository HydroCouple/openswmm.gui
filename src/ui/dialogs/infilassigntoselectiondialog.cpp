/*!
 * \file   infilassigntoselectiondialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/infilassigntoselectiondialog.h"

#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/meshcommands.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui {

void InfilAssignToSelectionDialog::showFor(SWMM2DMeshLayer  *meshLayer,
                                           MapCanvas        *canvas,
                                           SelectionManager *selection,
                                           const QString    &depthUnitLabel,
                                           QWidget          *parent)
{
    if (!meshLayer) return;
    // Per-session singleton ([[feedback_mvc_synchronized_uis]]): the toolbar
    // action and the Model ▸ Mesh menu mirror must raise ONE window over the
    // selection, not stack two views that can disagree. WA_DeleteOnClose
    // nullifies the QPointer on close, so the next invocation builds fresh.
    static QPointer<InfilAssignToSelectionDialog> dlg;
    if (!dlg) {
        dlg = new InfilAssignToSelectionDialog(meshLayer, canvas, selection,
                                               depthUnitLabel, parent);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
    } else {
        // The active project window may have changed since the dialog was
        // built; writing into the previous project's mesh would be silent
        // corruption.
        dlg->rebind(meshLayer, canvas, selection, depthUnitLabel);
    }
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

InfilAssignToSelectionDialog::InfilAssignToSelectionDialog(
        SWMM2DMeshLayer *meshLayer, MapCanvas *canvas,
        SelectionManager *selection, const QString &depthUnitLabel,
        QWidget *parent)
    : QDialog(parent),
      m_mesh(meshLayer),
      m_canvas(canvas),
      m_selection(selection),
      m_depthUnitLabel(depthUnitLabel)
{
    setWindowTitle(tr("Assign Infiltration to Selection"));
    setObjectName(QStringLiteral("infilAssignToSelectionDialog"));
    // Modal: the form applies to the selection as it stands, so the selection
    // must not move under the user while they fill it in.
    setModal(true);

    buildUi();
    if (m_selection)
        connect(m_selection, &SelectionManager::selectionChanged,
                this, &InfilAssignToSelectionDialog::onSelectionChanged);
    refreshSelectionState();
}

void InfilAssignToSelectionDialog::rebind(SWMM2DMeshLayer  *meshLayer,
                                          MapCanvas        *canvas,
                                          SelectionManager *selection,
                                          const QString    &depthUnitLabel)
{
    if (m_selection)
        disconnect(m_selection, &SelectionManager::selectionChanged,
                   this, &InfilAssignToSelectionDialog::onSelectionChanged);
    m_mesh           = meshLayer;
    m_canvas         = canvas;
    m_selection      = selection;
    m_depthUnitLabel = depthUnitLabel;
    if (m_selection)
        connect(m_selection, &SelectionManager::selectionChanged,
                this, &InfilAssignToSelectionDialog::onSelectionChanged);
    refreshParamFields();       // depth unit may have changed with the project
    refreshSelectionState();
}

// ===========================================================================
// UI
// ===========================================================================

void InfilAssignToSelectionDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    m_selectionLbl = new QLabel(this);
    m_selectionLbl->setWordWrap(true);
    outer->addWidget(m_selectionLbl);

    auto *form = new QFormLayout;

    m_methodCombo = new QComboBox(this);
    {
        const QStringList labels = mesh::infilMethodLabels();
        for (int i = 0; i < labels.size(); ++i)
            m_methodCombo->addItem(labels.at(i),
                                   int(mesh::InfilMethod::None) + i);
    }
    m_methodCombo->setToolTip(tr(
        "Infiltration model applied to the selected cells "
        "([2D_INFILTRATION] METHOD). \"None\" writes an explicit no-model row, "
        "which is how a cell opts OUT of a region default."));
    connect(m_methodCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &InfilAssignToSelectionDialog::onMethodChanged);
    form->addRow(tr("Method:"), m_methodCombo);

    // Five positional parameter rows. Which of them apply — and what they are
    // called — is a function of the method (mesh::infilUsesParam /
    // infilParamLabel), so the rows are retitled and shown/hidden rather than
    // duplicated per method.
    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot) {
        m_paramLabels[slot] = new QLabel(this);
        m_paramSpins[slot]  = new QDoubleSpinBox(this);
        m_paramSpins[slot]->setKeyboardTracking(false);
        m_paramSpins[slot]->setMinimumWidth(120);
        form->addRow(m_paramLabels[slot], m_paramSpins[slot]);
    }

    m_destCombo = new QComboBox(this);
    {
        const QStringList labels = mesh::infilDestLabels();
        for (int i = 0; i < labels.size(); ++i) {
            const auto d = static_cast<mesh::InfilDest>(i);
            m_destCombo->addItem(labels.at(i), int(d));
            if (mesh::infilDestSupported(d)) continue;
            // Engine D-I4 — the token parses so the grammar is stable, but the
            // engine rejects it. Visible-but-unselectable beats a silent
            // validation failure at run time.
            if (auto *model = qobject_cast<QStandardItemModel *>(m_destCombo->model()))
                if (QStandardItem *item = model->item(i)) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                    item->setData(tr("Available from the groundwater release."),
                                  Qt::ToolTipRole);
                }
        }
    }
    m_destCombo->setToolTip(tr(
        "Where infiltrated water goes. Only \"Lost\" is accepted by the engine "
        "in this release; the others are reserved for the groundwater work."));
    form->addRow(tr("Destination:"), m_destCombo);

    outer->addLayout(form);

    // ---- Write target ------------------------------------------------------
    {
        auto *group = new QGroupBox(tr("Write as"), this);
        auto *v = new QVBoxLayout(group);
        m_writeCells = new QRadioButton(tr("Per-cell overrides"), group);
        m_writeTag   = new QRadioButton(tr("Region defaults (by tag)"), group);
        auto *bg = new QButtonGroup(group);
        bg->addButton(m_writeCells);
        bg->addButton(m_writeTag);
        m_writeCells->setChecked(true);
        m_writeCells->setToolTip(tr(
            "One [2D_INFILTRATION] row per selected cell. The cells stop "
            "inheriting from their region until the override is cleared."));
        m_writeTag->setToolTip(tr(
            "One [2D_INFILTRATION_DEFAULTS] row for the region the selected "
            "cells share. Every cell in that region picks it up by "
            "inheritance — including cells outside the current selection — so "
            "the assignment stays editable as a region afterwards (engine "
            "D-I3). Available only when the whole selection carries one tag."));
        v->addWidget(m_writeCells);
        v->addWidget(m_writeTag);
        outer->addWidget(group);
    }

    m_statusLbl = new QLabel(this);
    m_statusLbl->setWordWrap(true);
    outer->addWidget(m_statusLbl);

    auto *buttons = new QDialogButtonBox(this);
    m_applyBtn = buttons->addButton(tr("Apply"), QDialogButtonBox::ApplyRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_applyBtn, &QPushButton::clicked,
            this, &InfilAssignToSelectionDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    refreshParamFields();
}

void InfilAssignToSelectionDialog::refreshParamFields()
{
    const mesh::InfilMethod method = currentMethod();
    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot) {
        const bool used = mesh::infilUsesParam(method, slot);
        m_paramLabels[slot]->setVisible(used);
        m_paramSpins[slot]->setVisible(used);
        if (!used) continue;

        // Editor configuration comes from the shared registry, so the ranges,
        // decimals and tooltips match the attribute table and the region
        // defaults table cell-for-cell.
        const QByteArray key = mesh::infilParamKey(method, slot);
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
        QString label = mesh::infilParamLabel(method, slot);
        if (spec && spec->lengthUnit && !m_depthUnitLabel.isEmpty())
            label = QStringLiteral("%1 (%2)").arg(label, m_depthUnitLabel);
        m_paramLabels[slot]->setText(label + QLatin1Char(':'));

        QSignalBlocker block(m_paramSpins[slot]);
        if (spec) {
            m_paramSpins[slot]->setDecimals(spec->decimals);
            m_paramSpins[slot]->setRange(spec->min, spec->max);
            m_paramSpins[slot]->setSingleStep(spec->step);
            m_paramSpins[slot]->setToolTip(spec->tooltip);
            m_paramSpins[slot]->setValue(spec->defaultValue);
        }
    }
}

// ===========================================================================
// Selection
// ===========================================================================

QVector<int> InfilAssignToSelectionDialog::selectedCells() const
{
    QVector<int> out;
    if (!m_selection || !m_mesh) return out;
    // Keyed filter — a project can carry two meshes, and an unkeyed sweep
    // would write one mesh's selection into the other's triangle indices.
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_mesh->sourcePath());
    const int nTri = m_mesh->mesh().triangles.size();
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshCell) continue;
        QString lk;
        int tri = -1;
        if (!mesh::MeshObjectRef::parseCell(ref, &lk, &tri)) continue;
        if (lk != wantKey) continue;
        if (tri < 0 || tri >= nTri) continue;
        out.append(tri);
    }
    // QSet iteration order is unspecified; sorting keeps the undo entry (and
    // any diff of it) reproducible run to run.
    std::sort(out.begin(), out.end());
    return out;
}

QString InfilAssignToSelectionDialog::commonTag() const
{
    if (!m_mesh) return {};
    const QVector<int> cells = selectedCells();
    if (cells.isEmpty()) return {};
    const auto &tris = m_mesh->mesh().triangles;
    const QString tag = tris[cells.front()].tag;
    if (tag.isEmpty()) return {};
    for (int t : cells)
        if (tris[t].tag != tag) return {};
    return tag;
}

void InfilAssignToSelectionDialog::onSelectionChanged() { refreshSelectionState(); }

void InfilAssignToSelectionDialog::refreshSelectionState()
{
    const QVector<int> cells = selectedCells();
    m_selectionLbl->setText(
        cells.isEmpty()
            ? tr("No 2D cells are selected on this mesh. Pick cells with the "
                 "Select 2D Cells tool, then reopen this dialog.")
            : tr("%n cell(s) selected.", nullptr, int(cells.size())));

    const QString tag = commonTag();
    if (tag.isEmpty()) {
        m_writeTag->setEnabled(false);
        m_writeTag->setText(tr("Region defaults (by tag)"));
        if (m_writeTag->isChecked()) m_writeCells->setChecked(true);
    } else {
        m_writeTag->setEnabled(true);
        m_writeTag->setText(tr("Region defaults — tag \"%1\"").arg(tag));
    }

    // Seed the form from the selection when every cell already resolves to the
    // same row, so opening the dialog on a homogeneous selection shows what is
    // there instead of the registry defaults.
    if (!cells.isEmpty() && m_mesh) {
        const mesh::MeshResult &m = m_mesh->mesh();
        const mesh::InfilRow first = mesh::resolveInfil(m, cells.front()).row;
        bool same = true;
        for (int t : cells)
            if (!(mesh::resolveInfil(m, t).row == first)) { same = false; break; }
        if (same) {
            {
                QSignalBlocker block(m_methodCombo);
                const int row = m_methodCombo->findData(int(first.method));
                if (row >= 0) m_methodCombo->setCurrentIndex(row);
            }
            refreshParamFields();
            for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot) {
                if (!mesh::infilUsesParam(first.method, slot)) continue;
                if (!std::isfinite(first.p[slot])) continue;   // unset — keep the default
                QSignalBlocker block(m_paramSpins[slot]);
                m_paramSpins[slot]->setValue(first.p[slot]);
            }
            QSignalBlocker blockDest(m_destCombo);
            const int drow = m_destCombo->findData(int(first.dest));
            if (drow >= 0) m_destCombo->setCurrentIndex(drow);
        }
    }

    m_applyBtn->setEnabled(m_mesh && !cells.isEmpty());
}

// ===========================================================================
// Form → row → mesh
// ===========================================================================

mesh::InfilMethod InfilAssignToSelectionDialog::currentMethod() const
{
    return static_cast<mesh::InfilMethod>(m_methodCombo->currentData().toInt());
}

mesh::InfilRow InfilAssignToSelectionDialog::currentRow() const
{
    mesh::InfilRow row;
    row.method = currentMethod();
    row.dest   = static_cast<mesh::InfilDest>(m_destCombo->currentData().toInt());
    // Slots the method does not use stay NaN, which is what the writer emits
    // as "-" — a stale number in an unused column would round-trip into the
    // file and reappear the next time the method changed.
    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
        if (mesh::infilUsesParam(row.method, slot))
            row.p[slot] = m_paramSpins[slot]->value();
    return row;
}

void InfilAssignToSelectionDialog::onMethodChanged() { refreshParamFields(); }

void InfilAssignToSelectionDialog::onApply()
{
    if (!m_mesh) {
        m_statusLbl->setText(tr("The mesh layer was closed."));
        return;
    }
    const QVector<int> cells = selectedCells();
    if (cells.isEmpty()) {
        m_statusLbl->setText(tr("Nothing selected — nothing applied."));
        return;
    }
    const mesh::InfilRow row = currentRow();

    if (m_writeTag->isChecked()) {
        const QString tag = commonTag();
        if (tag.isEmpty()) {          // selection moved since the radio was enabled
            m_statusLbl->setText(
                tr("The selection no longer shares one region tag."));
            refreshSelectionState();
            return;
        }
        const int changed = mesh::pushInfilDefaultsEdit(
            m_mesh, {mesh::InfilDefaultRow{tag, row}}, m_canvas);
        m_statusLbl->setText(
            changed ? tr("Region \"%1\" now uses this infiltration row; every "
                         "cell in it inherits the change.").arg(tag)
                    : tr("Region \"%1\" already uses this row — nothing "
                         "applied.").arg(tag));
        return;
    }

    // One command for the whole selection, on the same undo stack every other
    // cell edit uses; the command restores inheritance on undo for cells that
    // were inheriting.
    const int changed = mesh::pushCellInfilEdit(m_mesh, cells, row, m_canvas);
    m_statusLbl->setText(
        changed ? tr("Applied to %n cell(s) as per-cell overrides.", nullptr,
                     changed)
                : tr("Every selected cell already carries this override — "
                     "nothing applied."));
}

} // namespace openswmmvis::ui
