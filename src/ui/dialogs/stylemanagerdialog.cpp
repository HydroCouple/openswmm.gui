/*!
 * \file   stylemanagerdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/stylemanagerdialog.h"
#include "ui/theme/themehelpers.h"

#include "layers/openswmmvislayer.h"
#include "render/rule.h"
#include "render/rulelist.h"
#include "render/rulelistio.h"
// Slice Z.17b — QGIS .qml interop.
#include "render/qmlrulelistio.h"
#include "ui/theme/iconfactory.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

constexpr const char *kStyleSuffix = "swmm-rule.json";

// Slice Z.17b — dispatch native vs QGIS by file suffix. We treat any
// path ending in ".qml" as QGIS .qml; everything else as native
// .swmm-rule.json. The content-sniff fallback isn't needed here because
// the StyleManagerDialog only exposes these two formats — users who
// hand a foreign file get a parse error from the matching loader.
[[nodiscard]] bool isQmlPath(const QString &path)
{
    return path.endsWith(QStringLiteral(".qml"), Qt::CaseInsensitive);
}

[[nodiscard]] OpenSWMM::Render::RuleListIoResult
saveByPath(const OpenSWMM::Render::RuleList *rl, const QString &path)
{
    if (isQmlPath(path))
        return OpenSWMM::Render::QmlRuleListIO::save(rl, path);
    return OpenSWMM::Render::RuleListIO::save(rl, path);
}

[[nodiscard]] OpenSWMM::Render::RuleListIoResult
loadByPath(const QString &path, OpenSWMM::Render::RuleList *rl)
{
    if (isQmlPath(path))
        return OpenSWMM::Render::QmlRuleListIO::load(path, rl);
    return OpenSWMM::Render::RuleListIO::load(path, rl);
}

/*! Build a small human-readable preview from a freshly-loaded RuleList.
 *  We don't pretty-print the JSON itself because the file is on disk —
 *  the user can open it in an editor if they want raw schema. The
 *  preview is a summary: one line per Rule plus its renderer id. */
[[nodiscard]] QString summarisePreview(const OpenSWMM::Render::RuleList &rl,
                                        const QStringList &warnings,
                                        int rulesLoaded,
                                        int rulesSkipped)
{
    QString out;
    out += QObject::tr("Rules loaded: %1\n").arg(rulesLoaded);
    if (rulesSkipped > 0)
        out += QObject::tr("Rules skipped: %1\n").arg(rulesSkipped);
    out += QObject::tr("\n— Rules —\n");
    for (int i = 0; i < rl.count(); ++i) {
        const auto *r = rl.at(i);
        if (!r) continue;
        out += QStringLiteral("  %1. %2  (%3)%4\n")
                   .arg(i + 1)
                   .arg(r->name())
                   .arg(r->renderer() ? r->renderer()->rendererId()
                                       : QStringLiteral("?"))
                   .arg(r->isVisible() ? QString() : QObject::tr("  [hidden]"));
    }
    if (!warnings.isEmpty()) {
        out += QObject::tr("\n— Warnings —\n");
        for (const QString &w : warnings)
            out += QStringLiteral("  • ") + w + QLatin1Char('\n');
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------------

StyleManagerDialog::StyleManagerDialog(OpenSWMMVisLayer *activeLayer,
                                        QWidget *parent)
    : QDialog(parent), m_layer(activeLayer)
{
    setWindowTitle(tr("Style Manager"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("StyleManagerDialog"));
    resize(720, 480);

    m_libraryDir = resolveLibraryDir();

    auto *outer = new QVBoxLayout(this);

    auto *splitRow = new QHBoxLayout;

    // ── Left: library list + folder hint ────────────────────────────────
    auto *leftBox = new QGroupBox(tr("Style library"), this);
    auto *leftLay = new QVBoxLayout(leftBox);

    m_libDirLabel = new QLabel(leftBox);
    m_libDirLabel->setText(m_libraryDir);
    m_libDirLabel->setWordWrap(true);
    m_libDirLabel->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    m_libDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    leftLay->addWidget(m_libDirLabel);

    m_openDirBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Browse")),
                                   tr("Open folder…"), leftBox);
    leftLay->addWidget(m_openDirBtn);

    m_list = new QListWidget(leftBox);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLay->addWidget(m_list, 1);

    splitRow->addWidget(leftBox, 1);

    // ── Right: preview + actions ────────────────────────────────────────
    auto *rightBox = new QGroupBox(tr("Preview"), this);
    auto *rightLay = new QVBoxLayout(rightBox);

    m_preview = new QPlainTextEdit(rightBox);
    m_preview->setReadOnly(true);
    m_preview->setLineWrapMode(QPlainTextEdit::NoWrap);
    rightLay->addWidget(m_preview, 1);

    auto *actionRow = new QHBoxLayout;
    m_applyBtn  = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("LayerStyling")),
                                  tr("Apply to layer"), rightBox);
    m_saveBtn   = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Save")),
                                  tr("Save current…"), rightBox);
    m_importBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Open")),
                                  tr("Import…"), rightBox);
    m_exportBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("ExportCsv")),
                                  tr("Export…"), rightBox);
    m_deleteBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                                  tr("Delete"), rightBox);
    actionRow->addWidget(m_applyBtn);
    actionRow->addWidget(m_saveBtn);
    actionRow->addWidget(m_importBtn);
    actionRow->addWidget(m_exportBtn);
    actionRow->addWidget(m_deleteBtn);
    rightLay->addLayout(actionRow);

    splitRow->addWidget(rightBox, 2);
    outer->addLayout(splitRow, 1);

    // Close button at the bottom — this is a read/write management dialog,
    // not an editor with a discrete Apply→close cycle.
    m_btns = new QDialogButtonBox(QDialogButtonBox::Close, this);
    outer->addWidget(m_btns);

    // ── Wiring ──────────────────────────────────────────────────────────
    connect(m_list, &QListWidget::currentRowChanged,
            this, [this](int) { onSelectionChanged(); });
    connect(m_openDirBtn, &QPushButton::clicked,
            this, &StyleManagerDialog::onOpenLibraryFolder);
    connect(m_applyBtn,  &QPushButton::clicked, this, &StyleManagerDialog::onApply);
    connect(m_saveBtn,   &QPushButton::clicked, this, &StyleManagerDialog::onSaveCurrent);
    connect(m_importBtn, &QPushButton::clicked, this, &StyleManagerDialog::onImport);
    connect(m_exportBtn, &QPushButton::clicked, this, &StyleManagerDialog::onExport);
    connect(m_deleteBtn, &QPushButton::clicked, this, &StyleManagerDialog::onDelete);
    connect(m_btns, &QDialogButtonBox::rejected, this, &QDialog::accept);

    // Apply / Save Current need a layer; if there's none, the buttons sit
    // disabled. Import / Export / Delete still work on the library directly.
    const bool haveLayer = (m_layer != nullptr);
    m_applyBtn->setEnabled(false);   // also gated by selection — see onSelectionChanged
    m_saveBtn ->setEnabled(haveLayer && m_layer->ruleList() != nullptr);

    refreshLibrary();
    onSelectionChanged();
}

StyleManagerDialog::~StyleManagerDialog() = default;

// ---------------------------------------------------------------------------

QString StyleManagerDialog::resolveLibraryDir() const
{
    // QStandardPaths::AppLocalDataLocation gives a per-user, per-app
    // writable location. Creating "/styles" under it keeps the library
    // discoverable but doesn't pollute the root of that directory.
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    if (base.isEmpty()) return {};
    const QString dir = base + QStringLiteral("/styles");
    QDir().mkpath(dir);
    return dir;
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::refreshLibrary()
{
    const QString priorSelection = currentFilePath();

    m_list->clear();
    if (m_libraryDir.isEmpty()) return;

    QDir d(m_libraryDir);
    // Slice Z.17b — the library accepts both native .swmm-rule.json and
    // QGIS .qml entries; the suffix is preserved on disk so the saver
    // / loader pair (saveByPath / loadByPath) routes to the right
    // implementation per entry.
    const QStringList filters = {
        QStringLiteral("*.") + kStyleSuffix,
        QStringLiteral("*.qml"),
    };
    const auto entries = d.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : entries) {
        auto *item = new QListWidgetItem(fi.completeBaseName());
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        m_list->addItem(item);
        if (fi.absoluteFilePath() == priorSelection)
            m_list->setCurrentItem(item);
    }
    if (!m_list->currentItem() && m_list->count() > 0)
        m_list->setCurrentRow(0);
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::refreshPreview()
{
    m_preview->clear();
    const QString path = currentFilePath();
    if (path.isEmpty()) return;

    OpenSWMM::Render::RuleList tmp;
    const auto result = loadByPath(path, &tmp);
    if (!result.ok) {
        m_preview->setPlainText(tr("Failed to load: %1").arg(result.error));
        return;
    }
    m_preview->setPlainText(summarisePreview(
        tmp, result.warnings, result.rulesLoaded, result.rulesSkipped));
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::onSelectionChanged()
{
    refreshPreview();
    const bool haveSelection = (m_list->currentItem() != nullptr);
    const bool haveLayer     = (m_layer != nullptr);
    const bool haveLayerRL   = haveLayer && m_layer->ruleList() != nullptr;

    m_applyBtn ->setEnabled(haveSelection && haveLayerRL);
    m_exportBtn->setEnabled(haveSelection);
    m_deleteBtn->setEnabled(haveSelection);
}

// ---------------------------------------------------------------------------

QString StyleManagerDialog::currentFilePath() const
{
    if (!m_list) return {};
    const auto *item = m_list->currentItem();
    if (!item) return {};
    return item->data(Qt::UserRole).toString();
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::onApply()
{
    const QString path = currentFilePath();
    if (path.isEmpty() || !m_layer) return;
    auto *rl = m_layer->ruleList();
    if (!rl) return;

    const auto result = loadByPath(path, rl);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Apply style"),
            tr("Could not load %1:\n%2").arg(path, result.error));
        return;
    }
    if (result.rulesSkipped > 0 || !result.warnings.isEmpty()) {
        QString msg = tr("Applied %1 rule(s).").arg(result.rulesLoaded);
        if (result.rulesSkipped > 0)
            msg += QLatin1Char('\n') +
                   tr("Skipped %1 rule(s) with unknown renderer ids.")
                       .arg(result.rulesSkipped);
        QMessageBox::information(this, tr("Apply style"), msg);
    }
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::onSaveCurrent()
{
    if (!m_layer) return;
    const auto *rl = m_layer->ruleList();
    if (!rl) return;

    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        tr("Save current style"),
        tr("Library entry name:"),
        QLineEdit::Normal,
        m_layer->name(),
        &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    // Sanitize so it can sit on disk safely. Replace path separators +
    // unusual characters with '_'; leave dots intact (the suffix is added
    // back below).
    QString safe = name.trimmed();
    safe.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")),
                 QStringLiteral("_"));

    const QString path = m_libraryDir + QLatin1Char('/') + safe +
                          QLatin1Char('.') + QLatin1String(kStyleSuffix);

    if (QFile::exists(path)) {
        const auto r = QMessageBox::question(this, tr("Overwrite?"),
            tr("\"%1\" already exists in the library. Overwrite?").arg(safe),
            QMessageBox::Yes | QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }

    const auto result = saveByPath(rl, path);
    if (!result.ok) {
        QMessageBox::warning(this, tr("Save current style"),
            tr("Could not write %1:\n%2").arg(path, result.error));
        return;
    }
    refreshLibrary();
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::onImport()
{
    const QString src = QFileDialog::getOpenFileName(
        this,
        tr("Import style"),
        QString(),
        tr("Style files (*.%1 *.qml);;SWMM Rule (*.%1);;QGIS QML (*.qml);;All files (*)")
            .arg(kStyleSuffix));
    if (src.isEmpty()) return;

    const QString dst = m_libraryDir + QLatin1Char('/') +
                         QFileInfo(src).fileName();
    if (QFile::exists(dst)) {
        const auto r = QMessageBox::question(this, tr("Overwrite?"),
            tr("A file with that name already exists in the library. Overwrite?"),
            QMessageBox::Yes | QMessageBox::No);
        if (r != QMessageBox::Yes) return;
        QFile::remove(dst);
    }
    if (!QFile::copy(src, dst)) {
        QMessageBox::warning(this, tr("Import style"),
            tr("Could not copy %1 to %2.").arg(src, dst));
        return;
    }
    refreshLibrary();
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::onExport()
{
    const QString src = currentFilePath();
    if (src.isEmpty()) return;
    const QString dst = QFileDialog::getSaveFileName(
        this,
        tr("Export style"),
        QFileInfo(src).fileName(),
        tr("Style files (*.%1 *.qml);;SWMM Rule (*.%1);;QGIS QML (*.qml);;All files (*)")
            .arg(kStyleSuffix));
    if (dst.isEmpty()) return;

    // Slice Z.17b — load through the source format, save through the
    // destination format. When the suffixes match, this is equivalent to
    // a file-copy round-trip (slightly slower but correct). When they
    // differ (.swmm-rule.json → .qml or vice versa), the dispatchers
    // convert: this is what makes "share with a QGIS colleague"
    // actually produce a QGIS-readable file.
    OpenSWMM::Render::RuleList tmp;
    const auto loadResult = loadByPath(src, &tmp);
    if (!loadResult.ok) {
        QMessageBox::warning(this, tr("Export style"),
            tr("Could not load %1:\n%2").arg(src, loadResult.error));
        return;
    }
    const auto saveResult = saveByPath(&tmp, dst);
    if (!saveResult.ok) {
        QMessageBox::warning(this, tr("Export style"),
            tr("Could not write %1:\n%2").arg(dst, saveResult.error));
        return;
    }
    if (!saveResult.warnings.isEmpty()) {
        QMessageBox::information(this, tr("Export style"),
            saveResult.warnings.join(QLatin1Char('\n')));
    }
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::onDelete()
{
    const QString path = currentFilePath();
    if (path.isEmpty()) return;
    const auto r = QMessageBox::question(this, tr("Delete style"),
        tr("Remove \"%1\" from the library? This deletes the file on disk.")
            .arg(QFileInfo(path).completeBaseName()),
        QMessageBox::Yes | QMessageBox::No);
    if (r != QMessageBox::Yes) return;
    if (!QFile::remove(path)) {
        QMessageBox::warning(this, tr("Delete style"),
            tr("Could not delete %1.").arg(path));
        return;
    }
    refreshLibrary();
}

// ---------------------------------------------------------------------------

void StyleManagerDialog::onOpenLibraryFolder()
{
    if (m_libraryDir.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_libraryDir));
}

} // namespace openswmmvis::ui
