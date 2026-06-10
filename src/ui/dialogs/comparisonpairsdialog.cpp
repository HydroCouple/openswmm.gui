/*!
 * \file   comparisonpairsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/comparisonpairsdialog.h"

#include "plot/comparisonplotmodel.h"
#include "plot/plotattribute.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

using namespace openswmmvis::plot;

namespace openswmmvis::ui {

ComparisonPairsDialog::ComparisonPairsDialog(ComparisonPlotModel *model,
                                             QWidget *parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(tr("Configure 1v1 Comparisons"));
    buildUi();
    refreshPairList();
}

void ComparisonPairsDialog::buildUi()
{
    auto *lay = new QVBoxLayout(this);

    auto *hint = new QLabel(
        tr("Pair any two series that share an attribute row. When no pairs "
           "are configured, 1v1 plots pair the baseline run against every "
           "other run automatically."), this);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // Pair builder row: X combo, "vs", Y combo, Add.
    auto *builder = new QHBoxLayout;
    m_xCombo = new QComboBox(this);
    m_yCombo = new QComboBox(this);
    for (int s = 0; m_model && s < m_model->seriesCount(); ++s) {
        const QString label = seriesLabel(s);
        m_xCombo->addItem(label, s);
        m_yCombo->addItem(label, s);
    }
    m_addBtn = new QPushButton(tr("Add"), this);
    connect(m_addBtn, &QPushButton::clicked,
            this, &ComparisonPairsDialog::onAddClicked);
    builder->addWidget(m_xCombo, 1);
    builder->addWidget(new QLabel(tr("vs"), this));
    builder->addWidget(m_yCombo, 1);
    builder->addWidget(m_addBtn);
    lay->addLayout(builder);

    m_pairList = new QListWidget(this);
    m_pairList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lay->addWidget(m_pairList, 1);

    auto *btnRow = new QHBoxLayout;
    m_removeBtn = new QPushButton(tr("Remove Selected"), this);
    connect(m_removeBtn, &QPushButton::clicked,
            this, &ComparisonPairsDialog::onRemoveClicked);
    m_resetBtn = new QPushButton(tr("Reset to Auto"), this);
    m_resetBtn->setToolTip(tr("Drop all configured pairs and return to "
                              "baseline-vs-all auto pairing"));
    connect(m_resetBtn, &QPushButton::clicked,
            this, &ComparisonPairsDialog::onResetClicked);
    btnRow->addWidget(m_removeBtn);
    btnRow->addWidget(m_resetBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
}

QString ComparisonPairsDialog::seriesLabel(int seriesIndex) const
{
    const SeriesSpec &spec = m_model->spec(seriesIndex);
    if (!spec.legendOverride.isEmpty())
        return spec.legendOverride;
    const QString runLabel =
        (spec.runIndex >= 0 && spec.runIndex < m_model->runSourceCount())
            ? m_model->runSource(spec.runIndex).label
            : tr("(unknown run)");
    const QString objLabel =
        spec.objectRef.kind == ObjectRef::Kind::Mesh2DCell
            ? tr("Cell %1").arg(spec.objectRef.triIdx)
            : spec.objectRef.name;
    return QStringLiteral("%1 — %2 (%3)")
        .arg(runLabel, objLabel, labelFor(spec.attribute));
}

void ComparisonPairsDialog::refreshPairList()
{
    m_pairList->clear();
    if (!m_model)
        return;
    for (const ComparisonPair &p : m_model->pairs()) {
        m_pairList->addItem(QStringLiteral("%1   ↔   %2")
                                .arg(seriesLabel(p.xSeriesIndex),
                                     seriesLabel(p.ySeriesIndex)));
    }
}

void ComparisonPairsDialog::onAddClicked()
{
    if (!m_model || m_xCombo->count() == 0)
        return;
    ComparisonPair pair;
    pair.xSeriesIndex = m_xCombo->currentData().toInt();
    pair.ySeriesIndex = m_yCombo->currentData().toInt();
    if (m_model->addPair(pair) < 0) {
        QMessageBox::information(this, tr("Couldn't add pair"),
            tr("Pairs must use two different series that share the same "
               "attribute, and can't repeat an existing pair."));
        return;
    }
    refreshPairList();
}

void ComparisonPairsDialog::onRemoveClicked()
{
    if (!m_model)
        return;
    // Remove bottom-up so indices stay valid while erasing.
    QList<int> rows;
    const auto selected = m_pairList->selectedItems();
    rows.reserve(selected.size());
    for (QListWidgetItem *it : selected)
        rows.push_back(m_pairList->row(it));
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int r : rows)
        m_model->removePair(r);
    refreshPairList();
}

void ComparisonPairsDialog::onResetClicked()
{
    if (!m_model)
        return;
    m_model->clearPairs();
    refreshPairList();
}

} // namespace openswmmvis::ui
