/*!
 * \file   profilepathpickerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/profilepathpickerdialog.h"

#include "layers/swmmmodellayer.h"
#include "render/categoricalpalette.h"
#include "ui/util/numerictablewidgetitem.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

// Tiny color-chip swatch for the leftmost table column.
QPixmap chip(const QColor &c, int sizePx = 14)
{
    QPixmap p(sizePx, sizePx);
    p.fill(Qt::transparent);
    QPainter g(&p);
    g.setRenderHint(QPainter::Antialiasing, true);
    g.setBrush(c);
    g.setPen(QPen(c.darker(140), 1));
    g.drawEllipse(1, 1, sizePx - 2, sizePx - 2);
    return p;
}

} // namespace

ProfilePathPickerDialog::ProfilePathPickerDialog(
    SWMMModelLayer *model,
    const QVector<ProfileRouter::Path> &paths,
    bool truncated,
    QWidget *parent)
    : QDialog(parent),
      m_model(model),
      m_paths(paths)
{
    setWindowTitle(tr("Select Profile Path"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("ProfilePathPickerDialog"));
    // Non-modal by default — the host (MapToolSelectProfile) reconfigures
    // window flags / modality at show time so the dialog floats above the
    // main window while leaving the map interactive (pan / zoom / hover
    // candidate paths).
    setModal(false);
    resize(560, 320);

    auto *layout = new QVBoxLayout(this);

    const QString headerText = truncated
        ? tr("Showing %1 candidate paths between the selected endpoints "
             "(search was truncated — more paths may exist). "
             "Hover a row to highlight that path on the map; click OK to confirm.")
              .arg(m_paths.size())
        : tr("%1 candidate paths found between the selected endpoints. "
             "Hover a row to highlight that path on the map; click OK to confirm.")
              .arg(m_paths.size());
    auto *header = new QLabel(headerText, this);
    header->setWordWrap(true);
    layout->addWidget(header);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        { tr(""), tr("Length"), tr("Conduits"), tr("Other"), tr("Drop") });
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setMouseTracking(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setColumnWidth(0, 26);
    layout->addWidget(m_table, /*stretch=*/1);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(m_buttons);

    populateTable();
    m_table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    m_table->setSortingEnabled(true);

    connect(m_table, &QTableWidget::currentCellChanged,
            this, [this](int curRow, int curCol, int prevRow, int prevCol) {
        Q_UNUSED(curCol) Q_UNUSED(prevCol)
        onCurrentRowChanged(curRow, prevRow);
    });
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, [this](int row, int /*col*/) {
        if (auto *item = m_table->item(row, 0))
            emit zoomToPathRequested(item->data(Qt::UserRole).toInt());
    });
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this]() {
        auto *item = m_table->item(m_table->currentRow(), 0);
        m_selectedIdx = item ? item->data(Qt::UserRole).toInt() : -1;
        if (m_selectedIdx < 0 && !m_paths.isEmpty()) m_selectedIdx = 0;
        accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, [this]() {
        m_selectedIdx = -1;
        reject();
    });

    // Default: select first row so OK is meaningful without extra clicks.
    if (!m_paths.isEmpty())
        m_table->selectRow(0);
}

int ProfilePathPickerDialog::selectedPathIndex() const
{
    return m_selectedIdx;
}

void ProfilePathPickerDialog::onCurrentRowChanged(int currentRow, int previousRow)
{
    Q_UNUSED(previousRow)
    auto *item = m_table->item(currentRow, 0);
    emit hoveredPathChanged(item ? item->data(Qt::UserRole).toInt() : -1);
}

void ProfilePathPickerDialog::populateTable()
{
    m_table->setRowCount(m_paths.size());
    for (int i = 0; i < m_paths.size(); ++i) {
        double lengthFt = 0.0;
        int    conduits = 0, nonConduits = 0;
        double dropFt = 0.0;
        summarizePath(i, lengthFt, conduits, nonConduits, dropFt);

        auto *colorItem = new QTableWidgetItem();
        colorItem->setData(Qt::UserRole, i);
        colorItem->setIcon(QIcon(chip(CategoricalPalette::at(i))));
        m_table->setItem(i, 0, colorItem);

        m_table->setItem(i, 1, new openswmmvis::ui::NumericTableWidgetItem(QString::number(lengthFt, 'f', 1)));
        m_table->setItem(i, 2, new openswmmvis::ui::NumericTableWidgetItem(QString::number(conduits)));
        m_table->setItem(i, 3, new openswmmvis::ui::NumericTableWidgetItem(QString::number(nonConduits)));
        m_table->setItem(i, 4, new openswmmvis::ui::NumericTableWidgetItem(QString::number(dropFt, 'f', 2)));
    }
}

void ProfilePathPickerDialog::summarizePath(int pathIdx,
                                            double &lengthOut,
                                            int &conduitsOut,
                                            int &nonConduitsOut,
                                            double &dropOut) const
{
    lengthOut = 0.0;
    conduitsOut = 0;
    nonConduitsOut = 0;
    dropOut = 0.0;
    if (pathIdx < 0 || pathIdx >= m_paths.size()) return;
    const auto &p = m_paths[pathIdx];

    // Sum weights for length (router's weight == length for conduits and a
    // small fixed value for non-conduits).
    lengthOut = p.weight;

    // Inspect each edge's kind via the model.  We don't have direct access
    // here, but we can use SWMMModelLayer's engine handle if present.
    SWMM_Engine eng = m_model ? m_model->engine() : nullptr;
    if (eng) {
        for (int engLinkIdx : p.linkIds) {
            int type = 0;
            swmm_link_get_type(eng, engLinkIdx, &type);
            if (type == 0) ++conduitsOut;
            else           ++nonConduitsOut;
        }

        // Drop: invertElev of head node minus invertElev of tail node.
        if (!p.nodes.isEmpty()) {
            double zHead = 0.0, zTail = 0.0;
            swmm_node_get_invert_elev(eng, p.nodes.first(), &zHead);
            swmm_node_get_invert_elev(eng, p.nodes.last(),  &zTail);
            dropOut = zHead - zTail;
        }
    }
}
