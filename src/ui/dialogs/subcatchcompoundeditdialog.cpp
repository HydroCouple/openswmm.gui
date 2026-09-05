/*!
 * \file   subcatchcompoundeditdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/subcatchcompoundeditdialog.h"
#include "ui/uiscrollhelpers.h"

#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_pollutants.h>      // loadings rows
#include <openswmm/engine/openswmm_quality.h>        // land uses
#include <openswmm/engine/openswmm_infrastructure.h>  // LID

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr double kBig = 1e12;
}

SubcatchCompoundEditDialog::SubcatchCompoundEditDialog(SubcatchCompoundEditRef ref,
                                                       QWidget *parent)
    : QDialog(parent), m_ref(std::move(ref))
{
    const char *titles[] = { "Land Use Coverage", "Groundwater", "LID Usage",
                             "Initial Loadings" };
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("SubcatchCompoundEditDialog"));
    setWindowTitle(tr("%1 — %2")
                       .arg(QString::fromLatin1(titles[m_ref.kind]), m_ref.subName));
    resize(560, 420);

    m_stack = new QStackedWidget(this);
    buildLandUsePage();      // index 0
    buildLidUsagePage();     // index 1
    buildLoadingsPage();     // index 2
    m_stack->setCurrentIndex(pageIndexFor(m_ref.kind));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_stack, 1);
    lay->addWidget(m_buttons);

    refreshActivePage();
}

int SubcatchCompoundEditDialog::pageIndexFor(SubcatchCompoundEditRef::Kind kind)
{
    // Groundwater moved to GroundwaterExchangeDialog (AQUIFER_GROUNDWATER_
    // EXCHANGE plan D2), so Kind values no longer map 1:1 onto stack pages.
    switch (kind) {
    case SubcatchCompoundEditRef::LandUse:  return 0;
    case SubcatchCompoundEditRef::LidUsage: return 1;
    case SubcatchCompoundEditRef::Loadings: return 2;
    case SubcatchCompoundEditRef::Groundwater: break;
    }
    return 0;
}

int SubcatchCompoundEditDialog::subIdx() const
{
    if (!m_ref.engine || m_ref.subName.isEmpty()) return -1;
    return swmm_subcatch_index(m_ref.engine, m_ref.subName.toUtf8().constData());
}

// ---------------------------------------------------------------------------
// Land-use coverage page
// ---------------------------------------------------------------------------
void SubcatchCompoundEditDialog::buildLandUsePage()
{
    // Iteration 4 — editable full-matrix table: one row per DEFINED land
    // use (including 0%), percent edited in place, apply-as-you-go. The
    // rows re-list on every refresh, so land uses added while the dialog
    // is open appear (the old populate-once combo never noticed them).
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_luSummary = new QLabel(page);
    m_luSummary->setWordWrap(true);
    vlay->addWidget(m_luSummary);

    m_luTable = new QTableWidget(0, 2, page);
    m_luTable->setHorizontalHeaderLabels({ tr("Land Use"), tr("Coverage (%)") });
    m_luTable->horizontalHeader()->setStretchLastSection(true);
    m_luTable->verticalHeader()->setVisible(false);
    m_luTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_luTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_luTable->setEditTriggers(QAbstractItemView::DoubleClicked
                               | QAbstractItemView::EditKeyPressed
                               | QAbstractItemView::SelectedClicked);
    vlay->addWidget(m_luTable, 1);

    connect(m_luTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
        if (m_luRefreshing || !item || item->column() != 1) return;
        const int s = subIdx();
        if (s < 0) return;
        QTableWidgetItem *name = m_luTable->item(item->row(), 0);
        if (!name) return;
        const int lu = swmm_landuse_index(m_ref.engine,
                                          name->text().toUtf8().constData());
        if (lu < 0) return;
        bool ok = false;
        const double pct = item->text().toDouble(&ok);
        if (!ok || pct < 0.0 || pct > 100.0) {
            QMessageBox::warning(this, tr("Set Coverage"),
                tr("Coverage must be a percent between 0 and 100."));
            refreshActivePage();
            return;
        }
        const int rc = swmm_subcatch_set_coverage(m_ref.engine, s, lu, pct);
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Set Coverage"),
                tr("Engine rejected coverage set (error %1).").arg(rc));
        }
        refreshActivePage();
    });

    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
}

// ---------------------------------------------------------------------------
// Initial loadings page ([LOADINGS], iteration 4)
// ---------------------------------------------------------------------------
void SubcatchCompoundEditDialog::buildLoadingsPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_loadSummary = new QLabel(page);
    m_loadSummary->setWordWrap(true);
    vlay->addWidget(m_loadSummary);

    m_loadTable = new QTableWidget(0, 2, page);
    m_loadTable->setHorizontalHeaderLabels(
        { tr("Pollutant"), tr("Initial Buildup (mass/area)") });
    m_loadTable->horizontalHeader()->setStretchLastSection(true);
    m_loadTable->verticalHeader()->setVisible(false);
    m_loadTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_loadTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_loadTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                 | QAbstractItemView::EditKeyPressed
                                 | QAbstractItemView::SelectedClicked);
    vlay->addWidget(m_loadTable, 1);

    connect(m_loadTable, &QTableWidget::itemChanged, this,
            [this](QTableWidgetItem *item) {
        if (m_loadRefreshing || !item || item->column() != 1) return;
        const int s = subIdx();
        if (s < 0) return;
        QTableWidgetItem *name = m_loadTable->item(item->row(), 0);
        if (!name) return;
        const int p = swmm_pollutant_index(m_ref.engine,
                                           name->text().toUtf8().constData());
        if (p < 0) return;
        bool ok = false;
        const double w = item->text().toDouble(&ok);
        if (!ok || w < 0.0) {
            QMessageBox::warning(this, tr("Set Initial Loading"),
                tr("Initial buildup must be a non-negative number."));
            refreshActivePage();
            return;
        }
        const int rc = swmm_subcatch_set_initial_loading(m_ref.engine, s, p, w);
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Set Initial Loading"),
                tr("Engine rejected the loading set (error %1).").arg(rc));
        }
        refreshActivePage();
    });

    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
}

// ---------------------------------------------------------------------------
// LID usage page
// ---------------------------------------------------------------------------
void SubcatchCompoundEditDialog::buildLidUsagePage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_lidSummary = new QLabel(page);
    m_lidSummary->setWordWrap(true);
    vlay->addWidget(m_lidSummary);

    m_lidTable = new QTableWidget(0, 6, page);
    m_lidTable->setHorizontalHeaderLabels({
        tr("LID Control"), tr("#"), tr("Area"), tr("Width"),
        tr("Init.Sat"), tr("%Imperv") });
    m_lidTable->horizontalHeader()->setStretchLastSection(true);
    m_lidTable->verticalHeader()->setVisible(false);
    m_lidTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lidTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_lidTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vlay->addWidget(m_lidTable, 1);

    m_lidRemoveBtn = new QPushButton(tr("Remove Selected"), page);
    m_lidRemoveBtn->setEnabled(false);
    vlay->addWidget(m_lidRemoveBtn);
    connect(m_lidTable->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() {
        m_lidRemoveBtn->setEnabled(!m_lidTable->selectedItems().isEmpty());
    });
    connect(m_lidRemoveBtn, &QPushButton::clicked, this, [this]() {
        const auto rows = m_lidTable->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        QTableWidgetItem *first = m_lidTable->item(rows.first().row(), 0);
        if (!first) return;
        const int globalIdx = first->data(Qt::UserRole).toInt();
        const int rc = swmm_lid_usage_remove(m_ref.engine, globalIdx);
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Remove LID Usage"),
                tr("Engine rejected LID usage remove (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    auto *grp  = new QGroupBox(tr("Add LID Usage"), page);
    auto *form = new QFormLayout(grp);
    m_lidCombo   = new QComboBox(grp);
    m_lidNumber  = new QSpinBox(grp);       m_lidNumber->setRange(1, 1000000);
    m_lidArea    = new QDoubleSpinBox(grp); m_lidArea->setRange(0.0, kBig);    m_lidArea->setDecimals(4);
    m_lidWidth   = new QDoubleSpinBox(grp); m_lidWidth->setRange(0.0, kBig);   m_lidWidth->setDecimals(4);
    m_lidInitSat = new QDoubleSpinBox(grp); m_lidInitSat->setRange(0.0, 100.0);m_lidInitSat->setDecimals(2);
    m_lidFromImp = new QDoubleSpinBox(grp); m_lidFromImp->setRange(0.0, 100.0);m_lidFromImp->setDecimals(2);
    form->addRow(tr("LID Control"),      m_lidCombo);
    form->addRow(tr("Nu&mber of Units"),  m_lidNumber);
    form->addRow(tr("Area (&per unit)"),  m_lidArea);
    form->addRow(tr("Top Width"),        m_lidWidth);
    form->addRow(tr("Init. Saturation"), m_lidInitSat);
    form->addRow(tr("% From Impervious"),m_lidFromImp);

    auto *addBtn = new QPushButton(tr("Add"), grp);
    form->addRow(QString(), addBtn);
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        const int s = subIdx();
        if (s < 0) return;
        const int lid = swmm_lid_index(m_ref.engine,
                                       m_lidCombo->currentText().toUtf8().constData());
        if (lid < 0) {
            QMessageBox::warning(this, tr("Add LID Usage"),
                tr("Select a defined LID control first."));
            return;
        }
        const int rc = swmm_lid_usage_add(m_ref.engine, s, lid,
            m_lidNumber->value(), m_lidArea->value(), m_lidWidth->value(),
            m_lidInitSat->value(), m_lidFromImp->value());
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Add LID Usage"),
                tr("Engine rejected LID usage add (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    vlay->addWidget(grp);
    m_stack->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_stack));
}

// ---------------------------------------------------------------------------
// Engine read on load / after each commit.
// ---------------------------------------------------------------------------
void SubcatchCompoundEditDialog::refreshActivePage()
{
    const int s = subIdx();
    if (s < 0) { m_ref.summary = tr("(subcatchment removed)"); return; }
    SWMM_Engine e = m_ref.engine;

    switch (m_ref.kind) {
    case SubcatchCompoundEditRef::LandUse: {
        // Full matrix: every defined land use gets an editable percent row.
        const int nLu = swmm_landuse_count(e);
        m_luRefreshing = true;
        m_luTable->setRowCount(0);
        int assigned = 0;
        double sum = 0.0;
        for (int i = 0; i < nLu; ++i) {
            double pct = 0.0;
            if (swmm_subcatch_get_coverage(e, s, i, &pct) != SWMM_OK) continue;
            const int row = m_luTable->rowCount();
            m_luTable->insertRow(row);
            const char *id = swmm_landuse_id(e, i);
            auto *nameItem = new QTableWidgetItem(
                id ? QString::fromUtf8(id) : QString());
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            m_luTable->setItem(row, 0, nameItem);
            m_luTable->setItem(row, 1, new QTableWidgetItem(
                QString::number(pct, 'g', 6)));
            if (pct > 0.0) ++assigned;
            sum += pct;
        }
        m_luRefreshing = false;
        QString text = tr("<b>%1</b> of %2 land use(s) assigned. Edit the "
                          "Coverage column in place; 0 removes a coverage.")
                           .arg(assigned).arg(nLu);
        if (sum > 100.0 + 1e-9)
            text += tr("<br><b>Warning:</b> coverages sum to %1% "
                       "(&gt; 100%).").arg(QString::number(sum, 'g', 6));
        m_luSummary->setText(text);
        m_ref.summary = assigned > 0 ? tr("%1 land use(s)").arg(assigned)
                                     : tr("(none)");
        break;
    }
    case SubcatchCompoundEditRef::Loadings: {
        // [LOADINGS] — every pollutant gets an editable initial-buildup row.
        const int nP = swmm_pollutant_count(e);
        m_loadRefreshing = true;
        m_loadTable->setRowCount(0);
        int assigned = 0;
        for (int i = 0; i < nP; ++i) {
            double w = 0.0;
            if (swmm_subcatch_get_initial_loading(e, s, i, &w) != SWMM_OK)
                continue;
            const int row = m_loadTable->rowCount();
            m_loadTable->insertRow(row);
            const char *id = swmm_pollutant_id(e, i);
            auto *nameItem = new QTableWidgetItem(
                id ? QString::fromUtf8(id) : QString());
            nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
            m_loadTable->setItem(row, 0, nameItem);
            m_loadTable->setItem(row, 1, new QTableWidgetItem(
                QString::number(w, 'g', 6)));
            if (w > 0.0) ++assigned;
        }
        m_loadRefreshing = false;
        m_loadSummary->setText(tr(
            "<b>%1</b> of %2 pollutant(s) carry an initial buildup at "
            "simulation start ([LOADINGS]); it overrides the DRY_DAYS-derived "
            "buildup. Edit in place; 0 removes a loading.")
                                   .arg(assigned).arg(nP));
        m_ref.summary = assigned > 0 ? tr("%1 pollutant(s)").arg(assigned)
                                     : tr("(none)");
        break;
    }
    case SubcatchCompoundEditRef::LidUsage: {
        // LID control combo once.
        if (m_lidCombo->count() == 0) {
            const int nC = swmm_lid_count(e);
            for (int i = 0; i < nC; ++i)
                if (const char *id = swmm_lid_id(e, i))
                    m_lidCombo->addItem(QString::fromUtf8(id));
        }
        m_lidTable->setRowCount(0);
        m_lidRemoveBtn->setEnabled(false);
        const int nU = swmm_lid_usage_count(e);
        int mine = 0;
        for (int i = 0; i < nU; ++i) {
            int sc=-1, lid=-1, num=0, tp=0;
            double area=0,width=0,isat=0,fimp=0,fperv=0;
            if (swmm_lid_usage_get(e, i, &sc, &lid, &num, &area, &width,
                                   &isat, &fimp, &tp, &fperv) != SWMM_OK) continue;
            if (sc != s) continue;
            const int row = m_lidTable->rowCount();
            m_lidTable->insertRow(row);
            const char *cid = swmm_lid_id(e, lid);
            auto *c0 = new QTableWidgetItem(cid ? QString::fromUtf8(cid) : QString());
            c0->setData(Qt::UserRole, i);   // stash global usage index for remove
            m_lidTable->setItem(row, 0, c0);
            m_lidTable->setItem(row, 1, new QTableWidgetItem(QString::number(num)));
            m_lidTable->setItem(row, 2, new QTableWidgetItem(QString::number(area, 'g', 6)));
            m_lidTable->setItem(row, 3, new QTableWidgetItem(QString::number(width, 'g', 6)));
            m_lidTable->setItem(row, 4, new QTableWidgetItem(QString::number(isat, 'g', 6)));
            m_lidTable->setItem(row, 5, new QTableWidgetItem(QString::number(fimp, 'g', 6)));
            ++mine;
        }
        m_lidSummary->setText(tr("<b>%1</b> LID usage(s) on this subcatchment.")
                                  .arg(mine));
        m_ref.summary = mine > 0 ? tr("%1 LID(s)").arg(mine) : tr("(none)");
        break;
    }
    case SubcatchCompoundEditRef::Groundwater:
        break;   // handled by GroundwaterExchangeDialog
    }
}
