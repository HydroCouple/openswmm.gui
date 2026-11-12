/**
 * @file      mesh2dgroundwaterdialog.cpp
 * @brief     GG1-GG4, GG6 (2026-09-07) — the 2D two-zone groundwater editor.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/mesh2dgroundwaterdialog.h"
#include "ui/hydrothermalstate.h"
#include "ui/processlock.h"

#include "core/unitsystem.h"
#include "results/gwledgermodel.h"
#include "ui/dialogs/mesh2daquifermodel.h"
#include "ui/dialogs/mesh2dgwsourcemodel.h"
#include "ui/dialogs/mesh2dsectionroweditor.h"
#include "ui/dialogs/mesh2dsectionrowmodel.h"
#include "ui/util/tableclipboard.h"

#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_tables.h>

#include <initializer_list>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QUndoStack>
#include <QUndoCommand>
#include <QApplication>
#include <QItemSelectionModel>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemDelegate>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QScrollArea>
#include <openswmm/engine/openswmm_gw_transport.h>
#include <QPushButton>
#include <QPointer>
#include <QSpinBox>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui
{

namespace
{

/*! Combo-box editor for a column whose value IS the combo's row index.
 *  Used for the soil law and the closure, where the model deliberately keeps
 *  index == enum code so a reorder cannot silently remap authored rows. */
class ComboDelegate : public QStyledItemDelegate
{
public:
    /**
     * @brief Own an ordered combo-label catalog for integer-valued model fields.
     * @param[in] items Owned labels moved into the delegate; their zero-based order is stored as
     * item data.
     * @param[in] parent Optional Qt owner of the delegate.
     * @param[in] inherited True prepends a model-default item with data -1 without shifting the
     * ordinary stored ordinals.
     */
    ComboDelegate(QStringList items, QObject *parent, bool inherited = false)
        : QStyledItemDelegate(parent), m_items(std::move(items)), m_inherited(inherited)
    {
    }

    /**
     * @brief Create a combo containing optional inheritance and copied ordinal labels.
     * @param[in] parent Optional Qt parent owning the new editor.
     * @return Newly allocated QComboBox; caller/parent owns it. Unnamed style-option/index
     * inputs are ignored and no engine state is queried.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *cb = new QComboBox(parent);
        if (m_inherited)
            cb->addItem(tr("(model default)"), -1);
        for (int i = 0; i < m_items.size(); ++i)
            cb->addItem(m_items[i], i);
        return cb;
    }
    /**
     * @brief Select the combo item whose user data matches the cell's edit integer.
     * @param[in,out] editor Borrowed editor; non-QComboBox values are ignored via qobject_cast.
     * @param[in] index Borrowed model cell; its EditRole value is converted with toInt().
     * @details No matching data selects index -1; does not write model state.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor))
            cb->setCurrentIndex(cb->findData(index.data(Qt::EditRole).toInt()));
    }
    /**
     * @brief Write the combo's current stored ordinal into a model cell.
     * @param[in] editor Borrowed editor; non-QComboBox values are ignored.
     * @param[in,out] model Non-null borrowed target model when a combo is supplied.
     * @param[in] index Borrowed destination cell passed unchanged to setData(Qt::EditRole).
     * @details Does not inspect setter success or retain arguments.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *cb = qobject_cast<QComboBox *>(editor))
            model->setData(index, cb->currentData().toInt(), Qt::EditRole);
    }

private:
    QStringList m_items;
    bool m_inherited = false;
};

class NodeConnectionUndo final : public QUndoCommand
{
    std::shared_ptr<Mesh2DAquiferNodeModel> draft_;
    SWMM_Engine engine_;
    std::function<bool()> current_;
    std::function<void()> edited_;
    bool first_ = true;
    /**
     * @brief Replay the captured node-bed/bottom draft only while the external context guard
     * passes.
     * @param[in] forward True calls draft commit(engine,false), false calls
     * restoreLoaded(engine).
     * @details Missing/false guard or model/helper error makes the command obsolete and warns;
     * success invokes the optional edited callback. Delegated model concurrency/restoration
     * contracts remain applicable and this wrapper adds no independent transaction or exception
     * handling.
     */
    void install(bool forward)
    {
        QString error;
        if (!current_ || !current_())
            error = QObject::tr("The model or mesh changed after this connection edit.");
        else
            error = forward ? draft_->commit(engine_, false) : draft_->restoreLoaded(engine_);
        if (!error.isEmpty()) {
            setObsolete(true);
            QMessageBox::warning(QApplication::activeWindow(), QObject::tr("LID bottom connection"),
                                 error);
            return;
        }
        if (edited_)
            edited_();
    }

public:
    /**
     * @brief Own an already-applied node-bed/bottom transaction draft for guarded undo/redo.
     * @param[in] draft Non-null shared model containing retained before/after snapshots, moved
     * into the command; independent of editor lifetime.
     * @param[in] engine Borrowed engine pointer used only while current() authorizes replay;
     * never owned.
     * @param[in] current Owned GUI-thread callback required to confirm engine/model identity is
     * still safe before every replay; missing/false refuses.
     * @param[in] edited Optional owned GUI-thread nonthrowing callback after successful replay;
     * must not require a live dialog.
     * @details First redo intentionally skips the already-installed Apply. Command owns
     * callbacks/model, not the engine or external context.
     */
    NodeConnectionUndo(std::shared_ptr<Mesh2DAquiferNodeModel> draft, SWMM_Engine engine,
                       std::function<bool()> current, std::function<void()> edited)
        : draft_(std::move(draft)), engine_(engine), current_(std::move(current)),
          edited_(std::move(edited))
    {
        setText(QObject::tr("Edit node bottom connections"));
    }
    /**
     * @brief Restore the captured loaded state through guarded install(false).
     * @details Expected-state/context failures mark obsolete and warn rather than merging newer
     * unrelated edits.
     */
    void undo() override { install(false); }
    /**
     * @brief Skip the initial push, then replay through guarded install(true).
     * @details Later successful publication retains the original draft baseline for subsequent
     * undo; initial redo writes/notifies nothing.
     */
    void redo() override
    {
        if (first_) {
            first_ = false;
            return;
        }
        install(true);
    }
};

class BoundaryUndo final : public QUndoCommand
{
    std::shared_ptr<Mesh2DAquiferBoundaryModel> draft_;
    SWMM_Engine engine_;
    std::function<bool()> current_;
    std::function<void()> edited_;
    bool first_ = true;
    /**
     * @brief Replay the captured boundary draft only while the external context guard passes.
     * @param[in] forward True calls draft commit(engine,false), false calls
     * restoreLoaded(engine).
     * @details Missing/false guard or model/helper error makes the command obsolete and warns;
     * success invokes the optional edited callback. Delegated model concurrency/restoration
     * contracts remain applicable and this wrapper adds no independent transaction or exception
     * handling.
     */
    void install(bool forward)
    {
        QString error;
        if (!current_ || !current_())
            error = QObject::tr("The model or mesh changed after this boundary edit.");
        else
            error = forward ? draft_->commit(engine_, false) : draft_->restoreLoaded(engine_);
        if (!error.isEmpty()) {
            setObsolete(true);
            QMessageBox::warning(QApplication::activeWindow(),
                                 QObject::tr("Groundwater boundaries"), error);
            return;
        }
        if (edited_)
            edited_();
    }

public:
    /**
     * @brief Own an already-applied boundary transaction draft for guarded undo/redo.
     * @param[in] draft Non-null shared model containing retained before/after snapshots, moved
     * into the command; independent of editor lifetime.
     * @param[in] engine Borrowed engine pointer used only while current() authorizes replay;
     * never owned.
     * @param[in] current Owned GUI-thread callback required to confirm engine/model identity is
     * still safe before every replay; missing/false refuses.
     * @param[in] edited Optional owned GUI-thread nonthrowing callback after successful replay;
     * must not require a live dialog.
     * @details First redo intentionally skips the already-installed Apply. Command owns
     * callbacks/model, not the engine or external context.
     */
    BoundaryUndo(std::shared_ptr<Mesh2DAquiferBoundaryModel> draft, SWMM_Engine engine,
                 std::function<bool()> current, std::function<void()> edited)
        : draft_(std::move(draft)), engine_(engine), current_(std::move(current)),
          edited_(std::move(edited))
    {
        setText(QObject::tr("Edit groundwater boundaries"));
    }
    /**
     * @brief Restore the captured loaded state through guarded install(false).
     * @details Expected-state/context failures mark obsolete and warn rather than merging newer
     * unrelated edits.
     */
    void undo() override { install(false); }
    /**
     * @brief Skip the initial push, then replay through guarded install(true).
     * @details Later successful publication retains the original draft baseline for subsequent
     * undo; initial redo writes/notifies nothing.
     */
    void redo() override
    {
        if (first_) {
            first_ = false;
            return;
        }
        install(true);
    }
};

/**
 * @brief Read a groundwater or prefixed transport option into an owned string.
 * @param[in] e Borrowed engine queried synchronously.
 * @param[in] key Borrowed non-null UTF-8 NUL-terminated key; GW_TRANSPORT: dispatches its suffix
 * to transport getters, other keys to gw2d getters.
 * @return UTF-8 QString from a zero-initialized 128-byte buffer on SWMM_OK, otherwise empty.
 * Does not independently detect successful API truncation or modify authoring state.
 */
QString optionText(SWMM_Engine e, const char *key)
{
    char buf[128] = {0};
    const QString name = QString::fromUtf8(key);
    const int result =
        name.startsWith("GW_TRANSPORT:")
            ? swmm_gw_transport_option_get(e, name.mid(13).toUtf8().constData(), buf, sizeof buf)
            : swmm_gw2d_option_get(e, key, buf, sizeof buf);
    if (result != SWMM_OK)
        return {};
    return QString::fromUtf8(buf);
}

/**
 * @brief Dispatch one native authoring option setter by its optional transport prefix.
 * @param[in,out] e Borrowed engine accepting authoring changes; lifecycle/validation is enforced
 * by the called API.
 * @param[in] key Borrowed non-null UTF-8 NUL-terminated key; GW_TRANSPORT: is removed before
 * transport dispatch.
 * @param[in] value Borrowed native-unit/token value copied to temporary UTF-8 storage for the
 * call.
 * @return True for SWMM_OK, false otherwise; setters can mark options authored and no rollback
 * occurs here.
 */
bool setOption(SWMM_Engine e, const char *key, const QString &value)
{
    const QString name = QString::fromUtf8(key);
    return (name.startsWith("GW_TRANSPORT:")
                ? swmm_gw_transport_option_set(e, name.mid(13).toUtf8().constData(),
                                               value.toUtf8().constData())
                : swmm_gw2d_option_set(e, key, value.toUtf8().constData())) == SWMM_OK;
}

/**
 * @brief Probe lifecycle state without invoking setters or marking options authored.
 * @param[in] engine Borrowed engine, read synchronously; invalid/null handles fail the API
 * query.
 * @return True only for successful BUILDING/OPENED state; false otherwise.
 */
bool canEdit(SWMM_Engine engine)
{
    int state = SWMM_STATE_CREATED;
    return swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

/**
 * @brief Probe whether the engine exposes a recognized porous-heat option value.
 * @param[in] engine Borrowed optional engine; null returns false.
 * @return porousSoilHeatOptionIsSupported() for THERMAL_MODEL getter success and trimmed text
 * from a 64-byte buffer; no authoring setter is called.
 */
bool supportsPorousThermalOptions(SWMM_Engine engine)
{
    if (!engine)
        return false;
    char value[64]{};
    const int result = swmm_gw_transport_option_get(engine, "THERMAL_MODEL", value, sizeof value);
    return porousSoilHeatOptionIsSupported(result == SWMM_OK, QString::fromUtf8(value).trimmed());
}

} // namespace

QStringList Mesh2DGroundwaterDialog::soilModelTokens()
{
    return Mesh2DAquiferModel::soilTokens();
}

QStringList Mesh2DGroundwaterDialog::closureTokens()
{
    return Mesh2DAquiferModel::closureTokens();
}

QStringList Mesh2DGroundwaterDialog::configurationOptionKeys()
{
    QStringList keys = {QStringLiteral("SOIL_CHAR"),
                        QStringLiteral("CLOSURE"),
                        QStringLiteral("M_LAYERS"),
                        QStringLiteral("CAPILLARY_DIFF"),
                        QStringLiteral("C_GW"),
                        QStringLiteral("C_COL"),
                        QStringLiteral("FORCE_CLOSED_FORM"),
                        QStringLiteral("DUNNE"),
                        QStringLiteral("MODE"),
                        QStringLiteral("GW_ET"),
                        QStringLiteral("NODE_ENROLMENT"),
                        QStringLiteral("GW_CLOCK"),
                        QStringLiteral("GW_BASE_STEP"),
                        QStringLiteral("GW_MAX_STEP"),
                        QStringLiteral("GW_BUDGET_FRACTION"),
                        QStringLiteral("GW_HEAD_TOLERANCE"),
                        QStringLiteral("GW_BINDS_SURFACE_DT")};
    const QStringList transportKeys = {QStringLiteral("UNSAT_TRANSPORT"),
                                       QStringLiteral("TRANSPORT_POLLUTANTS"),
                                       QStringLiteral("TRANSPORT_MSX"),
                                       QStringLiteral("TRANSPORT_AGE"),
                                       QStringLiteral("TRANSPORT_TEMPERATURE"),
                                       QStringLiteral("DISPERSION"),
                                       QStringLiteral("THERMAL_MODEL"),
                                       QStringLiteral("CONDUCTION"),
                                       QStringLiteral("THERMAL_MIXING"),
                                       QStringLiteral("LATERAL_CONDUCTION"),
                                       QStringLiteral("SOIL_ET_HEAT"),
                                       QStringLiteral("THERMAL_LAYERS"),
                                       QStringLiteral("THERMAL_MAX_STEP"),
                                       QStringLiteral("WATER_THERMAL_CONDUCTIVITY"),
                                       QStringLiteral("AIR_THERMAL_CONDUCTIVITY"),
                                       QStringLiteral("SURFACE_THERMAL_RESISTANCE"),
                                       QStringLiteral("C_DIFF"),
                                       QStringLiteral("SURFACE_THERMAL_BC"),
                                       QStringLiteral("DEEP_THERMAL_BC")};
    for (const auto &key : transportKeys)
        keys << QStringLiteral("GW_TRANSPORT:") + key;
    return keys;
}

void Mesh2DGroundwaterDialog::setThermalOptionsOnly(bool enabled)
{
    if (!m_tabs)
        return;
    for (int i = 1; i < m_tabs->count(); ++i)
        if (i != 4) // Options (0) and Transport and thermal settings (4) stay
            m_tabs->setTabVisible(i, !enabled);
}

Mesh2DGroundwaterDialog::Mesh2DGroundwaterDialog(SWMM_Engine engine, QWidget *parent,
                                                 Page initialPage, const UnitSystem *units)
    : QDialog(parent), m_engine(engine), m_units(units),
      m_porousThermalOptionsSupported(supportsPorousThermalOptions(engine))
{
    setObjectName(QStringLiteral("Mesh2DGroundwaterDialog"));
    setWindowTitle(tr("2D Groundwater"));
    m_clockOptionsSupported = !optionText(engine, "GW_CLOCK").isEmpty();
    buildUi(initialPage);
    loadFromEngine();
    if (parent) {
        if (parent->metaObject()->indexOfSignal("engineAboutToClose()") >= 0)
            connect(parent, SIGNAL(engineAboutToClose()), this, SLOT(invalidateContext()));
        connect(parent, &QObject::destroyed, this, &Mesh2DGroundwaterDialog::invalidateContext);
    }
    if (units) {
        connect(units, &UnitSystem::unitsChanged, this,
                &Mesh2DGroundwaterDialog::invalidateContext);
        connect(units, &QObject::destroyed, this, &Mesh2DGroundwaterDialog::invalidateContext);
    }
}
void Mesh2DGroundwaterDialog::invalidateContext()
{
    m_engine = nullptr;
    m_editable = false;
    setEnabled(false);
    QDialog::reject();
}

void Mesh2DGroundwaterDialog::buildUi(Page initialPage)
{
    auto *outer = new QVBoxLayout(this);

    m_banner = new QLabel(this);
    m_banner->setWordWrap(true);
    m_banner->setFrameShape(QFrame::StyledPanel);
    m_banner->setContentsMargins(8, 6, 8, 6);
    m_banner->hide();
    outer->addWidget(m_banner);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildOptionsPage(), tr("Options"));
    m_tabs->addTab(buildAquiferPage(), tr("Aquifer"));
    m_tabs->addTab(buildNodeBedPage(), tr("Node Beds"));
    m_tabs->addTab(buildStatePage(), tr("State"));
    m_tabs->addTab(buildTransportPage(), tr("Transport and thermal settings"));
    m_tabs->addTab(buildBoundaryPage(), tr("Edge boundaries"));
    m_tabs->addTab(buildSourcesPage(), tr("Sources"));
    using Section = Mesh2DSectionRowModel::Section;
    const QString scopes = tr("Rows apply to all cells (<b>*</b>), every cell with a <b>tag</b>, "
                              "or one <b>cell</b>; the most specific row wins.");
    const struct {
        Section section;
        const char *name;
        QString title, hint;
    } rowPages[] = {
        {Section::GwParams, "gwTransportParams", tr("Transport parameters"),
         tr("Porous-media properties in SI units. The running kernel uses grain density for "
            "sorption retardation and the dispersivities and molecular diffusivity for "
            "dispersion; the thermal and vapour columns are kept for the thermal model. %1")
             .arg(scopes)},
        {Section::GwSorption, "gwSorption", tr("Sorption"),
         tr("Linear sorption and first-order decay of an aquifer species. Leave the decay blank "
            "to use the pollutant's own decay. %1")
             .arg(scopes)},
        {Section::GwInitialQuality, "gwInitialQuality", tr("Initial quality"),
         tr("Starting concentration, water age or temperature in the saturated (SAT) or "
            "unsaturated (UNSAT) zone. %1 Assign Groundwater sets per-cell rows from a "
            "selection.")
             .arg(scopes)},
        {Section::AquiferLinks, "gwAquiferLinks", tr("Conduit links"),
         tr("Per-conduit overrides of seepage exchange with the aquifer. KC and DC apply with "
            "LINK_SEEPAGE TWO_WAY; clear Exchange to keep a conduit's seepage a system loss.")},
    };
    for (const auto &page : rowPages) {
        auto *model = new Mesh2DSectionRowModel(page.section, this);
        model->setObjectName(QString::fromLatin1(page.name));
        m_tabs->addTab(addRowPage(model, page.hint), page.title);
    }
    switch (initialPage) {
    case Page::Options:
        m_tabs->setCurrentIndex(0);
        break;
    case Page::InitialConditions:
        m_tabs->setCurrentIndex(1);
        break; // hg0 lives on the rows
    case Page::State:
        m_tabs->setCurrentIndex(3);
        break;
    case Page::NodeBeds:
        m_tabs->setCurrentIndex(2);
        break;
    case Page::EdgeBoundaries:
        m_tabs->setCurrentIndex(5);
        break;
    default:
        m_tabs->setCurrentIndex(1);
        break;
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
            &Mesh2DGroundwaterDialog::onApply);
    outer->addWidget(m_buttons);

    resize(880, 520);
}

// ---------------------------------------------------------------------------
// GG1 — [2D_AQUIFER_OPTIONS]
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildOptionsPage()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_soilCombo = new QComboBox(page);
    m_soilCombo->addItems(soilModelTokens());
    m_soilCombo->setToolTip(tr("Default soil characteristic law. Gardner is the only one whose "
                               "quasi-steady recharge is a published closed form; the other three "
                               "reproduce the right equilibrium and sign, but their relaxation "
                               "rate is this engine's modelling choice. Prefer the σ column "
                               "closure where the answer matters."));
    form->addRow(tr("&Soil law:"), m_soilCombo);

    m_closureCombo = new QComboBox(page);
    m_closureCombo->addItems(closureTokens());
    m_closureCombo->setToolTip(
        tr("Default unsaturated-zone closure. AUTO chooses per cell from αL: "
           "below 1 the column tracks the table closely enough to be dropped "
           "(ENSLAVED), above 5 the quasi-steady assumption fails and the "
           "cell gets a real σ column."));
    form->addRow(tr("&Closure:"), m_closureCombo);

    m_layersSpin = new QSpinBox(page);
    m_layersSpin->setRange(2, 128);
    m_layersSpin->setToolTip(tr("Layers in each σ column. Fixed for the run: the layers stretch "
                                "and compress with the water table instead of being regridded."));
    form->addRow(tr("σ &layers:"), m_layersSpin);

    m_capillaryCheck = new QCheckBox(tr("Include the capillary diffusion term"), page);
    m_capillaryCheck->setToolTip(tr("Off by default. It tightens the column's stability step "
                                    "considerably and changes little in most storm-scale runs."));
    form->addRow(QString(), m_capillaryCheck);

    m_cgwSpin = new QDoubleSpinBox(page);
    m_cgwSpin->setRange(0.01, 1.0);
    m_cgwSpin->setDecimals(3);
    m_cgwSpin->setSingleStep(0.05);
    m_cgwSpin->setToolTip(tr("Safety factor on the saturated zone's explicit "
                             "stability step."));
    form->addRow(tr("Saturated safety &factor:"), m_cgwSpin);

    m_ccolSpin = new QDoubleSpinBox(page);
    m_ccolSpin->setRange(0.01, 1.0);
    m_ccolSpin->setDecimals(3);
    m_ccolSpin->setSingleStep(0.05);
    m_ccolSpin->setToolTip(tr("Safety factor on the unsaturated column's "
                              "explicit stability step."));
    form->addRow(tr("Column safet&y factor:"), m_ccolSpin);

    m_forceCfCheck =
        new QCheckBox(tr("Keep the closed form even when AUTO would switch to σ"), page);
    m_forceCfCheck->setToolTip(
        tr("For like-for-like comparisons against a closed-form run. It does "
           "not make the closed form valid at large αL — it makes it "
           "reachable."));
    form->addRow(QString(), m_forceCfCheck);

    m_dunneCheck = new QCheckBox(tr("Return saturation excess to the surface (Dunne)"), page);
    m_dunneCheck->setToolTip(tr("On by default, and it is not an option in the usual sense: with "
                                "it off, water that reaches a saturated table has nowhere to go "
                                "and leaves the continuity balance. Turn it off only to compare "
                                "against a model that also loses it."));
    form->addRow(QString(), m_dunneCheck);

    m_modeCombo = new QComboBox(page);
    m_modeCombo->addItems({QStringLiteral("MESH"), QStringLiteral("PER_SUBCATCH")});
    m_modeCombo->setToolTip(tr("MESH runs one aquifer per mesh cell with lateral Darcy between "
                               "them. PER_SUBCATCH runs one degenerate cell per subcatchment with "
                               "no lateral flux — the same kernel, as a drop-in for the legacy "
                               "subcatchment aquifer."));
    form->addRow(tr("&Mode:"), m_modeCombo);

    m_gwEtCombo = new QComboBox(page);
    m_gwEtCombo->setObjectName("gwDetailedEt");
    m_gwEtCombo->addItem(tr("Automatic"), "AUTO");
    m_gwEtCombo->addItem(tr("None"), "NONE");
    m_gwEtCombo->addItem(tr("Capillary rise"), "CAPILLARY_RISE");
    m_gwEtCombo->addItem(tr("Boundary ET"), "BOUNDARY_ET");
    m_gwEtCombo->addItem(tr("Both"), "BOTH");
    m_gwEtCombo->setToolTip(tr("CAPILLARY_RISE lets recharge run upward when the column is drier "
                               "than its equilibrium. BOUNDARY_ET takes evaporation from the top "
                               "of the column with a smooth Feddes stress rather than an on/off "
                               "gate."));
    form->addRow(tr("Groundwater &ET:"), m_gwEtCombo);
    auto *etStatus = new QLabel(page);
    etStatus->setWordWrap(true);
    etStatus->setObjectName("gwDetailedEtStatus");
    form->addRow(etStatus);
    auto refreshEt = [this, etStatus] {
        char configured[64]{}, enabled[64]{};
        if (m_engine) {
            swmm_gw2d_option_get(m_engine, "CONFIGURED", configured, sizeof configured);
            swmm_options_get_ext(m_engine, "GROUNDWATER", enabled, sizeof enabled);
        }
        etStatus->setText(groundwaterEtState(m_gwEtCombo->currentData().toString(),
                                             m_modeCombo->currentText(),
                                             QString::fromUtf8(enabled) != "NO",
                                             QString::fromUtf8(configured) == "YES")
                              .reason);
    };
    connect(m_gwEtCombo, &QComboBox::currentIndexChanged, this, refreshEt);
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, refreshEt);
    refreshEt();

    m_enrolmentCombo = new QComboBox(page);
    m_enrolmentCombo->setObjectName("gwNodeEnrolment");
    m_enrolmentCombo->addItem(tr("Automatic"), "AUTO");
    m_enrolmentCombo->addItem(tr("Node Beds rows only"), "ROWS");
    m_enrolmentCombo->setToolTip(
        tr("AUTO connects every node inside the mesh (outfalls excepted) to the aquifer under it; "
           "a Node Beds row overrides or opts a node out. ROWS connects only the nodes listed on "
           "the Node Beds tab."));
    form->addRow(tr("Node &enrolment:"), m_enrolmentCombo);

    auto *clock = new QGroupBox(tr("Groundwater time stepping"), page);
    auto *clockForm = new QFormLayout(clock);
    m_clockCombo = new QComboBox(clock);
    m_clockCombo->setObjectName("gwClock");
    m_clockCombo->addItem(tr("Shared with surface"), "SHARED");
    m_clockCombo->addItem(tr("Independent (experimental)"), "INDEPENDENT");
    m_clockCombo->setAccessibleName(tr("Groundwater time stepping"));
    m_clockCombo->setToolTip(tr("Shared is the default. Independent updates aquifer cells on their "
                                "own time steps and requires first-order CPU integration."));
    clockForm->addRow(tr("&Time stepping:"), m_clockCombo);
    const struct {
        const char *key;
        QString label, help;
        double maximum, initial;
    } clockFields[] = {
        {"GW_BASE_STEP", tr("&Base interval (s):"),
         tr("Base check interval, in seconds. Use a smaller value for rapid exchange; 0.125 s "
            "was qualified for the node-fill case. Physical stability can require shorter steps."),
         1e9, 1.},
        {"GW_MAX_STEP", tr("Ma&ximum groundwater step (s):"),
         tr("Maximum elapsed groundwater step, in seconds. It may be shorter than the base "
            "interval; a fine maximum also refines producer checks."),
         1e9, 3600.},
        {"GW_BUDGET_FRACTION", tr("Storage b&udget fraction:"),
         tr("Fraction of available storage or head room used before requesting an update. "
            "Must be greater than zero and at most one."),
         1., .5},
        {"GW_HEAD_TOLERANCE", tr("Relative &head-change budget:"),
         tr("Scheduling budget for relative head change, with a 0.01 m head floor. "
            "This is not a global error guarantee. Must be greater than zero and less than one."),
         .999999999, .05},
    };
    for (const auto &field : clockFields) {
        auto *spin = new QDoubleSpinBox(clock);
        spin->setObjectName(QStringLiteral("gwClock_") + QString::fromLatin1(field.key));
        spin->setDecimals(9);
        spin->setRange(1e-9, field.maximum);
        spin->setValue(field.initial);
        spin->setAccessibleName(QString(field.label).remove(QLatin1Char('&')));
        spin->setToolTip(field.help);
        spin->setAccessibleDescription(field.help);
        clockForm->addRow(field.label, spin);
        m_clockNumbers.insert(QString::fromLatin1(field.key), spin);
    }
    m_bindsSurfaceCheck =
        new QCheckBox(tr("Also let groundwater stability limit the surface step"), clock);
    m_bindsSurfaceCheck->setObjectName("gwBindsSurfaceDt");
    m_bindsSurfaceCheck->setToolTip(tr("Applies to independent stepping. Off keeps a stiff "
                                       "groundwater column from shortening the surface base step. "
                                       "Shared stepping always includes the groundwater limit."));
    clockForm->addRow(m_bindsSurfaceCheck);
    m_clockStatus = new QLabel(clock);
    m_clockStatus->setObjectName("gwClockStatus");
    m_clockStatus->setWordWrap(true);
    clockForm->addRow(m_clockStatus);
    form->addRow(clock);
    connect(m_clockCombo, &QComboBox::currentIndexChanged, this,
            &Mesh2DGroundwaterDialog::refreshClockGates);
    refreshClockGates();
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
}

void Mesh2DGroundwaterDialog::refreshClockGates()
{
    const bool independent = m_clockCombo->currentData() == QStringLiteral("INDEPENDENT");
    m_clockCombo->setEnabled(m_editable && m_clockOptionsSupported);
    for (auto *spin : m_clockNumbers)
        spin->setEnabled(m_editable && m_clockOptionsSupported && independent);
    m_bindsSurfaceCheck->setEnabled(m_editable && m_clockOptionsSupported && independent);
    m_clockStatus->setText(
        !m_clockOptionsSupported
            ? tr("This engine does not provide independent groundwater stepping.")
        : independent
            ? tr("Experimental independent stepping requires first-order CPU integration. "
                 "Choose a smaller base interval for rapid node exchange.")
            : tr("Groundwater follows the surface clock. Shared stepping always includes "
                 "the groundwater stability limit in the surface step."));
    m_clockStatus->setAccessibleDescription(m_clockStatus->text());
}

// ---------------------------------------------------------------------------
// GG2 — [2D_AQUIFER]
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildAquiferPage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *hint = new QLabel(tr("Rows resolve least-specific first: <b>*</b> then <b>Tag</b> then "
                               "<b>Cell</b>, and within one scope the lower row wins. Leave the "
                               "soil law and closure at \"(model default)\" to follow the Options "
                               "tab."),
                            page);
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_aquifer = new Mesh2DAquiferModel(this);
    m_aquiferView = new QTableView(page);
    m_aquiferView->setModel(m_aquifer);
    m_aquiferView->setAccessibleName(tr("Aquifer properties and initial conditions"));
    m_aquiferView->setAccessibleDescription(
        tr("Select a row with the arrow keys. Press F2 to edit the current cell."));
    m_aquiferView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_aquiferView->horizontalHeader()->setStretchLastSection(false);
    m_aquiferView->setItemDelegateForColumn(
        Mesh2DAquiferModel::ColScope,
        new ComboDelegate({tr("All cells"), tr("Tag"), tr("Cell")}, this));
    m_aquiferView->setItemDelegateForColumn(Mesh2DAquiferModel::ColSoil,
                                            new ComboDelegate(soilModelTokens(), this, true));
    m_aquiferView->setItemDelegateForColumn(Mesh2DAquiferModel::ColClosure,
                                            new ComboDelegate(closureTokens(), this, true));
    openswmmvis::ui::TableClipboardOptions aquiferClip;
    aquiferClip.appendRow = [this] { return m_aquifer->appendRow() >= 0; };
    openswmmvis::ui::TableClipboard::install(m_aquiferView, aquiferClip);
    v->addWidget(m_aquiferView, 1);

    auto *row = new QHBoxLayout;
    m_addRowBtn = new QPushButton(tr("Add row"), page);
    m_addRowBtn->setAutoDefault(false);
    m_delRowBtn = new QPushButton(tr("Remove row"), page);
    m_delRowBtn->setAutoDefault(false);
    connect(m_addRowBtn, &QPushButton::clicked, this, &Mesh2DGroundwaterDialog::onAddAquiferRow);
    connect(m_delRowBtn, &QPushButton::clicked, this, &Mesh2DGroundwaterDialog::onRemoveAquiferRow);
    row->addWidget(m_addRowBtn);
    row->addWidget(m_delRowBtn);
    row->addStretch(1);
    v->addLayout(row);

    return page;
}

// ---------------------------------------------------------------------------
// GG3 — [2D_AQUIFER_NODE]
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildNodeBedPage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *hint = new QLabel(tr("Each row lets one 1D node exchange with the aquifer under one "
                               "mesh cell — infiltration into a leaky pipe, or exfiltration out "
                               "of a surcharged one. A node may have at most one bed."),
                            page);
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_nodes = new Mesh2DAquiferNodeModel(this);
    m_nodeView = new QTableView(page);
    m_nodeView->setModel(m_nodes);
    m_nodeView->setItemDelegateForColumn(
        Mesh2DAquiferNodeModel::ColBottom,
        new ComboDelegate(Mesh2DAquiferNodeModel::bottomLabels(), m_nodeView));
    m_nodeView->setAccessibleName(tr("Groundwater node beds"));
    m_nodeView->setAccessibleDescription(tr(
        "Select a row with the arrow keys. Press F2 to edit the current cell. Press Space to toggle aquifer exchange."));
    m_nodeView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nodeView->horizontalHeader()->setStretchLastSection(false);
    m_nodeView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_nodeView->horizontalHeader()->moveSection(Mesh2DAquiferNodeModel::ColExchange, 2);
    openswmmvis::ui::TableClipboardOptions bedClip;
    bedClip.appendRow = [this] { return m_nodes->appendRow(tr("<node>"), 0) >= 0; };
    openswmmvis::ui::TableClipboard::install(m_nodeView, bedClip);
    v->addWidget(m_nodeView, 1);

    auto *row = new QHBoxLayout;
    m_addBedBtn = new QPushButton(tr("Add bed"), page);
    m_addBedBtn->setAutoDefault(false);
    m_delBedBtn = new QPushButton(tr("Remove bed"), page);
    m_delBedBtn->setAutoDefault(false);
    connect(m_addBedBtn, &QPushButton::clicked, this, &Mesh2DGroundwaterDialog::onAddNodeBed);
    connect(m_delBedBtn, &QPushButton::clicked, this, &Mesh2DGroundwaterDialog::onRemoveNodeBed);
    row->addWidget(m_addBedBtn);
    row->addWidget(m_delBedBtn);
    row->addStretch(1);
    v->addLayout(row);

    auto *form = new QFormLayout;
    m_bottomNode = new QComboBox(page);
    m_bottomNode->setObjectName("lidBottomNode");
    m_bottomNode->setAccessibleName(tr("Storage LID node"));
    m_bottomDestination = new QComboBox(page);
    m_bottomDestination->setObjectName("lidBottomDestination");
    m_bottomDestination->setAccessibleName(tr("Bottom destination"));
    m_bottomDestination->addItems(Mesh2DAquiferNodeModel::bottomLabels());
    m_useFootprint = new QPushButton(tr("Use current LID footprint"), page);
    m_useFootprint->setObjectName("lidUseFootprint");
    m_useFootprint->setAutoDefault(false);
    form->addRow(tr("Storage LID &node"), m_bottomNode);
    form->addRow(tr("Bottom &destination"), m_bottomDestination);
    form->addRow(QString{}, m_useFootprint);
    v->addLayout(form);
    m_bottomSummary = new QLabel(page);
    m_bottomSummary->setObjectName("lidBottomPreview");
    m_bottomSummary->setWordWrap(true);
    v->addWidget(m_bottomSummary);
    connect(m_bottomNode, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &Mesh2DGroundwaterDialog::updateBottomConnection);
    connect(m_bottomDestination, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int mode) {
                m_nodes->setBottomDestination(m_bottomNode->currentText(), mode);
                updateBottomConnection();
            });
    connect(m_useFootprint, &QPushButton::clicked, this, [this] {
        const auto error = m_nodes->useLidFootprint(m_bottomNode->currentText());
        updateBottomConnection();
        if (!error.isEmpty())
            m_bottomSummary->setText(error);
    });
    connect(m_nodeView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &index) {
                const QString name =
                    m_nodes->data(m_nodes->index(index.row(), 0), Qt::DisplayRole).toString();
                const int at = m_bottomNode->findText(name);
                if (at >= 0)
                    m_bottomNode->setCurrentIndex(at);
            });
    connect(m_nodes, &QAbstractItemModel::dataChanged, this, [this] { updateBottomConnection(); });

    return page;
}

// ---------------------------------------------------------------------------
// GG4 + GG6 — live state and diagnostics
// ---------------------------------------------------------------------------

QWidget *Mesh2DGroundwaterDialog::buildStatePage()
{
    auto *page = new QWidget(this);
    auto *v = new QVBoxLayout(page);

    auto *hint = new QLabel(tr("Read-only. Everything here is in SI (metres, seconds, m³), which "
                               "is what the kernel holds — it is not converted, so a number "
                               "quoted from here means the same thing on any project."),
                            page);
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_stateText = new QPlainTextEdit(page);
    m_stateText->setReadOnly(true);
    m_stateText->setAccessibleName(tr("Groundwater state and water balance"));
    m_stateText->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = m_stateText->font();
    mono.setStyleHint(QFont::Monospace);
    mono.setFamily(QStringLiteral("Monospace"));
    m_stateText->setFont(mono);
    v->addWidget(m_stateText, 1);

    auto *row = new QHBoxLayout;
    m_refreshBtn = new QPushButton(tr("Refresh"), page);
    m_refreshBtn->setAutoDefault(false);
    connect(m_refreshBtn, &QPushButton::clicked, this, &Mesh2DGroundwaterDialog::refreshState);
    row->addStretch(1);
    row->addWidget(m_refreshBtn);
    v->addLayout(row);

    // V4: the saved run's ledgers — still here after the run and after a
    // reopen, unlike the live text above; the comparison plot reads the
    // same model (results::GwLedgerModel).
    auto *saved = new QGroupBox(tr("Saved run ledgers (2D results file)"), page);
    auto *sv = new QVBoxLayout(saved);
    m_ledgerStatus = new QLabel(saved);
    m_ledgerStatus->setWordWrap(true);
    sv->addWidget(m_ledgerStatus);
    m_ledger = new results::GwLedgerModel(this);
    m_ledgerView = new QTableView(saved);
    openswmmvis::ui::TableClipboard::install(m_ledgerView, {.copyOnly = true});
    m_ledgerView->setModel(m_ledger);
    m_ledgerView->setAccessibleName(tr("Saved groundwater water and species ledgers"));
    m_ledgerView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ledgerView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_ledgerView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ledgerView->verticalHeader()->setVisible(false);
    m_ledgerView->horizontalHeader()->setStretchLastSection(true);
    sv->addWidget(m_ledgerView, 1);
    auto *plotRow = new QHBoxLayout;
    m_plotLedgerBtn = new QPushButton(tr("Plot Selected in Comparison Plot"), saved);
    m_plotLedgerBtn->setAutoDefault(false);
    m_plotLedgerBtn->setToolTip(
        tr("Plot the selected terms as cumulative time series on the 2D results run."));
    // Rows in table order; unavailable rows have no series and are skipped.
    const auto selectedTerms = [this] {
        QList<int> rows;
        for (const QModelIndex &index : m_ledgerView->selectionModel()->selectedRows())
            rows.append(index.row());
        std::sort(rows.begin(), rows.end());
        QVector<plot::ResultDescriptor> terms;
        for (int r : rows)
            if (const auto d = m_ledger->descriptor(r); d.isValid())
                terms.append(d);
        return terms;
    };
    connect(m_plotLedgerBtn, &QPushButton::clicked, this, [this, selectedTerms] {
        if (const auto terms = selectedTerms(); !terms.isEmpty())
            emit plotLedgerRequested(terms);
    });
    connect(m_ledgerView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this, selectedTerms] { m_plotLedgerBtn->setEnabled(!selectedTerms().isEmpty()); });
    plotRow->addStretch(1);
    plotRow->addWidget(m_plotLedgerBtn);
    sv->addLayout(plotRow);
    v->addWidget(saved, 1);
    setSavedResultsFile(QString());

    return page;
}

void Mesh2DGroundwaterDialog::setSavedResultsFile(const QString &h5Path)
{
    if (!m_ledger)
        return;
    m_ledger->load(h5Path);
    m_ledgerStatus->setText(m_ledger->status());
    m_ledgerView->resizeColumnsToContents();
    m_plotLedgerBtn->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Load / apply
// ---------------------------------------------------------------------------

void Mesh2DGroundwaterDialog::loadFromEngine()
{
    if (!m_engine) {
        setEditable(false, tr("No model is open."));
        refreshState();
        return;
    }

    const QString lengthLabel = m_units ? m_units->lengthLabel() : QStringLiteral("m");
    const bool si = lengthLabel == QStringLiteral("m");
    const QString rateLabel = si ? QStringLiteral("mm/hr") : QStringLiteral("in/hr");
    const QString areaLabel = si ? QStringLiteral("m²") : QStringLiteral("ft²");
    m_aquifer->setUnitLabels(lengthLabel, rateLabel);
    m_nodes->setUnitLabels(lengthLabel, rateLabel, areaLabel);

    auto pick = [](QComboBox *cb, const QString &token) {
        int i = cb->findData(token);
        if (i < 0)
            i = cb->findText(token, Qt::MatchFixedString);
        if (i >= 0)
            cb->setCurrentIndex(i);
    };
    pick(m_soilCombo, optionText(m_engine, "SOIL_CHAR"));
    pick(m_closureCombo, optionText(m_engine, "CLOSURE"));
    pick(m_modeCombo, optionText(m_engine, "MODE"));
    pick(m_gwEtCombo, optionText(m_engine, "GW_ET"));
    pick(m_enrolmentCombo, optionText(m_engine, "NODE_ENROLMENT"));
    if (m_clockOptionsSupported) {
        pick(m_clockCombo, optionText(m_engine, "GW_CLOCK"));
        for (auto it = m_clockNumbers.cbegin(); it != m_clockNumbers.cend(); ++it)
            it.value()->setValue(optionText(m_engine, it.key().toUtf8().constData()).toDouble());
        m_bindsSurfaceCheck->setChecked(
            m_clockCombo->currentData() == QStringLiteral("INDEPENDENT") &&
            optionText(m_engine, "GW_BINDS_SURFACE_DT") == QLatin1String("YES"));
    }
    m_layersSpin->setValue(optionText(m_engine, "M_LAYERS").toInt());
    m_capillaryCheck->setChecked(optionText(m_engine, "CAPILLARY_DIFF") == QLatin1String("YES"));
    m_forceCfCheck->setChecked(optionText(m_engine, "FORCE_CLOSED_FORM") == QLatin1String("YES"));
    m_dunneCheck->setChecked(optionText(m_engine, "DUNNE") == QLatin1String("YES"));
    m_cgwSpin->setValue(optionText(m_engine, "C_GW").toDouble());
    m_ccolSpin->setValue(optionText(m_engine, "C_COL").toDouble());

    for (auto it = m_transportOptions.cbegin(); it != m_transportOptions.cend(); ++it) {
        const auto value = optionText(m_engine, ("GW_TRANSPORT:" + it.key()).toUtf8().constData());
        if (auto *box = qobject_cast<QCheckBox *>(it.value()))
            box->setChecked(value == "YES");
        else if (auto *combo = qobject_cast<QComboBox *>(it.value())) {
            int index = combo->findData(value);
            if (index < 0)
                index = combo->findText(value, Qt::MatchFixedString);
            if (index >= 0)
                combo->setCurrentIndex(index);
        } else if (auto *integer = qobject_cast<QSpinBox *>(it.value()))
            integer->setValue(value.toInt());
        else if (auto *number = qobject_cast<QDoubleSpinBox *>(it.value()))
            number->setValue(value.toDouble());
        else if (auto *text = qobject_cast<QLineEdit *>(it.value()))
            text->setText(value);
    }
    auto loadBoundary = [&](const QString &key, QComboBox *kind, QLineEdit *argument,
                            QDoubleSpinBox *depth) {
        const auto tokens = optionText(m_engine, ("GW_TRANSPORT:" + key).toUtf8().constData())
                                .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (tokens.isEmpty())
            return;
        const int index = kind->findData(tokens.first());
        if (index >= 0)
            kind->setCurrentIndex(index);
        argument->clear();
        if (depth)
            depth->setValue(0);
        if (tokens.size() > 1 && tokens[1] != QLatin1String("DEPTH"))
            argument->setText(tokens[1]);
        if (depth)
            for (int i = 2; i + 1 < tokens.size(); ++i)
                if (tokens[i] == QLatin1String("DEPTH")) {
                    depth->setValue(tokens[i + 1].toDouble());
                    break;
                }
    };
    loadBoundary("SURFACE_THERMAL_BC", m_surfaceThermalKind, m_surfaceThermalValue, nullptr);
    loadBoundary("DEEP_THERMAL_BC", m_deepThermalKind, m_deepThermalValue, m_deepThermalDepth);
    m_loadedOptions = readOptions();
    m_displayedOptions = optionDraft();
    m_aquifer->load(m_engine);
    m_nodes->load(m_engine);
    const QStringList tags = meshCellTags(m_engine);
    for (int i = 0; i < m_rowModels.size(); ++i) {
        auto *model = m_rowModels[i];
        model->setProjectUnits(si);
        model->load(m_engine);
        model->setTagChoices(tags);
        if (!model->fileReference().isEmpty()) // [GW_INITIAL_QUALITY] FILE, shown read-only
            m_rowEditors[i]->setNote(tr("Also seeded from FILE %1 (%2 rows); edit that file to "
                                        "change them.")
                                         .arg(model->fileReference())
                                         .arg(model->fileRowCount()));
    }
    m_sources->setTagChoices(tags);
    m_sources->load(m_engine);
    {
        QSignalBlocker block(m_bottomNode);
        m_bottomNode->clear();
        m_bottomNode->addItems(m_nodes->lidNodes());
    }
    updateBottomConnection();
    refreshState();

    // The setter also marks options authored; querying lifecycle avoids a
    // hidden model change when this dialog is only inspected or cancelled.
    // Switched off, the aquifer stays inspectable but read-only (plan §5).
    const bool editable = canEdit(m_engine);
    const QString locked = processLockReason(m_engine, LockableProcess::Groundwater2D);
    setEditable(editable && locked.isEmpty(),
                !editable ? tr("A simulation is running. The aquifer rows seed "
                               "the kernel when it starts, so they cannot be "
                               "changed now — reset the run to edit them. The "
                               "State tab still updates.")
                          : locked);
}

void Mesh2DGroundwaterDialog::setEditable(bool on, const QString &whyNot)
{
    m_editable = on;
    updateBottomConnection();
    if (!on)
        m_buttons->setStandardButtons(QDialogButtonBox::Close);
    m_refreshBtn->setEnabled(m_engine != nullptr);
    for (auto *field : m_transportOptions)
        field->setEnabled(on);
    if (m_boundaries)
        m_boundaries->setEditable(on);
    if (m_boundaryPage) {
        m_boundaryPage->setEnabled(on);
        for (auto *view : m_boundaryPage->findChildren<QTableView *>())
            view->setEditTriggers(on ? QAbstractItemView::AllEditTriggers
                                     : QAbstractItemView::NoEditTriggers);
    }
    for (QWidget *w : std::initializer_list<QWidget *>{
             m_soilCombo, m_closureCombo, m_layersSpin, m_capillaryCheck, m_cgwSpin, m_ccolSpin,
             m_forceCfCheck, m_dunneCheck, m_modeCombo, m_gwEtCombo, m_addRowBtn, m_delRowBtn,
             m_addBedBtn, m_delBedBtn, m_surfaceThermalKind, m_surfaceThermalValue,
             m_deepThermalKind, m_deepThermalValue, m_deepThermalDepth, m_enrolmentCombo})
        if (w)
            w->setEnabled(on);
    refreshThermalGates();
    refreshClockGates();
    if (m_aquiferView)
        m_aquiferView->setEditTriggers(on ? QAbstractItemView::AllEditTriggers
                                          : QAbstractItemView::NoEditTriggers);
    if (m_nodeView)
        m_nodeView->setEditTriggers(on ? QAbstractItemView::AllEditTriggers
                                       : QAbstractItemView::NoEditTriggers);
    for (auto *editor : m_rowEditors)
        editor->setEditable(on);
    if (m_sourcePage) {
        for (auto *button : m_sourcePage->findChildren<QPushButton *>())
            button->setEnabled(on);
        for (auto *view : {m_sourceView, m_termView})
            view->setEditTriggers(on ? QAbstractItemView::AllEditTriggers
                                     : QAbstractItemView::NoEditTriggers);
    }
    if (!whyNot.isEmpty()) {
        m_banner->setText(whyNot);
        m_banner->show();
    } else {
        m_banner->hide();
    }
}

QWidget *Mesh2DGroundwaterDialog::buildBoundaryPage()
{
    m_boundaryPage = new QWidget;
    auto *layout = new QVBoxLayout(m_boundaryPage);
    auto *note = new QLabel(
        tr("Add one Water row for each exterior groundwater edge, then its species rows. HEAD is a water-table elevation in project units; FLOW is total m³/s, positive inward. Potential inflow needs a concentration for every enabled groundwater species. Imposed MASSFLUX and HEATFLUX rows may accompany a concentration."),
        m_boundaryPage);
    note->setWordWrap(true);
    layout->addWidget(note);
    m_boundaries = new Mesh2DAquiferBoundaryModel(this);
    m_boundaries->setObjectName("gwBoundaryModel");
    auto *view = new QTableView(m_boundaryPage);
    view->setObjectName("gwBoundaryTable");
    view->setAccessibleName(tr("Groundwater hydraulic and chemistry boundaries"));
    view->setModel(m_boundaries);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    view->horizontalHeader()->setStretchLastSection(true);
    // Rows are added by kind (hydraulic edge / species row), so a paste
    // only fills existing rows.
    openswmmvis::ui::TableClipboard::install(view);
    layout->addWidget(view);
    auto *buttons = new QHBoxLayout;
    auto *water = new QPushButton(tr("Add hydraulic edge"), m_boundaryPage);
    auto *chemistry = new QPushButton(tr("Add species row"), m_boundaryPage);
    auto *remove = new QPushButton(tr("Remove row"), m_boundaryPage);
    water->setObjectName("gwBoundaryAddWater");
    chemistry->setObjectName("gwBoundaryAddSpecies");
    remove->setObjectName("gwBoundaryRemove");
    for (auto *button : {water, chemistry, remove}) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
    buttons->addWidget(water);
    buttons->addWidget(chemistry);
    buttons->addWidget(remove);
    buttons->addStretch();
    layout->addLayout(buttons);
    connect(water, &QPushButton::clicked, m_boundaries, &Mesh2DAquiferBoundaryModel::addHydraulic);
    connect(chemistry, &QPushButton::clicked, m_boundaries,
            &Mesh2DAquiferBoundaryModel::addChemistry);
    connect(remove, &QPushButton::clicked, this,
            [this, view] { m_boundaries->remove(view->currentIndex().row()); });
    m_boundaries->load(m_engine);
    return m_boundaryPage;
}

QWidget *Mesh2DGroundwaterDialog::addRowPage(Mesh2DSectionRowModel *model, const QString &hint)
{
    auto *editor = new Mesh2DSectionRowEditor(model, hint, this);
    m_rowModels.append(model);
    m_rowEditors.append(editor);
    return editor;
}

QWidget *Mesh2DGroundwaterDialog::buildSourcesPage()
{
    m_sourcePage = new QWidget(this);
    auto *layout = new QVBoxLayout(m_sourcePage);
    auto *hint = new QLabel(
        tr("Named wells, injections and extractions. Flow is total m³/s, positive into the "
           "aquifer; a source on all cells (*) or a tag is shared among its active cells by "
           "area. An XY source is placed in the cell whose centroid is nearest; choose XY in "
           "Scope to locate one by coordinates. Species terms inject a concentration (CONC) or "
           "add mass per second (MASS)."),
        m_sourcePage);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto table = [](QTableView *view, QAbstractItemModel *model, const QString &name) {
        view->setModel(model);
        view->setItemDelegate(new Mesh2DChoiceDelegate(view));
        view->setAccessibleName(name);
        view->setAccessibleDescription(
            tr("Select a row with the arrow keys. Press F2 to edit the current cell."));
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        view->horizontalHeader()->setStretchLastSection(true);
    };
    auto button = [](const QString &text, const char *name, QWidget *parent) {
        auto *b = new QPushButton(text, parent);
        b->setObjectName(QString::fromLatin1(name));
        b->setAutoDefault(false);
        return b;
    };

    m_sources = new Mesh2DGwSourceModel(this);
    m_sources->setObjectName(QStringLiteral("gwSources"));
    m_sourceView = new QTableView(m_sourcePage);
    m_sourceView->setObjectName(QStringLiteral("gwSourcesTable"));
    table(m_sourceView, m_sources, tr("Groundwater sources"));
    openswmmvis::ui::TableClipboardOptions sourceClip;
    sourceClip.appendRow = [this] { return m_sources->appendSource() >= 0; };
    openswmmvis::ui::TableClipboard::install(m_sourceView, sourceClip);
    layout->addWidget(m_sourceView, 1);

    auto *row = new QHBoxLayout;
    auto *add = button(tr("Add source"), "gwSourceAdd", m_sourcePage);
    auto *remove = button(tr("Remove source"), "gwSourceRemove", m_sourcePage);
    for (auto *b : {add, remove})
        row->addWidget(b);
    row->addStretch(1);
    layout->addLayout(row);

    auto *termsBox = new QGroupBox(tr("Species terms of the selected source"), m_sourcePage);
    auto *termsLayout = new QVBoxLayout(termsBox);
    m_sourceTerms = new Mesh2DGwSourceTermModel(m_sources, this);
    m_sourceTerms->setObjectName(QStringLiteral("gwSourceTerms"));
    m_termView = new QTableView(termsBox);
    m_termView->setObjectName(QStringLiteral("gwSourceTermsTable"));
    table(m_termView, m_sourceTerms, tr("Species terms of the selected groundwater source"));
    openswmmvis::ui::TableClipboardOptions termClip;
    termClip.appendRow = [this] { return m_sourceTerms->appendTerm() >= 0; };
    openswmmvis::ui::TableClipboard::install(m_termView, termClip);
    termsLayout->addWidget(m_termView);
    auto *termRow = new QHBoxLayout;
    auto *addTerm = button(tr("Add term"), "gwSourceTermAdd", termsBox);
    auto *removeTerm = button(tr("Remove term"), "gwSourceTermRemove", termsBox);
    termRow->addWidget(addTerm);
    termRow->addWidget(removeTerm);
    termRow->addStretch(1);
    termsLayout->addLayout(termRow);
    layout->addWidget(termsBox, 1);

    const auto updateSelection = [this] {
        m_sourceTerms->setSourceRow(m_sourceView->currentIndex().row());
    };
    connect(m_sourceView->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            updateSelection);
    connect(m_sources, &QAbstractItemModel::modelReset, this, updateSelection);
    connect(add, &QPushButton::clicked, this, [this] {
        const int at = m_sources->appendSource();
        m_sourceView->selectRow(at);
        m_sourceView->scrollTo(m_sources->index(at, 0));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int r = m_sourceView->currentIndex().row();
        if (r >= 0)
            m_sources->removeRow(r);
    });
    connect(addTerm, &QPushButton::clicked, this, [this] {
        const int at = m_sourceTerms->appendTerm();
        if (at >= 0)
            m_termView->selectRow(at);
    });
    connect(removeTerm, &QPushButton::clicked, this,
            [this] { m_sourceTerms->removeTerm(m_termView->currentIndex().row()); });
    return m_sourcePage;
}

QWidget *Mesh2DGroundwaterDialog::buildTransportPage()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    auto *note = new QLabel(
        tr("These are saved groundwater transport settings. They apply only with an active mesh aquifer and the corresponding project process. "
           "Initial values and injection/extraction sources have their own tabs; Assign Groundwater sets them for selected cells. Porous soil heat is opt-in and requires the porous-equilibrium model, active 2D groundwater, heat transport and groundwater temperature transport. "
           "Thermal settings remain saved while those parent processes are off."),
        content);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *form = new QFormLayout;
    const QStringList keys = {"TRANSPORT_POLLUTANTS", "TRANSPORT_MSX", "TRANSPORT_AGE",
                              "TRANSPORT_TEMPERATURE", "DISPERSION"};
    const QStringList labels = {tr("Carry pollutants"), tr("Carry reaction species"),
                                tr("Carry water age"), tr("Carry temperature"),
                                tr("Solute dispersion")};
    for (int i = 0; i < keys.size(); ++i) {
        auto *box = new QCheckBox(labels[i], content);
        box->setObjectName("gwTransport_" + keys[i]);
        m_transportOptions[keys[i]] = box;
        form->addRow(box);
    }
    auto *chemistry = new QComboBox(content);
    chemistry->setObjectName("gwTransport_UNSAT_TRANSPORT");
    chemistry->addItem(tr("Well-mixed column"), "BULK");
    chemistry->addItem(tr("Separate soil layers"), "LAYERED");
    chemistry->setToolTip(tr(
        "Separate chemistry follows the SIGMA water layers. Choose SIGMA as the aquifer closure. This profile currently uses water-only temperature storage."));
    chemistry->setAccessibleName(tr("Unsaturated chemistry"));
    m_transportOptions["UNSAT_TRANSPORT"] = chemistry;
    form->addRow(tr("Unsaturated &chemistry:"), chemistry);
    auto *diffusion = new QDoubleSpinBox(content);
    diffusion->setObjectName("gwTransport_C_DIFF");
    diffusion->setRange(1e-6, 1);
    diffusion->setDecimals(6);
    diffusion->setKeyboardTracking(false);
    diffusion->setAccessibleName(tr("Diffusion safety factor"));
    diffusion->setToolTip(tr(
        "Controls the explicit solute diffusion timestep and the porous thermal diffusion bound."));
    m_transportOptions["C_DIFF"] = diffusion;
    form->addRow(tr("Diffusion safety &factor:"), diffusion);
    connect(qobject_cast<QCheckBox *>(m_transportOptions.value("DISPERSION")), &QCheckBox::toggled,
            this, &Mesh2DGroundwaterDialog::refreshThermalGates);
    layout->addLayout(form);

    auto *thermalBox = new QGroupBox(tr("Porous soil heat and conduction"), content);
    auto *thermalForm = new QFormLayout(thermalBox);
    const auto setThermalAccessibility = [](QWidget *field, QString label,
                                            const QString &description = QString()) {
        label.remove('&');
        field->setAccessibleName(label.trimmed());
        if (!description.isEmpty())
            field->setAccessibleDescription(description);
    };
    auto addCombo = [&](const QString &key, const QString &label, const QStringList &names,
                        const QStringList &values) {
        auto *combo = new QComboBox(thermalBox);
        combo->setObjectName("gwTransport_" + key);
        for (int i = 0; i < names.size(); ++i)
            combo->addItem(names[i], values[i]);
        setThermalAccessibility(combo, label, combo->toolTip());
        m_transportOptions[key] = combo;
        m_thermalFields.push_back(combo);
        thermalForm->addRow(label, combo);
        return combo;
    };
    auto addDouble = [&](const QString &key, const QString &label, double minimum, double maximum,
                         int decimals, const QString &suffix) {
        auto *spin = new QDoubleSpinBox(thermalBox);
        spin->setObjectName("gwTransport_" + key);
        spin->setRange(minimum, maximum);
        spin->setDecimals(decimals);
        spin->setKeyboardTracking(false);
        spin->setSuffix(suffix);
        setThermalAccessibility(spin, label, spin->toolTip());
        m_transportOptions[key] = spin;
        m_thermalFields.push_back(spin);
        thermalForm->addRow(label, spin);
        return spin;
    };
    addCombo("THERMAL_MODEL", tr("Soil heat &model:"),
             {tr("Water temperature only"), tr("Porous soil heat storage")},
             {"WATER_ONLY", "POROUS_EQUILIBRIUM"});
    addCombo("CONDUCTION", tr("Vertical soil conduc&tion:"), {tr("Off"), tr("On")}, {"NO", "YES"});
    addCombo("THERMAL_MIXING", tr("Conductivity mi&xing:"), {tr("Arithmetic"), tr("Geometric")},
             {"ARITHMETIC", "GEOMETRIC"});
    auto *lateral =
        new QCheckBox(tr("Enable lateral conduction between saturated cells"), thermalBox);
    lateral->setObjectName("gwTransport_LATERAL_CONDUCTION");
    setThermalAccessibility(lateral, lateral->text());
    m_transportOptions["LATERAL_CONDUCTION"] = lateral;
    m_thermalFields.push_back(lateral);
    thermalForm->addRow(QString(), lateral);
    auto *soilEt = addCombo("SOIL_ET_HEAT", tr("Soil ET heat treat&ment:"),
                            {tr("Latent heat at surface"), tr("Sensible energy only")},
                            {"LATENT_SURFACE", "SENSIBLE_ONLY"});
    soilEt->setToolTip(tr(
        "LATENT_SURFACE debits the latent heat of accepted soil ET from the surface boundary. SENSIBLE_ONLY carries water sensible heat without that latent debit."));
    soilEt->setAccessibleDescription(soilEt->toolTip());
    const QString layerLabel = tr("Vertical thermal resolu&tion:");
    auto *layers = new QSpinBox(thermalBox);
    layers->setObjectName("gwTransport_THERMAL_LAYERS");
    layers->setRange(1, 128);
    layers->setSuffix(tr(" layers"));
    layers->setToolTip(
        tr("Number of fixed thermal cells from the surface to the bottom of the soil profile."));
    setThermalAccessibility(layers, layerLabel, layers->toolTip());
    m_transportOptions["THERMAL_LAYERS"] = layers;
    m_thermalFields.push_back(layers);
    thermalForm->addRow(layerLabel, layers);
    addDouble("THERMAL_MAX_STEP", tr("Maximum thermal &step:"), 1e-6, 1e9, 6, tr(" s"));
    addDouble("WATER_THERMAL_CONDUCTIVITY", tr("&Water thermal conductivity:"), 1e-9, 1e6, 8,
              tr(" W/m/K"));
    addDouble("AIR_THERMAL_CONDUCTIVITY", tr("&Air thermal conductivity:"), 1e-9, 1e6, 8,
              tr(" W/m/K"));
    addDouble("SURFACE_THERMAL_RESISTANCE", tr("Surface thermal resist&ance:"), 1e-9, 1e12, 8,
              tr(" m² K/W"));

    auto *surfaceKind = new QComboBox(thermalBox);
    surfaceKind->setObjectName("gwTransport_SURFACE_THERMAL_BC_KIND");
    const QStringList surfaceModes = {"ADIABATIC",  "AIR",       "SURFACE_WATER",  "FIXED",
                                      "TIMESERIES", "HEAT_FLUX", "FLUX_TIMESERIES"};
    const QStringList surfaceNames = {tr("Insulated"),
                                      tr("Air temperature"),
                                      tr("Surface-water temperature"),
                                      tr("Fixed temperature"),
                                      tr("Temperature time series"),
                                      tr("Fixed heat flux"),
                                      tr("Heat-flux time series")};
    for (int i = 0; i < surfaceModes.size(); ++i)
        surfaceKind->addItem(surfaceNames[i], surfaceModes[i]);
    const QString surfaceKindLabel = tr("Surface &boundary:");
    m_surfaceThermalKind = surfaceKind;
    setThermalAccessibility(surfaceKind, surfaceKindLabel);
    m_thermalFields.push_back(surfaceKind);
    thermalForm->addRow(surfaceKindLabel, surfaceKind);
    const QString surfaceValueLabel = tr("Surface boundary value / series:");
    m_surfaceThermalValue = new QLineEdit(thermalBox);
    m_surfaceThermalValue->setObjectName("gwTransport_SURFACE_THERMAL_BC_VALUE");
    m_surfaceThermalValue->setToolTip(tr(
        "Temperature in °C or heat flux in W/m² for numeric modes; otherwise enter a time-series name."));
    setThermalAccessibility(m_surfaceThermalValue, surfaceValueLabel,
                            m_surfaceThermalValue->toolTip());
    m_thermalFields.push_back(m_surfaceThermalValue);
    thermalForm->addRow(surfaceValueLabel, m_surfaceThermalValue);

    auto *deepKind = new QComboBox(thermalBox);
    deepKind->setObjectName("gwTransport_DEEP_THERMAL_BC_KIND");
    const QStringList deepModes = {"ADIABATIC", "GEOTHERMAL_FLUX", "FIXED_TEMP", "TIMESERIES",
                                   "FLUX_TIMESERIES"};
    const QStringList deepNames = {tr("Insulated"), tr("Geothermal heat flux"),
                                   tr("Fixed temperature"), tr("Temperature time series"),
                                   tr("Heat-flux time series")};
    for (int i = 0; i < deepModes.size(); ++i)
        deepKind->addItem(deepNames[i], deepModes[i]);
    const QString deepKindLabel = tr("&Deep boundary:");
    m_deepThermalKind = deepKind;
    setThermalAccessibility(deepKind, deepKindLabel);
    m_thermalFields.push_back(deepKind);
    thermalForm->addRow(deepKindLabel, deepKind);
    const QString deepValueLabel = tr("Deep boundary value / series:");
    m_deepThermalValue = new QLineEdit(thermalBox);
    m_deepThermalValue->setObjectName("gwTransport_DEEP_THERMAL_BC_VALUE");
    m_deepThermalValue->setToolTip(tr(
        "Temperature in °C or flux in W/m² for numeric modes; otherwise enter a time-series name."));
    setThermalAccessibility(m_deepThermalValue, deepValueLabel, m_deepThermalValue->toolTip());
    m_thermalFields.push_back(m_deepThermalValue);
    thermalForm->addRow(deepValueLabel, m_deepThermalValue);
    const QString deepDepthLabel = tr("Boundary &depth:");
    m_deepThermalDepth = new QDoubleSpinBox(thermalBox);
    m_deepThermalDepth->setObjectName("gwTransport_DEEP_THERMAL_BC_DEPTH");
    m_deepThermalDepth->setRange(0, 1e9);
    m_deepThermalDepth->setDecimals(6);
    m_deepThermalDepth->setSuffix(tr(" m"));
    m_deepThermalDepth->setSpecialValueText(tr("Use groundwater bottom"));
    setThermalAccessibility(m_deepThermalDepth, deepDepthLabel, m_deepThermalDepth->toolTip());
    m_thermalFields.push_back(m_deepThermalDepth);
    thermalForm->addRow(deepDepthLabel, m_deepThermalDepth);
    auto updateBoundaryArguments = [this] {
        const auto surface = m_surfaceThermalKind->currentData().toString();
        const bool surfaceArg = surface == "FIXED" || surface == "TIMESERIES" ||
                                surface == "HEAT_FLUX" || surface == "FLUX_TIMESERIES";
        const auto model = qobject_cast<QComboBox *>(m_transportOptions.value("THERMAL_MODEL"));
        const bool porous = model && model->currentData().toString() == "POROUS_EQUILIBRIUM";
        m_surfaceThermalValue->setEnabled(m_editable && m_porousThermalOptionsSupported && porous &&
                                          surfaceArg);
        m_surfaceThermalValue->setPlaceholderText(surface == "TIMESERIES" ? tr("Series name")
                                                  : surface == "FLUX_TIMESERIES"
                                                      ? tr("Flux series name")
                                                  : surface == "HEAT_FLUX" ? tr("W/m²")
                                                                           : tr("°C"));
        const auto deep = m_deepThermalKind->currentData().toString();
        const bool deepArg = deep != "ADIABATIC";
        m_deepThermalValue->setEnabled(m_editable && m_porousThermalOptionsSupported && porous &&
                                       deepArg);
        m_deepThermalDepth->setEnabled(m_editable && m_porousThermalOptionsSupported && porous &&
                                       deepArg);
        m_deepThermalValue->setPlaceholderText(deep == "TIMESERIES"        ? tr("Series name")
                                               : deep == "FLUX_TIMESERIES" ? tr("Flux series name")
                                               : deep == "GEOTHERMAL_FLUX" ? tr("W/m²")
                                                                           : tr("°C"));
    };
    connect(surfaceKind, &QComboBox::currentIndexChanged, this, updateBoundaryArguments);
    connect(deepKind, &QComboBox::currentIndexChanged, this, updateBoundaryArguments);
    updateBoundaryArguments();
    m_thermalStatus = new QLabel(thermalBox);
    m_thermalStatus->setObjectName("gwThermalActivationStatus");
    m_thermalStatus->setAccessibleName(tr("Porous soil heat run status"));
    m_thermalStatus->setWordWrap(true);
    thermalForm->addRow(tr("Run status:"), m_thermalStatus);
    connect(static_cast<QComboBox *>(m_transportOptions.value("THERMAL_MODEL")),
            &QComboBox::currentIndexChanged, this, &Mesh2DGroundwaterDialog::refreshThermalGates);
    connect(static_cast<QComboBox *>(m_transportOptions.value("CONDUCTION")),
            &QComboBox::currentIndexChanged, this, &Mesh2DGroundwaterDialog::refreshThermalGates);
    connect(static_cast<QCheckBox *>(m_transportOptions.value("TRANSPORT_TEMPERATURE")),
            &QCheckBox::toggled, this, &Mesh2DGroundwaterDialog::refreshThermalGates);
    layout->addWidget(thermalBox);
    layout->addStretch();
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    return scroll;
}

void Mesh2DGroundwaterDialog::refreshThermalGates()
{
    auto *model = qobject_cast<QComboBox *>(m_transportOptions.value("THERMAL_MODEL"));
    if (!model || !m_thermalStatus)
        return;
    const bool porous = model->currentData().toString() == "POROUS_EQUILIBRIUM";
    const auto *dispersion = qobject_cast<QCheckBox *>(m_transportOptions.value("DISPERSION"));
    if (auto *factor = m_transportOptions.value("C_DIFF"))
        factor->setEnabled(m_editable && (porous || (dispersion && dispersion->isChecked())));
    const auto setStatus = [this](const QString &text) {
        m_thermalStatus->setText(text);
        m_thermalStatus->setAccessibleDescription(text);
    };
    for (auto *field : m_thermalFields)
        if (field)
            field->setEnabled(m_editable && m_porousThermalOptionsSupported);
    if (!m_engine) {
        setStatus(tr("Unavailable until a model is open."));
        return;
    }
    if (!m_porousThermalOptionsSupported) {
        setStatus(tr(
            "Unavailable: this engine does not expose the porous-soil heat option. These controls are disabled because this engine cannot validate or apply them."));
        return;
    }
    for (auto it = m_transportOptions.cbegin(); it != m_transportOptions.cend(); ++it) {
        if (it.key() == "THERMAL_MODEL" || it.key().startsWith("TRANSPORT_") ||
            it.key() == "DISPERSION" || it.key() == "UNSAT_TRANSPORT" || it.key() == "C_DIFF")
            continue;
        it.value()->setEnabled(m_editable && porous);
    }
    const auto surface = m_surfaceThermalKind->currentData().toString();
    const auto deep = m_deepThermalKind->currentData().toString();
    const bool surfaceArg = surface == "FIXED" || surface == "TIMESERIES" ||
                            surface == "HEAT_FLUX" || surface == "FLUX_TIMESERIES";
    m_surfaceThermalKind->setEnabled(m_editable && porous);
    m_surfaceThermalValue->setEnabled(m_editable && porous && surfaceArg);
    m_deepThermalKind->setEnabled(m_editable && porous);
    m_deepThermalValue->setEnabled(m_editable && porous && deep != "ADIABATIC");
    m_deepThermalDepth->setEnabled(m_editable && porous && deep != "ADIABATIC");
    if (!porous) {
        setStatus(tr(
            "Inactive: Water temperature only is selected. Porous heat settings are retained for later use."));
        return;
    }
    QStringList inactive;
    char groundwater[64]{};
    if (swmm_options_get_ext(m_engine, "GROUNDWATER", groundwater, sizeof groundwater) != SWMM_OK ||
        QString::fromUtf8(groundwater).compare("NO", Qt::CaseInsensitive) == 0)
        inactive.append(tr("2D groundwater is off"));
    int heat = 0;
    if (swmm_heat_get_enabled(m_engine, &heat) != SWMM_OK || !heat)
        inactive.append(tr("heat transport is off"));
    const auto *temperature =
        qobject_cast<QCheckBox *>(m_transportOptions.value("TRANSPORT_TEMPERATURE"));
    if (!temperature || !temperature->isChecked())
        inactive.append(tr("groundwater temperature transport is off"));
    if (!inactive.isEmpty())
        setStatus(
            tr("Inactive until %1. These settings are saved and do not activate a disabled parent process.")
                .arg(inactive.join(tr("; "))));
    else {
        const auto *conduction = qobject_cast<QComboBox *>(m_transportOptions.value("CONDUCTION"));
        if (!conduction || conduction->currentData().toString() == "NO")
            setStatus(tr(
                "Porous heat transport is ready for covered aquifer cells; vertical conduction is turned off."));
        else
            setStatus(tr(
                "Ready for covered aquifer cells. Actual activation still depends on mesh-to-aquifer coverage and valid material rows."));
    }
}

QMap<QString, QString> Mesh2DGroundwaterDialog::optionDraft() const
{
    QMap<QString, QString> values{{"SOIL_CHAR", m_soilCombo->currentText()},
                                  {"CLOSURE", m_closureCombo->currentText()},
                                  {"M_LAYERS", QString::number(m_layersSpin->value())},
                                  {"CAPILLARY_DIFF", m_capillaryCheck->isChecked() ? "YES" : "NO"},
                                  {"C_GW", QString::number(m_cgwSpin->value(), 'g', 17)},
                                  {"C_COL", QString::number(m_ccolSpin->value(), 'g', 17)},
                                  {"FORCE_CLOSED_FORM", m_forceCfCheck->isChecked() ? "YES" : "NO"},
                                  {"DUNNE", m_dunneCheck->isChecked() ? "YES" : "NO"},
                                  {"MODE", m_modeCombo->currentText()},
                                  {"GW_ET", m_gwEtCombo->currentData().toString()},
                                  {"NODE_ENROLMENT", m_enrolmentCombo->currentData().toString()}};
    if (m_clockOptionsSupported) {
        values["GW_CLOCK"] = m_clockCombo->currentData().toString();
        for (auto it = m_clockNumbers.cbegin(); it != m_clockNumbers.cend(); ++it)
            values[it.key()] = QString::number(it.value()->value(), 'g', 17);
        values["GW_BINDS_SURFACE_DT"] = m_clockCombo->currentData() == QStringLiteral("SHARED") ||
                                                m_bindsSurfaceCheck->isChecked()
                                            ? "YES"
                                            : "NO";
    }
    for (auto it = m_transportOptions.cbegin(); it != m_transportOptions.cend(); ++it) {
        QString value;
        if (auto *box = qobject_cast<QCheckBox *>(it.value()))
            value = box->isChecked() ? "YES" : "NO";
        else if (auto *combo = qobject_cast<QComboBox *>(it.value()))
            value = combo->currentData().toString();
        else if (auto *integer = qobject_cast<QSpinBox *>(it.value()))
            value = QString::number(integer->value());
        else if (auto *number = qobject_cast<QDoubleSpinBox *>(it.value()))
            value = QString::number(number->value(), 'g', 17);
        else if (auto *text = qobject_cast<QLineEdit *>(it.value()))
            value = text->text().trimmed();
        values["GW_TRANSPORT:" + it.key()] = value;
    }
    auto surface = m_surfaceThermalKind->currentData().toString();
    const auto surfaceArg = m_surfaceThermalValue->text().trimmed();
    if ((surface == "FIXED" || surface == "TIMESERIES" || surface == "HEAT_FLUX" ||
         surface == "FLUX_TIMESERIES") &&
        !surfaceArg.isEmpty())
        surface += QLatin1Char(' ') + surfaceArg;
    values["GW_TRANSPORT:SURFACE_THERMAL_BC"] = surface;
    auto deep = m_deepThermalKind->currentData().toString();
    const auto deepArg = m_deepThermalValue->text().trimmed();
    if (deep != "ADIABATIC" && !deepArg.isEmpty())
        deep += QLatin1Char(' ') + deepArg;
    if (deep != "ADIABATIC" && m_deepThermalDepth->value() > 0)
        deep += QStringLiteral(" DEPTH ") + QString::number(m_deepThermalDepth->value(), 'g', 17);
    values["GW_TRANSPORT:DEEP_THERMAL_BC"] = deep;
    return values;
}
QMap<QString, QString> Mesh2DGroundwaterDialog::readOptions() const
{
    QMap<QString, QString> values;
    for (const auto &key : optionDraft().keys())
        values[key] = optionText(m_engine, key.toUtf8().constData());
    return values;
}
QString Mesh2DGroundwaterDialog::applyOptions()
{
    const auto draft = optionDraft();
    const bool clockChanged = draft.value("GW_CLOCK") != m_displayedOptions.value("GW_CLOCK");
    for (auto it = draft.cbegin(); it != draft.cend(); ++it)
        // SHARED reports an effective YES even when its stored binding is NO.
        // Publish the staged binding on a clock transition as well.
        if (it.value() != m_displayedOptions.value(it.key()) ||
            (clockChanged && it.key() == QLatin1String("GW_BINDS_SURFACE_DT")))
            if (!setOption(m_engine, it.key().toUtf8().constData(), it.value()))
                return tr("The engine refused %1 = %2.").arg(it.key(), it.value());
    return {};
}

void Mesh2DGroundwaterDialog::onApply()
{
    applyChanges();
}

bool Mesh2DGroundwaterDialog::applyChanges()
{
    if (!m_engine || !m_editable)
        return false;
    if (!canEdit(m_engine)) {
        QMessageBox::warning(
            this, tr("2D Groundwater"),
            tr("Reset the simulation before applying edits. Your draft remains here."));
        return false;
    }
    if (const QString locked = processLockReason(m_engine, LockableProcess::Groundwater2D);
        !locked.isEmpty()) {
        QMessageBox::warning(this, tr("2D Groundwater"),
                             locked + QLatin1Char(' ') + tr("Your draft remains here."));
        return false;
    }
    if (auto *focus = focusWidget())
        focus->clearFocus();
    const auto draft = optionDraft();
    const bool optionsChanged = draft != m_displayedOptions, aqChanged = m_aquifer->isDirty(),
               nodesChanged = m_nodes->isDirty(), edgesChanged = m_boundaries->isDirty();
    QList<Mesh2DSectionRowModel *> rowsChanged;
    for (auto *model : m_rowModels)
        if (model->isDirty())
            rowsChanged.append(model);
    const bool sourcesChanged = m_sources->isDirty();
    if (!optionsChanged && !aqChanged && !nodesChanged && !edgesChanged && rowsChanged.isEmpty() &&
        !sourcesChanged)
        return true;
    // Sources and the transport/link rows are independent of the aquifer rows, so they are
    // checked against the live engine like the edge table.
    const auto validateRows = [&] {
        QString problem = sourcesChanged ? m_sources->validateCommit(m_engine) : QString();
        for (auto *model : rowsChanged)
            if (problem.isEmpty())
                problem = model->validateCommit(m_engine);
        return problem;
    };
    QString error;
    if (draft.value("GW_TRANSPORT:UNSAT_TRANSPORT") == "LAYERED") {
        if (draft.value("CLOSURE") != "SIGMA")
            error = tr("Choose SIGMA as the aquifer closure for separate soil-layer chemistry.");
    }
    if (readOptions() != m_loadedOptions)
        error = tr(
            "Groundwater options changed outside this editor. Reopen it to preserve newer edits.");
    if (error.isEmpty())
        error = m_aquifer->validateCommit(m_engine);
    if (error.isEmpty())
        error = m_nodes->validateCommit(m_engine, !optionsChanged && !aqChanged);
    if (error.isEmpty() && edgesChanged)
        error = m_boundaries->validateCommit(m_engine, !optionsChanged && !aqChanged);
    if (error.isEmpty())
        error = validateRows();
    if (error.isEmpty() && optionsChanged) {
        SWMM_Engine check = swmm_engine_new();
        if (!check)
            error = tr("Cannot allocate option validation model.");
        else {
            for (auto it = draft.cbegin(); it != draft.cend(); ++it)
                if (it.value() != m_displayedOptions.value(it.key()) &&
                    !setOption(check, it.key().toUtf8().constData(), it.value())) {
                    error = tr("Invalid groundwater option %1.").arg(it.key());
                    break;
                }
            swmm_engine_destroy(check);
        }
    }
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("2D Groundwater"), error);
        return false;
    }
    QPointer<Mesh2DGroundwaterDialog> self = this;
    emit changesMayHaveBeenApplied();
    if (!self || !m_engine || !canEdit(m_engine))
        return false;
    // Dirty observers may close or modify a model synchronously. Recheck after
    // that signal, before the first write, without restoring over their edits.
    if (readOptions() != m_loadedOptions)
        error = tr("Groundwater options changed before commit.");
    if (error.isEmpty())
        error = m_aquifer->validateCommit(m_engine);
    if (error.isEmpty())
        error = m_nodes->validateCommit(m_engine, !optionsChanged && !aqChanged);
    if (error.isEmpty() && edgesChanged)
        error = m_boundaries->validateCommit(m_engine, !optionsChanged && !aqChanged);
    if (error.isEmpty())
        error = validateRows();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("2D Groundwater"), error);
        return false;
    }
    error = applyOptions();
    if (error.isEmpty())
        error = m_aquifer->commit(m_engine, false);
    if (error.isEmpty())
        error = m_nodes->commit(m_engine, false);
    if (error.isEmpty() && edgesChanged)
        error = m_boundaries->commit(m_engine, false);
    if (error.isEmpty() && sourcesChanged)
        error = m_sources->commit(m_engine, false);
    for (auto *model : rowsChanged)
        if (error.isEmpty())
            error = model->commit(m_engine, false);
    if (!error.isEmpty()) {
        QStringList failed;
        for (auto *model : rowsChanged) {
            auto e = model->restoreLoaded(m_engine);
            if (!e.isEmpty())
                failed.append(e);
        }
        if (sourcesChanged) {
            auto e = m_sources->restoreLoaded(m_engine);
            if (!e.isEmpty())
                failed.append(e);
        }
        if (edgesChanged) {
            auto e = m_boundaries->restoreLoaded(m_engine);
            if (!e.isEmpty())
                failed.append(e);
        }
        if (aqChanged) {
            auto e = m_aquifer->restoreLoaded(m_engine);
            if (!e.isEmpty())
                failed.append(e);
        }
        if (nodesChanged) {
            auto e = m_nodes->restoreLoaded(m_engine);
            if (!e.isEmpty())
                failed.append(e);
        }
        for (auto it = m_loadedOptions.cbegin(); it != m_loadedOptions.cend(); ++it)
            if (draft.value(it.key()) != m_displayedOptions.value(it.key()) &&
                !setOption(m_engine, it.key().toUtf8().constData(), it.value()))
                failed.append(it.key());
        error +=
            failed.isEmpty()
                ? tr(" Previous values restored; your draft remains available.")
                : tr(" Rollback incomplete: %1. Reload the saved model before further editing.")
                      .arg(failed.join(", "));
        QMessageBox::warning(this, tr("2D Groundwater"), error);
        return false;
    }
    if (nodesChanged && m_nodeUndo)
        m_nodeUndo->push(new NodeConnectionUndo(m_nodes->transactionCopy(), m_engine, m_undoCurrent,
                                                m_undoEdited));
    if (edgesChanged && m_nodeUndo)
        m_nodeUndo->push(new BoundaryUndo(m_boundaries->transactionCopy(), m_engine, m_undoCurrent,
                                          m_undoEdited));
    m_boundaries->acceptChanges();
    m_aquifer->acceptChanges();
    m_nodes->acceptChanges();
    if (sourcesChanged)
        m_sources->acceptChanges();
    for (auto *model : rowsChanged)
        model->acceptChanges();
    m_loadedOptions = readOptions();
    m_displayedOptions = draft;
    refreshState();
    emit applied();
    return true;
}

void Mesh2DGroundwaterDialog::setNodeUndoStack(QUndoStack *stack, std::function<bool()> current,
                                               std::function<void()> edited)
{
    m_nodeUndo = stack;
    m_undoCurrent = std::move(current);
    m_undoEdited = std::move(edited);
}
void Mesh2DGroundwaterDialog::focusNode(const QString &node)
{
    m_tabs->setCurrentIndex(2);
    const int at = m_bottomNode->findText(node);
    if (at >= 0)
        m_bottomNode->setCurrentIndex(at);
    const int row = m_nodes->bedRow(node);
    if (row >= 0)
        m_nodeView->selectRow(row);
    updateBottomConnection();
}
void Mesh2DGroundwaterDialog::setTargetEdges(const QList<QPair<int, int>> &edges)
{
    m_targetEdges = edges;
    if (edges.isEmpty() || !m_boundaryPage)
        return;
    m_tabs->setCurrentWidget(m_boundaryPage);
    if (!m_targetSummary) {
        auto *bar = new QHBoxLayout;
        m_targetSummary = new QLabel(m_boundaryPage);
        m_targetSummary->setObjectName("gwBoundaryTargetSummary");
        m_targetSummary->setWordWrap(true);
        m_setForTargets = new QPushButton(tr("Set for Eligible Edges…"), m_boundaryPage);
        m_setForTargets->setObjectName("gwBoundarySetForTargets");
        m_setForTargets->setAutoDefault(false);
        bar->addWidget(m_targetSummary, 1);
        bar->addWidget(m_setForTargets);
        static_cast<QVBoxLayout *>(m_boundaryPage->layout())->insertLayout(1, bar);
        connect(m_setForTargets, &QPushButton::clicked, this,
                &Mesh2DGroundwaterDialog::setForTargetEdges);
    }
    refreshTargetEdges();
}

void Mesh2DGroundwaterDialog::refreshTargetEdges()
{
    if (!m_targetSummary || !m_engine)
        return;
    const auto judged = Mesh2DAquiferBoundaryModel::eligibility(m_engine, m_targetEdges);
    QStringList reasons;
    if (judged.interior)
        reasons << tr("%1 interior").arg(judged.interior);
    if (judged.uncovered)
        reasons << tr("%1 not under an active aquifer row").arg(judged.uncovered);
    if (judged.invalid)
        reasons << tr("%1 not in this mesh").arg(judged.invalid);
    QString text = !judged.error.isEmpty()
                       ? judged.error
                       : tr("Selected edges: %1. Can take an aquifer boundary: %2.")
                             .arg(m_targetEdges.size())
                             .arg(judged.eligible.size());
    if (judged.error.isEmpty() && !reasons.isEmpty())
        text += QLatin1Char(' ') + tr("Excluded: %1.").arg(reasons.join(QStringLiteral(", ")));
    m_targetSummary->setText(text);
    m_setForTargets->setEnabled(m_editable && !judged.eligible.isEmpty());
    m_setForTargets->setToolTip(
        judged.eligible.isEmpty()
            ? tr("None of the selected edges is an exterior edge under an active aquifer row.")
            : tr("Stage one boundary on the eligible edges (%1); Apply writes it.")
                  .arg(judged.eligible.size()));
    // Show the target's records: select every row on a target edge.
    auto *view = m_boundaryPage->findChild<QTableView *>("gwBoundaryTable");
    if (!view || !view->selectionModel())
        return;
    QItemSelection rows;
    for (int row = 0; row < m_boundaries->rowCount(); ++row) {
        const QPair<int, int> edge{
            m_boundaries->index(row, Mesh2DAquiferBoundaryModel::Cell).data(Qt::EditRole).toInt() - 1,
            m_boundaries->index(row, Mesh2DAquiferBoundaryModel::Edge).data(Qt::EditRole).toInt()};
        if (m_targetEdges.contains(edge))
            rows.select(m_boundaries->index(row, 0),
                        m_boundaries->index(row, Mesh2DAquiferBoundaryModel::Columns - 1));
    }
    view->selectionModel()->select(rows, QItemSelectionModel::ClearAndSelect);
}

void Mesh2DGroundwaterDialog::setForTargetEdges()
{
    if (!m_engine || !m_editable)
        return;
    const auto judged = Mesh2DAquiferBoundaryModel::eligibility(m_engine, m_targetEdges);
    if (judged.eligible.isEmpty())
        return;
    QString speciesError;
    const QStringList species = Mesh2DAquiferBoundaryModel::inflowSpecies(m_engine, &speciesError);
    QStringList series;
    for (int t = 0; t < swmm_table_count(m_engine); ++t) {
        int type = -1;
        if (const char *id = swmm_table_id(m_engine, t);
            id && swmm_table_get_type(m_engine, t, &type) == SWMM_OK && type == 0)
            series << QString::fromUtf8(id);
    }
    // Edge lengths, for a total flow split by length (mesh units; only ratios matter).
    QList<double> lengths;
    for (const auto &edge : judged.eligible) {
        int v[4]{}, nv = 0;
        double ax = 0, ay = 0, az = 0, bx = 0, by = 0, bz = 0;
        if (swmm_2d_cell_get_vertices(m_engine, edge.first, v, &nv) != SWMM_OK || nv < 3 ||
            swmm_2d_vertex_get_xyz(m_engine, v[(edge.second + 1) % nv], &ax, &ay, &az) != SWMM_OK ||
            swmm_2d_vertex_get_xyz(m_engine, v[(edge.second + 2) % nv], &bx, &by, &bz) != SWMM_OK)
            lengths << 0.0;
        else
            lengths << std::hypot(bx - ax, by - ay);
    }
    double totalLength = 0;
    for (double length : lengths)
        totalLength += length;

    QDialog ask(this);
    ask.setObjectName("gwBoundarySetDialog");
    ask.setWindowTitle(tr("Aquifer Boundary for Eligible Edges (%1)").arg(judged.eligible.size()));
    auto *form = new QFormLayout(&ask);
    auto *kind = new QComboBox(&ask);
    kind->setObjectName("gwBoundarySetKind");
    kind->addItem(tr("Head (water-table elevation)"), QStringLiteral("HEAD"));
    kind->addItem(tr("Flow (positive into the aquifer)"), QStringLiteral("FLOW"));
    form->addRow(tr("Boundary:"), kind);
    auto *source = new QComboBox(&ask);
    source->setObjectName("gwBoundarySetSeries");
    source->addItem(tr("Constant value"), QString());
    for (const QString &name : series)
        source->addItem(tr("Time series %1").arg(name), name);
    form->addRow(tr("Value from:"), source);
    auto *value = new QDoubleSpinBox(&ask);
    value->setObjectName("gwBoundarySetValue");
    value->setRange(-1.0e9, 1.0e9);
    value->setDecimals(6);
    form->addRow(tr("Head:"), value); // relabelled by update()
    auto *distribution = new QComboBox(&ask);
    distribution->setObjectName("gwBoundarySetDistribution");
    distribution->addItem(tr("The same flow on each edge"), QStringLiteral("EACH"));
    distribution->addItem(tr("One total, split over the edges by length"), QStringLiteral("SPLIT"));
    form->addRow(tr("Distribution:"), distribution);
    QMap<QString, QLineEdit *> concentrations;
    auto *chemistry = new QLabel(
        species.isEmpty() ? QString()
                          : tr("Water flowing in carries these concentrations (native units; "
                               "temperature in °C). Each must be entered."),
        &ask);
    chemistry->setWordWrap(true);
    form->addRow(chemistry);
    for (const QString &name : species) {
        auto *edit = new QLineEdit(&ask);
        edit->setObjectName(QStringLiteral("gwBoundarySetConc:") + name);
        edit->setValidator(new QDoubleValidator(edit));
        concentrations.insert(name, edit);
        form->addRow(name == "__TEMPERATURE__" ? tr("Temperature:")
                     : name == "__WATER_AGE__" ? tr("Water age:")
                                               : name + QLatin1Char(':'),
                     edit);
    }
    auto *preview = new QLabel(&ask);
    preview->setObjectName("gwBoundarySetPreview");
    preview->setWordWrap(true);
    form->addRow(preview);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &ask);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);

    const int n = int(judged.eligible.size());
    const QString length = m_units ? m_units->lengthLabel() : QStringLiteral("project units");
    // One value per eligible edge, in the record's native units.
    const auto perEdge = [&] {
        QList<double> values;
        const bool split = kind->currentData() == "FLOW" && source->currentData().toString().isEmpty() &&
                           distribution->currentData() == "SPLIT" && totalLength > 0;
        for (int i = 0; i < n; ++i)
            values << (split ? value->value() * lengths[i] / totalLength : value->value());
        return values;
    };
    const auto inflow = [&] {
        return kind->currentData() == "HEAD" || !source->currentData().toString().isEmpty() ||
               value->value() > 0;
    };
    const auto update = [&] {
        const bool flow = kind->currentData() == "FLOW";
        const bool constant = source->currentData().toString().isEmpty();
        auto *label = qobject_cast<QLabel *>(form->labelForField(value));
        if (label)
            label->setText(flow ? tr("Flow (m³/s):") : tr("Head (%1):").arg(length));
        value->setEnabled(constant);
        distribution->setEnabled(flow && constant);
        const bool needsChemistry = inflow();
        chemistry->setVisible(needsChemistry && !species.isEmpty());
        bool complete = speciesError.isEmpty();
        for (auto *edit : concentrations) {
            edit->setEnabled(needsChemistry);
            if (auto *row = form->labelForField(edit))
                row->setVisible(needsChemistry);
            edit->setVisible(needsChemistry);
            int pos = 0;
            QString text = edit->text();
            if (needsChemistry &&
                edit->validator()->validate(text, pos) != QValidator::Acceptable)
                complete = false;
        }
        QString summary;
        if (!speciesError.isEmpty())
            summary = speciesError;
        else if (!constant)
            summary = tr("Each edge follows time series %1.")
                          .arg(source->currentData().toString());
        else if (!flow)
            summary = tr("Head %1 %2 on each edge.").arg(value->value()).arg(length);
        else {
            const QList<double> values = perEdge();
            double total = 0;
            for (double v : values)
                total += v;
            QStringList parts;
            for (int i = 0; i < n && i < 6; ++i)
                parts << tr("cell %1 edge %2: %3")
                             .arg(judged.eligible[i].first + 1)
                             .arg(judged.eligible[i].second)
                             .arg(values[i], 0, 'g', 6);
            if (n > 6)
                parts << tr("…");
            summary = tr("Flow per edge (m³/s) — %1. Total %2 m³/s.")
                          .arg(parts.join(QStringLiteral("; ")))
                          .arg(total, 0, 'g', 6);
        }
        if (judged.interior || judged.uncovered || judged.invalid)
            summary += QLatin1Char(' ') +
                       tr("Excluded edges: %1.")
                           .arg(judged.interior + judged.uncovered + judged.invalid);
        preview->setText(summary);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(complete);
    };
    connect(kind, &QComboBox::currentIndexChanged, &ask, update);
    connect(source, &QComboBox::currentIndexChanged, &ask, update);
    connect(distribution, &QComboBox::currentIndexChanged, &ask, update);
    connect(value, &QDoubleSpinBox::valueChanged, &ask, update);
    for (auto *edit : concentrations)
        connect(edit, &QLineEdit::textChanged, &ask, update);
    update();
    if (ask.exec() != QDialog::Accepted)
        return;

    QList<Mesh2DAquiferBoundaryModel::Row> rows;
    const QList<double> values = perEdge();
    const QString seriesName = source->currentData().toString();
    for (int i = 0; i < n; ++i) {
        Mesh2DAquiferBoundaryModel::Row water;
        water.cell = judged.eligible[i].first;
        water.edge = judged.eligible[i].second;
        water.kind = kind->currentData().toString();
        water.series = seriesName;
        water.value = seriesName.isEmpty() ? values[i] : 0.0;
        rows << water;
        if (!inflow())
            continue;
        for (auto it = concentrations.cbegin(); it != concentrations.cend(); ++it) {
            Mesh2DAquiferBoundaryModel::Row term = water;
            term.species = it.key();
            term.kind = QStringLiteral("CONC");
            term.series.clear();
            term.value = it.value()->locale().toDouble(it.value()->text());
            rows << term;
        }
    }
    m_boundaries->stageEdges(rows);
    refreshTargetEdges();
    m_targetSummary->setText(m_targetSummary->text() + QLatin1Char(' ') +
                             tr("Staged on %1 edges; Apply writes them.").arg(n));
}

void Mesh2DGroundwaterDialog::updateBottomConnection()
{
    if (!m_bottomDestination)
        return;
    m_bottomNode->setEnabled(m_engine && !m_nodes->lidNodes().isEmpty());
    const auto node = m_bottomNode->currentText();
    const int mode = m_nodes->bottomDestination(node);
    {
        QSignalBlocker block(m_bottomDestination);
        m_bottomDestination->setCurrentIndex(std::max(0, mode));
    }
    m_bottomDestination->setEnabled(m_editable && mode >= 0);
    m_useFootprint->setEnabled(m_editable && mode >= 0);
    m_bottomSummary->setText(m_nodes->connectionSummary(node));
}

void Mesh2DGroundwaterDialog::onAddAquiferRow()
{
    const int at = m_aquifer->appendRow();
    m_aquiferView->selectRow(at);
    m_aquiferView->scrollTo(m_aquifer->index(at, 0));
}

void Mesh2DGroundwaterDialog::onRemoveAquiferRow()
{
    const auto sel = m_aquiferView->selectionModel()->selectedRows();
    QList<int> rows;
    for (const auto &i : sel)
        rows.append(i.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        m_aquifer->removeRow(row);
}

void Mesh2DGroundwaterDialog::onAddNodeBed()
{
    const int at = m_nodes->appendRow(tr("<node>"), 0);
    m_nodeView->selectRow(at);
    m_nodeView->edit(m_nodes->index(at, Mesh2DAquiferNodeModel::ColNode));
}

void Mesh2DGroundwaterDialog::onRemoveNodeBed()
{
    const auto sel = m_nodeView->selectionModel()->selectedRows();
    QList<int> rows;
    for (const auto &i : sel)
        rows.append(i.row());
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
        m_nodes->removeRow(row);
}

void Mesh2DGroundwaterDialog::refreshState()
{
    if (!m_stateText)
        return;
    if (!m_engine) {
        m_stateText->setPlainText(tr("No model is open."));
        return;
    }
    int active = 0;
    swmm_gw2d_is_active(m_engine, &active);
    if (!active) {
        m_stateText->setPlainText(tr("The groundwater kernel is not running.\n\n"
                                     "It starts when a [2D_AQUIFER] row resolves onto the mesh and "
                                     "the simulation begins. Add a row on the Aquifer tab, then "
                                     "run."));
        return;
    }

    int nCells = 0, mLayers = 0;
    swmm_gw2d_get_dimensions(m_engine, &nCells, &mLayers);

    QString s;
    s += tr("Cells: %1    σ layers: %2\n\n").arg(nCells).arg(mLayers);

    double residual = 0.0;
    if (swmm_gw2d_get_continuity_error(m_engine, &residual) == SWMM_OK) {
        double storage = 0.0;
        swmm_gw2d_get_ledger(m_engine, SWMM_GW2D_LED_STORAGE, &storage);
        s += tr("Continuity residual: %1 m³").arg(residual, 0, 'e', 3);
        if (storage > 0.0)
            s += tr("   (%1 of storage)").arg(std::abs(residual) / storage, 0, 'e', 2);
        s += QStringLiteral("\n\n");
    }

    struct Term {
        int code;
        const char *label;
    };
    const Term terms[] = {
        {SWMM_GW2D_LED_INIT_STORAGE, "Initial storage"},
        {SWMM_GW2D_LED_STORAGE, "Storage now"},
        {SWMM_GW2D_LED_INFIL_IN, "Infiltration in"},
        {SWMM_GW2D_LED_RECHARGE, "Recharge (internal)"},
        {SWMM_GW2D_LED_CAPRISE, "Capillary rise (internal)"},
        {SWMM_GW2D_LED_LATERAL, "Lateral net in (includes edges)"},
        {SWMM_GW2D_LED_BOUNDARY_IN, "Boundary in (within lateral)"},
        {SWMM_GW2D_LED_BOUNDARY_OUT, "Boundary out (within lateral)"},
        {SWMM_GW2D_LED_DEEP, "Deep percolation out"},
        {SWMM_GW2D_LED_NODE, "Node exchange out"},
        {SWMM_GW2D_LED_ET, "Subsurface ET out"},
        {SWMM_GW2D_LED_DUNNE, "Saturation excess to surface"},
    };
    s += tr("Water balance (m³, cumulative)\n");
    for (const Term &t : terms) {
        double v = 0.0;
        if (swmm_gw2d_get_ledger(m_engine, t.code, &v) != SWMM_OK)
            continue;
        s += QStringLiteral("  %1 %2\n")
                 .arg(QString::fromLatin1(t.label).leftJustified(32, ' '))
                 .arg(v, 14, 'g', 8);
    }

    const int boundaries = swmm_gw2d_boundary_count(m_engine);
    if (boundaries > 0) {
        double requestedIn = 0, requestedOut = 0, acceptedIn = 0, acceptedOut = 0;
        bool valid = true;
        for (int edge = 0; edge < boundaries; ++edge) {
            double ri = 0, ro = 0, ai = 0, ao = 0;
            if (swmm_gw2d_boundary_water(m_engine, edge, &ri, &ro, &ai, &ao) != SWMM_OK) {
                valid = false;
                break;
            }
            requestedIn += ri;
            requestedOut += ro;
            acceptedIn += ai;
            acceptedOut += ao;
        }
        if (valid)
            s +=
                tr("\nHydraulic edges (gross m³, cumulative)\n  In: requested %1, accepted %2\n  Out: requested %3, accepted %4\n")
                    .arg(requestedIn, 0, 'g', 8)
                    .arg(acceptedIn, 0, 'g', 8)
                    .arg(requestedOut, 0, 'g', 8)
                    .arg(acceptedOut, 0, 'g', 8);
    }
    int species = 0;
    if (swmm_gw2d_species_count(m_engine, &species) == SWMM_OK && species > 0) {
        s += tr("\nSpecies balance (native concentration × m³; reaction loss may be negative)\n");
        for (int row = 0; row < species; ++row) {
            char name[4096]{};
            double loss = 0, residual = 0, in = 0, out = 0, fluxIn = 0, fluxOut = 0;
            if (swmm_gw2d_species_name(m_engine, row, name, sizeof name) != SWMM_OK)
                continue;
            const auto term = [&](int code, double &value) {
                return swmm_gw2d_get_species_ledger(m_engine, row, code, &value) == SWMM_OK;
            };
            if (!term(SWMM_GW2D_SPL_REACTED, loss) || !term(SWMM_GW2D_SPL_RESIDUAL, residual))
                continue;
            term(SWMM_GW2D_SPL_BOUNDARY_IN, in);
            term(SWMM_GW2D_SPL_BOUNDARY_OUT, out);
            term(SWMM_GW2D_SPL_BOUNDARY_FLUX_IN, fluxIn);
            term(SWMM_GW2D_SPL_BOUNDARY_FLUX_OUT, fluxOut);
            s +=
                tr("  %1: advective edge in %2, out %3; imposed in %4, out %5; reaction loss %6; residual %7\n")
                    .arg(QString::fromUtf8(name))
                    .arg(in, 0, 'g', 8)
                    .arg(out, 0, 'g', 8)
                    .arg(fluxIn, 0, 'g', 8)
                    .arg(fluxOut, 0, 'g', 8)
                    .arg(loss, 0, 'g', 8)
                    .arg(residual, 0, 'g', 8);
        }
    }

    long hist[8] = {0};
    int written = 0;
    if (swmm_gw2d_get_tier_histogram(m_engine, hist, 8, &written) == SWMM_OK && written > 0) {
        s += tr("\nLTS firings per tier (tier k fires every 2^k substeps)\n");
        for (int i = 0; i < written; ++i)
            s += QStringLiteral("  tier %1: %2\n").arg(i).arg(hist[i]);
    }

    // A short census rather than every cell: a mesh has too many to read, and
    // the numbers that matter are the extremes and the closure mix.
    if (nCells > 0) {
        QVector<double> hg(nCells), tier(nCells), closure(nCells);
        int got = 0;
        const bool ok = swmm_gw2d_get_cell_bulk(m_engine, SWMM_GW2D_VAR_HG, hg.data(), nCells,
                                                &got) == SWMM_OK &&
                        swmm_gw2d_get_cell_bulk(m_engine, SWMM_GW2D_VAR_TIER, tier.data(), nCells,
                                                &got) == SWMM_OK &&
                        swmm_gw2d_get_cell_bulk(m_engine, SWMM_GW2D_VAR_CLOSURE, closure.data(),
                                                nCells, &got) == SWMM_OK;
        if (ok) {
            double lo = hg.at(0), hi = hg.at(0), sum = 0.0;
            int nEns = 0, nCf = 0, nSig = 0;
            for (int i = 0; i < nCells; ++i) {
                lo = std::min(lo, hg.at(i));
                hi = std::max(hi, hg.at(i));
                sum += hg.at(i);
                switch (static_cast<int>(closure.at(i))) {
                case SWMM_GW2D_CLOSURE_ENSLAVED:
                    ++nEns;
                    break;
                case SWMM_GW2D_CLOSURE_SIGMA:
                    ++nSig;
                    break;
                default:
                    ++nCf;
                    break;
                }
            }
            s += tr("\nSaturated thickness (m): min %1  mean %2  max %3\n")
                     .arg(lo, 0, 'g', 6)
                     .arg(sum / nCells, 0, 'g', 6)
                     .arg(hi, 0, 'g', 6);
            s += tr("Closures resolved: %1 ENSLAVED, %2 CLOSED_FORM, %3 SIGMA\n")
                     .arg(nEns)
                     .arg(nCf)
                     .arg(nSig);
        }
    }

    m_stateText->setPlainText(s);
}

} // namespace openswmmvis::ui
