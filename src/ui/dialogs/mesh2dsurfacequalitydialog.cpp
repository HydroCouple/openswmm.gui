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
#include "ui/dialogs/mesh2dsectionroweditor.h"
#include "ui/dialogs/mesh2dsectionrowmodel.h"

#include <QDialogButtonBox>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

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

    if (!engine)
        setEditable(false, tr("No model is open."));
    else if (!canEdit(engine))
        setEditable(false, tr("A simulation is running. These rows seed the surface when it "
                              "starts, so they cannot be changed now — reset the run to edit "
                              "them."));
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

void Mesh2DSurfaceQualityDialog::setEditable(bool on, const QString &whyNot)
{
    m_editable = on;
    for (auto *editor : m_editors)
        editor->setEditable(on);
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
