/*!
 * \file   preferencesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/dialogs/preferencesdialog.h"

#include "core/linkrenderingprefs.h"
#include "core/preferencesmanager.h"
#include "core/selectionrenderingprefs.h"
#include "ui/dialogs/licenseagreementdialog.h"
#include "version.h"
#include "legacy_version.h"

#include "qpropertymodel.h"
#include "qpropertyitemdelegate.h"

#include <QThread>
#include <QTreeView>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setMinimumSize(560, 420);
    buildUi();
    readFromManager();
}

void PreferencesDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Left-list / right-stack layout.
    auto *split = new QHBoxLayout();
    split->setSpacing(8);
    root->addLayout(split, 1);

    m_categoryList = new QListWidget(this);
    m_categoryList->setFixedWidth(180);
    m_categoryList->addItem(tr("General"));
    m_categoryList->addItem(tr("Selection"));
    m_categoryList->addItem(tr("Canvas && CRS"));
    m_categoryList->addItem(tr("Rendering"));
    m_categoryList->addItem(tr("Simulation"));
    m_categoryList->addItem(tr("Simulation Defaults"));
    m_categoryList->addItem(tr("Dynamic Wave Defaults"));
    m_categoryList->addItem(tr("Map Display"));
    m_categoryList->addItem(tr("Measure Tool"));
    m_categoryList->addItem(tr("Naming"));
    split->addWidget(m_categoryList);

    m_pages = new QStackedWidget(this);
    m_pages->addWidget(buildGeneralPage());
    m_pages->addWidget(buildSelectionPage());
    m_pages->addWidget(buildCanvasPage());
    m_pages->addWidget(buildRenderingPage());
    m_pages->addWidget(buildSimulationPage());
    m_pages->addWidget(buildSimulationDefaultsPage());
    m_pages->addWidget(buildDynamicWaveDefaultsPage());
    m_pages->addWidget(buildMapDisplayPage());
    m_pages->addWidget(buildMeasureToolPage());
    m_pages->addWidget(buildNamingPage());
    split->addWidget(m_pages, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);
    m_categoryList->setCurrentRow(1);   // start on Selection — the most
                                         // frequently-tuned page today.

    // Buttons: Reset | Apply / Cancel / OK.
    auto *btnRow = new QHBoxLayout();
    root->addLayout(btnRow);
    auto *resetBtn = new QPushButton(tr("Reset to defaults"), this);
    connect(resetBtn, &QPushButton::clicked,
            this, &PreferencesDialog::onResetToDefaults);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch(1);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
        this);
    btnRow->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &PreferencesDialog::onApply);
}

QWidget *PreferencesDialog::buildGeneralPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_showLicenseOnStartupBox = new QCheckBox(
        tr("Show license agreement on startup"), page);
    m_showLicenseOnStartupBox->setToolTip(tr(
        "When checked, the MIT license agreement dialog is shown each time "
        "the application starts. Uncheck to suppress it after you have "
        "accepted the terms."));
    f->addRow(m_showLicenseOnStartupBox);

    m_autoLengthBox = new QCheckBox(tr("Auto-length conduits on edit"), page);
    m_autoLengthBox->setToolTip(tr(
        "When checked, conduit lengths recompute from geometry whenever "
        "their endpoints move. Default on; toggled per-project from the "
        "status-bar Auto-Length switch."));
    f->addRow(m_autoLengthBox);

    m_defaultEngineCombo = new QComboBox(page);
    m_defaultEngineCombo->addItem(
        tr("OpenSWMM %1 (refactored)").arg(QLatin1String(SWMM_VERSION_FULL)),
        QLatin1String(SWMM_VERSION));
    m_defaultEngineCombo->addItem(
        tr("SWMM %1 (legacy)").arg(QLatin1String(OPENSWMM_LEGACY_FULL_VERSION)),
        QLatin1String(LEGACY_SWMM_VERSION));
    m_defaultEngineCombo->setToolTip(tr(
        "Engine version selected by default when a new project opens. "
        "The status-bar engine picker remains available for per-project overrides."));
    f->addRow(tr("Default engine mode"), m_defaultEngineCombo);

    return page;
}

QWidget *PreferencesDialog::buildSelectionPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_clickTolerancePxSpin = new QSpinBox(page);
    m_clickTolerancePxSpin->setRange(1, 200);
    m_clickTolerancePxSpin->setSuffix(tr(" px"));
    m_clickTolerancePxSpin->setToolTip(tr(
        "Minimum pick radius for click-selection. The effective radius "
        "at pick time is max(this value, largest-rendered-glyph half-"
        "bound + 4 px halo), so clicks inside any visible marker always "
        "hit regardless of this number."));
    f->addRow(tr("Click tolerance"),           m_clickTolerancePxSpin);

    m_dragThresholdPxSpin = new QSpinBox(page);
    m_dragThresholdPxSpin->setRange(1, 200);
    m_dragThresholdPxSpin->setSuffix(tr(" px"));
    m_dragThresholdPxSpin->setToolTip(tr(
        "Cursor distance required before a click turns into a rubber-band "
        "select. Higher values forgive trackpad jitter on single clicks."));
    f->addRow(tr("Drag threshold"),             m_dragThresholdPxSpin);

    m_clearOnMissBox = new QCheckBox(tr("Clear selection when clicking "
                                         "empty space"), page);
    f->addRow(m_clearOnMissBox);

    // Per-class selection pens + brushes. QPenPropertyItem and
    // QBrushPropertyItem from QPropertyModel give the full set of
    // children (colour / width / cap / join / style / dash for pens,
    // colour / style for brushes). Edits route through
    // SelectionRenderingPrefs's Q_PROPERTY setters straight into
    // PreferencesManager. Width semantics for the link pen are
    // ADDITIVE — see PreferencesManager::selectionPen() docs.
    auto *page2 = page;   // keep `page` reference for QFormLayout above
    auto *selGroup = new QGroupBox(tr("Selection Pens & Fills"), page2);
    auto *gv       = new QVBoxLayout(selGroup);
    gv->setContentsMargins(8, 8, 8, 8);

    auto *intro = new QLabel(
        tr("Edit the stroke pen for each class (link halo, polygon "
           "outline, glyph outline) and the fill brush for polygonal "
           "and glyph classes. Link pen width adds on top of the "
           "link's own pen — a width of 2 means a 2 px halo."),
        selGroup);
    intro->setWordWrap(true);
    gv->addWidget(intro);

    m_selectionPrefs = new SelectionRenderingPrefs(this);
    m_selectionModel = new QPropertyModel(this);
    m_selectionModel->setData(
        QVariant::fromValue(static_cast<QObject*>(m_selectionPrefs)));

    auto *tree = new QTreeView(selGroup);
    tree->setModel(m_selectionModel);
    tree->setItemDelegate(new QPropertyItemDelegate(m_selectionModel));
    tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    tree->setAlternatingRowColors(true);
    tree->setMinimumHeight(240);
    tree->expandToDepth(0);
    tree->resizeColumnToContents(0);
    gv->addWidget(tree, 1);

    f->addRow(selGroup);

    return page;
}

QWidget *PreferencesDialog::buildCanvasPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_defaultToolCombo = new QComboBox(page);
    m_defaultToolCombo->addItem(tr("Select"), QStringLiteral("Select"));
    m_defaultToolCombo->addItem(tr("Pan"),    QStringLiteral("Pan"));
    m_defaultToolCombo->addItem(tr("Zoom"),   QStringLiteral("Zoom"));
    m_defaultToolCombo->setToolTip(tr(
        "Tool automatically activated when a project window opens."));
    f->addRow(tr("Default tool on project open"), m_defaultToolCombo);

    // CRS mode — auto-local (default) or custom EPSG code.
    auto *crsGroup = new QGroupBox(tr("Default CRS (when .inp has no CRS)"), page);
    auto *crsVlay  = new QVBoxLayout(crsGroup);

    m_crsAutoRadio = new QRadioButton(tr("Auto — use map units from .inp (recommended)"), crsGroup);
    m_crsAutoRadio->setToolTip(tr(
        "Reads the [MAP] Units field (FEET / METERS) and assigns a local\n"
        "projected CRS with the correct unit. Scale bar and distances are\n"
        "physically meaningful without needing a geographic CRS."));
    crsVlay->addWidget(m_crsAutoRadio);

    m_crsCustomRadio = new QRadioButton(tr("Custom CRS:"), crsGroup);
    m_crsCustomRadio->setToolTip(tr(
        "Use a specific authority + code when the loaded .inp has no CRS."));
    crsVlay->addWidget(m_crsCustomRadio);

    auto *crsCustomRow = new QHBoxLayout();
    m_crsAuthorityEdit = new QLineEdit(crsGroup);
    m_crsAuthorityEdit->setToolTip(tr("CRS authority, e.g. \"EPSG\"."));
    m_crsAuthorityEdit->setFixedWidth(80);
    m_crsCodeSpin = new QSpinBox(crsGroup);
    m_crsCodeSpin->setRange(1, 999999);
    m_crsCodeSpin->setToolTip(tr("CRS code, e.g. 4326 for WGS 84."));
    crsCustomRow->addWidget(m_crsAuthorityEdit);
    crsCustomRow->addWidget(m_crsCodeSpin);
    crsCustomRow->addStretch();
    crsVlay->addLayout(crsCustomRow);

    // Enable custom fields only when the custom radio is selected.
    auto syncCrsWidgets = [this]() {
        const bool custom = m_crsCustomRadio->isChecked();
        m_crsAuthorityEdit->setEnabled(custom);
        m_crsCodeSpin->setEnabled(custom);
    };
    connect(m_crsAutoRadio,   &QRadioButton::toggled, this, [syncCrsWidgets](bool){ syncCrsWidgets(); });
    connect(m_crsCustomRadio, &QRadioButton::toggled, this, [syncCrsWidgets](bool){ syncCrsWidgets(); });
    m_crsAutoRadio->setChecked(true);
    syncCrsWidgets();

    f->addRow(crsGroup);

    // Snapping section.
    auto *snapGroup = new QGroupBox(tr("Snapping"), page);
    auto *snapVlay  = new QVBoxLayout(snapGroup);

    m_snapEnabledBox = new QCheckBox(tr("Enable snapping"), snapGroup);
    m_snapEnabledBox->setToolTip(tr(
        "When checked, vertex placement in drawing tools snaps to nearby\n"
        "nodes, link vertices, and subcatchment polygon vertices."));
    snapVlay->addWidget(m_snapEnabledBox);

    auto *snapRow = new QHBoxLayout();
    auto *tolLabel = new QLabel(tr("Tolerance:"), snapGroup);
    m_snapToleranceSpin = new QSpinBox(snapGroup);
    m_snapToleranceSpin->setRange(4, 64);
    m_snapToleranceSpin->setSuffix(tr(" px"));
    m_snapToleranceSpin->setToolTip(tr("Snap detection radius in screen pixels."));
    snapRow->addWidget(tolLabel);
    snapRow->addWidget(m_snapToleranceSpin);
    snapRow->addStretch();
    snapVlay->addLayout(snapRow);

    m_snapToVerticesBox = new QCheckBox(tr("Also snap to link and subcatchment vertices"), snapGroup);
    m_snapToVerticesBox->setToolTip(tr(
        "In addition to node/gage centres, also snap to intermediate vertices\n"
        "of links and polygon corners of subcatchments."));
    snapVlay->addWidget(m_snapToVerticesBox);

    auto syncSnapWidgets = [this]() {
        const bool on = m_snapEnabledBox->isChecked();
        m_snapToleranceSpin->setEnabled(on);
        m_snapToVerticesBox->setEnabled(on);
    };
    connect(m_snapEnabledBox, &QCheckBox::toggled, this,
            [syncSnapWidgets](bool) { syncSnapWidgets(); });
    syncSnapWidgets();

    f->addRow(snapGroup);

    return page;
}

QWidget *PreferencesDialog::buildRenderingPage()
{
    auto *page = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    auto *f    = new QFormLayout();

    m_labelLodSpin = new QDoubleSpinBox(page);
    m_labelLodSpin->setRange(0.01, 100.0);
    m_labelLodSpin->setSingleStep(0.1);
    m_labelLodSpin->setDecimals(2);
    m_labelLodSpin->setToolTip(tr(
        "Minimum view-transform scale (m11) at which labels are drawn. "
        "Higher values hide labels sooner when zooming out; lower "
        "values draw labels even at coarse zoom (at a performance cost)."));
    f->addRow(tr("Label zoom-out threshold (m11)"), m_labelLodSpin);

    auto *lodGroup = new QGroupBox(tr("Label Rendering"), page);
    lodGroup->setLayout(f);
    outer->addWidget(lodGroup);

    // Link pens — full QPen editor per link type. QPenPropertyItem
    // exposes width / dash-offset / style / cap / join / brush as
    // expandable children, so users can dial in not just colour but
    // line thickness and cap shape (the non-conduit defaults are
    // round-capped — see kPumpPenDefault et al. in preferencesmanager.cpp).
    auto *linkGroup = new QGroupBox(tr("Link Pens"), page);
    auto *lv        = new QVBoxLayout(linkGroup);
    lv->setContentsMargins(8, 8, 8, 8);

    auto *intro = new QLabel(
        tr("Edit colour, width, line style, cap, and join per link type. "
           "Expand a row to access individual pen attributes. Changes apply "
           "immediately to open project views."),
        linkGroup);
    intro->setWordWrap(true);
    lv->addWidget(intro);

    m_linkPrefs    = new LinkRenderingPrefs(this);
    m_linkPenModel = new QPropertyModel(this);
    m_linkPenModel->setData(QVariant::fromValue(static_cast<QObject*>(m_linkPrefs)));

    auto *tree = new QTreeView(linkGroup);
    tree->setModel(m_linkPenModel);
    tree->setItemDelegate(new QPropertyItemDelegate(m_linkPenModel));
    tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    tree->setAlternatingRowColors(true);
    tree->setMinimumHeight(220);
    tree->expandToDepth(0);
    tree->resizeColumnToContents(0);
    lv->addWidget(tree, 1);

    outer->addWidget(linkGroup, 1);

    outer->addStretch(0);

    return page;
}

QWidget *PreferencesDialog::buildSimulationPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);

    m_progressTickMsSpin = new QSpinBox(page);
    m_progressTickMsSpin->setRange(50, 10000);
    m_progressTickMsSpin->setSingleStep(100);
    m_progressTickMsSpin->setSuffix(tr(" ms"));
    m_progressTickMsSpin->setToolTip(tr(
        "Rate at which the running simulation pushes progress / current-"
        "time updates to the Simulation Status dock. Lower = more live, "
        "more overhead. Default 1000 ms (1 Hz)."));
    f->addRow(tr("Progress-tick interval"),       m_progressTickMsSpin);

    return page;
}

QWidget *PreferencesDialog::buildSimulationDefaultsPage()
{
    auto *page  = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    auto *intro = new QLabel(
        tr("Defaults applied when File→New creates a blank project. "
           "Existing projects are not affected — use Simulation Options "
           "per project to change a running model."), page);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    // ── Process models ───────────────────────────────────────────────────
    auto *procGroup = new QGroupBox(tr("Process models"), page);
    auto *procForm  = new QFormLayout(procGroup);

    m_simFlowUnitsCombo = new QComboBox(procGroup);
    m_simFlowUnitsCombo->addItems({QStringLiteral("CFS"), QStringLiteral("GPM"),
                                    QStringLiteral("MGD"), QStringLiteral("CMS"),
                                    QStringLiteral("LPS"), QStringLiteral("MLD")});
    procForm->addRow(tr("Flow units (FLOW_UNITS)"), m_simFlowUnitsCombo);

    m_simInfiltrationCombo = new QComboBox(procGroup);
    m_simInfiltrationCombo->addItem(tr("Horton"),               QStringLiteral("HORTON"));
    m_simInfiltrationCombo->addItem(tr("Modified Horton"),      QStringLiteral("MODIFIED_HORTON"));
    m_simInfiltrationCombo->addItem(tr("Green-Ampt"),           QStringLiteral("GREEN_AMPT"));
    m_simInfiltrationCombo->addItem(tr("Modified Green-Ampt"),  QStringLiteral("MODIFIED_GREEN_AMPT"));
    m_simInfiltrationCombo->addItem(tr("Curve Number"),         QStringLiteral("CURVE_NUMBER"));
    procForm->addRow(tr("Infiltration model (INFILTRATION)"), m_simInfiltrationCombo);

    m_simFlowRoutingCombo = new QComboBox(procGroup);
    m_simFlowRoutingCombo->addItem(tr("Steady"),         QStringLiteral("STEADY"));
    m_simFlowRoutingCombo->addItem(tr("Kinematic Wave"), QStringLiteral("KINWAVE"));
    m_simFlowRoutingCombo->addItem(tr("Dynamic Wave"),   QStringLiteral("DYNWAVE"));
    procForm->addRow(tr("Hydraulic routing method (FLOW_ROUTING)"), m_simFlowRoutingCombo);

    outer->addWidget(procGroup);

    // ── Process toggles (all default OFF) ────────────────────────────────
    auto *togGroup = new QGroupBox(tr("Process modules (off by default)"), page);
    auto *togLay   = new QVBoxLayout(togGroup);

    auto mkBox = [togGroup](const QString &label, const QString &tip) {
        auto *b = new QCheckBox(label, togGroup);
        if (!tip.isEmpty()) b->setToolTip(tip);
        return b;
    };
    m_simIgnoreRainfallBox    = mkBox(tr("Rainfall / Runoff"),
        tr("When unchecked, IGNORE_RAINFALL is emitted into [OPTIONS]."));
    m_simIgnoreRdiiBox        = mkBox(tr("RDII (rainfall-derived inflow)"),
        tr("When unchecked, IGNORE_RDII is emitted."));
    m_simIgnoreSnowmeltBox    = mkBox(tr("Snowmelt"),
        tr("When unchecked, IGNORE_SNOWMELT is emitted."));
    m_simIgnoreGroundwaterBox = mkBox(tr("Groundwater"),
        tr("When unchecked, IGNORE_GROUNDWATER is emitted."));
    m_simModule2DBox          = mkBox(tr("2D Surface Routing"),
        tr("Enables the 2D module when a fresh project opens. Off by default."));
    m_simIgnoreRoutingBox     = mkBox(tr("Flow Routing"),
        tr("When unchecked, IGNORE_ROUTING is emitted (no link routing)."));
    m_simIgnoreQualityBox     = mkBox(tr("Water Quality"),
        tr("When unchecked, IGNORE_QUALITY is emitted."));
    m_simAllowPondingBox      = mkBox(tr("Allow ponding (ALLOW_PONDING)"), QString());
    m_simSkipSteadyStateBox   = mkBox(tr("Skip steady-state periods (SKIP_STEADY_STATE)"), QString());

    togLay->addWidget(m_simIgnoreRainfallBox);
    togLay->addWidget(m_simIgnoreRdiiBox);
    togLay->addWidget(m_simIgnoreSnowmeltBox);
    togLay->addWidget(m_simIgnoreGroundwaterBox);
    togLay->addWidget(m_simModule2DBox);
    togLay->addWidget(m_simIgnoreRoutingBox);
    togLay->addWidget(m_simIgnoreQualityBox);
    togLay->addWidget(m_simAllowPondingBox);
    togLay->addWidget(m_simSkipSteadyStateBox);
    outer->addWidget(togGroup);

    // ── Geometry / hydraulics defaults ───────────────────────────────────
    auto *geomGroup = new QGroupBox(tr("Hydraulics"), page);
    auto *geomForm  = new QFormLayout(geomGroup);

    m_simMinSlopePctSpin = new QDoubleSpinBox(geomGroup);
    m_simMinSlopePctSpin->setRange(0.0, 100.0);
    m_simMinSlopePctSpin->setDecimals(4);
    m_simMinSlopePctSpin->setSuffix(QStringLiteral(" %"));
    m_simMinSlopePctSpin->setToolTip(tr("Minimum conduit slope (MIN_SLOPE)."));
    geomForm->addRow(tr("Minimum conduit slope"), m_simMinSlopePctSpin);
    outer->addWidget(geomGroup);

    // ── Schedule defaults ────────────────────────────────────────────────
    auto *schedGroup = new QGroupBox(tr("Schedule"), page);
    auto *schedForm  = new QFormLayout(schedGroup);

    auto *startReportNote = new QLabel(
        tr("<i>Start / Report-start default to today at midnight; End defaults to start + 24 h. "
           "Adjust on a per-project basis in the New Project dialog or Simulation Options.</i>"),
        schedGroup);
    startReportNote->setWordWrap(true);
    schedForm->addRow(startReportNote);

    m_simDryDaysSpin = new QDoubleSpinBox(schedGroup);
    m_simDryDaysSpin->setRange(0.0, 3650.0);
    m_simDryDaysSpin->setDecimals(2);
    m_simDryDaysSpin->setSuffix(QStringLiteral(" d"));
    schedForm->addRow(tr("Antecedent dry days (DRY_DAYS)"), m_simDryDaysSpin);

    outer->addWidget(schedGroup);

    // ── Time-step defaults ───────────────────────────────────────────────
    auto *stepGroup = new QGroupBox(tr("Time steps"), page);
    auto *stepForm  = new QFormLayout(stepGroup);

    auto makeMinSpin = [stepGroup]() {
        auto *s = new QSpinBox(stepGroup);
        s->setRange(1, 24 * 60);
        s->setSuffix(QStringLiteral(" min"));
        return s;
    };
    m_simReportStepSpin = makeMinSpin();
    m_simDryStepSpin    = makeMinSpin();
    m_simWetStepSpin    = makeMinSpin();
    stepForm->addRow(tr("Reporting (REPORT_STEP)"), m_simReportStepSpin);
    stepForm->addRow(tr("Runoff dry-weather (DRY_STEP)"), m_simDryStepSpin);
    stepForm->addRow(tr("Runoff wet-weather (WET_STEP)"), m_simWetStepSpin);

    m_simRuleStepSpin = new QSpinBox(stepGroup);
    m_simRuleStepSpin->setRange(0, 3600 * 24);
    m_simRuleStepSpin->setSuffix(QStringLiteral(" s"));
    m_simRuleStepSpin->setSpecialValueText(tr("use routing step"));
    stepForm->addRow(tr("Control rule (RULE_STEP)"), m_simRuleStepSpin);

    m_simRoutingStepSpin = new QDoubleSpinBox(stepGroup);
    m_simRoutingStepSpin->setRange(0.1, 3600.0);
    m_simRoutingStepSpin->setDecimals(2);
    m_simRoutingStepSpin->setSuffix(QStringLiteral(" s"));
    stepForm->addRow(tr("Routing (ROUTING_STEP)"), m_simRoutingStepSpin);

    outer->addWidget(stepGroup);

    // ── Tolerances ───────────────────────────────────────────────────────
    auto *tolGroup = new QGroupBox(tr("Solver tolerances"), page);
    auto *tolForm  = new QFormLayout(tolGroup);

    m_simSysFlowTolSpin = new QDoubleSpinBox(tolGroup);
    m_simSysFlowTolSpin->setRange(0.0, 100.0);
    m_simSysFlowTolSpin->setSuffix(QStringLiteral(" %"));
    tolForm->addRow(tr("System flow tolerance (SYS_FLOW_TOL)"), m_simSysFlowTolSpin);

    m_simLatFlowTolSpin = new QDoubleSpinBox(tolGroup);
    m_simLatFlowTolSpin->setRange(0.0, 100.0);
    m_simLatFlowTolSpin->setSuffix(QStringLiteral(" %"));
    tolForm->addRow(tr("Lateral flow tolerance (LAT_FLOW_TOL)"), m_simLatFlowTolSpin);

    m_simMaxTrialsSpin = new QSpinBox(tolGroup);
    m_simMaxTrialsSpin->setRange(1, 100);
    tolForm->addRow(tr("Max trials (MAX_TRIALS)"), m_simMaxTrialsSpin);

    outer->addWidget(tolGroup);

    outer->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildDynamicWaveDefaultsPage()
{
    auto *page  = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    auto *intro = new QLabel(
        tr("Dynamic-wave-specific defaults. Some keys (semi-implicit node "
           "continuity, Anderson acceleration) only emit when the default "
           "engine is the refactored engine; legacy engine .inp output stays "
           "SWMM5-compatible."), page);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto *condGroup = new QGroupBox(tr("Conduit / channel"), page);
    auto *condForm  = new QFormLayout(condGroup);

    m_simInertialDampCombo = new QComboBox(condGroup);
    m_simInertialDampCombo->addItem(tr("None"),    QStringLiteral("NONE"));
    m_simInertialDampCombo->addItem(tr("Dampen"),  QStringLiteral("PARTIAL"));
    m_simInertialDampCombo->addItem(tr("Ignore"),  QStringLiteral("FULL"));
    m_simInertialDampCombo->setToolTip(tr("INERTIAL_DAMPING."));
    condForm->addRow(tr("Inertial terms"), m_simInertialDampCombo);

    m_simNormalFlowCombo = new QComboBox(condGroup);
    m_simNormalFlowCombo->addItem(tr("Slope"),                 QStringLiteral("SLOPE"));
    m_simNormalFlowCombo->addItem(tr("Froude"),                QStringLiteral("FROUDE"));
    m_simNormalFlowCombo->addItem(tr("Slope and Froude"),      QStringLiteral("BOTH"));
    m_simNormalFlowCombo->addItem(tr("Neither"),               QStringLiteral("NEITHER"));
    m_simNormalFlowCombo->setToolTip(tr("NORMAL_FLOW_LIMITED."));
    condForm->addRow(tr("Normal flow criterion"), m_simNormalFlowCombo);

    m_simForceMainCombo = new QComboBox(condGroup);
    m_simForceMainCombo->addItem(tr("Hazen-Williams"), QStringLiteral("H-W"));
    m_simForceMainCombo->addItem(tr("Darcy-Weisbach"), QStringLiteral("D-W"));
    condForm->addRow(tr("Force-main equation"), m_simForceMainCombo);

    m_simSurchargeCombo = new QComboBox(condGroup);
    m_simSurchargeCombo->addItem(tr("EXTRAN (legacy)"),    QStringLiteral("EXTRAN"));
    m_simSurchargeCombo->addItem(tr("SLOT (Preissmann)"),  QStringLiteral("SLOT"));
    m_simSurchargeCombo->addItem(tr("DYNAMIC_SLOT"),       QStringLiteral("DYNAMIC_SLOT"));
    condForm->addRow(tr("Surcharge method"), m_simSurchargeCombo);

    outer->addWidget(condGroup);

    // ── Variable timestep ────────────────────────────────────────────────
    auto *vsGroup = new QGroupBox(tr("Variable timestep"), page);
    auto *vsLay   = new QVBoxLayout(vsGroup);

    m_simVariableStepBox = new QCheckBox(tr("Use variable timestep"), vsGroup);
    m_simVariableStepBox->setToolTip(tr(
        "When checked, VARIABLE_STEP is set to the relaxation factor below. "
        "When unchecked, VARIABLE_STEP is forced to 0 (disabled)."));
    vsLay->addWidget(m_simVariableStepBox);

    auto *vsForm = new QFormLayout();
    m_simVariableStepFactorSpin = new QDoubleSpinBox(vsGroup);
    m_simVariableStepFactorSpin->setRange(0.0, 1.0);
    m_simVariableStepFactorSpin->setDecimals(3);
    m_simVariableStepFactorSpin->setSingleStep(0.05);
    m_simVariableStepFactorSpin->setSuffix(QString());
    vsForm->addRow(tr("Timestep relaxation (VARIABLE_STEP)"), m_simVariableStepFactorSpin);

    m_simMinRoutingStepSpin = new QDoubleSpinBox(vsGroup);
    m_simMinRoutingStepSpin->setRange(0.01, 60.0);
    m_simMinRoutingStepSpin->setDecimals(3);
    m_simMinRoutingStepSpin->setSuffix(QStringLiteral(" s"));
    vsForm->addRow(tr("Minimum variable timestep (MINIMUM_STEP)"), m_simMinRoutingStepSpin);

    m_simLengtheningStepSpin = new QDoubleSpinBox(vsGroup);
    m_simLengtheningStepSpin->setRange(0.0, 3600.0);
    m_simLengtheningStepSpin->setDecimals(3);
    m_simLengtheningStepSpin->setSuffix(QStringLiteral(" s"));
    vsForm->addRow(tr("Conduit lengthening (LENGTHENING_STEP)"), m_simLengtheningStepSpin);

    vsLay->addLayout(vsForm);

    auto syncVsFields = [this]() {
        const bool on = m_simVariableStepBox->isChecked();
        m_simVariableStepFactorSpin->setEnabled(on);
        m_simMinRoutingStepSpin->setEnabled(on);
    };
    connect(m_simVariableStepBox, &QCheckBox::toggled, this,
            [syncVsFields](bool) { syncVsFields(); });
    syncVsFields();
    outer->addWidget(vsGroup);

    // ── Convergence + solver ─────────────────────────────────────────────
    auto *solvGroup = new QGroupBox(tr("Solver"), page);
    auto *solvForm  = new QFormLayout(solvGroup);

    m_simHeadToleranceSpin = new QDoubleSpinBox(solvGroup);
    m_simHeadToleranceSpin->setRange(1e-6, 1.0);
    m_simHeadToleranceSpin->setDecimals(6);
    m_simHeadToleranceSpin->setSingleStep(0.001);
    solvForm->addRow(tr("Head convergence (HEAD_TOLERANCE)"), m_simHeadToleranceSpin);

    m_simNodeContinuityCombo = new QComboBox(solvGroup);
    m_simNodeContinuityCombo->addItem(tr("Explicit (legacy)"),   QStringLiteral("EXPLICIT"));
    m_simNodeContinuityCombo->addItem(tr("Semi-implicit (new)"), QStringLiteral("SEMI_IMPLICIT"));
    m_simNodeContinuityCombo->setToolTip(tr(
        "NODE_CONTINUITY. Semi-implicit requires the refactored engine."));
    solvForm->addRow(tr("Node continuity"), m_simNodeContinuityCombo);

    m_simAndersonAccelBox = new QCheckBox(tr("Anderson acceleration (ANDERSON_ACCEL)"), solvGroup);
    m_simAndersonAccelBox->setToolTip(tr(
        "Refactored-engine option for iterative-solver acceleration."));
    solvForm->addRow(QString(), m_simAndersonAccelBox);

    m_simThreadsSpin = new QSpinBox(solvGroup);
    m_simThreadsSpin->setRange(0, 256);
    m_simThreadsSpin->setSpecialValueText(tr("auto"));
    m_simThreadsSpin->setToolTip(tr(
        "Number of OpenMP worker threads written to [OPTIONS] THREADS for "
        "new projects. 0 = engine auto. The Reset button maxes this to the "
        "machine's logical-processor count."));
    solvForm->addRow(tr("Worker threads (THREADS)"), m_simThreadsSpin);

    outer->addWidget(solvGroup);
    outer->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildMapDisplayPage()
{
    auto *page  = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    auto *group = new QGroupBox(tr("Scale Bar"), page);
    auto *f     = new QFormLayout(group);

    // Color button (same pattern as selection colors)
    m_scaleBarColorBtn = new QPushButton(group);
    m_scaleBarColorBtn->setMinimumWidth(120);
    m_scaleBarColorBtn->setAutoFillBackground(true);
    connect(m_scaleBarColorBtn, &QPushButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(
            m_pendingScaleBarColor.isValid() ? m_pendingScaleBarColor : Qt::black,
            this,
            tr("Scale bar color"),
            QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        m_pendingScaleBarColor = c;
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        m_scaleBarColorBtn->setStyleSheet(css);
        m_scaleBarColorBtn->setText(c.name(QColor::HexRgb).toUpper());
    });
    f->addRow(tr("Color"), m_scaleBarColorBtn);

    m_scaleBarPenWidthSpin = new QSpinBox(group);
    m_scaleBarPenWidthSpin->setRange(1, 20);
    m_scaleBarPenWidthSpin->setSuffix(tr(" px"));
    f->addRow(tr("Line width"), m_scaleBarPenWidthSpin);

    m_scaleBarPenStyleCombo = new QComboBox(group);
    m_scaleBarPenStyleCombo->addItem(tr("Solid"),       static_cast<int>(Qt::SolidLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dash"),        static_cast<int>(Qt::DashLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dot"),         static_cast<int>(Qt::DotLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dash-Dot"),    static_cast<int>(Qt::DashDotLine));
    m_scaleBarPenStyleCombo->addItem(tr("Dash-Dot-Dot"), static_cast<int>(Qt::DashDotDotLine));
    f->addRow(tr("Line style"), m_scaleBarPenStyleCombo);

    m_scaleBarFontBtn = new QPushButton(group);
    connect(m_scaleBarFontBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QFont f = QFontDialog::getFont(&ok, m_pendingScaleBarFont, this, tr("Scale bar font"));
        if (!ok) return;
        m_pendingScaleBarFont = f;
        m_scaleBarFontBtn->setText(QStringLiteral("%1, %2pt")
            .arg(f.family()).arg(f.pointSize()));
    });
    f->addRow(tr("Font"), m_scaleBarFontBtn);

    m_scaleBarUnitsCombo = new QComboBox(group);
    m_scaleBarUnitsCombo->addItem(tr("Auto"),       0);
    m_scaleBarUnitsCombo->addItem(tr("Meters"),     1);
    m_scaleBarUnitsCombo->addItem(tr("Feet"),        2);
    m_scaleBarUnitsCombo->addItem(tr("Kilometers"), 3);
    m_scaleBarUnitsCombo->addItem(tr("Miles"),      4);
    f->addRow(tr("Units"), m_scaleBarUnitsCombo);

    m_scaleBarPositionCombo = new QComboBox(group);
    m_scaleBarPositionCombo->addItem(tr("Bottom Left"),  0);
    m_scaleBarPositionCombo->addItem(tr("Bottom Right"), 1);
    m_scaleBarPositionCombo->addItem(tr("Top Left"),     2);
    m_scaleBarPositionCombo->addItem(tr("Top Right"),    3);
    f->addRow(tr("Position"), m_scaleBarPositionCombo);

    m_scaleBarMaxBarLengthSpin = new QSpinBox(group);
    m_scaleBarMaxBarLengthSpin->setRange(20, 500);
    m_scaleBarMaxBarLengthSpin->setSuffix(tr(" px"));
    m_scaleBarMaxBarLengthSpin->setToolTip(tr(
        "Reference pixel length used to calculate the nearest round-number "
        "distance. Larger values produce a longer scale bar."));
    f->addRow(tr("Max bar length"), m_scaleBarMaxBarLengthSpin);

    m_scaleBarLabelDecimalsSpin = new QSpinBox(group);
    m_scaleBarLabelDecimalsSpin->setRange(-1, 4);
    m_scaleBarLabelDecimalsSpin->setSpecialValueText(tr("Auto"));
    m_scaleBarLabelDecimalsSpin->setToolTip(tr(
        "Number of decimal places shown in the scale bar label.\n"
        "-1 (Auto) removes trailing zeros (e.g. \"1 km\", \"2.5 km\").\n"
        "0 rounds to a whole number (e.g. \"1 km\").\n"
        "1–4 shows that many fixed decimal places."));
    f->addRow(tr("Label decimals"), m_scaleBarLabelDecimalsSpin);

    m_scaleBarCompactNotationBox = new QCheckBox(tr("Compact notation (\"1k\" / \"500m\")"), group);
    m_scaleBarCompactNotationBox->setToolTip(tr(
        "When checked, omits the space and shortens units:\n"
        "\"1 km\" → \"1k\",  \"500 m\" → \"500m\"."));
    f->addRow(QString(), m_scaleBarCompactNotationBox);

    outer->addWidget(group);
    outer->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildMeasureToolPage()
{
    auto *page  = new QWidget(this);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(12);

    // Helper: create a color button with live background preview.
    auto makeColorBtn = [this](QGroupBox *parent) -> QPushButton * {
        auto *btn = new QPushButton(parent);
        btn->setMinimumWidth(120);
        btn->setAutoFillBackground(true);
        return btn;
    };
    auto applyColorBtn = [](QPushButton *btn, const QColor &c) {
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        btn->setStyleSheet(css);
        btn->setText(c.name(QColor::HexRgb).toUpper());
    };

    // ── Line & vertex group ───────────────────────────────────────────────
    auto *lineGroup = new QGroupBox(tr("Lines && Vertices"), page);
    auto *lf        = new QFormLayout(lineGroup);

    m_measureLineColorBtn = makeColorBtn(lineGroup);
    connect(m_measureLineColorBtn, &QPushButton::clicked, this, [this, applyColorBtn]() {
        const QColor c = QColorDialog::getColor(
            m_pendingMeasureLineColor.isValid() ? m_pendingMeasureLineColor : Qt::red,
            this, tr("Measure line color"));
        if (!c.isValid()) return;
        m_pendingMeasureLineColor = c;
        applyColorBtn(m_measureLineColorBtn, c);
    });
    lf->addRow(tr("Line && vertex color"), m_measureLineColorBtn);
    outer->addWidget(lineGroup);

    // ── Labels group ─────────────────────────────────────────────────────
    auto *labelGroup = new QGroupBox(tr("Labels"), page);
    auto *labf       = new QFormLayout(labelGroup);

    m_measureLabelFontBtn = new QPushButton(labelGroup);
    connect(m_measureLabelFontBtn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QFont f = QFontDialog::getFont(&ok, m_pendingMeasureLabelFont,
                                              this, tr("Measure label font"));
        if (!ok) return;
        m_pendingMeasureLabelFont = f;
        m_measureLabelFontBtn->setText(QStringLiteral("%1, %2pt")
            .arg(f.family()).arg(f.pointSize()));
    });
    labf->addRow(tr("Font"), m_measureLabelFontBtn);

    m_measureLabelDecimalsSpin = new QSpinBox(labelGroup);
    m_measureLabelDecimalsSpin->setRange(0, 6);
    m_measureLabelDecimalsSpin->setToolTip(tr("Number of decimal places shown in distance and area labels."));
    labf->addRow(tr("Decimal places"), m_measureLabelDecimalsSpin);
    outer->addWidget(labelGroup);

    // ── Area fill group ───────────────────────────────────────────────────
    auto *fillGroup = new QGroupBox(tr("Area Fill"), page);
    auto *ff        = new QFormLayout(fillGroup);

    m_measureFillColorBtn = makeColorBtn(fillGroup);
    connect(m_measureFillColorBtn, &QPushButton::clicked, this, [this, applyColorBtn]() {
        const QColor c = QColorDialog::getColor(
            m_pendingMeasureFillColor.isValid() ? m_pendingMeasureFillColor
                                                : QColor(100, 149, 237),
            this, tr("Area fill color"));
        if (!c.isValid()) return;
        m_pendingMeasureFillColor = c;
        applyColorBtn(m_measureFillColorBtn, c);
    });
    ff->addRow(tr("Fill color"), m_measureFillColorBtn);

    m_measureFillOpacitySpin = new QSpinBox(fillGroup);
    m_measureFillOpacitySpin->setRange(0, 100);
    m_measureFillOpacitySpin->setSuffix(tr(" %"));
    m_measureFillOpacitySpin->setToolTip(tr("Opacity of the area polygon fill (0 = transparent, 100 = opaque)."));
    ff->addRow(tr("Fill opacity"), m_measureFillOpacitySpin);
    outer->addWidget(fillGroup);

    outer->addStretch(1);
    return page;
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------

void PreferencesDialog::readFromManager()
{
    auto *p = PreferencesManager::instance();

    m_showLicenseOnStartupBox->setChecked(LicenseAgreementDialog::shouldShowOnStartup());
    m_autoLengthBox->setChecked(p->autoLengthEnabled());
    {
        QString currentEngine = p->defaultEngineMode();
        if (currentEngine.isEmpty()) currentEngine = QLatin1String(SWMM_VERSION);
        const int idx = m_defaultEngineCombo->findData(currentEngine);
        m_defaultEngineCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    m_clickTolerancePxSpin->setValue(p->clickTolerancePx());
    m_dragThresholdPxSpin->setValue(p->dragThresholdPx());
    m_clearOnMissBox->setChecked(p->clearSelectionOnMiss());

    auto applyColor = [](QPushButton *btn, QColor &held, const QColor &c) {
        held = c;
        QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        btn->setStyleSheet(css);
        btn->setText(c.name(QColor::HexRgb).toUpper());
    };
    // Selection pens + brushes reflect live through SelectionRendering-
    // Prefs's getters; refresh the model rather than re-populating widgets.
    if (m_selectionModel) m_selectionModel->refreshValues();

    const int toolIdx = m_defaultToolCombo->findData(p->defaultTool());
    m_defaultToolCombo->setCurrentIndex(toolIdx >= 0 ? toolIdx : 0);
    if (p->defaultCrsMode() == QStringLiteral("LocalAuto"))
        m_crsAutoRadio->setChecked(true);
    else
        m_crsCustomRadio->setChecked(true);
    m_crsAuthorityEdit->setText(p->defaultCrsAuthority());
    m_crsCodeSpin->setValue(p->defaultCrsCode());

    m_snapEnabledBox->setChecked(p->snapEnabled());
    m_snapToleranceSpin->setValue(p->snapTolerancePx());
    m_snapToVerticesBox->setChecked(p->snapToVertices());

    m_labelLodSpin->setValue(p->labelLodM11Min());
    // Link pens reflect live through LinkRenderingPrefs's getters, so
    // refresh the QPropertyModel rather than re-populating widgets.
    if (m_linkPenModel) m_linkPenModel->refreshValues();

    m_progressTickMsSpin->setValue(p->progressTickMs());

    // Simulation Defaults
    {
        const auto d = p->simulationDefaults();
        auto selData = [](QComboBox *c, const QString &v) {
            const int i = c->findData(v);
            c->setCurrentIndex(i >= 0 ? i : 0);
        };
        selData(m_simFlowUnitsCombo,        d.flowUnits.isEmpty() ? QStringLiteral("CFS") : d.flowUnits);
        // FlowUnits combo uses raw text, not userData — fall back to setCurrentText.
        m_simFlowUnitsCombo->setCurrentText(d.flowUnits);
        selData(m_simInfiltrationCombo,     d.infiltrationModel);
        selData(m_simFlowRoutingCombo,      d.flowRouting);

        m_simIgnoreRainfallBox   ->setChecked(!d.ignoreRainfall);
        m_simIgnoreRdiiBox       ->setChecked(!d.ignoreRdii);
        m_simIgnoreSnowmeltBox   ->setChecked(!d.ignoreSnowmelt);
        m_simIgnoreGroundwaterBox->setChecked(!d.ignoreGroundwater);
        m_simIgnoreQualityBox    ->setChecked(!d.ignoreQuality);
        m_simIgnoreRoutingBox    ->setChecked(!d.ignoreRouting);
        m_simModule2DBox         ->setChecked(d.module2DEnabled);

        m_simAllowPondingBox     ->setChecked(d.allowPonding);
        m_simSkipSteadyStateBox  ->setChecked(d.skipSteadyState);
        m_simMinSlopePctSpin     ->setValue(d.minSlopePct);

        m_simDryDaysSpin         ->setValue(d.dryDays);

        m_simReportStepSpin      ->setValue(qMax(1, d.reportStepSec / 60));
        m_simDryStepSpin         ->setValue(qMax(1, d.dryStepSec    / 60));
        m_simWetStepSpin         ->setValue(qMax(1, d.wetStepSec    / 60));
        m_simRuleStepSpin        ->setValue(d.ruleStepSec);
        m_simRoutingStepSpin     ->setValue(d.routingStepSec);

        m_simSysFlowTolSpin      ->setValue(d.sysFlowTolPct);
        m_simLatFlowTolSpin      ->setValue(d.latFlowTolPct);
        m_simMaxTrialsSpin       ->setValue(d.maxTrials);

        selData(m_simInertialDampCombo,     d.inertialDamping);
        selData(m_simNormalFlowCombo,       d.normalFlowLimited);
        selData(m_simForceMainCombo,        d.forceMainEquation);
        selData(m_simSurchargeCombo,        d.surchargeMethod);

        m_simVariableStepBox     ->setChecked(d.variableStepOn);
        m_simVariableStepFactorSpin->setValue(d.variableStepFactor);
        m_simMinRoutingStepSpin  ->setValue(d.minRoutingStepSec);
        m_simLengtheningStepSpin ->setValue(d.lengtheningStepSec);
        m_simHeadToleranceSpin   ->setValue(d.headTolerance);

        selData(m_simNodeContinuityCombo,   d.nodeContinuity);
        m_simAndersonAccelBox    ->setChecked(d.andersonAccel);
        m_simThreadsSpin         ->setValue(d.threads);
    }

    // Scale Bar
    applyColor(m_scaleBarColorBtn, m_pendingScaleBarColor, p->scaleBarPenColor());
    m_scaleBarPenWidthSpin->setValue(p->scaleBarPenWidth());
    {
        const int idx = m_scaleBarPenStyleCombo->findData(p->scaleBarPenStyle());
        m_scaleBarPenStyleCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_pendingScaleBarFont = QFont(p->scaleBarFontFamily(), p->scaleBarFontSize());
    m_scaleBarFontBtn->setText(QStringLiteral("%1, %2pt")
        .arg(m_pendingScaleBarFont.family()).arg(m_pendingScaleBarFont.pointSize()));
    {
        const int idx = m_scaleBarUnitsCombo->findData(p->scaleBarUnits());
        m_scaleBarUnitsCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    {
        const int idx = m_scaleBarPositionCombo->findData(p->scaleBarPosition());
        m_scaleBarPositionCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_scaleBarMaxBarLengthSpin->setValue(p->scaleBarMaxBarLength());
    m_scaleBarLabelDecimalsSpin->setValue(p->scaleBarLabelDecimals());
    m_scaleBarCompactNotationBox->setChecked(p->scaleBarCompactNotation());

    // Measure Tool
    applyColor(m_measureLineColorBtn, m_pendingMeasureLineColor, p->measureLineColor());
    m_pendingMeasureLabelFont = QFont(p->measureLabelFontFamily(), p->measureLabelFontSize());
    m_measureLabelFontBtn->setText(QStringLiteral("%1, %2pt")
        .arg(m_pendingMeasureLabelFont.family()).arg(m_pendingMeasureLabelFont.pointSize()));
    m_measureLabelDecimalsSpin->setValue(p->measureLabelDecimals());
    applyColor(m_measureFillColorBtn, m_pendingMeasureFillColor, p->measureFillColor());
    m_measureFillOpacitySpin->setValue(p->measureFillOpacity());

    // Naming prefixes
    m_prefixJunction    ->setText(p->elementNamePrefix(QStringLiteral("junction")));
    m_prefixOutfall     ->setText(p->elementNamePrefix(QStringLiteral("outfall")));
    m_prefixStorage     ->setText(p->elementNamePrefix(QStringLiteral("storage")));
    m_prefixDivider     ->setText(p->elementNamePrefix(QStringLiteral("divider")));
    m_prefixConduit     ->setText(p->elementNamePrefix(QStringLiteral("conduit")));
    m_prefixPump        ->setText(p->elementNamePrefix(QStringLiteral("pump")));
    m_prefixOrifice     ->setText(p->elementNamePrefix(QStringLiteral("orifice")));
    m_prefixWeir        ->setText(p->elementNamePrefix(QStringLiteral("weir")));
    m_prefixOutlet      ->setText(p->elementNamePrefix(QStringLiteral("outlet")));
    m_prefixRaingage    ->setText(p->elementNamePrefix(QStringLiteral("raingage")));
    m_prefixSubcatchment->setText(p->elementNamePrefix(QStringLiteral("subcatchment")));
}

void PreferencesDialog::writeToManager()
{
    auto *p = PreferencesManager::instance();

    LicenseAgreementDialog::setShowOnStartup(m_showLicenseOnStartupBox->isChecked());
    p->setAutoLengthEnabled(m_autoLengthBox->isChecked());
    p->setDefaultEngineMode(m_defaultEngineCombo->currentData().toString());

    p->setClickTolerancePx(m_clickTolerancePxSpin->value());
    p->setDragThresholdPx(m_dragThresholdPxSpin->value());
    p->setClearSelectionOnMiss(m_clearOnMissBox->isChecked());

    // Selection pens + brushes were written through SelectionRendering-
    // Prefs's Q_PROPERTY setters at edit time — nothing to flush here.

    p->setDefaultTool(m_defaultToolCombo->currentData().toString());
    p->setDefaultCrsMode(m_crsAutoRadio->isChecked()
                         ? QStringLiteral("LocalAuto")
                         : QStringLiteral("EPSG"));
    p->setDefaultCrsAuthority(m_crsAuthorityEdit->text().trimmed());
    p->setDefaultCrsCode(m_crsCodeSpin->value());

    p->setSnapEnabled(m_snapEnabledBox->isChecked());
    p->setSnapTolerancePx(m_snapToleranceSpin->value());
    p->setSnapToVertices(m_snapToVerticesBox->isChecked());

    p->setLabelLodM11Min(m_labelLodSpin->value());
    // Link pens already wrote through LinkRenderingPrefs's Q_PROPERTY
    // setters at edit time — nothing to flush here.

    p->setProgressTickMs(m_progressTickMsSpin->value());

    // Simulation Defaults — package the page state and persist via one setter.
    {
        PreferencesManager::SimulationDefaults d;
        d.flowUnits          = m_simFlowUnitsCombo->currentText();
        d.infiltrationModel  = m_simInfiltrationCombo->currentData().toString();
        d.flowRouting        = m_simFlowRoutingCombo->currentData().toString();

        d.ignoreRainfall     = !m_simIgnoreRainfallBox   ->isChecked();
        d.ignoreRdii         = !m_simIgnoreRdiiBox       ->isChecked();
        d.ignoreSnowmelt     = !m_simIgnoreSnowmeltBox   ->isChecked();
        d.ignoreGroundwater  = !m_simIgnoreGroundwaterBox->isChecked();
        d.ignoreQuality      = !m_simIgnoreQualityBox    ->isChecked();
        d.ignoreRouting      = !m_simIgnoreRoutingBox    ->isChecked();
        d.module2DEnabled    =  m_simModule2DBox         ->isChecked();

        d.allowPonding       =  m_simAllowPondingBox     ->isChecked();
        d.skipSteadyState    =  m_simSkipSteadyStateBox  ->isChecked();
        d.minSlopePct        =  m_simMinSlopePctSpin     ->value();

        d.dryDays            =  m_simDryDaysSpin         ->value();

        d.reportStepSec      =  m_simReportStepSpin      ->value() * 60;
        d.dryStepSec         =  m_simDryStepSpin         ->value() * 60;
        d.wetStepSec         =  m_simWetStepSpin         ->value() * 60;
        d.ruleStepSec        =  m_simRuleStepSpin        ->value();
        d.routingStepSec     =  m_simRoutingStepSpin     ->value();

        d.maxTrials          =  m_simMaxTrialsSpin       ->value();
        d.headTolerance      =  m_simHeadToleranceSpin   ->value();
        d.sysFlowTolPct      =  m_simSysFlowTolSpin      ->value();
        d.latFlowTolPct      =  m_simLatFlowTolSpin      ->value();

        d.inertialDamping    =  m_simInertialDampCombo   ->currentData().toString();
        d.normalFlowLimited  =  m_simNormalFlowCombo     ->currentData().toString();
        d.forceMainEquation  =  m_simForceMainCombo      ->currentData().toString();
        d.surchargeMethod    =  m_simSurchargeCombo      ->currentData().toString();
        d.variableStepOn     =  m_simVariableStepBox     ->isChecked();
        d.variableStepFactor =  m_simVariableStepFactorSpin->value();
        d.minRoutingStepSec  =  m_simMinRoutingStepSpin  ->value();
        d.lengtheningStepSec =  m_simLengtheningStepSpin ->value();

        d.nodeContinuity     =  m_simNodeContinuityCombo ->currentData().toString();
        d.andersonAccel      =  m_simAndersonAccelBox    ->isChecked();
        d.threads            =  m_simThreadsSpin         ->value();

        p->setSimulationDefaults(d);
    }

    // Scale Bar
    if (m_pendingScaleBarColor.isValid())
        p->setScaleBarPenColor(m_pendingScaleBarColor);
    p->setScaleBarPenWidth(m_scaleBarPenWidthSpin->value());
    p->setScaleBarPenStyle(m_scaleBarPenStyleCombo->currentData().toInt());
    p->setScaleBarFontFamily(m_pendingScaleBarFont.family());
    p->setScaleBarFontSize(m_pendingScaleBarFont.pointSize());
    p->setScaleBarUnits(m_scaleBarUnitsCombo->currentData().toInt());
    p->setScaleBarPosition(m_scaleBarPositionCombo->currentData().toInt());
    p->setScaleBarMaxBarLength(m_scaleBarMaxBarLengthSpin->value());
    p->setScaleBarLabelDecimals(m_scaleBarLabelDecimalsSpin->value());
    p->setScaleBarCompactNotation(m_scaleBarCompactNotationBox->isChecked());

    // Measure Tool
    if (m_pendingMeasureLineColor.isValid())
        p->setMeasureLineColor(m_pendingMeasureLineColor);
    p->setMeasureLabelFontFamily(m_pendingMeasureLabelFont.family());
    p->setMeasureLabelFontSize(m_pendingMeasureLabelFont.pointSize());
    p->setMeasureLabelDecimals(m_measureLabelDecimalsSpin->value());
    if (m_pendingMeasureFillColor.isValid())
        p->setMeasureFillColor(m_pendingMeasureFillColor);
    p->setMeasureFillOpacity(m_measureFillOpacitySpin->value());

    // Naming prefixes
    auto savePrefix = [&](QLineEdit *ed, const QString &kind) {
        const QString t = ed->text().trimmed();
        if (!t.isEmpty()) p->setElementNamePrefix(kind, t);
    };
    savePrefix(m_prefixJunction,     QStringLiteral("junction"));
    savePrefix(m_prefixOutfall,      QStringLiteral("outfall"));
    savePrefix(m_prefixStorage,      QStringLiteral("storage"));
    savePrefix(m_prefixDivider,      QStringLiteral("divider"));
    savePrefix(m_prefixConduit,      QStringLiteral("conduit"));
    savePrefix(m_prefixPump,         QStringLiteral("pump"));
    savePrefix(m_prefixOrifice,      QStringLiteral("orifice"));
    savePrefix(m_prefixWeir,         QStringLiteral("weir"));
    savePrefix(m_prefixOutlet,       QStringLiteral("outlet"));
    savePrefix(m_prefixRaingage,     QStringLiteral("raingage"));
    savePrefix(m_prefixSubcatchment, QStringLiteral("subcatchment"));
}

void PreferencesDialog::onApply()
{
    writeToManager();
}

void PreferencesDialog::onAccept()
{
    writeToManager();
    accept();
}

void PreferencesDialog::onResetToDefaults()
{
    // Defaults mirror the `k…` constants in preferencesmanager.cpp — not
    // shared via a header because the dialog also wants to show them as
    // live values after a reset-click. Keep in sync if you change the
    // compiled-in defaults on the manager side.
    m_showLicenseOnStartupBox->setChecked(true);
    m_autoLengthBox->setChecked(true);
    {
        const int idx = m_defaultEngineCombo->findData(QLatin1String(SWMM_VERSION));
        m_defaultEngineCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    m_clickTolerancePxSpin->setValue(16);
    m_dragThresholdPxSpin->setValue(8);
    m_clearOnMissBox->setChecked(true);

    // Selection pens + brushes — wipe per-class keys so the next read
    // falls back to defaultSelectionPen / defaultSelectionBrush in
    // preferencesmanager.cpp; refresh the property tree.
    {
        auto *p = PreferencesManager::instance();
        const QStringList classes = { QStringLiteral("link"),
                                      QStringLiteral("subcatchment"),
                                      QStringLiteral("node"),
                                      QStringLiteral("gage") };
        for (const QString &c : classes) p->resetSelectionStyleToDefault(c);
        if (m_selectionModel) m_selectionModel->refreshValues();
    }

    const int selIdx = m_defaultToolCombo->findData(QStringLiteral("Select"));
    m_defaultToolCombo->setCurrentIndex(selIdx >= 0 ? selIdx : 0);
    m_crsAutoRadio->setChecked(true);
    m_crsAuthorityEdit->setText(QStringLiteral("EPSG"));
    m_crsCodeSpin->setValue(4326);

    m_snapEnabledBox->setChecked(true);
    m_snapToleranceSpin->setValue(12);
    m_snapToVerticesBox->setChecked(true);

    m_labelLodSpin->setValue(0.5);

    // Link pens — re-seed each type with its compile-time default
    // (see kConduitPenDefault / kPumpPenDefault / … in
    // preferencesmanager.cpp). resetLinkPenToDefault clears the
    // QSettings key so linkPen() falls back to the default, and emits
    // preferenceChanged so the project window's LinkPen/ listener
    // refreshes the layer mirror.
    {
        auto *p = PreferencesManager::instance();
        const QStringList keys = { QStringLiteral("conduit"),
                                   QStringLiteral("pump"),
                                   QStringLiteral("orifice"),
                                   QStringLiteral("weir"),
                                   QStringLiteral("outlet") };
        for (const QString &k : keys) p->resetLinkPenToDefault(k);
        if (m_linkPenModel) m_linkPenModel->refreshValues();
    }

    auto resetColorButton = [](QPushButton *btn, QColor &held, const QColor &c) {
        held = c;
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: %2; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(c.name(QColor::HexRgb),
                 c.lightness() > 128 ? QStringLiteral("black")
                                     : QStringLiteral("white"));
        btn->setStyleSheet(css);
        btn->setText(c.name(QColor::HexRgb).toUpper());
    };

    m_progressTickMsSpin->setValue(1000);

    // Simulation Defaults — restore the struct's compile-time seeds and
    // max the THREADS knob to the machine's logical-processor count
    // (per the user request: "Number of threads should be maxed to the
    // total number of logical processors").
    {
        const PreferencesManager::SimulationDefaults d;  // compile-time defaults
        auto sel = [](QComboBox *c, const QString &v) {
            const int i = c->findData(v);
            c->setCurrentIndex(i >= 0 ? i : 0);
        };
        m_simFlowUnitsCombo->setCurrentText(d.flowUnits);
        sel(m_simInfiltrationCombo, d.infiltrationModel);
        sel(m_simFlowRoutingCombo,  d.flowRouting);

        m_simIgnoreRainfallBox   ->setChecked(!d.ignoreRainfall);
        m_simIgnoreRdiiBox       ->setChecked(!d.ignoreRdii);
        m_simIgnoreSnowmeltBox   ->setChecked(!d.ignoreSnowmelt);
        m_simIgnoreGroundwaterBox->setChecked(!d.ignoreGroundwater);
        m_simIgnoreQualityBox    ->setChecked(!d.ignoreQuality);
        m_simIgnoreRoutingBox    ->setChecked(!d.ignoreRouting);
        m_simModule2DBox         ->setChecked(d.module2DEnabled);
        m_simAllowPondingBox     ->setChecked(d.allowPonding);
        m_simSkipSteadyStateBox  ->setChecked(d.skipSteadyState);
        m_simMinSlopePctSpin     ->setValue(d.minSlopePct);

        m_simDryDaysSpin         ->setValue(d.dryDays);

        m_simReportStepSpin      ->setValue(d.reportStepSec / 60);
        m_simDryStepSpin         ->setValue(d.dryStepSec    / 60);
        m_simWetStepSpin         ->setValue(d.wetStepSec    / 60);
        m_simRuleStepSpin        ->setValue(d.ruleStepSec);
        m_simRoutingStepSpin     ->setValue(d.routingStepSec);

        m_simSysFlowTolSpin      ->setValue(d.sysFlowTolPct);
        m_simLatFlowTolSpin      ->setValue(d.latFlowTolPct);
        m_simMaxTrialsSpin       ->setValue(d.maxTrials);

        sel(m_simInertialDampCombo, d.inertialDamping);
        sel(m_simNormalFlowCombo,   d.normalFlowLimited);
        sel(m_simForceMainCombo,    d.forceMainEquation);
        sel(m_simSurchargeCombo,    d.surchargeMethod);

        m_simVariableStepBox     ->setChecked(d.variableStepOn);
        m_simVariableStepFactorSpin->setValue(d.variableStepFactor);
        m_simMinRoutingStepSpin  ->setValue(d.minRoutingStepSec);
        m_simLengtheningStepSpin ->setValue(d.lengtheningStepSec);
        m_simHeadToleranceSpin   ->setValue(d.headTolerance);

        sel(m_simNodeContinuityCombo, d.nodeContinuity);
        m_simAndersonAccelBox    ->setChecked(d.andersonAccel);

        const int hwThreads = QThread::idealThreadCount();
        m_simThreadsSpin         ->setValue(hwThreads > 0 ? hwThreads : 0);
    }

    // Scale bar defaults
    {
        const QColor black = Qt::black;
        const QString css = QStringLiteral(
            "QPushButton { background-color: %1; color: white; "
            "border: 1px solid #777; padding: 3px 8px; }")
            .arg(black.name(QColor::HexRgb));
        m_scaleBarColorBtn->setStyleSheet(css);
        m_scaleBarColorBtn->setText(black.name(QColor::HexRgb).toUpper());
        m_pendingScaleBarColor = black;
    }
    m_scaleBarPenWidthSpin->setValue(2);
    m_scaleBarPenStyleCombo->setCurrentIndex(0);    // Solid
    m_pendingScaleBarFont = QFont(QStringLiteral("sans-serif"), 8);
    m_scaleBarFontBtn->setText(QStringLiteral("sans-serif, 8pt"));
    m_scaleBarUnitsCombo->setCurrentIndex(0);       // Auto
    m_scaleBarPositionCombo->setCurrentIndex(0);    // Bottom Left
    m_scaleBarMaxBarLengthSpin->setValue(100);
    m_scaleBarLabelDecimalsSpin->setValue(-1);
    m_scaleBarCompactNotationBox->setChecked(false);

    // Measure Tool defaults
    resetColorButton(m_measureLineColorBtn,  m_pendingMeasureLineColor,  Qt::red);
    resetColorButton(m_measureFillColorBtn,  m_pendingMeasureFillColor,  QColor(100, 149, 237));
    m_pendingMeasureLabelFont = QFont(QStringLiteral("sans-serif"), 8);
    m_measureLabelFontBtn->setText(QStringLiteral("sans-serif, 8pt"));
    m_measureLabelDecimalsSpin->setValue(2);
    m_measureFillOpacitySpin->setValue(30);

    // Naming prefix defaults
    m_prefixJunction    ->setText(QStringLiteral("J"));
    m_prefixOutfall     ->setText(QStringLiteral("O"));
    m_prefixStorage     ->setText(QStringLiteral("S"));
    m_prefixDivider     ->setText(QStringLiteral("D"));
    m_prefixConduit     ->setText(QStringLiteral("C"));
    m_prefixPump        ->setText(QStringLiteral("Pu"));
    m_prefixOrifice     ->setText(QStringLiteral("Or"));
    m_prefixWeir        ->setText(QStringLiteral("W"));
    m_prefixOutlet      ->setText(QStringLiteral("Ou"));
    m_prefixRaingage    ->setText(QStringLiteral("RG"));
    m_prefixSubcatchment->setText(QStringLiteral("Sub"));
}

// ---------------------------------------------------------------------------
// Naming page
// ---------------------------------------------------------------------------

QWidget *PreferencesDialog::buildNamingPage()
{
    auto *page = new QWidget(this);
    auto *f    = new QFormLayout(page);
    f->setRowWrapPolicy(QFormLayout::WrapLongRows);

    auto mkEdit = [&](const QString &defaultText) -> QLineEdit * {
        auto *ed = new QLineEdit(page);
        ed->setPlaceholderText(defaultText);
        ed->setMaximumWidth(120);
        return ed;
    };

    f->addRow(new QLabel(tr(
        "<b>Element name prefixes</b><br>"
        "Auto-generated names use these prefixes followed by a sequential "
        "number (e.g. J1, J2, …). Changing the prefix here takes effect "
        "immediately for the next placed element."
        ), page));

    m_prefixJunction     = mkEdit(QStringLiteral("J"));
    m_prefixOutfall      = mkEdit(QStringLiteral("O"));
    m_prefixStorage      = mkEdit(QStringLiteral("S"));
    m_prefixDivider      = mkEdit(QStringLiteral("D"));
    m_prefixConduit      = mkEdit(QStringLiteral("C"));
    m_prefixPump         = mkEdit(QStringLiteral("Pu"));
    m_prefixOrifice      = mkEdit(QStringLiteral("Or"));
    m_prefixWeir         = mkEdit(QStringLiteral("W"));
    m_prefixOutlet       = mkEdit(QStringLiteral("Ou"));
    m_prefixRaingage     = mkEdit(QStringLiteral("RG"));
    m_prefixSubcatchment = mkEdit(QStringLiteral("Sub"));

    f->addRow(tr("Junction:"),      m_prefixJunction);
    f->addRow(tr("Outfall:"),       m_prefixOutfall);
    f->addRow(tr("Storage:"),       m_prefixStorage);
    f->addRow(tr("Divider:"),       m_prefixDivider);
    f->addRow(tr("Conduit:"),       m_prefixConduit);
    f->addRow(tr("Pump:"),          m_prefixPump);
    f->addRow(tr("Orifice:"),       m_prefixOrifice);
    f->addRow(tr("Weir:"),          m_prefixWeir);
    f->addRow(tr("Outlet:"),        m_prefixOutlet);
    f->addRow(tr("Rain Gage:"),     m_prefixRaingage);
    f->addRow(tr("Subcatchment:"),  m_prefixSubcatchment);

    return page;
}
