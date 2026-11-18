/**
 * @file      frozengrounddialog.cpp
 * @brief     Editor for the frozen-ground rows of subcatchments and mesh cells.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/frozengrounddialog.h"

#include "ui/dialogs/frozengroundrowmodel.h"
#include "ui/dialogs/mesh2dsectionroweditor.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>

namespace openswmmvis::ui
{

namespace
{
bool canEdit(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
} // namespace

FrozenGroundDialog::FrozenGroundDialog(SWMM_Engine engine, QWidget *parent)
    : QDialog(parent), m_engine(engine)
{
    setObjectName(QStringLiteral("FrozenGroundDialog"));
    setWindowTitle(tr("Frozen Ground"));
    auto *outer = new QVBoxLayout(this);
    m_banner = new QLabel(this);
    m_banner->setWordWrap(true);
    m_banner->setFrameShape(QFrame::StyledPanel);
    m_banner->setContentsMargins(8, 6, 8, 6);
    m_banner->hide();
    outer->addWidget(m_banner);

    auto *intro = new QLabel(
        tr("A row gives the ground a layer that conducts heat, freezes and thaws. Infiltration "
           "falls to zero as the layer freezes. All values are SI, whatever the project's flow "
           "units. The air temperature comes from the project's temperature data."),
        this);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    m_tabs = new QTabWidget(this);
    outer->addWidget(m_tabs, 1);
    struct PageSpec {
        FrozenGroundRowModel::Target target;
        QString title, hint;
    };
    const PageSpec pages[] = {
        {FrozenGroundRowModel::Target::Subcatchments, tr("Subcatchments"),
         tr("One row per subcatchment, or <b>*</b> for every subcatchment without its own row. "
            "The row also limits the subcatchment's LID units.")},
        {FrozenGroundRowModel::Target::Mesh, tr("2D Mesh"),
         tr("One row per mesh tag, or <b>*</b> for every cell. A tag row wins over the "
            "<b>*</b> row; a cell that no row covers is not limited.")},
    };
    QStringList errors;
    for (int p = 0; p < 2; ++p) {
        auto *model = new FrozenGroundRowModel(pages[p].target, this);
        if (const QString error = model->load(engine); !error.isEmpty())
            errors << error;
        m_models.append(model);

        auto *page = new QWidget(m_tabs);
        auto *layout = new QVBoxLayout(page);
        auto *hint = new QLabel(pages[p].hint, page);
        hint->setWordWrap(true);
        layout->addWidget(hint);
        auto *view = new QTableView(page);
        view->setObjectName(p == 0 ? QStringLiteral("frozenGroundSubcatchmentTable")
                                   : QStringLiteral("frozenGroundMeshTable"));
        view->setModel(model);
        view->setItemDelegate(new Mesh2DChoiceDelegate(view));
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        layout->addWidget(view, 1);
        m_views.append(view);

        auto *row = new QHBoxLayout;
        auto *add = new QPushButton(tr("Add Row"), page);
        auto *remove = new QPushButton(tr("Remove Row"), page);
        add->setAutoDefault(false);
        remove->setAutoDefault(false);
        add->setObjectName(p == 0 ? QStringLiteral("frozenGroundAddSubcatchmentRow")
                                  : QStringLiteral("frozenGroundAddMeshRow"));
        row->addWidget(add);
        row->addWidget(remove);
        row->addStretch(1);
        layout->addLayout(row);
        m_rowButtons << add << remove;
        connect(add, &QPushButton::clicked, this, [this, p] { addRow(p); });
        connect(remove, &QPushButton::clicked, this, [this, p] { removeSelectedRows(p); });
        m_tabs->addTab(page, pages[p].title);
    }

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    outer->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this] {
        if (!m_editable || applyChanges())
            accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    if (auto *apply = m_buttons->button(QDialogButtonBox::Apply))
        connect(apply, &QPushButton::clicked, this, [this] { applyChanges(); });

    if (!engine)
        setEditable(false, tr("No project is open."));
    else if (!errors.isEmpty())
        setEditable(false, errors.join(QLatin1Char(' ')));
    else if (!canEdit(engine))
        setEditable(false, tr("The model cannot be edited while a simulation is running."));
    else
        setEditable(true, {});
    resize(980, 420);
}

void FrozenGroundDialog::setCurrentPage(Page page)
{
    m_tabs->setCurrentIndex(static_cast<int>(page));
}

void FrozenGroundDialog::invalidateContext()
{
    m_engine = nullptr;
    reject();
}

void FrozenGroundDialog::addRow(int page)
{
    if (!m_editable)
        return;
    FrozenGroundRowModel *model = m_models[page];
    const int row = model->appendRow();
    if (row < 0) {
        QMessageBox::information(this, windowTitle(),
                                 page == MeshPage
                                     ? tr("Every mesh tag already has a row.")
                                     : tr("Every subcatchment already has a row."));
        return;
    }
    m_views[page]->selectRow(row);
    m_views[page]->scrollToBottom();
}

void FrozenGroundDialog::removeSelectedRows(int page)
{
    if (!m_editable)
        return;
    QList<int> rows;
    for (const QModelIndex &index : m_views[page]->selectionModel()->selectedRows())
        rows << index.row();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        m_models[page]->removeRows(row, 1);
}

void FrozenGroundDialog::setEditable(bool on, const QString &whyNot)
{
    m_editable = on;
    for (auto *view : m_views)
        view->setEditTriggers(on ? QAbstractItemView::DoubleClicked |
                                       QAbstractItemView::EditKeyPressed |
                                       QAbstractItemView::AnyKeyPressed
                                 : QAbstractItemView::NoEditTriggers);
    for (auto *button : m_rowButtons)
        button->setEnabled(on);
    if (!on)
        m_buttons->setStandardButtons(QDialogButtonBox::Close);
    m_banner->setText(whyNot);
    m_banner->setVisible(!whyNot.isEmpty());
}

bool FrozenGroundDialog::applyChanges()
{
    if (!m_engine || !m_editable)
        return false;
    if (auto *focus = focusWidget())
        focus->clearFocus(); // commit an open cell editor
    QList<FrozenGroundRowModel *> dirty;
    for (auto *model : m_models)
        if (model->isDirty())
            dirty.append(model);
    if (dirty.isEmpty())
        return true;
    QString error;
    for (auto *model : dirty)
        if (error.isEmpty())
            error = model->validateCommit(m_engine);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), error);
        return false;
    }
    QPointer<FrozenGroundDialog> self = this;
    emit changesMayHaveBeenApplied();
    if (!self || !m_engine)
        return false;
    QList<FrozenGroundRowModel *> written;
    for (auto *model : dirty) {
        error = model->commit(m_engine, false);
        if (!error.isEmpty())
            break;
        written.append(model);
    }
    if (!error.isEmpty()) {
        QStringList failed;
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
    emit applied();
    return true;
}

} // namespace openswmmvis::ui
