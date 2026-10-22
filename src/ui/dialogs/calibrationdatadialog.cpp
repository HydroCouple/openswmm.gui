/**
 * @file      calibrationdatadialog.cpp
 * @brief     Maintains a table of (object, attribute, observed-CSV path) bindings.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/calibrationdatadialog.h"

#include "swmmvisprojectwindow.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include "ui/util/fileopendialog.h"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QJsonArray>
#include <QJsonObject>
#include <QTableWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui
{

CalibrationDataDialog::CalibrationDataDialog(SWMMVisProjectWindow *projectWindow, QWidget *parent)
    : QDialog(parent), m_projectWindow(projectWindow)
{
    setWindowTitle(tr("Calibration Data"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("CalibrationDataDialog"));
    resize(820, 480);
    buildUi();
    loadFromProject();
}

CalibrationDataDialog::~CalibrationDataDialog() = default;

void CalibrationDataDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {tr("Object kind"), tr("Object name"), tr("Attribute"), tr("Observed file"), tr("Column")});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(false);
    root->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(tr("Add row"), this);
    auto *rmBtn = new QPushButton(tr("Remove"), this);
    auto *browseBtn = new QPushButton(tr("Browse for CSV…"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(rmBtn);
    btnRow->addWidget(browseBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);
    connect(addBtn, &QPushButton::clicked, this, &CalibrationDataDialog::onAddClicked);
    connect(rmBtn, &QPushButton::clicked, this, &CalibrationDataDialog::onRemoveClicked);
    connect(browseBtn, &QPushButton::clicked, this, &CalibrationDataDialog::onBrowseClicked);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(bb);
}

void CalibrationDataDialog::onAddClicked()
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    auto *kind = new QComboBox(m_table);
    kind->addItem(tr("Node"), "Node");
    kind->addItem(tr("Link"), "Link");
    kind->addItem(tr("Subcatch"), "Subcatch");
    m_table->setCellWidget(r, 0, kind);
    m_table->setItem(r, 1, new QTableWidgetItem());
    auto *attr = new QComboBox(m_table);
    attr->addItem(tr("Depth"), "depth");
    attr->addItem(tr("Head"), "head");
    attr->addItem(tr("Flow"), "flow");
    attr->addItem(tr("Velocity"), "velocity");
    attr->addItem(tr("Runoff"), "runoff");
    m_table->setCellWidget(r, 2, attr);
    m_table->setItem(r, 3, new QTableWidgetItem());
    m_table->setItem(r, 4, new QTableWidgetItem(QStringLiteral("col_1")));
}

void CalibrationDataDialog::onRemoveClicked()
{
    const int r = m_table->currentRow();
    if (r >= 0)
        m_table->removeRow(r);
}

void CalibrationDataDialog::onBrowseClicked()
{
    const int r = m_table->currentRow();
    if (r < 0)
        return;
    const QString path = openswmmvis::ui::FileOpenDialog::getOpenFileName(
        QStringLiteral("timeseries"), this, tr("Observed time-series file"), QString(),
        tr("CSV / TSV / TSF / DAT (*.csv *.tsv *.tsf *.dat);;All files (*)"));
    if (path.isEmpty())
        return;
    if (!m_table->item(r, 3))
        m_table->setItem(r, 3, new QTableWidgetItem());
    m_table->item(r, 3)->setText(path);
}

void CalibrationDataDialog::loadFromProject()
{
    QVector<CalibrationEntry> entries = entriesFromProject(m_projectWindow);
    for (const auto &e : entries) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);

        auto *kind = new QComboBox(m_table);
        kind->addItem(tr("Node"), "Node");
        kind->addItem(tr("Link"), "Link");
        kind->addItem(tr("Subcatch"), "Subcatch");
        const int kIdx = kind->findData(e.objectKind);
        if (kIdx >= 0)
            kind->setCurrentIndex(kIdx);
        m_table->setCellWidget(r, 0, kind);

        m_table->setItem(r, 1, new QTableWidgetItem(e.objectName));

        auto *attr = new QComboBox(m_table);
        attr->addItem(tr("Depth"), "depth");
        attr->addItem(tr("Head"), "head");
        attr->addItem(tr("Flow"), "flow");
        attr->addItem(tr("Velocity"), "velocity");
        attr->addItem(tr("Runoff"), "runoff");
        using A = openswmmvis::plot::PlotAttribute;
        const QString key = (e.attribute == A::NodeDepth || e.attribute == A::LinkDepth) ? "depth"
            : e.attribute == A::NodeHead ? "head" : e.attribute == A::LinkFlow ? "flow"
            : e.attribute == A::LinkVelocity ? "velocity" : "runoff";
        attr->setCurrentIndex(attr->findData(key));
        m_table->setCellWidget(r, 2, attr);

        m_table->setItem(r, 3, new QTableWidgetItem(e.observedPath));
        m_table->setItem(r, 4, new QTableWidgetItem(e.observedColumn));
    }
}

void CalibrationDataDialog::saveToProject()
{
    if (!m_projectWindow)
        return;
    QJsonArray entries;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *kind = qobject_cast<QComboBox *>(m_table->cellWidget(r, 0));
        auto *attr = qobject_cast<QComboBox *>(m_table->cellWidget(r, 2));
        if (!kind || !attr) continue;
        auto text = [&](int c) { return m_table->item(r,c) ? m_table->item(r,c)->text() : QString(); };
        entries.append(QJsonObject{{"kind",kind->currentData().toString()}, {"name",text(1)},
            {"attribute",attr->currentData().toString()}, {"path",text(3)}, {"column",text(4)}});
    }
    if (entries != m_projectWindow->calibrationEntries()) {
        m_projectWindow->setCalibrationEntries(entries);
        m_projectWindow->setHasChanges(true);
    }
}

void CalibrationDataDialog::accept()
{
    saveToProject();
    QDialog::accept();
}

QVector<CalibrationEntry> CalibrationDataDialog::entriesFromProject(SWMMVisProjectWindow *pw)
{
    QVector<CalibrationEntry> out;
    if (!pw) return out;
    for (const auto &value : pw->calibrationEntries()) {
        const auto row = value.toObject();
        CalibrationEntry e;
        e.objectKind = row.value("kind").toString();
        e.objectName = row.value("name").toString();
        const auto attr = row.value("attribute").toString();
        using A = openswmmvis::plot::PlotAttribute;
        if (attr == "depth") e.attribute = e.objectKind == "Link" ? A::LinkDepth : A::NodeDepth;
        else if (attr == "head") e.attribute = A::NodeHead;
        else if (attr == "flow") e.attribute = A::LinkFlow;
        else if (attr == "velocity") e.attribute = A::LinkVelocity;
        else if (attr == "runoff") e.attribute = A::SubcatchRunoff;
        e.observedPath = row.value("path").toString();
        e.observedColumn = row.value("column").toString();
        out.append(e);
    }
    return out;
}

} // namespace openswmmvis::ui
