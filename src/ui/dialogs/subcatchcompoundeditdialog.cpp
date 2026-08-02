/*!
 * \file   subcatchcompoundeditdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/subcatchcompoundeditdialog.h"

#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_quality.h>        // land uses
#include <openswmm/engine/openswmm_infrastructure.h>  // LID
#include <openswmm/engine/openswmm_nodes.h>           // gw receiving node

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
    const char *titles[] = { "Land Use Coverage", "Groundwater", "LID Usage" };
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("SubcatchCompoundEditDialog"));
    setWindowTitle(tr("%1 — %2")
                       .arg(QString::fromLatin1(titles[m_ref.kind]), m_ref.subName));
    resize(560, 420);

    m_stack = new QStackedWidget(this);
    buildLandUsePage();      // index 0
    buildGroundwaterPage();  // index 1
    buildLidUsagePage();     // index 2
    m_stack->setCurrentIndex(int(m_ref.kind));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *lay = new QVBoxLayout(this);
    lay->addWidget(m_stack, 1);
    lay->addWidget(m_buttons);

    refreshActivePage();
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
    m_luTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    vlay->addWidget(m_luTable, 1);

    auto *grp  = new QGroupBox(tr("Set Coverage"), page);
    auto *form = new QFormLayout(grp);
    m_luCombo  = new QComboBox(grp);
    m_luCovSpin = new QDoubleSpinBox(grp);
    m_luCovSpin->setRange(0.0, 100.0);
    m_luCovSpin->setDecimals(2);
    form->addRow(tr("Lan&d Use"),     m_luCombo);
    form->addRow(tr("Coverage (%)"), m_luCovSpin);

    auto *applyBtn = new QPushButton(tr("Set / Update"), grp);
    form->addRow(QString(), applyBtn);
    connect(applyBtn, &QPushButton::clicked, this, [this]() {
        const int s = subIdx();
        if (s < 0) return;
        const int lu = swmm_landuse_index(m_ref.engine,
                                          m_luCombo->currentText().toUtf8().constData());
        if (lu < 0) return;
        const int rc = swmm_subcatch_set_coverage(m_ref.engine, s, lu,
                                                   m_luCovSpin->value());
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Set Coverage"),
                tr("Engine rejected coverage set (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    // Selecting a row pre-fills the form for review/tweak.
    connect(m_luTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        const auto sel = m_luTable->selectionModel()->selectedRows();
        if (sel.isEmpty()) return;
        const int row = sel.first().row();
        if (auto *it = m_luTable->item(row, 0))
            m_luCombo->setCurrentText(it->text());
        if (auto *it = m_luTable->item(row, 1))
            m_luCovSpin->setValue(it->text().toDouble());
    });

    vlay->addWidget(grp);
    m_stack->addWidget(page);
}

// ---------------------------------------------------------------------------
// Groundwater page
// ---------------------------------------------------------------------------
void SubcatchCompoundEditDialog::buildGroundwaterPage()
{
    auto *page = new QWidget(this);
    auto *vlay = new QVBoxLayout(page);

    m_gwSummary = new QLabel(page);
    m_gwSummary->setWordWrap(true);
    vlay->addWidget(m_gwSummary);

    auto *grp  = new QGroupBox(tr("[GROUNDWATER]"), page);
    auto *form = new QFormLayout(grp);

    m_gwAquifer = new QComboBox(grp);
    m_gwNode    = new QComboBox(grp);
    auto mkSpin = [grp]() {
        auto *s = new QDoubleSpinBox(grp);
        s->setRange(-kBig, kBig);
        s->setDecimals(4);
        return s;
    };
    m_gwSurfEl = mkSpin();
    m_gwA1 = mkSpin(); m_gwB1 = mkSpin();
    m_gwA2 = mkSpin(); m_gwB2 = mkSpin();
    m_gwA3 = mkSpin();
    m_gwTw = mkSpin(); m_gwHstar = mkSpin();

    form->addRow(tr("Aq&uifer"),          m_gwAquifer);
    form->addRow(tr("&Receiving Node"),   m_gwNode);
    form->addRow(tr("Surfa&ce Elev."),    m_gwSurfEl);
    form->addRow(tr("A1 (&GW coeff.)"),   m_gwA1);
    form->addRow(tr("B1 (GW &expon.)"),   m_gwB1);
    form->addRow(tr("A2 (Surf. c&oeff.)"),m_gwA2);
    form->addRow(tr("B2 (Surf. e&xpon.)"),m_gwB2);
    form->addRow(tr("A3 (interaction)"), m_gwA3);
    form->addRow(tr("Threshold Twgr"),   m_gwTw);
    form->addRow(tr("Hstar"),            m_gwHstar);

    auto *applyBtn = new QPushButton(tr("Apply"), grp);
    form->addRow(QString(), applyBtn);
    connect(applyBtn, &QPushButton::clicked, this, [this]() {
        const int s = subIdx();
        if (s < 0) return;
        // Combo index 0 is "(none)" → -1; otherwise the object index.
        const int aq = m_gwAquifer->currentIndex() - 1;
        const int nd = m_gwNode->currentIndex() - 1;
        swmm_subcatch_set_aquifer(m_ref.engine, s, aq);
        swmm_subcatch_set_gw_node(m_ref.engine, s, nd);
        const int rc = swmm_subcatch_set_gw_params(m_ref.engine, s,
            m_gwSurfEl->value(), m_gwA1->value(), m_gwB1->value(),
            m_gwA2->value(), m_gwB2->value(), m_gwA3->value(),
            m_gwTw->value(), m_gwHstar->value());
        if (rc != SWMM_OK) {
            QMessageBox::warning(this, tr("Apply Groundwater"),
                tr("Engine rejected groundwater set (error %1).").arg(rc));
            return;
        }
        refreshActivePage();
    });

    vlay->addWidget(grp);
    m_stack->addWidget(page);
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
    m_stack->addWidget(page);
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
        // Populate the combo with every land use once.
        const int nLu = swmm_landuse_count(e);
        if (m_luCombo->count() == 0) {
            for (int i = 0; i < nLu; ++i)
                if (const char *id = swmm_landuse_id(e, i))
                    m_luCombo->addItem(QString::fromUtf8(id));
        }
        m_luTable->setRowCount(0);
        int assigned = 0;
        for (int i = 0; i < nLu; ++i) {
            double frac = 0.0;
            if (swmm_subcatch_get_coverage(e, s, i, &frac) != SWMM_OK) continue;
            if (frac <= 0.0) continue;   // only show assigned coverages
            const int row = m_luTable->rowCount();
            m_luTable->insertRow(row);
            const char *id = swmm_landuse_id(e, i);
            m_luTable->setItem(row, 0, new QTableWidgetItem(
                id ? QString::fromUtf8(id) : QString()));
            m_luTable->setItem(row, 1, new QTableWidgetItem(
                QString::number(frac, 'g', 6)));
            ++assigned;
        }
        m_luSummary->setText(tr("<b>%1</b> land use(s) assigned coverage "
                                "(of %2 defined). Set coverage to 0 to remove.")
                                 .arg(assigned).arg(nLu));
        m_ref.summary = assigned > 0 ? tr("%1 land use(s)").arg(assigned)
                                     : tr("(none)");
        break;
    }
    case SubcatchCompoundEditRef::Groundwater: {
        // Aquifer + node combos: "(none)" then every object, once.
        if (m_gwAquifer->count() == 0) {
            m_gwAquifer->addItem(tr("(none)"));
            const int nA = swmm_aquifer_count(e);
            for (int i = 0; i < nA; ++i)
                if (const char *id = swmm_aquifer_id(e, i))
                    m_gwAquifer->addItem(QString::fromUtf8(id));
        }
        if (m_gwNode->count() == 0) {
            m_gwNode->addItem(tr("(none)"));
            const int nN = swmm_node_count(e);
            for (int i = 0; i < nN; ++i)
                if (const char *id = swmm_node_id(e, i))
                    m_gwNode->addItem(QString::fromUtf8(id));
        }
        int aq = -1, nd = -1;
        swmm_subcatch_get_aquifer(e, s, &aq);
        swmm_subcatch_get_gw_node(e, s, &nd);
        m_gwAquifer->setCurrentIndex(aq >= 0 ? aq + 1 : 0);
        m_gwNode->setCurrentIndex(nd >= 0 ? nd + 1 : 0);
        double surf=0,a1=0,b1=0,a2=0,b2=0,a3=0,tw=0,hstar=0;
        swmm_subcatch_get_gw_params(e, s, &surf, &a1, &b1, &a2, &b2, &a3, &tw, &hstar);
        m_gwSurfEl->setValue(surf);
        m_gwA1->setValue(a1); m_gwB1->setValue(b1);
        m_gwA2->setValue(a2); m_gwB2->setValue(b2);
        m_gwA3->setValue(a3);
        m_gwTw->setValue(tw); m_gwHstar->setValue(hstar);
        const bool has = aq >= 0;
        m_gwSummary->setText(has
            ? tr("Aquifer assigned; edit and Apply to update routing.")
            : tr("No aquifer assigned. Pick one and Apply to enable groundwater."));
        m_ref.summary = has ? tr("aquifer set") : tr("(none)");
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
    }
}
