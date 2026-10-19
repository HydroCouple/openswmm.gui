/*!
 * \file   simulationoptionsdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simulationoptionsdialog.h"

#include "ui/uiscrollhelpers.h"
#include "ui/dialogs/climatologydialog.h"
#include "ui/dialogs/heatconfigdialog.h"
#include "ui/dialogs/heatrasterassigndialog.h"
#include "ui/configurationsession.h"
#include "ui/heatconfigurationundo.h"
#include "map/mapcanvas.h"
#ifdef OPENSWMM_HAS_2D
#include "ui/dialogs/meshsnowdialog.h"
#include "ui/dialogs/groundwaterassigndialog.h"
#include "ui/dialogs/mesh2dgroundwaterdialog.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#endif
#include <QFileInfo>
#include <QInputDialog>
#include "layers/swmmmodellayer.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/simoptions/simoptionscontext.h"
#include "ui/dialogs/simoptions/simoptionspage.h"
#include "ui/dialogs/simoptions/datespage.h"
#include "ui/dialogs/simoptions/filespage.h"
#include "ui/dialogs/simoptions/hydraulicspage.h"
#include "ui/dialogs/simoptions/meshpage.h"
#include "ui/dialogs/simoptions/modelspage.h"
#include "ui/dialogs/simoptions/performancepage.h"
#include "ui/dialogs/simoptions/qualitypage.h"
#include "ui/dialogs/simoptions/spatialpage.h"
#include "ui/dialogs/simoptions/titlenotespage.h"
#include "ui/dialogs/simoptions/twodpage.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Pure-helpers (parseEngineBool / engineBoolString / format/parse DateTime)
// live in simulationoptionshelpers.cpp so leaf QtTests can compile them
// without dragging in GDAL/OGR via the spatial-tab CRS code below.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SimulationOptionsDialog::SimulationOptionsDialog(SWMM_Engine engine,
                                                 SWMMModelLayer *layer,
                                                 const QString &engineVersion,
                                                 SWMMVisProjectWindow *projectWindow,
                                                 QWidget *parent)
    : QDialog(parent),
      m_engine(engine),
      m_layer(layer),
      m_projectWindow(projectWindow),
      m_engineVersion(engineVersion)
{
    setWindowTitle(tr("Simulation Options"));
    resize(620, 600);
    // Probe once, then hand every page the same context. Both must exist
    // before buildUi(), which constructs the page classes.
    m_caps = openswmmvis::ui::probeEngineCapabilities(m_engine, m_engineVersion);
    m_ctx  = std::make_unique<openswmmvis::ui::SimOptionsContext>(
        m_engine, m_layer, m_projectWindow, m_caps);
    buildUi();
    readFromEngine();
    applyEngineConstraints();
    if (m_layer) {
        connect(this, &SimulationOptionsDialog::changesApplied, m_layer, &SWMMModelLayer::markEdited);
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, this, &SimulationOptionsDialog::invalidateContext);
        connect(m_layer, &QObject::destroyed, this, &SimulationOptionsDialog::invalidateContext);
    }
    if (m_projectWindow) {
        connect(this, &SimulationOptionsDialog::changesApplied, m_projectWindow,
                [project = m_projectWindow] { project->setHasChanges(true); });
        connect(m_projectWindow, &SWMMVisProjectWindow::aboutToClose, this, &SimulationOptionsDialog::invalidateContext);
    }
    if (m_projectWindow && m_projectWindow->canvas())
        setUndoStack(m_projectWindow->canvas()->undoStack());
    // Cancel reverts nested edits accepted since the last Apply.
    connect(this, &QDialog::rejected, this, [this] {
        if (m_session && m_session->cancel()) m_wroteChanges = m_wroteAtCommit;
    });
}

void SimulationOptionsDialog::setUndoStack(QUndoStack *stack)
{
    if (m_session) m_session->cancel();
    m_session.reset();
    if (m_layer && stack)
        m_session = std::make_shared<OpenSWMMVis::ConfigurationSession>(m_layer, stack,
            tr("Edit simulation options"), QFileInfo(m_layer->modelFilePath()).absolutePath());
}

void SimulationOptionsDialog::openNestedSegment()
{
    if (m_session) m_session->openSegment();
}

bool SimulationOptionsDialog::runNestedClimatology(int tab)
{
    if (!m_engine) return false;
    openNestedSegment();
    const QString anchor = m_layer ? QFileInfo(m_layer->modelFilePath()).absolutePath() : QString();
    OpenSWMMVis::ConfigSnapshot before(swmm_config_snapshot_create(m_engine));
    const QString beforeIdentity = OpenSWMMVis::configurationIdentity(m_engine, anchor, {});
    ClimatologyDialog dialog(m_engine, m_layer, this);
    if (m_layer) {
        dialog.setProjectAnchor(anchor);
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, &dialog, &ClimatologyDialog::invalidateEngine);
        connect(m_layer, &QObject::destroyed, &dialog, &ClimatologyDialog::invalidateEngine);
    }
    dialog.setCurrentTab(tab);
    dialog.exec();
    if (!m_engine || !dialog.wroteAnyChanges()) return false;
    m_wroteChanges = true;
    if (m_layer) m_layer->markEdited();
    OpenSWMMVis::pushConfigurationUndo(m_session ? m_session->stack() : nullptr, m_layer,
            tr("Edit climatology"), std::move(before), beforeIdentity, anchor, {});
    return true;
}

// Destructor is now inline in the header to keep the moc vtable self-contained.

/*!
 * Disables controls the currently-selected engine does not support.
 *
 * Every block now lives in the owning page's applyCapabilities(), which
 * addPage() runs as each page is registered — Routing & Hydraulics takes the
 * FV routing item, TPA, the FV pressure closure, unsteady friction,
 * DYNAMIC_SLOT, SEMI_IMPLICIT and Anderson; Quality & Transport takes the
 * solver surface; Files takes signed heads and the legacy writers / [PLUGINS]
 * disable. What is left here is the re-callable entry point for a mid-session
 * engine-version change: re-probe, then let every page re-apply.
 *
 * The 2D module checkbox, Mesh page and 2D Surface Routing page stay editable
 * even on legacy SWMM 5: mesh preparation is a pure GUI concern and the engine
 * simply ignores 2D inputs at run time.
 */
void SimulationOptionsDialog::applyEngineConstraints()
{
    for (openswmmvis::ui::SimOptionsPage *p : m_pageOrder)
        p->applyCapabilities();
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Settings-style navigation — a category list on the left switching a
    // stacked page on the right (mirrors PreferencesDialog), replacing the
    // former wide tab bar. Each build*Tab() returns its page; addCategory
    // wraps it in a scroll area (so tall pages don't force the dialog tall)
    // and registers the sidebar row.
    auto *split = new QHBoxLayout();
    split->setSpacing(8);
    root->addLayout(split, 1);

    m_categoryList = new QListWidget(this);
    // Named so the restructure's gate tests can reach the sidebar by
    // objectName instead of a friend declaration. A QListWidget is not one of
    // the types dialoglayoutpersistence saves, so this does not enrol it.
    m_categoryList->setObjectName(QStringLiteral("categories"));
    m_categoryList->setMinimumWidth(190);
    split->addWidget(m_categoryList);

    m_pages = new QStackedWidget(this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence;
    // restoring the page needs the category highlight kept in sync
    // (same-row set is a no-op, so no signal loop).
    setObjectName(QStringLiteral("SimulationOptionsDialog"));
    m_pages->setObjectName(QStringLiteral("pages"));
    connect(m_pages, &QStackedWidget::currentChanged,
            m_categoryList, qOverload<int>(&QListWidget::setCurrentRow));
    split->addWidget(m_pages, 1);

    connect(m_categoryList, &QListWidget::currentRowChanged,
            m_pages, &QStackedWidget::setCurrentIndex);

    addPage(new openswmmvis::ui::TitleNotesPage(*m_ctx, this));
    m_modelsPage = new openswmmvis::ui::ModelsPage(*m_ctx, this);
    addPage(m_modelsPage);
    connect(m_modelsPage, &openswmmvis::ui::ModelsPage::showSnowSettingsRequested,
            this, [this] {
        if (runNestedClimatology(ClimatologyDialog::TabSnowMelt))
            emit changesApplied();
        // Refresh only the mirror; preserve all unapplied options drafts.
        m_modelsPage->refreshSnowSummary();
    });
    m_datesPage = new openswmmvis::ui::DatesPage(*m_ctx, this);
    addPage(m_datesPage);
    m_hydraulicsPage = new openswmmvis::ui::HydraulicsPage(*m_ctx, this);
    addPage(m_hydraulicsPage);
    m_hydraulicsRow = m_categoryList->count() - 1;
    // The mirror on Models is a label, not a second editor (PLAN §2.1): the
    // link just selects the row that owns the combo.
    connect(m_modelsPage,
            &openswmmvis::ui::ModelsPage::showFlowRoutingPageRequested,
            this, [this] {
                if (m_categoryList && m_hydraulicsRow >= 0)
                    m_categoryList->setCurrentRow(m_hydraulicsRow);
            });
    m_qualityPage = new openswmmvis::ui::QualityPage(*m_ctx, this);
    connect(m_qualityPage,&openswmmvis::ui::QualityPage::showHeatSettingsRequested,this,[this] {
        if(!m_engine)return;
        openNestedSegment();
        OpenSWMMVis::HeatSnapshot before(swmm_heat_snapshot_create(m_engine));
        const auto beforeIdentity=OpenSWMMVis::HeatConfigDialog::configurationSignature(m_engine);
        OpenSWMMVis::HeatConfigDialog dialog(m_engine,this);
        dialog.setTransportRequested(m_qualityPage->heatEnabled());
        bool assignRaster=false;connect(&dialog,&OpenSWMMVis::HeatConfigDialog::rasterAssignmentRequested,&dialog,[&]{assignRaster=true;});
        bool editRadiation=false;connect(&dialog,&OpenSWMMVis::HeatConfigDialog::radiationEditorRequested,&dialog,[&]{editRadiation=true;});
        bool editGroundwaterSettings=false;connect(&dialog,&OpenSWMMVis::HeatConfigDialog::groundwaterSettingsRequested,&dialog,[&]{editGroundwaterSettings=true;});
#ifdef OPENSWMM_HAS_2D
        bool openTwoDSettings=false;
        dialog.setTwoDTransportNavigationEnabled(m_twoDPage && m_2DRow >= 0);
        connect(&dialog,&OpenSWMMVis::HeatConfigDialog::twoDSettingsRequested,&dialog,[&]{openTwoDSettings=true;});
#endif
        if(m_layer){connect(m_layer,&SWMMModelLayer::engineAboutToClose,&dialog,&OpenSWMMVis::HeatConfigDialog::invalidateEngine);connect(m_layer,&QObject::destroyed,&dialog,&OpenSWMMVis::HeatConfigDialog::invalidateEngine);}
        dialog.exec();
        if(!m_engine)return;
        if(dialog.wroteAnyChanges()){
        m_wroteChanges=true;if(m_layer)m_layer->markEdited();
        int enabled=0;swmm_heat_get_enabled(m_engine,&enabled);m_qualityPage->setHeatEnabled(enabled);
        if(before&&m_session&&m_session->stack()) {
            OpenSWMMVis::HeatSnapshot after(swmm_heat_snapshot_create(m_engine));
            if(after){QPointer<SWMMModelLayer> model(m_layer);
                m_session->stack()->push(new OpenSWMMVis::HeatConfigurationUndo(model,m_engine,
                    std::move(before),std::move(after),beforeIdentity,OpenSWMMVis::HeatConfigDialog::configurationSignature(m_engine),
                    [model]{return model?model->engine():nullptr;},OpenSWMMVis::HeatConfigDialog::configurationSignature,
                    [model]{if(model){model->markEdited();emit model->optionsChanged({"HEAT_TRANSPORT","HEAT_FLUXES","HEAT_SOURCES"});}},[](const QString& error){QMessageBox::warning(nullptr,QObject::tr("Heat configuration"),error);}));}
        }
        }
        if(assignRaster&&m_layer&&m_projectWindow){OpenSWMMVis::HeatRasterAssignDialog assignment(m_layer,m_projectWindow->canvas(),this);assignment.exec();}
        if(editRadiation&&runNestedClimatology(ClimatologyDialog::TabRadiation))m_modelsPage->refreshSnowSummary();
#ifdef OPENSWMM_HAS_2D
        if(editGroundwaterSettings&&m_engine&&m_layer) {
            const QString anchor=QFileInfo(m_layer->modelFilePath()).absolutePath();
            const QStringList optionKeys=openswmmvis::ui::Mesh2DGroundwaterDialog::configurationOptionKeys();
            OpenSWMMVis::ConfigSnapshot before(swmm_config_snapshot_create(m_engine));
            const QString beforeIdentity=OpenSWMMVis::configurationIdentity(m_engine,anchor,optionKeys);
            openswmmvis::ui::Mesh2DGroundwaterDialog groundwater(m_engine,this,
                openswmmvis::ui::Mesh2DGroundwaterDialog::Page::Options,
                m_projectWindow?m_projectWindow->unitSystem():nullptr);
            groundwater.setThermalOptionsOnly(true);
            bool applied=false;
            connect(&groundwater,&openswmmvis::ui::Mesh2DGroundwaterDialog::changesMayHaveBeenApplied,
                    this,[this]{m_wroteChanges=true;if(m_layer)m_layer->markEdited();if(m_projectWindow)m_projectWindow->setHasChanges(true);});
            connect(&groundwater,&openswmmvis::ui::Mesh2DGroundwaterDialog::applied,
                    &groundwater,[&applied]{applied=true;});
            if(m_layer){connect(m_layer,&SWMMModelLayer::engineAboutToClose,&groundwater,
                    &openswmmvis::ui::Mesh2DGroundwaterDialog::invalidateContext);
                connect(m_layer,&QObject::destroyed,&groundwater,
                    &openswmmvis::ui::Mesh2DGroundwaterDialog::invalidateContext);}
            if(m_projectWindow)connect(m_projectWindow,&SWMMVisProjectWindow::aboutToClose,&groundwater,
                    &openswmmvis::ui::Mesh2DGroundwaterDialog::invalidateContext);
            groundwater.exec();
            if(applied&&m_engine&&m_layer) {
                m_wroteChanges=true;m_layer->markEdited();
                OpenSWMMVis::pushConfigurationUndo(m_session?m_session->stack():nullptr,m_layer,
                    tr("Edit 2D groundwater transport and thermal settings"),std::move(before),
                    beforeIdentity,anchor,optionKeys);
                emit m_layer->optionsChanged(optionKeys);
            }
        }
        if(openTwoDSettings&&m_categoryList&&m_2DRow>=0)
            m_categoryList->setCurrentRow(m_2DRow);
#endif

        refreshGates();
    });
    addPage(m_qualityPage);
    m_qualityRow = m_categoryList->count() - 1;
    {
        auto *perf = new openswmmvis::ui::PerformancePage(*m_ctx, this);
        // T4: the preset now goes through the page, never through a widget
        // it does not own (PLAN §4.1).
        perf->setMinimumStepSetter([this](double v) {
            if (m_hydraulicsPage) m_hydraulicsPage->setMinimumStep(v);
        });
        addPage(perf);
    }
    m_spatialPage = new openswmmvis::ui::SpatialPage(*m_ctx, this);
    // The CRS pick writes straight through, so the page reports the dirty
    // edit itself rather than going through the write pass.
    connect(m_spatialPage, &openswmmvis::ui::SpatialPage::crsChanged,
            this, [this] { m_wroteChanges = true; });
    addPage(m_spatialPage);

    // Mesh configurations — file-management UI, lives outside any
    // OPENSWMM_HAS_2D guard because picking a *.2dm reference is a pure
    // GUI concern (the engine 2D solver isn't required to organise mesh
    // candidates). Always editable: creating/selecting a mesh is what
    // turns on the 2D module, not the other way around.
    {
        auto *mesh = new openswmmvis::ui::MeshPage(*m_ctx, this);
        mesh->set2DModuleSetter([this](bool on) {
            if (m_modelsPage) m_modelsPage->setModule2DEnabled(on);
        });
        addPage(mesh);
    }
    m_meshRow = m_categoryList->count() - 1;

#ifdef OPENSWMM_HAS_2D
    m_twoDPage = new openswmmvis::ui::TwoDPage(*m_ctx, this);
    connect(m_twoDPage, &openswmmvis::ui::TwoDPage::showSnowAssignmentsRequested,
            this, [this] {
        if (!m_projectWindow || !m_layer || !m_engine) return;
        auto *canvas = m_projectWindow->canvas();
        QList<SWMM2DMeshLayer *> meshes;
        QStringList names;
        for (auto *layer : canvas->layers()) {
            if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(layer)) {
                meshes.append(mesh);
                names.append(tr("%1 — %2").arg(meshes.size()).arg(mesh->sourcePath()));
            }
        }
        if (meshes.isEmpty()) {
            QMessageBox::information(this, tr("Mesh snow"), tr("Add a mesh to the project before assigning snow."));
            return;
        }
        int index = 0;
        if (meshes.size() > 1) {
            bool ok = false;
            const auto name = QInputDialog::getItem(this, tr("Mesh snow"), tr("Mesh:"), names, 0, false, &ok);
            if (!ok) return;
            index = names.indexOf(name);
        }
        openNestedSegment();
        openswmmvis::ui::MeshSnowDialog dialog(m_layer, meshes[index], canvas,
                                             m_projectWindow->selectionManager(), this);
        dialog.exec();
        m_modelsPage->refreshSnowSummary();
    });
    connect(m_twoDPage,&openswmmvis::ui::TwoDPage::showEvaporationSettingsRequested,this,[this] {
        if(runNestedClimatology(ClimatologyDialog::TabEvaporation))m_twoDPage->refreshGates();
    });
    connect(m_twoDPage,&openswmmvis::ui::TwoDPage::showGroundwaterAssignmentsRequested,this,[this] {
        if(!m_projectWindow||!m_layer||!m_engine)return;
        auto* canvas=m_projectWindow->canvas();QList<SWMM2DMeshLayer*> meshes;QStringList names;
        for(auto* layer:canvas->layers())if(auto* mesh=qobject_cast<SWMM2DMeshLayer*>(layer)) {
            meshes.append(mesh);names.append(tr("%1 — %2").arg(meshes.size()).arg(mesh->sourcePath()));
        }
        if(meshes.isEmpty()){QMessageBox::information(this,tr("Groundwater"),tr("Add a mesh before assigning groundwater."));return;}
        int index=0;
        if(meshes.size()>1){bool ok=false;const auto name=QInputDialog::getItem(this,tr("Groundwater"),tr("Mesh:"),names,0,false,&ok);if(!ok)return;index=names.indexOf(name);}
        openNestedSegment();
        openswmmvis::ui::GroundwaterAssignDialog dialog(m_layer,meshes[index],canvas,m_projectWindow->selectionManager(),m_projectWindow->unitSystem(),this);
        connect(&dialog,&openswmmvis::ui::GroundwaterAssignDialog::applied,this,[this]{m_wroteChanges=true;m_twoDPage->refreshGates();m_modelsPage->refreshTransportMatrix();});
        dialog.exec();
    });
    // Two of the 2D page's steps mirror the project schedule, and its
    // output-size estimate multiplies by the run length — read through the
    // owning page's accessors, never its widgets (PLAN §4.1).
    m_twoDPage->setScheduleHooks({
        [this] { return m_datesPage->wetStepSeconds(); },
        [this] { return m_datesPage->reportStepSeconds(); },
        [this] { return m_datesPage->durationSeconds(); },
    });
    connect(m_datesPage, &openswmmvis::ui::DatesPage::scheduleChanged,
            m_twoDPage, &openswmmvis::ui::TwoDPage::scheduleChanged);
    // The Models page's transport matrix and the 2D page's TRANSPORT_* boxes
    // are two views of one model (CLAUDE.md §5.1).
    m_modelsPage->setTransport2DHooks(
        [this](int c) { return m_twoDPage->transport2DEnabled(c); },
        [this](int c, bool on) { m_twoDPage->setTransport2DEnabled(c, on); });
    connect(m_twoDPage, &openswmmvis::ui::TwoDPage::transportSelectionChanged,
            m_modelsPage, &openswmmvis::ui::ModelsPage::refreshTransportMatrix);
    m_modelsPage->setDraftHook([this] {
        SWMM_TransportDraft draft{};
        draft.heat=m_qualityPage->heatEnabled(); draft.age=m_qualityPage->ageEnabled();
        for(int c=0;c<4;++c)draft.surface_class[c]=m_twoDPage->transport2DEnabled(c);
        draft.groundwater=m_twoDPage->groundwaterRequested();
        return draft;
    });
    connect(m_qualityPage,&openswmmvis::ui::SimOptionsPage::gateInputsChanged,
        m_modelsPage,&openswmmvis::ui::ModelsPage::refreshTransportMatrix);
    // Its Initial Quality editor writes straight to the engine, so the page
    // reports the dirty edit itself rather than through the write pass.
    connect(m_twoDPage, &openswmmvis::ui::TwoDPage::engineEditedDirectly,
            this, [this] { m_wroteChanges = true; });
    addPage(m_twoDPage);
    m_2DRow = m_categoryList->count() - 1;  // sidebar row for the 2D page.
#endif

    // Slice AA-3.5 — Files / Plugins page (its own inner sub-tabs). Lives at
    // the end so existing ordering is preserved.
    m_filesPage = new openswmmvis::ui::FilesPage(*m_ctx, this);
    addPage(m_filesPage);

    m_categoryList->setCurrentRow(0);

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    root->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, this, &SimulationOptionsDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(bb->button(QDialogButtonBox::Apply), &QPushButton::clicked,
            this, &SimulationOptionsDialog::onApply);

    // Every gate target exists by now, so the table is built once and
    // evaluated. Pages emit gateInputsChanged() when a control that feeds a
    // gate moves; the dialog re-evaluates the whole table rather than each
    // page having to know which gates it affects.
    buildGateTable();
    for (openswmmvis::ui::SimOptionsPage *p : m_pageOrder)
        connect(p, &openswmmvis::ui::SimOptionsPage::gateInputsChanged,
                this, &SimulationOptionsDialog::refreshGates);
    refreshGates();
}

// ---------------------------------------------------------------------------
// Gating — one mechanism (PLAN §4.3)
// ---------------------------------------------------------------------------

namespace {

/*! Grey a sidebar row without hiding it, and move off it if it was current. */
void applyRowGate(QListWidget *list, int row, bool on, const QString &reason)
{
    if (!list || row < 0) return;
    QListWidgetItem *item = list->item(row);
    if (!item) return;
    // A QStackedWidget has no per-page "enabled tab", so gate at the sidebar
    // row instead: keep it visible but non-selectable when off.
    item->setFlags(on ? (Qt::ItemIsSelectable | Qt::ItemIsEnabled)
                      : Qt::ItemFlags(Qt::NoItemFlags));
    item->setToolTip(on ? QString() : reason);
    if (!on && list->currentRow() == row)
        list->setCurrentRow(0);
}

/*! Grey an inner tab, and redirect off it if it was showing. */
void applyTabGate(QTabWidget *tabs, int idx, bool on, const QString &reason)
{
    if (!tabs || idx < 0 || idx >= tabs->count()) return;
    tabs->setTabEnabled(idx, on);
    tabs->setTabToolTip(idx, on ? QString() : reason);
    // Never leave a greyed tab showing: switching FLOW_ROUTING while viewing
    // the tab that just went away must land somewhere usable.
    if (!on && tabs->currentIndex() == idx) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->isTabEnabled(i)) { tabs->setCurrentIndex(i); break; }
        }
    }
}

} // namespace

void SimulationOptionsDialog::refreshGates()
{
    // Intra-page widget gates first: a page knows its own widgets, the dialog
    // only knows rows and tabs.
    for (openswmmvis::ui::SimOptionsPage *p : m_pageOrder)
        p->refreshGates();
    // The read-only FLOW_ROUTING mirror on Models follows the page that owns
    // the combo (PLAN §2.1) — one key, one editor.
    if (m_modelsPage && m_hydraulicsPage)
        m_modelsPage->setFlowRoutingText(m_hydraulicsPage->flowRouting());
    if(m_modelsPage)m_modelsPage->refreshTransportMatrix();
    for (const PageGate &g : m_gates) {
        if (!g.enabled) continue;
        const bool on = g.enabled();
        switch (g.target) {
        case PageGate::Target::SidebarRow:
            applyRowGate(m_categoryList, g.row, on, g.reasonWhenOff); break;
        case PageGate::Target::Tab:
            applyTabGate(g.tabs, g.tabIndex, on, g.reasonWhenOff); break;
        case PageGate::Target::Widget:
            if (g.widget) {
                g.widget->setEnabled(on);
                g.widget->setToolTip(on ? QString() : g.reasonWhenOff);
            }
            break;
        }
    }
}

void SimulationOptionsDialog::buildGateTable()
{
    m_gates.clear();
    const auto rowGate = [this](int row, std::function<bool()> on,
                                const QString &why) {
        if (row < 0) return;
        PageGate g;
        g.target = PageGate::Target::SidebarRow;
        g.row = row;
        g.enabled = std::move(on);
        g.reasonWhenOff = why;
        m_gates << g;
    };
    const auto tabGate = [this](QTabWidget *tabs, int idx,
                                std::function<bool()> on, const QString &why) {
        if (!tabs) return;
        PageGate g;
        g.target = PageGate::Target::Tab;
        g.tabs = tabs;
        g.tabIndex = idx;
        g.enabled = std::move(on);
        g.reasonWhenOff = why;
        m_gates << g;
    };

#ifdef OPENSWMM_HAS_2D
    rowGate(m_2DRow,
            [this] { return m_modelsPage && m_modelsPage->module2DEnabled(); },
            tr("Enable the 2D module on Models / Processes \u203A Modules"));
#endif
    // Activation lives on this page: it must remain reachable even when
    // the project has no pollutants or reserved species yet.
    rowGate(m_qualityRow, [] { return true; }, QString());

    // ---- Routing & Hydraulics tabs (PLAN §4.3) ----
    if (m_hydraulicsPage && m_hydraulicsPage->tabs()) {
        QTabWidget *ht = m_hydraulicsPage->tabs();
        const auto routing = [this] { return m_hydraulicsPage->flowRouting(); };
        using HP = openswmmvis::ui::HydraulicsPage;
        tabGate(ht, HP::TabDynamicWave,
                [routing] { return routing() == QLatin1String("DYNWAVE"); },
                tr("Applies to Dynamic Wave routing only"));
        tabGate(ht, HP::TabFiniteVolume,
                [this, routing] { return m_caps.fv && routing() == QLatin1String("FV"); },
                m_caps.fv ? tr("Applies to Finite Volume routing only")
                          : tr("This engine does not support FV"));
        tabGate(ht, HP::TabUnsteadyFriction,
                [this, routing] {
                    return m_caps.uf && (routing() == QLatin1String("DYNWAVE")
                                      || routing() == QLatin1String("FV"));
                },
                m_caps.uf ? tr("Unsteady friction applies to Dynamic Wave and Finite Volume")
                          : tr("Not supported by this engine"));
    }

    // ---- Quality & Transport tabs (PLAN §4.3) ----
    // caps.transport is part of both gates: applyCapabilities() freezes the
    // solver combo on an engine that lacks the surface, and without it here a
    // gate would re-enable whichever tab matches that stale selection.
    if (m_qualityPage && m_qualityPage->tabs()) {
        QTabWidget *qt = m_qualityPage->tabs();
        const auto solver = [this] { return m_qualityPage->qualitySolver(); };
        using QP = openswmmvis::ui::QualityPage;
        tabGate(qt, QP::TabArd,
                [this, solver] {
                    return m_caps.transport && solver() == QLatin1String("EULERIAN_ARD");
                },
                m_caps.transport ? tr("Select the Eulerian ARD solver")
                                 : tr("Not supported by this engine"));
        tabGate(qt, QP::TabLard,
                [this, solver] {
                    return m_caps.transport && solver() == QLatin1String("LAGRANGIAN");
                },
                m_caps.transport ? tr("Select the Lagrangian solver")
                                 : tr("Not supported by this engine"));
    }
}

void SimulationOptionsDialog::addCategory(const QString &title, QWidget *page)
{
    m_categoryList->addItem(title);
    m_pages->addWidget(OpenSWMM::Ui::wrapInScrollArea(page, m_pages));
}

QStringList SimulationOptionsDialog::lastWriteKeys() const
{
    return m_ctx ? m_ctx->writtenKeys() : QStringList();
}

void SimulationOptionsDialog::addPage(openswmmvis::ui::SimOptionsPage *page)
{
    if (!page) return;
    m_pageOrder << page;
    addCategory(page->title(), page);
    // One-shot per-widget disable/tooltip from the probe. Ported pages carry
    // their own block; the monolith's applyEngineConstraints() shrinks by one
    // block each phase until T3 reduces it to this loop.
    page->applyCapabilities();
}



QString SimulationOptionsDialog::threadLimitsSummary(const SWMM_ThreadInfo &ti)
{
    QStringList lines;
    lines << tr("This machine: %1 logical processors").arg(ti.logical_cpus);
    if (ti.perf_cores > 0)
        lines << tr("Performance cores: %1 (efficiency cores slow the solvers "
                    "and are avoided in auto mode)").arg(ti.perf_cores);
    if (!ti.omp_available)
        lines << tr("Engine built without OpenMP — every run is serial.");
    else if (ti.omp_max_threads < ti.logical_cpus)
        lines << tr("OpenMP limit in this process: %1 (OMP_NUM_THREADS, "
                    "OMP_THREAD_LIMIT or CPU affinity is limiting it).")
                     .arg(ti.omp_max_threads);
    if (ti.kokkos_omp_threads > 0)
        lines << tr("2D Kokkos backend already running with %1 threads "
                    "(fixed until restart).").arg(ti.kokkos_omp_threads);
    return lines.join(QLatin1Char('\n'));
}


// ---------------------------------------------------------------------------
// Engine ↔ widgets
// ---------------------------------------------------------------------------

void SimulationOptionsDialog::readFromEngine()
{
    // Every page reads its own slice; the dialog owns only the order.
    for (openswmmvis::ui::SimOptionsPage *p : m_pageOrder)
        p->read();
    // A read can change any gate input (the 2D module box, the quality
    // toggles, FLOW_ROUTING), and pages seed with their signals blocked — so
    // re-evaluate once, here, rather than making every page announce it.
    refreshGates();
}

int SimulationOptionsDialog::writeToEngine()
{
    if (!m_engine) return 0;
    // One write pass: the key record starts empty and collects every key this
    // pass considered, so lastWriteKeys() can be compared against the tagged
    // editors (the reachability seam).
    if (m_ctx) m_ctx->beginWritePass();
    int n = 0;
    for (openswmmvis::ui::SimOptionsPage *p : m_pageOrder)
        n += p->write();
    if (n > 0) {
        m_wroteChanges = true;
        emit changesApplied();
    }
    return n;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

bool SimulationOptionsDialog::confirmBeforeWrite()
{
    // Every page validates its own slice: Dates & Times blocks on an event row
    // with Start >= End, Files / Output / Plugins on an empty plugin id or
    // hot-start path. Softer findings (overlapping events, an empty "Selected"
    // report list, a missing parent directory) come back through warn and are
    // non-blocking.
    QString warn;
    for (openswmmvis::ui::SimOptionsPage *p : m_pageOrder) {
        if (p->validate(&warn)) continue;
        QMessageBox::warning(this, tr("Invalid entry on %1").arg(p->title()),
            tr("One or more rows on %1 are missing a required value or are "
               "out of order. Fix the highlighted rows first.").arg(p->title()));
        return false;
    }
    if (!warn.isEmpty()) {
        const auto choice = QMessageBox::warning(this, tr("Warnings"),
            warn + tr("\nApply anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice != QMessageBox::Yes) return false;
    }
    return true;
}

void SimulationOptionsDialog::invalidateContext()
{
    // The model is closing: leave accepted edits on the history; their own
    // identity checks refuse once the engine is gone.
    if (m_session) m_session->abandon();
    m_session.reset();
    m_engine = nullptr;
    m_layer = nullptr;
    m_projectWindow = nullptr;
    if (m_ctx) m_ctx->invalidate();
    setEnabled(false);
    reject();
}

bool SimulationOptionsDialog::applyPass()
{
    if (!m_engine) return false;
    if (!confirmBeforeWrite() || !m_engine) return false;
    OpenSWMMVis::ConfigSnapshot before(m_session ? swmm_config_snapshot_create(m_engine) : nullptr);
    writeToEngine();
    if (m_session) m_session->commit(std::move(before), lastWriteKeys());
    m_wroteAtCommit = m_wroteChanges;
    return true;
}

void SimulationOptionsDialog::onApply()
{
    if (!applyPass()) return;
    // Re-read after write so the controls reflect whatever the engine
    // actually accepted (some keys may be clamped or normalised).
    readFromEngine();
}

void SimulationOptionsDialog::onAccept()
{
    if (!applyPass()) return;
    accept();
}
