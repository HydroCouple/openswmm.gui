/*!
 * \file   filespage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/filespage.h"

#include "ui/dialogs/simulationoptionsdialog.h"

#include "ui/dialogs/simulationoptionsdialog.h"
#include "ui/dialogs/wateragesourcesdialog.h"
#include "ui/dialogs/initialqualitydialog.h"
#include "ui/theme/themehelpers.h"
#include "ui/uiscrollhelpers.h"
#include "ui/dialogs/crsselectiondialog.h"
#include "ui/dialogs/simoptions/simoptionscontext.h"
#include "ui/dialogs/simoptions/simoptionspage.h"
#include "ui/dialogs/simoptions/spatialpage.h"
#include "ui/dialogs/simoptions/meshpage.h"
#include "ui/dialogs/simoptions/performancepage.h"
#include "ui/dialogs/simoptions/titlenotespage.h"
#include "ui/dialogs/hotstartsavesmodel.h"
#include "ui/widgets/relativepathpicker.h"
#include "ui/dialogs/pathbrowsedelegate.h"
#include "ui/dialogs/pluginstablemodel.h"
#include "ui/dialogs/processcomponentsmodel.h"   // U1
#include "core/preferencesmanager.h"
#include "layers/swmmmodellayer.h"
#include "mesh/inpmeshwriter.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "plugins/filefilterregistry.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include <openswmm/plugin_sdk/PluginDiscovery.hpp>
#include <qcustomeditors.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDoubleValidator>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QStandardItemModel>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>             // Slice RC.4 — built-in plugin-id lookup
#include <QSettings>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableView>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QBrush>
#include <QButtonGroup>
#include <QColor>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QRadioButton>
#include <QTabWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>

namespace openswmmvis::ui
{

FilesPage::FilesPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
    tagWidgets();
}

QString FilesPage::title() const
{
    return tr("Files / Output / Plugins");
}

void FilesPage::read()
{
    readPluginsFromEngine();
    readProcessComponentsFromEngine();   // U1
    readFilesSectionFromEngine();
    readWriterCombosFromEngine();
    readReportContentsFromEngine();
    readOutputPathsFromSettings();
}

int FilesPage::write()
{
    int n = 0;
    n += writePluginsToEngine();
    n += writeProcessComponentsToEngine();   // U1
    n += writeFilesSectionToEngine();
    n += writeWriterCombosToEngine();
    n += writeReportContentsToEngine();
    writeOutputPathsToSettings();
    return n;
}

void FilesPage::tagWidgets()
{
    // Moved verbatim from the monolith's tagOptionWidgets(): every editor that
    // reads or writes an option key carries its key, so the reachability gate
    // can prove the key has a laid-out widget behind it.
    tagOption(m_rptDisabledBox, "RPT_DISABLED");
    tagOption(m_rptInputBox, "RPT_INPUT");
    tagOption(m_rptContinuityBox, "RPT_CONTINUITY");
    tagOption(m_rptFlowstatsBox, "RPT_FLOWSTATS");
    tagOption(m_rptControlsBox, "RPT_CONTROLS");
    tagOption(m_rptAveragesBox, "RPT_AVERAGES");
    tagOption(m_signedHeadsCheck, "REPORT_SIGNED_HEADS");
    tagOption(m_rptSubcatchNoneRadio, "RPT_SUBCATCHMENTS");
    tagOption(m_rptNodeNoneRadio, "RPT_NODES");
    tagOption(m_rptLinkNoneRadio, "RPT_LINKS");
    tagOption(m_rainfallModeCombo, "RAINFALL_MODE");
    tagOption(m_runoffModeCombo, "RUNOFF_MODE");
    tagOption(m_rdiiModeCombo, "RDII_MODE");
}

void FilesPage::applyCapabilities()
{
    const EngineCapabilities &c = ctx_.caps();
    const QString mixedFlowTip =
        c.legacy ? tr("Not available in SWMM 5 (legacy engine).")
                 : tr("This engine build predates the mixed-flow "
                      "(TPA / unsteady-friction) option surface.");

    // Signed-heads output option.
    if (!c.signedHeads && m_signedHeadsCheck) {
        m_signedHeadsCheck->setEnabled(false);
        m_signedHeadsCheck->setToolTip(mixedFlowTip);
    }

    // Plugin writers and the [PLUGINS] table are new-engine only.
    if (!c.pluginWriters) {
        const QString tip = tr("Not available in SWMM 5 (legacy engine).");
        if (m_writersGroup) {
            m_writersGroup->setEnabled(false);
            m_writersGroup->setToolTip(tip);
        }
        for (QWidget *w : {static_cast<QWidget *>(m_pluginsView),
                           static_cast<QWidget *>(m_pluginsAddBtn),
                           static_cast<QWidget *>(m_pluginsRemoveBtn)}) {
            if (w) {
                w->setEnabled(false);
                w->setToolTip(tip);
            }
        }
    }
}

bool FilesPage::validate(QString *warn)
{
    return validateFilesTab(warn);
}

bool FilesPage::validateFilesTab(QString *warn)
{
    bool anyInvalid = false;

    // ── [PLUGINS] table: empty id rejected ─────────────────────────────
    //
    // Phase 3.10.6 — model-backed.  The blocking error is surfaced via
    // the @p warn channel (previously a per-cell red background, which
    // QAbstractTableModel doesn't expose without an extra role round-
    // trip); anyInvalid still stops Apply.
    if (m_pluginsModel) {
        const int n = m_pluginsModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QString id = m_pluginsModel->pathAt(r).trimmed();
            if (id.isEmpty()) {
                anyInvalid = true;
                if (warn)
                    warn->append(tr("[PLUGINS] row %1: plugin id / path is required.\n")
                                     .arg(r + 1));
            }
        }
    }

    // ── Hot-start saves table: empty path rejected ─────────────────────
    //
    // Phase 3.10.5 — table is now MVC-backed.  We can't paint per-cell
    // background through the model without an extra role round-trip, so
    // the inline error affordance is the row's row-header text and a
    // tool-tipped warning surfaced via the @p warn channel; the blocking
    // anyInvalid flag still stops Apply.
    if (m_hotstartSavesModel) {
        const int n = m_hotstartSavesModel->rowCount();
        for (int r = 0; r < n; ++r) {
            const QString p = m_hotstartSavesModel->pathAt(r).trimmed();
            if (p.isEmpty()) {
                anyInvalid = true;
                if (warn)
                    warn->append(tr("Hot-start save row %1: path is required.\n")
                                     .arg(r + 1));
            }
        }
    }

    // ── Non-blocking warnings ──────────────────────────────────────────
    if (warn) {
        auto checkSelector = [warn](QRadioButton *some, QLineEdit *list,
                                    const QString &label) {
            if (!some || !list) return;
            if (some->isChecked() && list->text().trimmed().isEmpty()) {
                *warn += FilesPage::tr(
                    "%1: \"Selected\" is chosen but the name list is empty "
                    "(will be written as NONE).\n").arg(label);
            }
        };
        checkSelector(m_rptSubcatchSomeRadio, m_rptSubcatchListEdit,
                      tr("Subcatchments"));
        checkSelector(m_rptNodeSomeRadio,     m_rptNodeListEdit,
                      tr("Nodes"));
        checkSelector(m_rptLinkSomeRadio,     m_rptLinkListEdit,
                      tr("Links"));

        auto checkParentDir = [warn](QLineEdit *edit, const QString &label) {
            if (!edit) return;
            const QString path = edit->text().trimmed();
            if (path.isEmpty()) return;
            const QFileInfo fi(path);
            const QDir parent = fi.absoluteDir();
            if (!parent.exists()) {
                *warn += FilesPage::tr(
                    "%1: parent directory does not exist (%2).\n")
                    .arg(label, QDir::toNativeSeparators(parent.absolutePath()));
            }
        };
        checkParentDir(m_reportFilePathEdit, tr("Report file"));
        checkParentDir(m_outputFilePathEdit, tr("Output file"));
    }

    return !anyInvalid;
}

// ---------------------------------------------------------------------------
// [REPORT] contents editor (Slice BV.1 — 2026-05-22)
// ---------------------------------------------------------------------------
//
// Engine surface: RPT_DISABLED / RPT_INPUT / RPT_CONTINUITY / RPT_FLOWSTATS /
// RPT_CONTROLS / RPT_AVERAGES (booleans, YES/NO) plus RPT_SUBCATCHMENTS /
// RPT_NODES / RPT_LINKS (selectors, "ALL" / "NONE" / "name1,name2,...").

void FilesPage::buildReportContentsGroup(QVBoxLayout *parentLayout,
                                                       QWidget *page)
{
    auto *grp = new QGroupBox(tr("Report contents ([REPORT])"), page);
    auto *vlay = new QVBoxLayout(grp);
    grp->setToolTip(tr(
        "Controls which summary sections and which objects appear in the "
        "simulation report (.rpt) and binary output (.out) files."));

    // ---- bool flags row ------------------------------------------------
    auto *flagsGroup = new QGroupBox(tr("Summary sections"), grp);
    auto *flagsForm  = new QFormLayout(flagsGroup);

    m_rptDisabledBox   = new QCheckBox(tr("Disable all reporting (DISABLED)"),  flagsGroup);
    m_rptInputBox      = new QCheckBox(tr("Echo input summary (INPUT)"),         flagsGroup);
    m_rptContinuityBox = new QCheckBox(tr("Continuity errors (CONTINUITY)"),     flagsGroup);
    m_rptFlowstatsBox  = new QCheckBox(tr("Flow statistics (FLOWSTATS)"),        flagsGroup);
    m_rptControlsBox   = new QCheckBox(tr("Control rule actions (CONTROLS)"),    flagsGroup);
    m_rptAveragesBox   = new QCheckBox(tr("Time-averaged results (AVERAGES)"),   flagsGroup);

    flagsForm->addRow(QString(), m_rptDisabledBox);
    flagsForm->addRow(QString(), m_rptInputBox);
    flagsForm->addRow(QString(), m_rptContinuityBox);
    flagsForm->addRow(QString(), m_rptFlowstatsBox);
    flagsForm->addRow(QString(), m_rptControlsBox);
    flagsForm->addRow(QString(), m_rptAveragesBox);

    // DISABLED short-circuits everything else — grey out the dependent
    // controls when it's on so the user knows nothing else matters.
    auto syncDisabledShortCircuit = [this, flagsGroup]() {
        const bool disabled = m_rptDisabledBox && m_rptDisabledBox->isChecked();
        for (QCheckBox *cb : { m_rptInputBox, m_rptContinuityBox,
                               m_rptFlowstatsBox, m_rptControlsBox,
                               m_rptAveragesBox })
            if (cb) cb->setEnabled(!disabled);
        Q_UNUSED(flagsGroup);
    };
    connect(m_rptDisabledBox, &QCheckBox::toggled,
            this, [syncDisabledShortCircuit](bool) { syncDisabledShortCircuit(); });

    vlay->addWidget(flagsGroup);

    // REPORT_SIGNED_HEADS ([OPTIONS], engine issue #156 O-6; GUI issue #10).
    // Output option for any routing model; deliberately outside the
    // RPT_DISABLED short-circuit — it shapes the .out, not the .rpt.
    m_signedHeadsCheck = new QCheckBox(
        tr("Report signed piezometric heads (sub-atmospheric)"), grp);
    m_signedHeadsCheck->setToolTip(
        tr("When on, the binary output's HEAD variable carries the signed "
           "piezometric head, so sub-atmospheric full-pipe pressure (e.g. "
           "under the TPA closure) is visible; DEPTH stays floored at zero "
           "(REPORT_SIGNED_HEADS). Off (the default) keeps legacy "
           "bit-parity."));
    vlay->addWidget(m_signedHeadsCheck);

    // ---- selectors -----------------------------------------------------
    // Per-kind: a row with [○ None] [○ All] [○ Selected] [name list edit].
    // The line-edit greys out unless Selected is chosen.
    auto buildSelector = [this, grp](const QString &label,
                                      QRadioButton **noneR,
                                      QRadioButton **allR,
                                      QRadioButton **someR,
                                      QLineEdit    **listE)
    {
        auto *row = new QGroupBox(label, grp);
        auto *h = new QHBoxLayout(row);

        *noneR = new QRadioButton(tr("None"),     row);
        *allR  = new QRadioButton(tr("All"),      row);
        *someR = new QRadioButton(tr("Selected:"), row);

        auto *bg = new QButtonGroup(row);
        bg->addButton(*noneR, 0);
        bg->addButton(*allR,  1);
        bg->addButton(*someR, 2);

        *listE = new QLineEdit(row);
        (*listE)->setPlaceholderText(
            tr("comma- or space-separated object names"));
        (*listE)->setEnabled(false);

        h->addWidget(*noneR);
        h->addWidget(*allR);
        h->addWidget(*someR);
        h->addWidget(*listE, 1);

        // Edit field follows the Selected radio.
        connect(*someR, &QRadioButton::toggled, *listE, &QLineEdit::setEnabled);

        return row;
    };

    vlay->addWidget(buildSelector(tr("Subcatchments"),
        &m_rptSubcatchNoneRadio, &m_rptSubcatchAllRadio,
        &m_rptSubcatchSomeRadio, &m_rptSubcatchListEdit));
    vlay->addWidget(buildSelector(tr("Nodes"),
        &m_rptNodeNoneRadio, &m_rptNodeAllRadio,
        &m_rptNodeSomeRadio, &m_rptNodeListEdit));
    vlay->addWidget(buildSelector(tr("Links"),
        &m_rptLinkNoneRadio, &m_rptLinkAllRadio,
        &m_rptLinkSomeRadio, &m_rptLinkListEdit));

    parentLayout->addWidget(grp);
}

void FilesPage::readReportContentsFromEngine()
{
    if (!ctx_.engine()) return;

    // Bool keys ---------------------------------------------------------
    auto setBox = [this](QCheckBox *box, const char *key, bool fallback) {
        if (!box) return;
        const QString v = ctx_.option(key, fallback ? QStringLiteral("YES")
                                                  : QStringLiteral("NO"));
        QSignalBlocker blk(box);
        box->setChecked(SimulationOptionsDialog::parseEngineBool(v) == Qt::Checked);
    };
    setBox(m_rptDisabledBox,   "RPT_DISABLED",   false);
    setBox(m_rptInputBox,      "RPT_INPUT",      false);
    setBox(m_rptContinuityBox, "RPT_CONTINUITY", true);
    setBox(m_rptFlowstatsBox,  "RPT_FLOWSTATS",  true);
    setBox(m_rptControlsBox,   "RPT_CONTROLS",   false);
    setBox(m_rptAveragesBox,   "RPT_AVERAGES",   false);
    setBox(m_signedHeadsCheck, "REPORT_SIGNED_HEADS", false);

    // Sync the disabled-short-circuit state once after the initial read.
    if (m_rptDisabledBox) {
        const bool disabled = m_rptDisabledBox->isChecked();
        for (QCheckBox *cb : { m_rptInputBox, m_rptContinuityBox,
                               m_rptFlowstatsBox, m_rptControlsBox,
                               m_rptAveragesBox })
            if (cb) cb->setEnabled(!disabled);
    }

    // Selector keys -----------------------------------------------------
    auto setSelector = [this](QRadioButton *noneR, QRadioButton *allR,
                              QRadioButton *someR, QLineEdit *listE,
                              const char *key) {
        if (!noneR || !allR || !someR || !listE) return;
        const QString v = ctx_.option(key, QStringLiteral("ALL")).trimmed();
        QSignalBlocker b1(noneR), b2(allR), b3(someR), b4(listE);
        if (v.compare(QStringLiteral("NONE"), Qt::CaseInsensitive) == 0) {
            noneR->setChecked(true);
            listE->clear();
            listE->setEnabled(false);
        } else if (v.isEmpty()
                || v.compare(QStringLiteral("ALL"), Qt::CaseInsensitive) == 0) {
            allR->setChecked(true);
            listE->clear();
            listE->setEnabled(false);
        } else {
            someR->setChecked(true);
            listE->setText(v);
            listE->setEnabled(true);
        }
    };
    setSelector(m_rptSubcatchNoneRadio, m_rptSubcatchAllRadio,
                m_rptSubcatchSomeRadio, m_rptSubcatchListEdit,
                "RPT_SUBCATCHMENTS");
    setSelector(m_rptNodeNoneRadio, m_rptNodeAllRadio,
                m_rptNodeSomeRadio, m_rptNodeListEdit,
                "RPT_NODES");
    setSelector(m_rptLinkNoneRadio, m_rptLinkAllRadio,
                m_rptLinkSomeRadio, m_rptLinkListEdit,
                "RPT_LINKS");
}

int FilesPage::writeReportContentsToEngine()
{
    if (!ctx_.engine()) return 0;
    int n = 0;
    // Routed through the context so the key is recorded for the
    // reachability seam and the numeric-aware compare stays in one place.
    auto writeIfChanged = [this, &n](const char *key, const QString &newVal) {
        n += ctx_.writeIfChanged(key, newVal);
    };

    auto boolStr = [](QCheckBox *box, bool def) {
        return box ? SimulationOptionsDialog::engineBoolString(box->isChecked()) : SimulationOptionsDialog::engineBoolString(def);
    };
    writeIfChanged("RPT_DISABLED",   boolStr(m_rptDisabledBox,   false));
    writeIfChanged("RPT_INPUT",      boolStr(m_rptInputBox,      false));
    writeIfChanged("RPT_CONTINUITY", boolStr(m_rptContinuityBox, true));
    writeIfChanged("RPT_FLOWSTATS",  boolStr(m_rptFlowstatsBox,  true));
    writeIfChanged("RPT_CONTROLS",   boolStr(m_rptControlsBox,   false));
    writeIfChanged("RPT_AVERAGES",   boolStr(m_rptAveragesBox,   false));
    writeIfChanged("REPORT_SIGNED_HEADS", boolStr(m_signedHeadsCheck, false));

    auto selectorStr = [](QRadioButton *noneR, QRadioButton *allR,
                          QRadioButton *someR, QLineEdit *listE) {
        if (!noneR || !allR || !someR || !listE) return QStringLiteral("ALL");
        if (noneR->isChecked()) return QStringLiteral("NONE");
        if (allR->isChecked())  return QStringLiteral("ALL");
        // Selected — but empty list collapses to NONE so we don't push
        // an invalid empty SOME state through the engine.
        const QString text = listE->text().trimmed();
        return text.isEmpty() ? QStringLiteral("NONE") : text;
    };
    writeIfChanged("RPT_SUBCATCHMENTS",
        selectorStr(m_rptSubcatchNoneRadio, m_rptSubcatchAllRadio,
                    m_rptSubcatchSomeRadio, m_rptSubcatchListEdit));
    writeIfChanged("RPT_NODES",
        selectorStr(m_rptNodeNoneRadio, m_rptNodeAllRadio,
                    m_rptNodeSomeRadio, m_rptNodeListEdit));
    writeIfChanged("RPT_LINKS",
        selectorStr(m_rptLinkNoneRadio, m_rptLinkAllRadio,
                    m_rptLinkSomeRadio, m_rptLinkListEdit));

    return n;
}


void FilesPage::buildUi()
{
    QWidget *page = this;
    // Phase 3.10 (2026-05-22) — the legacy single "Files" page is split
    // into three sub-tabs (Files / Output / Plugins) via a nested
    // QTabWidget.  Existing widgets keep their member identities; this
    // method just regroups them by concern:
    //   • Files   — [FILES] secondary refs + scheduled hot-start saves
    //   • Output  — writer combos + [REPORT] flags + .rpt / .out paths
    //   • Plugins — [PLUGINS] table editor (Phase 3.10.3)
    auto *outerLay = new QVBoxLayout(page);
    outerLay->setContentsMargins(0, 0, 0, 0);

    auto *subTabs = new QTabWidget(page);
    subTabs->setDocumentMode(true);
    outerLay->addWidget(subTabs);

    // =====================================================================
    // Sub-tab "Files" — [FILES] secondary refs + scheduled hot-start saves
    // =====================================================================
    auto *filesPage = new QWidget(subTabs);
    auto *vlay = new QVBoxLayout(filesPage);

    // ── [FILES] secondary references group ─────────────────────────────
    auto *secondary = new QGroupBox(
        tr("Secondary file references (.inp [FILES] section)"), filesPage);
    auto *secForm = new QFormLayout(secondary);

    auto makeModeCombo = [secondary] {
        auto *c = new QComboBox(secondary);
        c->addItem(tr("(off)"), QString());
        c->addItem(tr("USE"),   QStringLiteral("USE"));
        c->addItem(tr("SAVE"),  QStringLiteral("SAVE"));
        return c;
    };
    // Slice IO-11a — every [FILES] row is a RelativePathPicker so the
    // line edit shows the path relative to the project anchor (defaulting
    // to the .inp directory) and the embedded "…" button opens the file
    // dialog. The picker re-routes the browse outcome through its own
    // resolveAgainst-anchor logic; we no longer build a separate
    // QPushButton + QFileDialog branch here.
    //
    // The mode combo (USE/SAVE) drives the picker's acceptMode so SAVE
    // rows pick the "Save as" dialog flavour. Filters are coarse — most
    // legacy SWMM secondary refs are plain text/CSV; the hot-start USE
    // row gets its own *.hsf filter.
    const QString projectAnchor =
        (ctx_.modelLayer() && !ctx_.modelLayer()->modelFilePath().isEmpty())
            ? QFileInfo(ctx_.modelLayer()->modelFilePath()).absolutePath()
            : QString();

    auto makePathRow = [secondary, secForm, this, projectAnchor](
                            const QString &label,
                            openswmmvis::ui::RelativePathPicker **edit,
                            QComboBox *modeCombo,
                            bool defaultSave,
                            const QString &dialogTitle,
                            const QString &filter) {
        auto *picker = new openswmmvis::ui::RelativePathPicker(secondary);
        picker->setProjectAnchor(projectAnchor);
        picker->setFileFilter(filter);
        picker->setDialogCaption(dialogTitle);
        picker->setAcceptMode(defaultSave ? QFileDialog::AcceptSave
                                          : QFileDialog::AcceptOpen);
        *edit = picker;

        auto *row = new QHBoxLayout();
        row->addWidget(picker, 1);
        if (modeCombo) {
            row->addWidget(new QLabel(QObject::tr("Mode:"), secondary));
            row->addWidget(modeCombo);

            // Mode change → pick the right file-dialog flavour on next browse.
            connect(modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                    picker, [picker, modeCombo, defaultSave] {
                bool save = defaultSave;
                const QString m = modeCombo->currentData().toString();
                if (!m.isEmpty())
                    save = (m.compare(QLatin1String("SAVE"),
                                       Qt::CaseInsensitive) == 0);
                picker->setAcceptMode(save ? QFileDialog::AcceptSave
                                            : QFileDialog::AcceptOpen);
            });
        }
        secForm->addRow(label, row);
    };

    const QString textFilter = tr("Text files (*.txt *.dat);;All Files (*)");
    const QString csvFilter  = tr("Text / CSV (*.txt *.dat *.csv);;All Files (*)");
    const QString hsfFilter  = tr("Hot-start files (*.hsf);;All Files (*)");

    m_rainfallModeCombo = makeModeCombo();
    makePathRow(tr("Rainfall:"), &m_rainfallPathEdit, m_rainfallModeCombo,
                false, tr("Choose Rainfall File"), textFilter);

    m_runoffModeCombo = makeModeCombo();
    makePathRow(tr("Runoff:"),   &m_runoffPathEdit,   m_runoffModeCombo,
                false, tr("Choose Runoff File"), textFilter);

    m_rdiiModeCombo = makeModeCombo();
    makePathRow(tr("RDII:"),     &m_rdiiPathEdit,     m_rdiiModeCombo,
                false, tr("Choose RDII File"), textFilter);

    makePathRow(tr("Inflows (USE only):"),   &m_inflowsPathEdit,  nullptr,
                false, tr("Choose Inflows File"),  csvFilter);
    makePathRow(tr("Outflows (SAVE only):"), &m_outflowsPathEdit, nullptr,
                true,  tr("Choose Outflows File"), csvFilter);
    makePathRow(tr("Hot-start file (USE):"), &m_hotstartUseEdit,  nullptr,
                false, tr("Choose Hot-Start File"), hsfFilter);

    vlay->addWidget(secondary);

    // ── Scheduled hot-start saves (Slice BV-01, 2026-05-21) ─────────────
    // Multi-row uncapped table replaces the legacy single SAVE field.
    // Backed by the new swmm_hotstart_saves_* engine C API.
    //
    // Phase 3.10.5 (2026-05-22): true MVC — a QTableView bound to a
    // HotstartSavesModel.  HotstartSavesPathDelegate renders each path
    // cell as a [QLineEdit][…] composite so the user can browse for the
    // save path inline; HotstartSavesDateTimeDelegate renders each
    // datetime cell as a QDateTimeEdit with the "(end of run)" sentinel.
    // Persistent editors keep both widgets visible without click-to-edit.
    auto *hsSaves = new QGroupBox(
        tr("Scheduled hot-start saves (.inp [FILES] SAVE HOTSTART)"), filesPage);
    auto *hsSavesLay = new QVBoxLayout(hsSaves);

    m_hotstartSavesModel   = new HotstartSavesModel(this);
    m_hotstartSavesPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::SaveFile,
        tr("Choose Hot-Start Save File"),
        tr("Hot-start files (*.hsf);;All Files (*)"),
        tr("path relative to the .inp directory"),
        this);
    // Slice IO-11c — feed the same project anchor into the delegate so
    // file picks under the .inp directory commit as relative tokens,
    // matching the [FILES] picker behaviour set up immediately above.
    m_hotstartSavesPathDel->setProjectAnchor(projectAnchor);
    m_hotstartSavesDtDel   = new HotstartSavesDateTimeDelegate(this);

    m_hotstartSavesView = new QTableView(hsSaves);
    m_hotstartSavesView->setModel(m_hotstartSavesModel);
    m_hotstartSavesView->setItemDelegateForColumn(
        HotstartSavesModel::ColPath, m_hotstartSavesPathDel);
    m_hotstartSavesView->setItemDelegateForColumn(
        HotstartSavesModel::ColDateTime, m_hotstartSavesDtDel);
    // Interactive resize: both columns are user-draggable.  Set sensible
    // defaults — path column takes the leftover space initially, datetime
    // is sized to fit the picker — but neither is locked.
    {
        auto *hdr = m_hotstartSavesView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(false);
        hdr->setSectionsMovable(false);
        hdr->setMinimumSectionSize(60);
        // Width hint: ~2/3 path, ~1/3 datetime — refined once the view is
        // shown (Qt will clamp later if the dialog is resized).
        hdr->resizeSection(HotstartSavesModel::ColPath,     360);
        hdr->resizeSection(HotstartSavesModel::ColDateTime, 200);
    }
    m_hotstartSavesView->verticalHeader()->setVisible(false);
    m_hotstartSavesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_hotstartSavesView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_hotstartSavesView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_hotstartSavesView->setToolTip(tr(
        "Each row schedules one hot-start save during the run.\n"
        "Leave Datetime empty (\"(end of run)\") to save at the end of the\n"
        "simulation; otherwise the engine writes the file when the sim\n"
        "clock crosses the chosen datetime."));
    hsSavesLay->addWidget(m_hotstartSavesView);

    auto openPersistentForRow = [this](int row) {
        if (!m_hotstartSavesView || !m_hotstartSavesModel) return;
        m_hotstartSavesView->openPersistentEditor(
            m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath));
        m_hotstartSavesView->openPersistentEditor(
            m_hotstartSavesModel->index(row, HotstartSavesModel::ColDateTime));
    };
    // Open persistent editors on every newly-inserted row so the path
    // line-edit + browse button and the QDateTimeEdit are visible by
    // default without click-to-edit.
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsInserted,
            this, [openPersistentForRow](const QModelIndex &, int first, int last) {
        for (int r = first; r <= last; ++r) openPersistentForRow(r);
    });
    connect(m_hotstartSavesModel, &QAbstractItemModel::modelReset, this,
            [this, openPersistentForRow] {
        if (!m_hotstartSavesModel) return;
        for (int r = 0; r < m_hotstartSavesModel->rowCount(); ++r)
            openPersistentForRow(r);
    });

    auto *hsBtnRow = new QHBoxLayout();
    m_hotstartSavesAddBtn    = new QPushButton(tr("Add…"),     hsSaves);
    m_hotstartSavesBrowseBtn = new QPushButton(tr("Browse…"),  hsSaves);
    m_hotstartSavesRemoveBtn = new QPushButton(tr("Remove"),   hsSaves);
    m_hotstartSavesUpBtn     = new QPushButton(tr("Move up"),  hsSaves);
    m_hotstartSavesDownBtn   = new QPushButton(tr("Move down"),hsSaves);
    m_hotstartSavesBrowseBtn->setToolTip(
        tr("Choose a save-as path for the selected row"));
    hsBtnRow->addWidget(m_hotstartSavesAddBtn);
    hsBtnRow->addWidget(m_hotstartSavesBrowseBtn);
    hsBtnRow->addWidget(m_hotstartSavesRemoveBtn);
    hsBtnRow->addWidget(m_hotstartSavesUpBtn);
    hsBtnRow->addWidget(m_hotstartSavesDownBtn);
    hsBtnRow->addStretch(1);
    hsSavesLay->addLayout(hsBtnRow);
    vlay->addWidget(hsSaves);

    auto updateHsButtons = [this] {
        const int row = (m_hotstartSavesView && m_hotstartSavesView->currentIndex().isValid())
                            ? m_hotstartSavesView->currentIndex().row() : -1;
        const int n   = m_hotstartSavesModel ? m_hotstartSavesModel->rowCount() : 0;
        const bool hasSel = row >= 0;
        if (m_hotstartSavesBrowseBtn) m_hotstartSavesBrowseBtn->setEnabled(hasSel);
        if (m_hotstartSavesRemoveBtn) m_hotstartSavesRemoveBtn->setEnabled(hasSel);
        if (m_hotstartSavesUpBtn)     m_hotstartSavesUpBtn->setEnabled(hasSel && row > 0);
        if (m_hotstartSavesDownBtn)   m_hotstartSavesDownBtn->setEnabled(hasSel && row < n - 1);
    };
    updateHsButtons();
    connect(m_hotstartSavesView->selectionModel(),
            &QItemSelectionModel::currentChanged,
            this, [updateHsButtons](const QModelIndex &, const QModelIndex &) {
        updateHsButtons();
    });
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsInserted,
            this, updateHsButtons);
    connect(m_hotstartSavesModel, &QAbstractItemModel::rowsRemoved,
            this, updateHsButtons);
    connect(m_hotstartSavesModel, &QAbstractItemModel::modelReset,
            this, updateHsButtons);

    connect(m_hotstartSavesAddBtn,    &QPushButton::clicked,
            this, &FilesPage::onHotstartSaveAddRow);
    connect(m_hotstartSavesBrowseBtn, &QPushButton::clicked,
            this, &FilesPage::onHotstartSaveBrowseRow);
    connect(m_hotstartSavesRemoveBtn, &QPushButton::clicked,
            this, &FilesPage::onHotstartSaveRemoveRow);
    connect(m_hotstartSavesUpBtn,     &QPushButton::clicked,
            this, &FilesPage::onHotstartSaveMoveRowUp);
    connect(m_hotstartSavesDownBtn,   &QPushButton::clicked,
            this, &FilesPage::onHotstartSaveMoveRowDown);

    vlay->addStretch(1);
    subTabs->addTab(filesPage, tr("Files"));

    // =====================================================================
    // Sub-tab "Output" — writer combos + [REPORT] flags + .rpt / .out paths
    // (Phase 3.10.2, 2026-05-22)
    // =====================================================================
    auto *outputPage = new QWidget(subTabs);
    auto *outVlay = new QVBoxLayout(outputPage);

    // ── Writer / Container group ────────────────────────────────────────
    // Three combos let the user pick the plugin driving each role.  The
    // combo's hidden `data()` is the plugin id (empty string for the
    // built-in `.inp` / `.out` / `.rpt` writer).  Picking a non-default
    // entry adds the corresponding [PLUGINS] row on Apply.
    m_writersGroup = new QGroupBox(tr("Writer / Container"), outputPage);
    auto *writerGroup = m_writersGroup;
    auto *writerForm  = new QFormLayout(writerGroup);

    auto populateWriterCombo = [](QComboBox *combo, openswmmvis::FilterKind k) {
        // First entry: built-in (empty plugin id = engine default).
        const char *defaultLabel =
            (k == openswmmvis::FilterKind::InputRead)
                ? "(built-in: .inp writer)"
                : (k == openswmmvis::FilterKind::ResultsWrite)
                      ? "(built-in: .out writer)"
                      : "(built-in: .rpt writer)";
        combo->addItem(QObject::tr(defaultLabel), QString());
        auto *registry = openswmmvis::FileFilterRegistry::instance();
        QStringList seen;  // dedupe by pluginId
        // Phase 3.10.6 (2026-05-22): the engine SDK has no INPUT_WRITE
        // role — plugins that handle the model input file advertise
        // INPUT_READ only, and the GUI registry sets `canWrite=false`
        // on those entries.  The previous gate `if (!canWrite) continue;`
        // therefore silently dropped every engine-provided input plugin
        // from the Input writer combo.  All three combos now show every
        // plugin id advertising the matching role, deduped.
        for (const auto &entry : registry->entriesFor(k)) {
            if (entry.pluginId.isEmpty()) continue;
            if (seen.contains(entry.pluginId)) continue;
            seen << entry.pluginId;
            const QString label = entry.description.isEmpty()
                ? entry.pluginId
                : QStringLiteral("%1 (%2)").arg(entry.description, entry.pluginId);
            combo->addItem(label, entry.pluginId);
        }
    };

    m_inputWriterCombo  = new QComboBox(writerGroup);
    populateWriterCombo(m_inputWriterCombo, openswmmvis::FilterKind::InputRead);
    writerForm->addRow(tr("Input writer:"),  m_inputWriterCombo);

    m_outputWriterCombo = new QComboBox(writerGroup);
    populateWriterCombo(m_outputWriterCombo, openswmmvis::FilterKind::ResultsWrite);
    writerForm->addRow(tr("Output writer:"), m_outputWriterCombo);

    m_reportWriterCombo = new QComboBox(writerGroup);
    populateWriterCombo(m_reportWriterCombo, openswmmvis::FilterKind::ReportWrite);
    writerForm->addRow(tr("Report writer:"), m_reportWriterCombo);

    m_singleContainerBox = new QCheckBox(
        tr("Single container (write input, output, and report to one file)"),
        writerGroup);
    m_singleContainerBox->setToolTip(
        tr("Enabled when the chosen Input writer plugin advertises input, "
           "output, and report roles for the same extension (e.g., GeoPackage). "
           "When checked, the Output and Report writer combos lock to the "
           "Input writer's plugin id."));
    m_singleContainerBox->setEnabled(false);
    writerForm->addRow(QString(), m_singleContainerBox);

    connect(m_inputWriterCombo,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { updateSingleContainerEnabled(); });
    connect(m_singleContainerBox, &QCheckBox::toggled,
            this, &FilesPage::onSingleContainerToggled);

    outVlay->addWidget(writerGroup);

    // ── Report contents ([REPORT] section — Slice BV.1, 2026-05-22) ───
    buildReportContentsGroup(outVlay, outputPage);

    // ── Report file path (Slice AA-4) ────────────────────────────────────
    auto *rptGroup = new QGroupBox(tr("Report file"), outputPage);
    auto *rptForm  = new QFormLayout(rptGroup);

    auto *rptPathRow = new QHBoxLayout();
    m_reportFilePathEdit = new QLineEdit(rptGroup);
    m_reportFilePathEdit->setPlaceholderText(tr("(auto — sibling of input file with .rpt extension)"));
    m_reportFilePathEdit->setToolTip(tr(
        "Override path for the simulation report file. Leave blank to "
        "derive the path automatically from the input file location. "
        "The format is determined by the Report writer combo above."));
    rptPathRow->addWidget(m_reportFilePathEdit, 1);
    auto *rptBrowse = new QPushButton(tr("Browse…"), rptGroup);
    connect(rptBrowse, &QPushButton::clicked,
            this, &FilesPage::browseForReportFile);
    rptPathRow->addWidget(rptBrowse);
    rptForm->addRow(tr("Report file path:"), rptPathRow);
    outVlay->addWidget(rptGroup);

    // ── Output (results) file path (Slice AA-4) ──────────────────────────
    auto *outGroup = new QGroupBox(tr("Results output file"), outputPage);
    auto *outForm  = new QFormLayout(outGroup);

    auto *outPathRow = new QHBoxLayout();
    m_outputFilePathEdit = new QLineEdit(outGroup);
    m_outputFilePathEdit->setPlaceholderText(tr("(auto — sibling of input file with .out extension)"));
    m_outputFilePathEdit->setToolTip(tr(
        "Override path for the binary results output file. Leave blank to "
        "derive the path automatically from the input file location. "
        "The format is determined by the Output writer combo above."));
    outPathRow->addWidget(m_outputFilePathEdit, 1);
    auto *outBrowse = new QPushButton(tr("Browse…"), outGroup);
    connect(outBrowse, &QPushButton::clicked,
            this, &FilesPage::browseForOutputFile);
    outPathRow->addWidget(outBrowse);
    outForm->addRow(tr("Output file path:"), outPathRow);
    outVlay->addWidget(outGroup);

    outVlay->addStretch(1);
    subTabs->addTab(outputPage, tr("O&utput"));

    // =====================================================================
    // Sub-tab "Plugins" — [PLUGINS] table (Phase 3.10.3, 2026-05-22)
    // =====================================================================
    auto *pluginsPage = new QWidget(subTabs);
    auto *plVlay = new QVBoxLayout(pluginsPage);

    auto *intro = new QLabel(
        tr("Plugins listed in the model's <b>[PLUGINS]</b> section.  Each row "
           "names a writer / output / report plugin (by id, <i>id:version</i>, "
           "or shared-library path) and any free-form arguments to pass to "
           "its initialize() call.  The first input-capable row is also used "
           "by File → Save As when picking a non-<code>.inp</code> "
           "extension."),
        pluginsPage);
    intro->setWordWrap(true);
    plVlay->addWidget(intro);

    // Phase 3.10.6 (2026-05-22) — MVC: model + QTableView with the
    // reusable PathBrowseDelegate on column 0 so each row's plugin
    // path field carries an inline "…" browse button targeting the
    // platform's shared-library extensions.  Column 1 (arguments)
    // uses the default QLineEdit delegate (free-form text).
    m_pluginsModel   = new PluginsTableModel(this);
    m_pluginsPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::OpenFile,
        tr("Choose Plugin Library"),
#if defined(Q_OS_WIN)
        tr("Plugin libraries (*.dll);;All Files (*)"),
#elif defined(Q_OS_MACOS)
        tr("Plugin libraries (*.dylib *.so *.bundle);;All Files (*)"),
#else
        tr("Plugin libraries (*.so);;All Files (*)"),
#endif
        tr("plugin id, id:version, or library path"),
        this);

    m_pluginsView = new QTableView(pluginsPage);
    m_pluginsView->setModel(m_pluginsModel);
    m_pluginsView->setItemDelegateForColumn(
        PluginsTableModel::ColPath, m_pluginsPathDel);
    m_pluginsView->verticalHeader()->setVisible(false);
    m_pluginsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_pluginsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_pluginsView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    {
        auto *hdr = m_pluginsView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(true);
        hdr->setMinimumSectionSize(60);
        hdr->resizeSection(PluginsTableModel::ColPath, 360);
        hdr->resizeSection(PluginsTableModel::ColArgs, 220);
    }
    plVlay->addWidget(m_pluginsView, 1);

    // Open the path-cell persistent editor for every row so the browse
    // "…" button is visible without click-to-edit.  Args column keeps
    // the default click-to-edit behaviour.
    auto openPersistentPluginPath = [this](int row) {
        if (!m_pluginsView || !m_pluginsModel) return;
        m_pluginsView->openPersistentEditor(
            m_pluginsModel->index(row, PluginsTableModel::ColPath));
    };
    connect(m_pluginsModel, &QAbstractItemModel::rowsInserted, this,
            [openPersistentPluginPath](const QModelIndex &, int first, int last) {
        for (int r = first; r <= last; ++r) openPersistentPluginPath(r);
    });
    connect(m_pluginsModel, &QAbstractItemModel::modelReset, this,
            [this, openPersistentPluginPath] {
        if (!m_pluginsModel) return;
        for (int r = 0; r < m_pluginsModel->rowCount(); ++r)
            openPersistentPluginPath(r);
    });

    auto *btnRow = new QHBoxLayout();
    m_pluginsAddBtn    = new QPushButton(tr("Add"), pluginsPage);
    m_pluginsRemoveBtn = new QPushButton(tr("Remove"), pluginsPage);
    m_pluginsRemoveBtn->setEnabled(false);
    btnRow->addWidget(m_pluginsAddBtn);
    btnRow->addWidget(m_pluginsRemoveBtn);
    btnRow->addStretch();
    plVlay->addLayout(btnRow);

    connect(m_pluginsAddBtn, &QPushButton::clicked, this, [this] {
        if (!m_pluginsModel || !m_pluginsView) return;
        const int row = m_pluginsModel->appendRow(QString(), QString());
        m_pluginsView->setCurrentIndex(
            m_pluginsModel->index(row, PluginsTableModel::ColPath));
    });
    connect(m_pluginsRemoveBtn, &QPushButton::clicked, this, [this] {
        if (!m_pluginsModel || !m_pluginsView) return;
        const QModelIndex cur = m_pluginsView->currentIndex();
        if (cur.isValid()) m_pluginsModel->removeRows(cur.row(), 1);
    });
    // Slice RC.4 — Plugins tab honours `is_builtin` from the engine's
    // plugin discovery API. Built-ins (today: Default Input / Output /
    // Report / StateIO + GeoPackage when OPENSWMM_HAS_GEOPACKAGE) are
    // statically linked into the engine, so a user attempting to
    // "Remove" the row from the .inp's [PLUGINS] section cannot
    // actually unload them — the singleton stays alive in-process.
    // Greying the Remove button when the selected row's path matches a
    // built-in plugin_id makes that constraint visible.
    auto builtInPluginIds = [] {
        QSet<QString> ids;
        for (const auto &p : openswmm::discover_plugins_by_id()) {
            if (p.is_builtin)
                ids.insert(QString::fromStdString(p.plugin_id));
        }
        return ids;
    }();
    connect(m_pluginsView->selectionModel(),
            &QItemSelectionModel::currentChanged, this,
            [this, builtInPluginIds](const QModelIndex &cur, const QModelIndex &) {
        if (!cur.isValid()) {
            m_pluginsRemoveBtn->setEnabled(false);
            m_pluginsRemoveBtn->setToolTip(QString());
            return;
        }
        const QString rowPath = m_pluginsModel
            ? m_pluginsModel->pathAt(cur.row()).trimmed()
            : QString();
        const bool isBuiltIn = builtInPluginIds.contains(rowPath);
        m_pluginsRemoveBtn->setEnabled(!isBuiltIn);
        m_pluginsRemoveBtn->setToolTip(
            isBuiltIn
              ? tr("\"%1\" is a built-in plugin statically linked into "
                   "the engine — removing the row would have no effect "
                   "(the plugin stays loaded in-process).").arg(rowPath)
              : QString());
    });

    // U1 (2026-09-07) — [PROCESS_COMPONENTS] table. Same MVC shape as the
    // plugins table above: model over the engine registrations, combo
    // delegate for the id (engine's built-in catalogue, free text allowed),
    // PathBrowseDelegate for the config file. Reaction editor auto-registers
    // its component straight into the engine; this table re-reads on open.
    auto *compGroup = new QGroupBox(tr("Process components ([PROCESS_COMPONENTS])"),
                                    pluginsPage);
    auto *compLay = new QVBoxLayout(compGroup);
    auto *compIntro = new QLabel(
        tr("Transport / reaction components bound to this model and the "
           "config file each reads (relative to the model file). The "
           "reactions component is registered automatically by the Reaction "
           "System editor's first save; other ids are listed from the "
           "engine's catalogue."),
        compGroup);
    compIntro->setWordWrap(true);
    compLay->addWidget(compIntro);

    m_componentsModel   = new ProcessComponentsModel(this);
    m_componentsIdDel   = new ProcessComponentIdDelegate(this);
    m_componentsPathDel = new PathBrowseDelegate(
        PathBrowseDelegate::OpenFile, tr("Choose Component Config File"),
        tr("Component config (*.rxn *.ard *.lard *.heat *.age *.i2d *.txt);;All Files (*)"),
        tr("config file, relative to the model"), this);
    m_componentsView = new QTableView(compGroup);
    m_componentsView->setObjectName(QStringLiteral("processComponentsView"));
    m_componentsView->setModel(m_componentsModel);
    m_componentsView->setItemDelegateForColumn(ProcessComponentsModel::ColId,
                                               m_componentsIdDel);
    m_componentsView->setItemDelegateForColumn(ProcessComponentsModel::ColConfig,
                                               m_componentsPathDel);
    m_componentsView->verticalHeader()->setVisible(false);
    m_componentsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_componentsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_componentsView->setEditTriggers(QAbstractItemView::AllEditTriggers);
    {
        auto *hdr = m_componentsView->horizontalHeader();
        hdr->setSectionResizeMode(QHeaderView::Interactive);
        hdr->setStretchLastSection(true);
        hdr->resizeSection(ProcessComponentsModel::ColId, 300);
        hdr->resizeSection(ProcessComponentsModel::ColConfig, 240);
    }
    compLay->addWidget(m_componentsView, 1);

    auto *compBtnRow = new QHBoxLayout();
    m_componentsAddBtn    = new QPushButton(tr("Add"), compGroup);
    m_componentsAddBtn->setObjectName(QStringLiteral("processComponentsAddBtn"));
    m_componentsRemoveBtn = new QPushButton(tr("Remove"), compGroup);
    m_componentsRemoveBtn->setObjectName(QStringLiteral("processComponentsRemoveBtn"));
    m_componentsRemoveBtn->setEnabled(false);
    compBtnRow->addWidget(m_componentsAddBtn);
    compBtnRow->addWidget(m_componentsRemoveBtn);
    compBtnRow->addStretch();
    compLay->addLayout(compBtnRow);
    connect(m_componentsAddBtn, &QPushButton::clicked, this, [this] {
        const int row = m_componentsModel->appendRow(QString(), QString());
        const QModelIndex idx = m_componentsModel->index(row, ProcessComponentsModel::ColId);
        m_componentsView->setCurrentIndex(idx);
        m_componentsView->edit(idx);
    });
    connect(m_componentsRemoveBtn, &QPushButton::clicked, this, [this] {
        const QModelIndex cur = m_componentsView->currentIndex();
        if (cur.isValid()) m_componentsModel->removeRows(cur.row(), 1);
    });
    connect(m_componentsView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &cur, const QModelIndex &) {
                m_componentsRemoveBtn->setEnabled(cur.isValid());
            });
    plVlay->addWidget(compGroup);

    subTabs->addTab(pluginsPage, tr("Plu&gins"));

}

void FilesPage::readProcessComponentsFromEngine()
{
    if (!m_componentsModel) return;
    const QString dir = ctx_.modelLayer() ? QFileInfo(ctx_.modelLayer()->modelFilePath()).absolutePath()
                                : QString();
    m_componentsModel->load(ctx_.engine(), dir);
}

int FilesPage::writeProcessComponentsToEngine()
{
    if (!m_componentsModel || !ctx_.engine()) return 0;
    return m_componentsModel->commit(ctx_.engine());
}

void FilesPage::readPluginsFromEngine()
{
    if (!m_pluginsModel) return;
    m_pluginsModel->clearRows();
    if (!ctx_.engine()) return;

    int count = 0;
    if (swmm_plugins_count(ctx_.engine(), &count) != 0) return;

    char path_buf[1024];
    char args_buf[2048];
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(ctx_.engine(), i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        m_pluginsModel->appendRow(QString::fromUtf8(path_buf),
                                  QString::fromUtf8(args_buf));
    }
}

// ---------------------------------------------------------------------------
// Writer / Container combos (Slice AA-3.5 full design)
//
// The combos drive a derived view onto the [PLUGINS] section: the user
// picks which plugin handles each role (input writer, results output,
// report).  Apply collects the selected plugin ids and ensures each
// non-empty id has a matching [PLUGINS] row — without disturbing any
// rows the user may have added manually via the table below.
// ---------------------------------------------------------------------------

namespace {

// Find the first plugin id in the current [PLUGINS] section that advertises
// @p role per the engine's grouped discovery.  Returns empty string when
// no such plugin is loaded (caller treats empty as "built-in default").
QString findActivePluginForRole(SWMM_Engine engine, openswmm::PluginRole role)
{
    if (!engine) return {};
    int count = 0;
    if (swmm_plugins_count(engine, &count) != 0 || count == 0) return {};

    // Build the role index once: plugin_id → roles-bitset.
    auto plugins = openswmm::discover_plugins_by_id();

    char path_buf[1024];
    char args_buf[512];  // ignored
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(engine, i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        const QString rowId = QString::fromUtf8(path_buf);
        for (const auto &p : plugins) {
            if (QString::fromStdString(p.plugin_id) != rowId) continue;
            for (auto r : p.roles) {
                if (r == role) return rowId;
            }
        }
    }
    return {};
}

// True when the plugin advertises all three writer roles (INPUT_READ,
// OUTPUT_WRITE, REPORT_WRITE) — the "Single container" precondition.
bool isTriRolePlugin(const QString &pluginId)
{
    if (pluginId.isEmpty()) return false;
    for (const auto &p : openswmm::discover_plugins_by_id()) {
        if (QString::fromStdString(p.plugin_id) != pluginId) continue;
        bool hasIn = false, hasOut = false, hasRpt = false;
        for (auto r : p.roles) {
            if      (r == openswmm::PluginRole::INPUT_READ)   hasIn  = true;
            else if (r == openswmm::PluginRole::OUTPUT_WRITE) hasOut = true;
            else if (r == openswmm::PluginRole::REPORT_WRITE) hasRpt = true;
        }
        return hasIn && hasOut && hasRpt;
    }
    return false;
}

void selectComboByPluginId(QComboBox *c, const QString &pluginId)
{
    if (!c) return;
    const int idx = c->findData(pluginId);
    c->setCurrentIndex(idx >= 0 ? idx : 0);
}

} // anonymous

void FilesPage::readWriterCombosFromEngine()
{
    selectComboByPluginId(m_inputWriterCombo,
        findActivePluginForRole(ctx_.engine(), openswmm::PluginRole::INPUT_READ));
    selectComboByPluginId(m_outputWriterCombo,
        findActivePluginForRole(ctx_.engine(), openswmm::PluginRole::OUTPUT_WRITE));
    selectComboByPluginId(m_reportWriterCombo,
        findActivePluginForRole(ctx_.engine(), openswmm::PluginRole::REPORT_WRITE));

    updateSingleContainerEnabled();
    if (m_singleContainerBox && m_singleContainerBox->isEnabled()) {
        // Auto-check when all three combos already resolve to the same id
        // — the simulation was previously set up as a single-container.
        const QString in  = m_inputWriterCombo  ? m_inputWriterCombo ->currentData().toString() : QString();
        const QString out = m_outputWriterCombo ? m_outputWriterCombo->currentData().toString() : QString();
        const QString rpt = m_reportWriterCombo ? m_reportWriterCombo->currentData().toString() : QString();
        if (!in.isEmpty() && in == out && in == rpt) {
            QSignalBlocker block(m_singleContainerBox);
            m_singleContainerBox->setChecked(true);
            // Also disable the locked combos so the UI is consistent.
            if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(false);
            if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(false);
        }
    }
}

int FilesPage::writeWriterCombosToEngine()
{
    if (!ctx_.engine()) return 0;
    QStringList wanted;
    auto addWanted = [&](QComboBox *c) {
        if (!c) return;
        const QString id = c->currentData().toString();
        if (!id.isEmpty() && !wanted.contains(id)) wanted << id;
    };
    addWanted(m_inputWriterCombo);
    addWanted(m_outputWriterCombo);
    addWanted(m_reportWriterCombo);

    // Collect existing [PLUGINS] row keys to check before inserting.  We
    // never auto-remove rows — the user manages those via the table so
    // any args they added stay intact.
    QSet<QString> existing;
    int count = 0;
    swmm_plugins_count(ctx_.engine(), &count);
    char path_buf[1024];
    char args_buf[512];
    for (int i = 0; i < count; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(ctx_.engine(), i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        existing.insert(QString::fromUtf8(path_buf));
    }

    int added = 0;
    for (const QString &id : wanted) {
        if (existing.contains(id)) continue;
        const QByteArray utf = id.toUtf8();
        if (swmm_plugin_set(ctx_.engine(), utf.constData(), nullptr) == SWMM_OK)
            ++added;
    }
    return added;
}

// ---------------------------------------------------------------------------
// Output / Report file path helpers (Slice AA-4)
// Paths are per-project, stored in QSettings keyed by model file path.
// ---------------------------------------------------------------------------

void FilesPage::readOutputPathsFromSettings()
{
    if (!ctx_.modelLayer() || !m_reportFilePathEdit || !m_outputFilePathEdit) return;
    QSettings s;
    const QString base = QStringLiteral("SWMMVis/Project/%1/")
                             .arg(ctx_.modelLayer()->modelFilePath());
    m_reportFilePathEdit->setText(s.value(base + QStringLiteral("ReportFilePath")).toString());
    m_outputFilePathEdit->setText(s.value(base + QStringLiteral("OutputFilePath")).toString());
}

void FilesPage::writeOutputPathsToSettings()
{
    if (!ctx_.modelLayer() || !m_reportFilePathEdit || !m_outputFilePathEdit) return;
    QSettings s;
    const QString base = QStringLiteral("SWMMVis/Project/%1/")
                             .arg(ctx_.modelLayer()->modelFilePath());
    s.setValue(base + QStringLiteral("ReportFilePath"),
               m_reportFilePathEdit->text().trimmed());
    s.setValue(base + QStringLiteral("OutputFilePath"),
               m_outputFilePathEdit->text().trimmed());
}

void FilesPage::browseForReportFile()
{
    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;
    auto *reg = FileFilterRegistry::instance();
    const QString filter = reg->filterFor(FilterKind::ReportWrite);
    const QString current = m_reportFilePathEdit ? m_reportFilePathEdit->text().trimmed() : QString();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Report File"),
        current.isEmpty() ? QString() : current,
        filter.isEmpty() ? tr("All Files (*)") : filter);
    if (!path.isEmpty() && m_reportFilePathEdit)
        m_reportFilePathEdit->setText(path);
}

void FilesPage::browseForOutputFile()
{
    using openswmmvis::FileFilterRegistry;
    using openswmmvis::FilterKind;
    auto *reg = FileFilterRegistry::instance();
    const QString filter = reg->filterFor(FilterKind::ResultsWrite);
    const QString current = m_outputFilePathEdit ? m_outputFilePathEdit->text().trimmed() : QString();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Output File"),
        current.isEmpty() ? QString() : current,
        filter.isEmpty() ? tr("All Files (*)") : filter);
    if (!path.isEmpty() && m_outputFilePathEdit)
        m_outputFilePathEdit->setText(path);
}

// ---------------------------------------------------------------------------
// Multi-row SAVE HOTSTART slots (Slice BV-01, 2026-05-21).
// ---------------------------------------------------------------------------

void FilesPage::onHotstartSaveAddRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const int row = m_hotstartSavesModel->appendRow(QString{}, 0.0);
    m_hotstartSavesView->setCurrentIndex(
        m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath));
    onHotstartSaveBrowseRow();   // prompt for the save-as path immediately
}

void FilesPage::onHotstartSaveBrowseRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0 || row >= m_hotstartSavesModel->rowCount()) return;

    const QString current = m_hotstartSavesModel->pathAt(row).trimmed();
    const QString path = QFileDialog::getSaveFileName(
        this,
        tr("Choose Hot-Start Save File"),
        current.isEmpty() ? QString() : current,
        tr("Hot-start files (*.hsf);;All Files (*)"));
    if (path.isEmpty()) return;
    m_hotstartSavesModel->setData(
        m_hotstartSavesModel->index(row, HotstartSavesModel::ColPath),
        path, Qt::EditRole);
}

void FilesPage::onHotstartSaveRemoveRow()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0) return;
    m_hotstartSavesModel->removeRows(row, 1);
}

void FilesPage::onHotstartSaveMoveRowUp()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row <= 0) return;
    moveHotstartSaveRow(row, row - 1);
}

void FilesPage::onHotstartSaveMoveRowDown()
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    const QModelIndex cur = m_hotstartSavesView->currentIndex();
    const int row = cur.isValid() ? cur.row() : -1;
    if (row < 0 || row >= m_hotstartSavesModel->rowCount() - 1) return;
    moveHotstartSaveRow(row, row + 1);
}

void FilesPage::moveHotstartSaveRow(int from, int to)
{
    if (!m_hotstartSavesModel || !m_hotstartSavesView) return;
    if (!m_hotstartSavesModel->swapRows(from, to)) return;
    m_hotstartSavesView->setCurrentIndex(
        m_hotstartSavesModel->index(to, HotstartSavesModel::ColPath));
}

void FilesPage::updateSingleContainerEnabled()
{
    if (!m_singleContainerBox || !m_inputWriterCombo) return;
    const QString id = m_inputWriterCombo->currentData().toString();
    const bool eligible = isTriRolePlugin(id);
    m_singleContainerBox->setEnabled(eligible);
    if (!eligible && m_singleContainerBox->isChecked()) {
        QSignalBlocker b(m_singleContainerBox);
        m_singleContainerBox->setChecked(false);
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(true);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(true);
    }
}

void FilesPage::onSingleContainerToggled(bool on)
{
    if (!m_inputWriterCombo) return;
    const QString id = m_inputWriterCombo->currentData().toString();

    if (on) {
        selectComboByPluginId(m_outputWriterCombo, id);
        selectComboByPluginId(m_reportWriterCombo, id);
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(false);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(false);
    } else {
        if (m_outputWriterCombo) m_outputWriterCombo->setEnabled(true);
        if (m_reportWriterCombo) m_reportWriterCombo->setEnabled(true);
    }
}

void FilesPage::readFilesSectionFromEngine()
{
    if (!ctx_.engine()) return;
    char buf[1024];

    auto getStr = [&](const char *key) -> QString {
        buf[0] = '\0';
        if (swmm_files_get(ctx_.engine(), key, buf, sizeof(buf)) != SWMM_OK) return {};
        return QString::fromUtf8(buf);
    };
    auto setMode = [&](QComboBox *c, const QString &mode) {
        if (!c) return;
        const int idx = c->findData(mode, Qt::UserRole, Qt::MatchFixedString);
        c->setCurrentIndex(idx >= 0 ? idx : 0);
    };

    if (m_rainfallPathEdit) m_rainfallPathEdit->setPath(getStr("RAINFALL_PATH"));
    setMode(m_rainfallModeCombo, getStr("RAINFALL_MODE"));
    if (m_runoffPathEdit)   m_runoffPathEdit->setPath(getStr("RUNOFF_PATH"));
    setMode(m_runoffModeCombo,   getStr("RUNOFF_MODE"));
    if (m_rdiiPathEdit)     m_rdiiPathEdit->setPath(getStr("RDII_PATH"));
    setMode(m_rdiiModeCombo,     getStr("RDII_MODE"));
    if (m_inflowsPathEdit)  m_inflowsPathEdit->setPath(getStr("INFLOWS_PATH"));
    if (m_outflowsPathEdit) m_outflowsPathEdit->setPath(getStr("OUTFLOWS_PATH"));
    if (m_hotstartUseEdit)  m_hotstartUseEdit->setPath(getStr("HOTSTART_USE_PATH"));

    // Multi-row SAVE HOTSTART table (Slice BV-01).  Phase 3.10.5 — pull
    // each entry from the engine's hotstart_saves vector into the
    // HotstartSavesModel.  Persistent editors get re-opened by the
    // rowsInserted hook installed in buildFilesTab().
    if (m_hotstartSavesModel) {
        m_hotstartSavesModel->clearRows();
        int count = 0;
        if (swmm_hotstart_saves_count(ctx_.engine(), &count) != SWMM_OK) count = 0;
        for (int i = 0; i < count; ++i) {
            char pbuf[1024] = {0};
            double dt = 0.0;
            swmm_hotstart_saves_get_path(ctx_.engine(), i, pbuf, sizeof(pbuf));
            swmm_hotstart_saves_get_datetime(ctx_.engine(), i, &dt);
            m_hotstartSavesModel->appendRow(QString::fromUtf8(pbuf),
                                            dt > 0.0 ? dt : 0.0);
        }
    }
}

int FilesPage::writeFilesSectionToEngine()
{
    if (!ctx_.engine()) return 0;
    int written = 0;
    char buf[1024];

    auto getCurrent = [&](const char *key) -> QString {
        buf[0] = '\0';
        swmm_files_get(ctx_.engine(), key, buf, sizeof(buf));
        return QString::fromUtf8(buf);
    };

    // Convert absolute paths to paths relative to the .inp directory
    // so the project folder stays portable.  Relative paths pass
    // through unchanged — the engine resolves them against the .inp
    // directory at run time (legacy SWMM5 behaviour).
    const QString inpPath = ctx_.modelLayer() ? ctx_.modelLayer()->modelFilePath() : QString();
    auto toRelative = [&](const QString &raw) -> QString {
        if (raw.isEmpty() || inpPath.isEmpty()) return raw;
        if (!QDir::isAbsolutePath(raw)) return raw;
        return ProjectSerializer::toRelativePath(raw, inpPath);
    };

    auto writeIfChanged = [&](const char *key, const QString &newVal) {
        if (getCurrent(key) == newVal) return;
        const QByteArray utf = newVal.toUtf8();
        if (swmm_files_set(ctx_.engine(), key, utf.constData()) == SWMM_OK)
            ++written;
    };
    auto writePathIfChanged = [&](const char *key, const QString &rawText) {
        writeIfChanged(key, toRelative(rawText.trimmed()));
    };

    if (m_rainfallPathEdit)
        writePathIfChanged("RAINFALL_PATH", m_rainfallPathEdit->absolutePath());
    if (m_rainfallModeCombo)
        writeIfChanged("RAINFALL_MODE",
                       m_rainfallModeCombo->currentData().toString());
    if (m_runoffPathEdit)
        writePathIfChanged("RUNOFF_PATH",   m_runoffPathEdit->absolutePath());
    if (m_runoffModeCombo)
        writeIfChanged("RUNOFF_MODE",
                       m_runoffModeCombo->currentData().toString());
    if (m_rdiiPathEdit)
        writePathIfChanged("RDII_PATH",     m_rdiiPathEdit->absolutePath());
    if (m_rdiiModeCombo)
        writeIfChanged("RDII_MODE",
                       m_rdiiModeCombo->currentData().toString());
    if (m_inflowsPathEdit)
        writePathIfChanged("INFLOWS_PATH",  m_inflowsPathEdit->absolutePath());
    if (m_outflowsPathEdit)
        writePathIfChanged("OUTFLOWS_PATH", m_outflowsPathEdit->absolutePath());
    if (m_hotstartUseEdit)
        writePathIfChanged("HOTSTART_USE_PATH",  m_hotstartUseEdit->absolutePath());

    // Multi-row SAVE HOTSTART (Slice BV-01).  The vector API is simpler
    // to drive than per-slot diffing: snapshot the engine's current
    // entries, compare against the model contents (path + datetime),
    // and only rebuild the vector when something actually changed.
    if (m_hotstartSavesModel) {
        struct HsRow { QString path; double dt; };
        QList<HsRow> desired;
        const int nRows = m_hotstartSavesModel->rowCount();
        for (int row = 0; row < nRows; ++row) {
            const QString rawPath = m_hotstartSavesModel->pathAt(row).trimmed();
            const QString relPath = toRelative(rawPath);
            const double dt       = m_hotstartSavesModel->oaDateAt(row);

            // Skip empty rows entirely — an empty path with no datetime
            // is a stub the user never filled in.
            if (relPath.isEmpty() && dt == 0.0) continue;
            desired.push_back({relPath, dt});
        }

        // Compare against current engine state.
        int curCount = 0;
        swmm_hotstart_saves_count(ctx_.engine(), &curCount);
        bool changed = (curCount != desired.size());
        for (int i = 0; !changed && i < curCount; ++i) {
            char pbuf[1024] = {0};
            double curDt = 0.0;
            swmm_hotstart_saves_get_path(ctx_.engine(), i, pbuf, sizeof(pbuf));
            swmm_hotstart_saves_get_datetime(ctx_.engine(), i, &curDt);
            if (QString::fromUtf8(pbuf) != desired[i].path) changed = true;
            else if (curDt != desired[i].dt)                changed = true;
        }
        if (changed) {
            swmm_hotstart_saves_clear(ctx_.engine());
            for (const auto &r : desired) {
                const QByteArray utf = r.path.toUtf8();
                swmm_hotstart_saves_add(ctx_.engine(), utf.constData(), r.dt);
            }
            ++written;
        }
    }
    return written;
}

int FilesPage::writePluginsToEngine()
{
    if (!m_pluginsModel || !ctx_.engine()) return 0;

    // Snapshot existing engine rows by key so we can compute the
    // additions, replacements, and removals that the table represents.
    int existingCount = 0;
    swmm_plugins_count(ctx_.engine(), &existingCount);

    QHash<QString, QString> existing;
    char path_buf[1024];
    char args_buf[2048];
    for (int i = 0; i < existingCount; ++i) {
        path_buf[0] = '\0';
        args_buf[0] = '\0';
        if (swmm_plugin_get(ctx_.engine(), i,
                            path_buf, sizeof(path_buf),
                            args_buf, sizeof(args_buf)) != 0) continue;
        existing.insert(QString::fromUtf8(path_buf),
                        QString::fromUtf8(args_buf));
    }

    int written = 0;
    QSet<QString> seen;
    const int nRows = m_pluginsModel->rowCount();
    for (int row = 0; row < nRows; ++row) {
        const QString key  = m_pluginsModel->pathAt(row).trimmed();
        const QString args = m_pluginsModel->argsAt(row).trimmed();
        if (key.isEmpty()) continue;
        seen.insert(key);

        // Only call set when the row is new or its args changed —
        // avoids spurious dirty-marker bumps in the engine.
        const auto it = existing.constFind(key);
        if (it == existing.constEnd() || it.value() != args) {
            const QByteArray k = key.toUtf8();
            const QByteArray a = args.toUtf8();
            if (swmm_plugin_set(ctx_.engine(), k.constData(),
                                args.isEmpty() ? nullptr : a.constData()) == 0)
                ++written;
        }
    }

    // Remove engine rows whose keys are no longer in the table.
    for (auto it = existing.constBegin(); it != existing.constEnd(); ++it) {
        if (!seen.contains(it.key())) {
            const QByteArray k = it.key().toUtf8();
            if (swmm_plugin_remove(ctx_.engine(), k.constData()) == 0)
                ++written;
        }
    }
    return written;
}

} // namespace openswmmvis::ui
