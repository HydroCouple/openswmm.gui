/*!
 * \file   meshpage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/meshpage.h"

#include <QAbstractItemView>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "layers/swmmmodellayer.h"
#include "mesh/inpmeshwriter.h"
#include "plugins/filefilterregistry.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/theme/themehelpers.h"

namespace openswmmvis::ui
{

namespace
{
// Mesh-list item classification (stored under Qt::UserRole). The inline row
// is synthetic — it has no .2dm file on disk — so the Set Active / Remove
// handlers must branch on it.
constexpr int kMeshKindRole = Qt::UserRole;
constexpr int kMeshExternal = 0;   ///< sibling *.2dm file
constexpr int kMeshInline   = 1;   ///< mesh embedded in the project .inp
} // namespace

MeshPage::MeshPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
}

QString MeshPage::title() const
{
    return tr("Mesh");
}

void MeshPage::set2DModuleSetter(std::function<void(bool)> setter)
{
    m_set2DModuleEnabled = std::move(setter);
}

void MeshPage::read()
{
    refreshMeshList();
}

int MeshPage::write()
{
    // The mesh reference is patched the moment Set Active / Import / Remove
    // runs, not deferred to the write pass.
    return 0;
}

void MeshPage::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    auto *header = new QLabel(tr(
        "Pick which 2D mesh configuration the engine reads: an external "
        "mesh file (.2dm, referenced via [2D_MESH_FILE]) or the inline mesh "
        "embedded in the project .inp. The list shows the .2dm files sitting "
        "next to the project — use Import… to bring one in from elsewhere on "
        "disk. New meshes are generated from the editing toolbar's Generate "
        "Mesh tool."), this);
    header->setWordWrap(true);
    vlay->addWidget(header);

    m_meshDirLabel = new QLabel(this);
    m_meshDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meshDirLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    // Both labels below carry a filesystem path. Unwrapped, their
    // minimumSizeHint is the full single-line width, which a deep project
    // directory pushes past the page — the whole Mesh page then scrolls
    // horizontally and clips the text above. Wrapping keeps the path fully
    // visible (and selectable) without dictating the page width.
    m_meshDirLabel->setWordWrap(true);
    vlay->addWidget(m_meshDirLabel);

    m_meshList = new QListWidget(this);
    m_meshList->setSelectionMode(QAbstractItemView::SingleSelection);
    vlay->addWidget(m_meshList, 1);

    // Two rows of two, not one row of four: four side-by-side push buttons are
    // wider than this page's viewport at the dialog's natural width, which
    // pushed the whole page into a horizontal scroll and clipped the text above.
    auto *btnRow = new QGridLayout;
    auto *btnSetActive = new QPushButton(tr("Set Active"), this);
    btnSetActive->setToolTip(tr("Patch [2D_MESH_FILE] to point at the "
                                 "selected configuration."));
    auto *btnRemove    = new QPushButton(tr("Remove"), this);
    btnRemove->setToolTip(tr("Delete the selected .2dm from disk."));
    auto *btnImport    = new QPushButton(tr("Import…"), this);
    btnImport->setToolTip(tr("Browse for an existing .2dm anywhere on disk, "
                              "copy it into the project folder and load it as "
                              "the active mesh."));
    auto *btnRefresh   = new QPushButton(tr("Refresh"), this);
    btnRow->addWidget(btnSetActive, 0, 0);
    btnRow->addWidget(btnRemove,    0, 1);
    btnRow->addWidget(btnImport,    1, 0);
    btnRow->addWidget(btnRefresh,   1, 1);
    btnRow->setColumnStretch(2, 1);   // keep the block left-aligned
    vlay->addLayout(btnRow);

    m_meshActiveLabel = new QLabel(this);
    m_meshActiveLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_meshActiveLabel->setWordWrap(true);   // may hold an absolute mesh path
    vlay->addWidget(m_meshActiveLabel);

    connect(btnRefresh, &QPushButton::clicked, this,
            &MeshPage::refreshMeshList);
    connect(btnSetActive, &QPushButton::clicked, this,
            &MeshPage::onMeshSetActive);
    connect(btnRemove, &QPushButton::clicked, this,
            &MeshPage::onMeshRemove);
    connect(btnImport, &QPushButton::clicked, this,
            &MeshPage::onMeshImport);
    // Importing needs a project window to attach the mesh layer to.
    btnImport->setEnabled(ctx_.projectWindow() != nullptr);

    refreshMeshList();
}

void MeshPage::refreshMeshList()
{
    if (!m_meshList || !m_meshDirLabel || !m_meshActiveLabel) return;
    m_meshList->clear();

    // Search the directory next to the active model (.inp). Without a
    // layer (e.g. dialog opened against a synthesized blank project)
    // we silently no-op — Generate New will create the first mesh.
    QString modelPath;
    if (ctx_.modelLayer()) modelPath = ctx_.modelLayer()->modelFilePath();
    const QFileInfo modelFi(modelPath);
    const QDir dir = modelPath.isEmpty() ? QDir() : modelFi.absoluteDir();

    m_meshDirLabel->setText(modelPath.isEmpty()
        ? tr("Search directory: <none — save the project first>")
        : tr("Search directory: %1").arg(dir.absolutePath()));

    // Read the .inp once: needed both to discover an inline mesh (embedded
    // [2D_*] sections, no sibling .2dm) and to read the current
    // [2D_MESH_FILE] reference.
    QString inpText;
    if (!modelPath.isEmpty())
    {
        QFile f(modelPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            inpText = QString::fromUtf8(f.readAll());
    }

    // External configurations: one row per sibling *.2dm file.
    if (!modelPath.isEmpty())
    {
        const QStringList meshes = dir.entryList(
            QStringList{QStringLiteral("*.2dm")},
            QDir::Files | QDir::Readable, QDir::Name);
        for (const QString &name : meshes)
        {
            auto *item = new QListWidgetItem(name);
            item->setData(kMeshKindRole, kMeshExternal);
            m_meshList->addItem(item);
        }
    }

    // Inline configuration: the engine reads mesh geometry straight from the
    // .inp when [2D_VERTICES] + [2D_TRIANGLES] are present. Surface it as a
    // selectable row (pinned to the top) so a freshly-generated inline mesh
    // appears here and the user can switch back to it from an external file.
    const bool hasInline =
        inpText.indexOf(QStringLiteral("[2D_VERTICES]"),  0, Qt::CaseInsensitive) >= 0 &&
        inpText.indexOf(QStringLiteral("[2D_TRIANGLES]"), 0, Qt::CaseInsensitive) >= 0;
    if (hasInline)
    {
        auto *item = new QListWidgetItem(
            tr("(Inline mesh — embedded in project .inp)"));
        item->setData(kMeshKindRole, kMeshInline);
        m_meshList->insertItem(0, item);
    }

    // Probe the .inp for a current [2D_MESH_FILE] reference. Tolerant — the
    // section may be absent (engine reads the inline mesh, if any).
    QString active;
    {
        const int sectIdx = inpText.indexOf(QStringLiteral("[2D_MESH_FILE]"),
                                             0, Qt::CaseInsensitive);
        if (sectIdx >= 0)
        {
            // Walk forward to the first non-comment, non-blank line and pull
            // the FILE token.
            int p = inpText.indexOf(QChar('\n'), sectIdx);
            while (p > 0 && p < inpText.size())
            {
                const int nl = inpText.indexOf(QChar('\n'), p + 1);
                const QString line = inpText.mid(p + 1, (nl < 0 ? inpText.size() : nl) - p - 1).trimmed();
                if (!line.isEmpty() && !line.startsWith(QStringLiteral(";"))
                    && !line.startsWith(QChar('[')))
                {
                    // Format: "FILE  <path>".
                    const auto parts = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  Qt::SkipEmptyParts);
                    if (parts.size() >= 2 && parts.first().compare(
                            QStringLiteral("FILE"), Qt::CaseInsensitive) == 0)
                        active = parts.mid(1).join(QChar(' '));
                    break;
                }
                if (line.startsWith(QChar('['))) break;  // next section
                if (nl < 0) break;
                p = nl;
            }
        }
    }

    // The active configuration is the external file when [2D_MESH_FILE] is
    // present, otherwise the inline mesh (if any). Reflect that in the label
    // and pre-select the matching row.
    if (!active.isEmpty())
    {
        m_meshActiveLabel->setText(tr("Active mesh reference: %1").arg(active));
        const QString activeName = QFileInfo(active).fileName();
        for (int i = 0; i < m_meshList->count(); ++i)
            if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshExternal
                && m_meshList->item(i)->text() == activeName)
                m_meshList->setCurrentRow(i);
    }
    else if (hasInline)
    {
        m_meshActiveLabel->setText(
            tr("Active mesh: inline mesh embedded in project .inp"));
        for (int i = 0; i < m_meshList->count(); ++i)
            if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshInline)
                m_meshList->setCurrentRow(i);
    }
    else
    {
        m_meshActiveLabel->setText(
            tr("Active mesh reference: <none — generate a 2D mesh first>"));
    }
}

void MeshPage::onMeshSetActive()
{
    if (!m_meshList || !ctx_.modelLayer()) return;

    QListWidgetItem *item = m_meshList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Set Active Mesh"),
            tr("Select a mesh (.2dm) from the list first."));
        return;
    }

    const QString modelPath = ctx_.modelLayer()->modelFilePath();
    if (modelPath.isEmpty()) {
        QMessageBox::warning(this, tr("Set Active Mesh"),
            tr("Save the project first — the [2D_MESH_FILE] reference is "
               "written into the .inp on disk."));
        return;
    }

    QString err;
    if (item->data(kMeshKindRole).toInt() == kMeshInline)
    {
        // Inline mesh: drop any [2D_MESH_FILE] reference so the engine reads
        // the mesh sections embedded directly in the .inp.
        if (!mesh::InpMeshWriter::clearMeshFileRef(modelPath, &err)) {
            QMessageBox::critical(this, tr("Set Active Mesh"),
                tr("Could not switch to the inline mesh:\n%1").arg(err));
            return;
        }
        // Mirror into the engine's in-memory model so a save doesn't re-add a
        // stale reference. Empty clears it (engine reverts to inline mesh).
        if (ctx_.engine())
            swmm_options_set_ext(ctx_.engine(), "MESH_FILE", "");
    }
    else
    {
        // External mesh: point [2D_MESH_FILE] at the selected .2dm.
        const QString meshPath =
            QFileInfo(modelPath).absoluteDir().absoluteFilePath(item->text());
        if (!mesh::InpMeshWriter::writeMeshFileRef(modelPath, meshPath, &err)) {
            QMessageBox::critical(this, tr("Set Active Mesh"),
                tr("Could not update [2D_MESH_FILE]:\n%1").arg(err));
            return;
        }
        // Mirror the reference into the engine's in-memory model. Without this
        // the engine re-serialises the .inp on the next save with mesh_file
        // empty and drops [2D_MESH_FILE] — the model silently reverts to 1D.
        if (ctx_.engine())
            swmm_options_set_ext(ctx_.engine(), "MESH_FILE",
                                 item->text().toUtf8().constData());
    }

    // Selecting an active mesh implies the user wants 2D on. Flip the
    // module checkbox so the corresponding tab + persistence follow.
    // The 2D module box lives on Models / Processes; the dialog supplies the
    // setter so this page never reaches into another page's widget.
    if (m_set2DModuleEnabled) m_set2DModuleEnabled(true);

    refreshMeshList();
}

void MeshPage::onMeshRemove()
{
    if (!m_meshList || !ctx_.modelLayer()) return;

    QListWidgetItem *item = m_meshList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("Remove Mesh"),
            tr("Select a mesh (.2dm) from the list first."));
        return;
    }

    if (item->data(kMeshKindRole).toInt() == kMeshInline) {
        QMessageBox::information(this, tr("Remove Mesh"),
            tr("The inline mesh is embedded in the project .inp — it can't be "
               "deleted from here. Re-generate the mesh, or set an external "
               ".2dm active, to replace it."));
        return;
    }

    const QString modelPath = ctx_.modelLayer()->modelFilePath();
    if (modelPath.isEmpty()) return;

    const QString name     = item->text();
    const QString meshPath =
        QFileInfo(modelPath).absoluteDir().absoluteFilePath(name);

    const bool isActive = m_meshActiveLabel &&
        m_meshActiveLabel->text().contains(name);
    const QString question = isActive
        ? tr("\"%1\" is the active [2D_MESH_FILE] reference. Deleting it will "
             "clear the active mesh and disable 2D Surface Routing for this "
             "model.\n\nDelete it anyway?").arg(name)
        : tr("Delete \"%1\" from disk? This cannot be undone.").arg(name);

    if (QMessageBox::question(this, tr("Remove Mesh"), question,
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    QFile f(meshPath);
    if (f.exists() && !f.remove()) {
        QMessageBox::critical(this, tr("Remove Mesh"),
            tr("Could not delete %1:\n%2").arg(meshPath, f.errorString()));
        return;
    }

    // Removing the ACTIVE mesh must not leave the model half-2D: null the
    // [2D_MESH_FILE] reference in the live engine and switch the 2D module
    // off (unchecking writes IGNORE_2D YES on OK, so the next run — which
    // auto-saves and re-opens the .inp — genuinely runs 1D-only).
    if (isActive) {
        if (ctx_.engine())
            swmm_options_set_ext(ctx_.engine(), "MESH_FILE", "");
        if (m_set2DModuleEnabled) m_set2DModuleEnabled(false);
    }

    refreshMeshList();
}

void MeshPage::onMeshImport()
{
    if (!ctx_.projectWindow()) return;

    // Anywhere on disk — the whole point of this button is that the list above
    // can only ever show .2dm files already sitting next to the project.
    const QString modelPath = ctx_.modelLayer() ? ctx_.modelLayer()->modelFilePath() : QString();
    const QString startDir  = modelPath.isEmpty()
        ? QDir::homePath()
        : QFileInfo(modelPath).absolutePath();

    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import 2D Mesh"), startDir,
        tr("2D Mesh — SWMMVis or SMS 2DM (*.2dm);;All Files (*)"));
    if (path.isEmpty()) return;

    // The project window owns the copy-into-project, parse and canvas
    // adoption; the outcome comes back once, asynchronously. The mesh becomes
    // the active layer, so [2D_MESH_FILE] follows it on the next save — the
    // list below just needs to re-read the folder.
    connect(ctx_.projectWindow(), &SWMMVisProjectWindow::meshImportFinished, this,
            [this](bool ok, const QString &message, const QString &meshPath) {
                if (!ok) {
                    if (!message.isEmpty())
                        QMessageBox::warning(this, tr("Import 2D Mesh"), message);
                    return;
                }
                refreshMeshList();
                // Select the imported file and run it through Set Active, so
                // the [2D_MESH_FILE] reference this tab reports (and the .inp
                // on disk) match the layer the import just activated.
                const QString name = QFileInfo(meshPath).fileName();
                for (int i = 0; m_meshList && i < m_meshList->count(); ++i) {
                    if (m_meshList->item(i)->data(kMeshKindRole).toInt() == kMeshExternal
                        && m_meshList->item(i)->text() == name) {
                        m_meshList->setCurrentRow(i);
                        onMeshSetActive();
                        break;
                    }
                }
            },
            static_cast<Qt::ConnectionType>(Qt::SingleShotConnection));

    ctx_.projectWindow()->importMeshFileAsync(path);
}

} // namespace openswmmvis::ui
