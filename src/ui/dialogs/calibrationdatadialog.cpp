/*!
 * \file   calibrationdatadialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/calibrationdatadialog.h"

#include "swmmvisprojectwindow.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

CalibrationDataDialog::CalibrationDataDialog(SWMMVisProjectWindow *projectWindow,
                                              QWidget *parent)
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
    m_table->setHorizontalHeaderLabels({
        tr("Object kind"), tr("Object name"), tr("Attribute"),
        tr("Observed file"), tr("Column")
    });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout;
    auto *addBtn    = new QPushButton(tr("Add row"), this);
    auto *rmBtn     = new QPushButton(tr("Remove"),  this);
    auto *browseBtn = new QPushButton(tr("Browse for CSV…"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(rmBtn);
    btnRow->addWidget(browseBtn);
    btnRow->addStretch(1);
    root->addLayout(btnRow);
    connect(addBtn,    &QPushButton::clicked, this, &CalibrationDataDialog::onAddClicked);
    connect(rmBtn,     &QPushButton::clicked, this, &CalibrationDataDialog::onRemoveClicked);
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
    kind->addItems({tr("Node"), tr("Link"), tr("Subcatch")});
    m_table->setCellWidget(r, 0, kind);
    m_table->setItem(r, 1, new QTableWidgetItem());
    auto *attr = new QComboBox(m_table);
    attr->addItems({tr("Depth"), tr("Head"), tr("Flow"), tr("Velocity"), tr("Runoff")});
    m_table->setCellWidget(r, 2, attr);
    m_table->setItem(r, 3, new QTableWidgetItem());
    m_table->setItem(r, 4, new QTableWidgetItem(QStringLiteral("col_1")));
}

void CalibrationDataDialog::onRemoveClicked()
{
    const int r = m_table->currentRow();
    if (r >= 0) m_table->removeRow(r);
}

void CalibrationDataDialog::onBrowseClicked()
{
    const int r = m_table->currentRow();
    if (r < 0) return;
    const QString path = QFileDialog::getOpenFileName(this,
        tr("Observed time-series file"), QString(),
        tr("CSV / TSV / TSF / DAT (*.csv *.tsv *.tsf *.dat);;All files (*)"));
    if (path.isEmpty()) return;
    if (!m_table->item(r, 3)) m_table->setItem(r, 3, new QTableWidgetItem());
    m_table->item(r, 3)->setText(path);
}

void CalibrationDataDialog::loadFromProject()
{
    QVector<CalibrationEntry> entries = entriesFromProject(m_projectWindow);
    for (const auto &e : entries) {
        const int r = m_table->rowCount();
        m_table->insertRow(r);

        auto *kind = new QComboBox(m_table);
        kind->addItems({tr("Node"), tr("Link"), tr("Subcatch")});
        const int kIdx = kind->findText(e.objectKind);
        if (kIdx >= 0) kind->setCurrentIndex(kIdx);
        m_table->setCellWidget(r, 0, kind);

        m_table->setItem(r, 1, new QTableWidgetItem(e.objectName));

        auto *attr = new QComboBox(m_table);
        attr->addItems({tr("Depth"), tr("Head"), tr("Flow"), tr("Velocity"), tr("Runoff")});
        attr->setCurrentText(openswmmvis::plot::labelFor(e.attribute));
        m_table->setCellWidget(r, 2, attr);

        m_table->setItem(r, 3, new QTableWidgetItem(e.observedPath));
        m_table->setItem(r, 4, new QTableWidgetItem(e.observedColumn));
    }
}

void CalibrationDataDialog::saveToProject()
{
    if (!m_projectWindow) return;
    // Persist into the project's QSettings group "calibrationData".
    QSettings settings;
    settings.beginGroup(QStringLiteral("project/calibrationData"));
    settings.remove(QString());   // wipe old
    settings.beginWriteArray(QStringLiteral("entries"));
    for (int r = 0; r < m_table->rowCount(); ++r) {
        auto *kind = qobject_cast<QComboBox*>(m_table->cellWidget(r, 0));
        auto *attr = qobject_cast<QComboBox*>(m_table->cellWidget(r, 2));
        if (!kind || !attr) continue;
        settings.setArrayIndex(r);
        settings.setValue(QStringLiteral("kind"),    kind->currentText());
        settings.setValue(QStringLiteral("name"),    m_table->item(r, 1) ? m_table->item(r, 1)->text() : QString());
        settings.setValue(QStringLiteral("attr"),    attr->currentText());
        settings.setValue(QStringLiteral("path"),    m_table->item(r, 3) ? m_table->item(r, 3)->text() : QString());
        settings.setValue(QStringLiteral("column"),  m_table->item(r, 4) ? m_table->item(r, 4)->text() : QString());
    }
    settings.endArray();
    settings.endGroup();
}

void CalibrationDataDialog::accept()
{
    saveToProject();
    QDialog::accept();
}

QVector<CalibrationEntry> CalibrationDataDialog::entriesFromProject(SWMMVisProjectWindow *)
{
    QVector<CalibrationEntry> out;
    QSettings settings;
    settings.beginGroup(QStringLiteral("project/calibrationData"));
    const int n = settings.beginReadArray(QStringLiteral("entries"));
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        settings.setArrayIndex(i);
        CalibrationEntry e;
        e.objectKind     = settings.value(QStringLiteral("kind")).toString();
        e.objectName     = settings.value(QStringLiteral("name")).toString();
        const QString attrLabel = settings.value(QStringLiteral("attr")).toString();
        // Reverse-lookup PlotAttribute from label.
        using openswmmvis::plot::PlotAttribute;
        if      (attrLabel.contains(QStringLiteral("Depth")))   e.attribute = PlotAttribute::NodeDepth;
        else if (attrLabel.contains(QStringLiteral("Head")))    e.attribute = PlotAttribute::NodeHead;
        else if (attrLabel.contains(QStringLiteral("Flow")))    e.attribute = PlotAttribute::LinkFlow;
        else if (attrLabel.contains(QStringLiteral("Velocity")))e.attribute = PlotAttribute::LinkVelocity;
        else if (attrLabel.contains(QStringLiteral("Runoff")))  e.attribute = PlotAttribute::SubcatchRunoff;
        e.observedPath   = settings.value(QStringLiteral("path")).toString();
        e.observedColumn = settings.value(QStringLiteral("column")).toString();
        out.append(e);
    }
    settings.endArray();
    settings.endGroup();
    return out;
}

} // namespace openswmmvis::ui
