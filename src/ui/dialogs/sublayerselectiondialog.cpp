/*!
 * \file   sublayerselectiondialog.cpp
 * \author OpenSWMM GUI
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/sublayerselectiondialog.h"
#include "ui/theme/iconfactory.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

SublayerSelectionDialog::SublayerSelectionDialog(
    const QString &sourcePath,
    const QList<GISVectorLayer::OgrSublayerInfo> &sublayers,
    QWidget *parent)
    : QDialog(parent)
    , m_sublayers(sublayers)
{
    setWindowTitle(tr("Select Layers to Add"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("SublayerSelectionDialog"));
    resize(640, 440);
    buildUi(sourcePath);
}

void SublayerSelectionDialog::buildUi(const QString &sourcePath)
{
    auto *root = new QVBoxLayout(this);

    auto *header = new QLabel(
        tr("<b>%1</b><br>%2 layer(s) found — check the ones to add:")
            .arg(QFileInfo(sourcePath).fileName())
            .arg(m_sublayers.size()),
        this);
    header->setTextFormat(Qt::RichText);
    header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(header);

    // ── Name filter ─────────────────────────────────────────────────────────
    auto *filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel(tr("Filter:"), this));
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(tr("type to filter by layer name"));
    connect(m_filter, &QLineEdit::textChanged, this,
            [this](const QString &t) { applyNameFilter(t); });
    filterRow->addWidget(m_filter, 1);
    root->addLayout(filterRow);

    // ── Table ────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(m_sublayers.size(), 4, this);
    m_table->setHorizontalHeaderLabels(
        {tr("Layer"), tr("Geometry"), tr("Features"), tr("CRS")});
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);

    for (int r = 0; r < m_sublayers.size(); ++r) {
        const GISVectorLayer::OgrSublayerInfo &s = m_sublayers.at(r);

        auto *nameItem = new QTableWidgetItem(s.name);
        nameItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        nameItem->setCheckState(Qt::Checked);   // default: add all
        m_table->setItem(r, 0, nameItem);

        m_table->setItem(r, 1, new QTableWidgetItem(s.geometryType));
        m_table->setItem(r, 2, new QTableWidgetItem(
            s.featureCount < 0 ? tr("?") : QString::number(s.featureCount)));
        m_table->setItem(r, 3, new QTableWidgetItem(
            s.crsDescription.isEmpty() ? tr("(unknown)") : s.crsDescription));
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    // ── Select all / none / invert ───────────────────────────────────────────
    auto *btnRow  = new QHBoxLayout;
    auto *allBtn  = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("SelectAll")),
                                    tr("Select all"), this);
    auto *noneBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("SelectNone")),
                                    tr("Select none"), this);
    auto *invBtn  = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("InvertSelection")),
                                    tr("Invert"), this);
    connect(allBtn,  &QPushButton::clicked, this, [this] { setAllChecked(true); });
    connect(noneBtn, &QPushButton::clicked, this, [this] { setAllChecked(false); });
    connect(invBtn,  &QPushButton::clicked, this, [this] { invertChecked(); });
    btnRow->addWidget(allBtn);
    btnRow->addWidget(noneBtn);
    btnRow->addWidget(invBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);

    // ── OK / Cancel ──────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void SublayerSelectionDialog::setAllChecked(bool checked)
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->isRowHidden(r))
            continue;
        if (auto *it = m_table->item(r, 0))
            it->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
}

void SublayerSelectionDialog::invertChecked()
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->isRowHidden(r))
            continue;
        if (auto *it = m_table->item(r, 0))
            it->setCheckState(it->checkState() == Qt::Checked ? Qt::Unchecked
                                                              : Qt::Checked);
    }
}

void SublayerSelectionDialog::applyNameFilter(const QString &text)
{
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const bool match = text.isEmpty()
            || m_sublayers.at(r).name.contains(text, Qt::CaseInsensitive);
        m_table->setRowHidden(r, !match);
    }
}

QStringList SublayerSelectionDialog::selectedLayerNames() const
{
    QStringList names;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (auto *it = m_table->item(r, 0))
            if (it->checkState() == Qt::Checked)
                names << m_sublayers.at(r).name;
    return names;
}

QList<GISVectorLayer::OgrSublayerInfo>
SublayerSelectionDialog::selectedSublayers() const
{
    QList<GISVectorLayer::OgrSublayerInfo> out;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (auto *it = m_table->item(r, 0))
            if (it->checkState() == Qt::Checked)
                out << m_sublayers.at(r);
    return out;
}
