/*!
 * \file   basemaphttpheaderswidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "ui/widgets/basemaphttpheaderswidget.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

BasemapHttpHeadersWidget::BasemapHttpHeadersWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // --- Referer row -------------------------------------------------------
    auto *refRow = new QHBoxLayout;
    refRow->addWidget(new QLabel(tr("Referer:"), this));
    m_refererEdit = new QLineEdit(this);
    m_refererEdit->setPlaceholderText(tr("https://example.com  (optional)"));
    refRow->addWidget(m_refererEdit);
    root->addLayout(refRow);

    // --- Collapsible extras group ------------------------------------------
    m_extrasGroup = new QGroupBox(tr("Additional headers"), this);
    m_extrasGroup->setCheckable(true);
    m_extrasGroup->setChecked(false);   // collapsed by default
    auto *gLayout = new QVBoxLayout(m_extrasGroup);
    gLayout->setSpacing(4);

    m_table = new QTableWidget(0, 2, m_extrasGroup);
    m_table->setHorizontalHeaderLabels({ tr("Header"), tr("Value") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setMinimumHeight(80);
    gLayout->addWidget(m_table);

    auto *btnRow = new QHBoxLayout;
    m_addBtn    = new QPushButton(tr("Add row"),    m_extrasGroup);
    m_removeBtn = new QPushButton(tr("Remove row"), m_extrasGroup);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_removeBtn);
    btnRow->addStretch();
    gLayout->addLayout(btnRow);

    root->addWidget(m_extrasGroup);

    connect(m_addBtn,    &QPushButton::clicked, this, &BasemapHttpHeadersWidget::onAddRow);
    connect(m_removeBtn, &QPushButton::clicked, this, &BasemapHttpHeadersWidget::onRemoveRow);
}

// ---------------------------------------------------------------------------

void BasemapHttpHeadersWidget::onAddRow()
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(QString()));
    m_table->setItem(row, 1, new QTableWidgetItem(QString()));
    m_table->editItem(m_table->item(row, 0));
}

void BasemapHttpHeadersWidget::onRemoveRow()
{
    const QList<QTableWidgetSelectionRange> sel = m_table->selectedRanges();
    // Remove from bottom to top to keep row indices valid
    QList<int> rows;
    for (const auto &r : sel)
        for (int i = r.topRow(); i <= r.bottomRow(); ++i)
            rows.append(i);
    std::sort(rows.rbegin(), rows.rend());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    for (int r : rows)
        m_table->removeRow(r);
}

// ---------------------------------------------------------------------------

BasemapHttpHeaders BasemapHttpHeadersWidget::headers() const
{
    BasemapHttpHeaders result;

    const QString referer = m_refererEdit->text().trimmed();
    if (!referer.isEmpty())
        result.insert("referer", referer);

    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QString key = m_table->item(r, 0)
            ? m_table->item(r, 0)->text().trimmed()
            : QString();
        const QString val = m_table->item(r, 1)
            ? m_table->item(r, 1)->text().trimmed()
            : QString();
        if (!key.isEmpty())
            result.insert(key, val);
    }
    return result;
}

void BasemapHttpHeadersWidget::setHeaders(const BasemapHttpHeaders &headers)
{
    m_refererEdit->setText(headers.value("referer"));

    m_table->setRowCount(0);
    for (auto it = headers.cbegin(); it != headers.cend(); ++it) {
        if (it.key().toLower() == "referer") continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        m_table->setItem(row, 0, new QTableWidgetItem(it.key()));
        m_table->setItem(row, 1, new QTableWidgetItem(it.value()));
    }

    // Auto-expand if there are extra headers
    if (m_table->rowCount() > 0)
        m_extrasGroup->setChecked(true);
}

void BasemapHttpHeadersWidget::clear()
{
    m_refererEdit->clear();
    m_table->setRowCount(0);
    m_extrasGroup->setChecked(false);
}
