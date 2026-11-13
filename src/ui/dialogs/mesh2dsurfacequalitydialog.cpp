/**
 * @file      mesh2dsurfacequalitydialog.cpp
 * @brief     S7 GUI half (2026-10-30) — the mesh surface-quality row editor.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/mesh2dsurfacequalitydialog.h"

#include "core/unitsystem.h"
#include "ui/dialogs/mesh2dboundaryqualitymodel.h"
#include "ui/dialogs/mesh2dsectionroweditor.h"
#include "ui/dialogs/mesh2dsectionrowmodel.h"
#include "ui/processlock.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace openswmmvis::ui
{

namespace
{
/*! Species column: a combo of the model's species. Concentration column: a C-locale number
 *  field that keeps every digit (the default spin box rounds to two decimals). */
class BoundaryRowDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        if (index.column() == Mesh2DBoundaryQualityModel::SpeciesColumn) {
            auto *combo = new QComboBox(parent);
            combo->addItems(index.data(Mesh2DBoundaryQualityModel::ChoicesRole).toStringList());
            return combo;
        }
        if (index.column() == Mesh2DBoundaryQualityModel::ConcentrationColumn) {
            auto *edit = new QLineEdit(parent);
            auto *validator = new QDoubleValidator(edit);
            validator->setLocale(QLocale::c());
            validator->setNotation(QDoubleValidator::ScientificNotation);
            edit->setValidator(validator);
            return edit;
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(editor))
            combo->setCurrentText(index.data(Qt::EditRole).toString());
        else if (auto *edit = qobject_cast<QLineEdit *>(editor))
            edit->setText(QLocale::c().toString(index.data(Qt::EditRole).toDouble(), 'g', 17));
        else
            QStyledItemDelegate::setEditorData(editor, index);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(editor)) {
            model->setData(index, combo->currentText(), Qt::EditRole);
        } else if (auto *edit = qobject_cast<QLineEdit *>(editor)) {
            bool ok = false;
            const double value = QLocale::c().toDouble(edit->text().trimmed(), &ok);
            if (ok)
                model->setData(index, value, Qt::EditRole);
        } else {
            QStyledItemDelegate::setModelData(editor, model, index);
        }
    }
};

bool canEdit(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
} // namespace

Mesh2DSurfaceQualityDialog::Mesh2DSurfaceQualityDialog(SWMM_Engine engine, QWidget *parent,
                                                       const UnitSystem *units)
    : QDialog(parent), m_engine(engine)
{
    setObjectName(QStringLiteral("Mesh2DSurfaceQualityDialog"));
    setWindowTitle(tr("2D Surface Quality"));
    auto *outer = new QVBoxLayout(this);
    m_banner = new QLabel(this);
    m_banner->setWordWrap(true);
    m_banner->setFrameShape(QFrame::StyledPanel);
    m_banner->setContentsMargins(8, 6, 8, 6);
    m_banner->hide();
    outer->addWidget(m_banner);

    const QString scopes = tr("Rows apply to all cells (<b>*</b>), every cell with a <b>tag</b>, "
                              "or one <b>cell</b>; the most specific row wins.");
    struct Page {
        Mesh2DSectionRowModel::Section section;
        const char *name;
        QString title, hint;
    };
    const QList<Page> pages = {
        {Mesh2DSectionRowModel::Section::Coverages, "sqCoverages", tr("Coverages"),
         tr("Land uses on the mesh surface, with the percent of cell area each covers. A row is "
            "the whole land-use set of its scope. %1 Buildup, washoff and sweeping follow each "
            "land use's [BUILDUP], [WASHOFF] and sweeping settings.")
             .arg(scopes)},
        {Mesh2DSectionRowModel::Section::Loadings, "sqLoadings", tr("Initial buildup"),
         tr("Pollutant buildup on the cells at the start of the run, divided among their covered "
            "land uses. Cells without a row start from the buildup reached after DRY_DAYS. %1")
             .arg(scopes)},
        {Mesh2DSectionRowModel::Section::CurbLength, "sqCurbLength", tr("Curb length"),
         tr("Curb length on the cells, needed by land uses whose buildup is per curb length. %1")
             .arg(scopes)},
        {Mesh2DSectionRowModel::Section::SurfaceInitialQuality, "sqInitialQuality",
         tr("Initial quality"),
         tr("Concentration of a transported species in the water standing on the cells at the "
            "start; a dry cell receives nothing. %1")
             .arg(scopes)},
    };
    m_tabs = new QTabWidget(this);
    const QStringList tags = meshCellTags(engine);
    const bool si = !units || units->isSI();
    for (const Page &page : pages) {
        auto *model = new Mesh2DSectionRowModel(page.section, this);
        model->setObjectName(QString::fromLatin1(page.name));
        model->setProjectUnits(si);
        model->load(engine);
        model->setTagChoices(tags);
        auto *editor = new Mesh2DSectionRowEditor(model, page.hint, m_tabs);
        m_tabs->addTab(editor, page.title);
        m_models.append(model);
        m_editors.append(editor);
    }
    // Phase 4d (review G-03): concentrations of the water entering through outer edges.
    m_boundary = new Mesh2DBoundaryQualityModel(this);
    m_boundary->setObjectName(QStringLiteral("sqBoundaryInflow"));
    m_boundary->load(engine);
    m_boundaryPage = new QWidget(m_tabs);
    m_boundaryPage->setObjectName(QStringLiteral("sqBoundaryInflowPage"));
    auto *page = new QVBoxLayout(m_boundaryPage);
    auto *boundaryHint = new QLabel(
        tr("Concentration of each species in the water that enters the mesh through an outer "
           "edge, in the species' own units (temperature in \u00B0C). An edge is a cell and its "
           "local edge; interior edges take no inflow."),
        m_boundaryPage);
    boundaryHint->setWordWrap(true);
    page->addWidget(boundaryHint);
    m_targetBar = new QWidget(m_boundaryPage);
    m_targetBar->setObjectName(QStringLiteral("sqBoundaryTargetBar"));
    auto *bar = new QHBoxLayout(m_targetBar);
    bar->setContentsMargins(0, 0, 0, 0);
    m_targetSummary = new QLabel(m_targetBar);
    m_targetSummary->setObjectName(QStringLiteral("sqBoundaryTargetSummary"));
    m_targetSummary->setWordWrap(true);
    m_targetSpecies = new QComboBox(m_targetBar);
    m_targetSpecies->setObjectName(QStringLiteral("sqBoundaryTargetSpecies"));
    m_targetSpecies->setAccessibleName(tr("Species for the selected edges"));
    m_targetSpecies->addItems(m_boundary->species());
    m_targetValue = new QLineEdit(m_targetBar);
    m_targetValue->setObjectName(QStringLiteral("sqBoundaryTargetValue"));
    m_targetValue->setAccessibleName(tr("Concentration for the selected edges"));
    m_targetValue->setPlaceholderText(tr("Concentration"));
    {
        auto *validator = new QDoubleValidator(m_targetValue);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::ScientificNotation);
        m_targetValue->setValidator(validator);
    }
    m_targetSet = new QPushButton(tr("Set for exterior edges"), m_targetBar);
    m_targetSet->setObjectName(QStringLiteral("sqBoundaryTargetSet"));
    bar->addWidget(m_targetSummary, 1);
    bar->addWidget(m_targetSpecies);
    bar->addWidget(m_targetValue);
    bar->addWidget(m_targetSet);
    m_targetBar->hide();
    m_targetSummary->hide(); // shown with the bar when the dialog opens on edges
    page->addWidget(m_targetBar);
    m_boundaryTable = new QTableView(m_boundaryPage);
    m_boundaryTable->setObjectName(QStringLiteral("sqBoundaryInflowTable"));
    m_boundaryTable->setAccessibleName(tr("Boundary inflow rows"));
    m_boundaryTable->setModel(m_boundary);
    m_boundaryTable->setItemDelegate(new BoundaryRowDelegate(m_boundaryTable));
    m_boundaryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_boundaryTable->horizontalHeader()->setStretchLastSection(true);
    page->addWidget(m_boundaryTable, 1);
    auto *rowButtons = new QHBoxLayout;
    m_addRow = new QPushButton(tr("Add row"), m_boundaryPage);
    m_removeRow = new QPushButton(tr("Remove rows"), m_boundaryPage);
    rowButtons->addWidget(m_addRow);
    rowButtons->addWidget(m_removeRow);
    rowButtons->addStretch(1);
    page->addLayout(rowButtons);
    m_tabs->addTab(m_boundaryPage, tr("Boundary inflow"));
    connect(m_addRow, &QPushButton::clicked, this, [this] {
        const QStringList species = m_boundary->species();
        m_boundary->addRow({0, 0, species.isEmpty() ? QString() : species.first(), 0.0});
        m_boundaryTable->scrollToBottom();
    });
    connect(m_removeRow, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        for (const QModelIndex &index : m_boundaryTable->selectionModel()->selectedRows())
            rows.append(index.row());
        std::sort(rows.begin(), rows.end(), std::greater<int>());
        for (int row : rows)
            m_boundary->removeRowAt(row);
    });
    connect(m_targetSet, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const double value = QLocale::c().toDouble(m_targetValue->text().trimmed(), &ok);
        if (!ok) {
            QMessageBox::warning(this, windowTitle(), tr("Enter the concentration to set."));
            return;
        }
        m_boundary->setForEdges(m_targetExterior, m_targetSpecies->currentText(), value);
    });
    outer->addWidget(m_tabs, 1);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (applyChanges())
            accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this] { applyChanges(); });
    outer->addWidget(m_buttons);
    resize(820, 480);

    const QString locked = processLockReason(engine, LockableProcess::SurfaceQuality);
    if (!engine)
        setEditable(false, tr("No model is open."));
    else if (!canEdit(engine))
        setEditable(false, tr("A simulation is running. These rows seed the surface when it "
                              "starts, so they cannot be changed now — reset the run to edit "
                              "them."));
    else if (!locked.isEmpty()) // switched off: inspectable but read-only (plan §5)
        setEditable(false, locked);
    else
        setEditable(true, QString());
    if (units) {
        connect(units, &UnitSystem::unitsChanged, this,
                &Mesh2DSurfaceQualityDialog::invalidateContext);
        connect(units, &QObject::destroyed, this, &Mesh2DSurfaceQualityDialog::invalidateContext);
    }
}

void Mesh2DSurfaceQualityDialog::invalidateContext()
{
    m_engine = nullptr;
    m_editable = false;
    setEnabled(false);
    QDialog::reject();
}

void Mesh2DSurfaceQualityDialog::setTargetEdges(const QList<QPair<int, int>> &edges)
{
    if (!m_boundary)
        return;
    QString error;
    const auto scope = Mesh2DBoundaryQualityModel::classifyEdges(m_engine, edges, &error);
    m_targetExterior = scope.exterior;
    const QSet<QPair<int, int>> unique(edges.cbegin(), edges.cend());
    QStringList parts{tr("%1 exterior").arg(scope.exterior.size())};
    if (scope.interior)
        parts << tr("%1 interior").arg(scope.interior);
    if (scope.invalid)
        parts << tr("%1 not on the mesh").arg(scope.invalid);
    const QString count = unique.size() == 1 ? tr("1 selected edge")
                                             : tr("%1 selected edges").arg(unique.size());
    m_targetSummary->setText(error.isEmpty()
                                 ? QStringLiteral("%1: %2").arg(count, parts.join(QStringLiteral(", ")))
                                 : error);
    m_targetSummary->show();
    m_targetBar->show();
    m_targetSet->setEnabled(m_editable && !m_targetExterior.isEmpty());
    // Select the rows the selected edges already have.
    const QSet<QPair<int, int>> wanted(scope.exterior.cbegin(), scope.exterior.cend());
    m_boundaryTable->clearSelection();
    for (int row = 0; row < m_boundary->rows().size(); ++row) {
        const auto &r = m_boundary->rows()[row];
        if (wanted.contains({r.cell, r.edge}))
            m_boundaryTable->selectionModel()->select(
                m_boundary->index(row, 0),
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    m_tabs->setCurrentWidget(m_boundaryPage);
}

void Mesh2DSurfaceQualityDialog::setEditable(bool on, const QString &whyNot)
{
    m_editable = on;
    for (auto *editor : m_editors)
        editor->setEditable(on);
    if (m_boundaryTable) {
        m_boundaryTable->setEditTriggers(on ? QAbstractItemView::DoubleClicked |
                                                  QAbstractItemView::EditKeyPressed |
                                                  QAbstractItemView::AnyKeyPressed
                                            : QAbstractItemView::NoEditTriggers);
        m_addRow->setEnabled(on);
        m_removeRow->setEnabled(on);
        m_targetSpecies->setEnabled(on);
        m_targetValue->setEnabled(on);
        m_targetSet->setEnabled(on && !m_targetExterior.isEmpty());
    }
    if (!on)
        m_buttons->setStandardButtons(QDialogButtonBox::Close);
    m_banner->setText(whyNot);
    m_banner->setVisible(!whyNot.isEmpty());
}

bool Mesh2DSurfaceQualityDialog::applyChanges()
{
    if (!m_engine || !m_editable)
        return false;
    if (auto *focus = focusWidget())
        focus->clearFocus(); // commit an open cell editor
    QList<Mesh2DSectionRowModel *> dirty;
    for (auto *model : m_models)
        if (model->isDirty())
            dirty.append(model);
    const bool boundaryDirty = m_boundary && m_boundary->isDirty();
    if (dirty.isEmpty() && !boundaryDirty)
        return true;
    QString error = processLockReason(m_engine, LockableProcess::SurfaceQuality);
    for (auto *model : dirty)
        if (error.isEmpty())
            error = model->validateCommit(m_engine);
    if (error.isEmpty() && boundaryDirty)
        error = m_boundary->validateCommit(m_engine);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }
    QPointer<Mesh2DSurfaceQualityDialog> self = this;
    emit changesMayHaveBeenApplied();
    if (!self || !m_engine)
        return false;
    QList<Mesh2DSectionRowModel *> written;
    for (auto *model : dirty) {
        error = model->commit(m_engine, false);
        if (!error.isEmpty())
            break;
        written.append(model);
    }
    bool boundaryWritten = false;
    if (error.isEmpty() && boundaryDirty) {
        error = m_boundary->commit(m_engine);
        boundaryWritten = true; // a failed write may have changed part of the rows
    }
    if (!error.isEmpty()) {
        QStringList failed;
        if (boundaryWritten) {
            const QString restore = m_boundary->restoreLoaded(m_engine);
            if (!restore.isEmpty())
                failed << restore;
        }
        for (auto *model : written) {
            const QString restore = model->restoreLoaded(m_engine);
            if (!restore.isEmpty())
                failed << restore;
        }
        error += failed.isEmpty()
                     ? tr(" Earlier sections were restored; your draft remains available.")
                     : tr(" Rollback incomplete: %1. Reload the saved model before further "
                          "editing.")
                           .arg(failed.join(QStringLiteral("; ")));
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }
    for (auto *model : dirty)
        model->acceptChanges();
    if (boundaryDirty)
        m_boundary->acceptChanges();
    emit applied();
    return true;
}

} // namespace openswmmvis::ui
