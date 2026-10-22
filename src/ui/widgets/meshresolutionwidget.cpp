// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshresolutionwidget.cpp
 * @brief     Shared mesh resolution editor for dialogs and preferences.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/widgets/meshresolutionwidget.h"
#include "ui/uiscrollhelpers.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
class ResolutionSpinBox : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    /**
     * @brief Format a resolution value using the current locale and twelve significant digits.
     * @param[in] value Accepted spin-box value.
     * @return Display text; unit suffix handling remains QDoubleSpinBox's responsibility.
     */
    QString textFromValue(double value) const override { return locale().toString(value, 'g', 12); }
};

class MeshCategoryDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    /**
     * @brief Size category rows with enough vertical space for the current font.
     * @param[in] option View options/font metrics.
     * @param[in] index Category model row.
     * @return Base width and at least font height plus twelve logical pixels.
     */
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), option.fontMetrics.height() + 12));
        return size;
    }
};
}

MeshResolutionWidget::MeshResolutionWidget(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *body = new QSplitter(Qt::Horizontal, this);
    outer->addWidget(body, 1);
    body->setObjectName("meshSettingsSplitter");
    body->setChildrenCollapsible(false);
    m_categories = new QTreeWidget(this);
    m_categories->setObjectName("meshSettingsCategories");
    m_categories->setAccessibleName(tr("Mesh setting categories"));
    m_categories->setHeaderHidden(true);
    m_categories->setRootIsDecorated(false);
    m_categories->setUniformRowHeights(true);
    // Let the delegate measure labels with the current font/style. Keep the
    // full column visible instead of capping its width or eliding long names.
    m_categories->setItemDelegate(new MeshCategoryDelegate(m_categories));
    m_categories->header()->setStretchLastSection(false);
    m_categories->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_categories->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_categories->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_pages = new QStackedWidget(this);
    m_pages->setObjectName("meshSettingsPages");
    body->addWidget(m_categories);
    body->addWidget(m_pages);
    body->setStretchFactor(0, 0);
    body->setStretchFactor(1, 1);
    connect(m_categories, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (item)
            m_pages->setCurrentIndex(m_categories->indexOfTopLevelItem(item));
    });
    auto group = [&](const QString &title) {
        auto *g = new QGroupBox(title, this);
        addSettingsPage(title, g);
        auto *f = new QFormLayout(g);
        f->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
        f->setRowWrapPolicy(QFormLayout::WrapLongRows);
        return f;
    };
    auto spin = [&](const char *name) {
        auto *s = new ResolutionSpinBox(this);
        s->setMaximumWidth(150);
        s->setObjectName(QLatin1String(name));
        s->setDecimals(6);
        s->setRange(.000001, 1e12);
        s->setSuffix(tr(" m"));
        s->setProperty("meshDistance", true);
        return s;
    };
    auto choice = [&](const char *name, const QStringList &items) {
        auto *c = new QComboBox(this);
        c->setObjectName(QLatin1String(name));
        c->addItems(items);
        return c;
    };
    auto row = [&](QComboBox *c, QDoubleSpinBox *s) {
        auto *w = new QWidget(this);
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->addWidget(c);
        l->addWidget(s);
        l->addStretch();
        return w;
    };
    auto note = [&](QFormLayout *f, const QString &text) {
        auto *l = new QLabel(text, this);
        l->setWordWrap(true);
        f->addRow(l);
        return l;
    };
    auto *sizes = group(tr("Cell size"));
    minimum = spin("meshMinCellSizeSpin");
    sizes->addRow(tr("Minimum target cell size:"), minimum);
    note(
        sizes,
        tr("Stop discretionary refinement at this scale. Required geometry and shape repair can produce smaller cells."));
    maximum = spin("meshMaxCellSizeSpin");
    maximumMode = choice("meshMaximumMode", {tr("No limit"), tr("Specify a size")});
    sizes->addRow(tr("Maximum target cell size:"), row(maximumMode, maximum));
    note(
        sizes,
        tr("No limit allows large cells where terrain, geometry and local targets permit. Sizes describe typical spacing, not every edge."));

    auto *local = group(tr("Local refinement"));
    refineFeatures = new QCheckBox(tr("Refine near selected features"), this);
    refineFeatures->setObjectName("meshRefineFeaturesBox");
    local->addRow(refineFeatures);
    feature = spin("meshCellSizeSpin");
    featureMode = choice("meshFeatureMode", {tr("Use minimum target"), tr("Custom target")});
    m_featureRow = row(featureMode, feature);
    local->addRow(m_featureRow);
    note(
        local,
        tr("Applies to included nodes, lines, building boundaries and region rings. Sources still constrain geometry when extra refinement is off."));
    auto *localTargets = note(local, QString());
    localTargets->setObjectName("meshLocalTargetsLink");
    localTargets->hide();

    auto *terrain = group(tr("Terrain accuracy"));
    m_refineTerrain = new QCheckBox(tr("Refine to match terrain"), this);
    m_refineTerrain->setObjectName("meshRefineTerrainBox");
    terrain->addRow(m_refineTerrain);
    tolerance = spin("meshTerrainTolSpin");
    toleranceMode = choice("meshToleranceMode", {tr("Automatic estimate"), tr("Specify a value")});
    terrain->addRow(tr("Elevation tolerance:"), row(toleranceMode, tolerance));
    note(
        terrain,
        tr("Allowed difference between the interpolated mesh surface and the selected terrain reference. A smaller value requests a closer fit; it does not change breakline detection."));
    reference = choice("meshTerrainReferenceCombo", {tr("Averaged DEM"), tr("Full DEM")});
    terrain->addRow(tr("Terrain reference:"), reference);
    m_referenceNote = note(terrain, QString());
    averaging = spin("meshAveragingSpin");
    averagingMode =
        choice("meshAveragingMode", {tr("Match minimum target"), tr("Custom averaging scale")});
    terrain->addRow(tr("Terrain averaging:"), row(averagingMode, averaging));

    auto *alignment = group(tr("Terrain features"));
    alignTerrain = new QCheckBox(tr("Align edges with terrain features"), this);
    alignTerrain->setObjectName("meshTerrainBreaklinesBox");
    alignment->addRow(alignTerrain);
    note(
        alignment,
        tr("Detect ridges, banks and slope breaks as mesh edges. Detection sensitivity is separate from elevation tolerance."));

    auto *toggle = new QToolButton(this);
    toggle->setText(tr("Advanced terrain settings"));
    toggle->setCheckable(true);
    toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toggle->setArrowType(Qt::RightArrow);
    terrain->addRow(toggle);
    auto *advanced = new QWidget(this);
    auto *f = new QFormLayout(advanced);
    f->setContentsMargins(0, 0, 0, 0);
    terrain->addRow(advanced);
    advanced->hide();
    connect(toggle, &QToolButton::toggled, this, [=](bool on) {
        advanced->setVisible(on);
        toggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    transition = spin("meshSizeRatioSpin");
    transition->setProperty("meshDistance", false);
    transition->setSuffix({});
    transition->setRange(1.05, 2);
    transition->setDecimals(2);
    transition->setSingleStep(.05);
    transition->setToolTip(tr(
        "Growth of the background size field. Smaller values make smoother transitions and more cells; final neighbouring cells can differ more near constraints."));
    sizes->addRow(tr("Size transition rate:"), transition);
    breaklineTolerance = spin("meshBreaklineTolSpin");
    breaklineMode = choice("meshBreaklineMode", {tr("Automatic estimate"), tr("Specify a value")});
    alignment->addRow(tr("Detection tolerance:"), row(breaklineMode, breaklineTolerance));
    minimumBreakline = spin("meshMinBreaklineLengthSpin");
    minimumBreaklineMode = choice("meshMinBreaklineLengthMode",
                                  {tr("Four times minimum target"), tr("Specify a length")});
    alignment->addRow(tr("Minimum breakline length:"), row(minimumBreaklineMode, minimumBreakline));
    maxBreaklines = new QSpinBox(this);
    maxBreaklines->setObjectName("meshMaxBreaklinesSpin");
    maxBreaklines->setRange(0, 10'000'000);
    maxBreaklines->setSpecialValueText(tr("Automatic from cell budget"));
    alignment->addRow(tr("Maximum breaklines:"), maxBreaklines);
    terrainMode = choice("meshTerrainModeCombo",
                         {tr("Adaptive elevation error"), tr("Legacy block sizing"), tr("Off")});
    f->addRow(tr("Terrain refinement method:"), terrainMode);
    auto *direct = new QCheckBox(tr("Read full DEM directly without a local copy"), this);
    direct->setObjectName("meshReadTerrainDirectBox");
    f->addRow(direct);
    connect(direct, &QCheckBox::toggled, this, [this] { sync(); });
    report = new QLabel(this);
    report->setObjectName("meshTerrainReportLabel");
    report->setWordWrap(true);
    outer->addWidget(report);
    for (auto *c : {maximumMode, featureMode, toleranceMode, breaklineMode, averagingMode,
                    reference, minimumBreaklineMode})
        connect(c, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { sync(); });
    for (auto *s : {minimum, averaging})
        connect(s, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this] { sync(); });
    for (auto *b : {refineFeatures, alignTerrain})
        connect(b, &QCheckBox::toggled, this, [this] { sync(); });
    connect(m_refineTerrain, &QCheckBox::toggled, this, [this](bool on) {
        terrainMode->setCurrentIndex(on ? 0 : 2);
        sync();
    });
    connect(terrainMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int mode) {
        QSignalBlocker block(m_refineTerrain);
        m_refineTerrain->setChecked(mode != 2);
        sync();
    });
    auto *shape = group(tr("Cell shape"));
    coarsenEnabled = new QCheckBox(tr("Remove unnecessary vertices after refinement"), this);
    coarsenEnabled->setObjectName("meshCoarsenEnabled");
    shape->addRow(coarsenEnabled);
    note(
        shape,
        tr("Removes free vertices only when angle, local size and elevation limits still hold. Constraint, node, feature and quad-region vertices are retained. Can reduce cell count at additional generation cost."));
    auto *coarsenAdvanced = new QToolButton(this);
    coarsenAdvanced->setText(tr("Advanced coarsening settings"));
    coarsenAdvanced->setCheckable(true);
    coarsenAdvanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    coarsenAdvanced->setArrowType(Qt::RightArrow);
    shape->addRow(coarsenAdvanced);
    auto *coarsenDetails = new QWidget(this);
    auto *coarsenLayout = new QFormLayout(coarsenDetails);
    coarsenLayout->setContentsMargins(0, 0, 0, 0);
    coarsenPasses = new QSpinBox(this);
    coarsenPasses->setObjectName("meshCoarsenPassesSpin");
    coarsenPasses->setRange(1, 10);
    coarsenPasses->setValue(3);
    coarsenPasses->setToolTip(
        tr("Maximum attempts per vertex. Neighbours are reconsidered after a removal."));
    coarsenLayout->addRow(tr("Coarsening passes:"), coarsenPasses);
    shape->addRow(coarsenDetails);
    coarsenDetails->hide();
    connect(coarsenAdvanced, &QToolButton::toggled, this, [=](bool on) {
        coarsenDetails->setVisible(on);
        coarsenAdvanced->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(coarsenEnabled, &QCheckBox::toggled, coarsenPasses, &QWidget::setEnabled);
    setSettings({});
}

void MeshResolutionWidget::addSettingsPage(const QString &title, QWidget *content)
{
    for (int i = 0; i < m_categories->topLevelItemCount(); ++i)
        if (m_categories->topLevelItem(i)->text(0) == title) {
            auto *scroll = qobject_cast<QScrollArea *>(m_pages->widget(i));
            auto *layout = qobject_cast<QVBoxLayout *>(scroll->widget()->layout());
            layout->insertWidget(layout->count() - 1, content);
            return;
        }
    auto *page = new QWidget(this);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->addWidget(content);
    layout->addStretch();
    auto *scroll = OpenSWMM::Ui::wrapInScrollArea(page, m_pages);
    scroll->setAccessibleName(title);
    m_pages->addWidget(scroll);
    auto *item = new QTreeWidgetItem(m_categories, {title});
    item->setToolTip(0, title);
    if (!m_categories->currentItem())
        m_categories->setCurrentItem(item);
}

void MeshResolutionWidget::showSettingsPage(QWidget *control)
{
    for (int i = 0; i < m_pages->count(); ++i) {
        auto *scroll = qobject_cast<QScrollArea *>(m_pages->widget(i));
        if (!scroll->isAncestorOf(control))
            continue;
        m_categories->setCurrentItem(m_categories->topLevelItem(i));
        scroll->ensureWidgetVisible(control);
        control->setFocus();
        return;
    }
}

void MeshResolutionWidget::sync()
{
    maximum->setVisible(maximumMode->currentIndex() == 1);
    m_featureRow->setVisible(refineFeatures->isChecked());
    feature->setVisible(featureMode->currentIndex() == 1);
    toleranceMode->setEnabled(m_hasTerrain && m_refineTerrain->isChecked());
    tolerance->setVisible(toleranceMode->currentIndex() == 1);
    tolerance->setEnabled(toleranceMode->isEnabled());
    averagingMode->setEnabled(m_hasTerrain && reference->currentIndex() == 0);
    averaging->setVisible(averagingMode->currentIndex() == 1);
    averaging->setEnabled(averagingMode->isEnabled());
    breaklineMode->setEnabled(m_hasTerrain && alignTerrain->isChecked());
    breaklineTolerance->setVisible(breaklineMode->currentIndex() == 1);
    breaklineTolerance->setEnabled(breaklineMode->isEnabled());
    minimumBreaklineMode->setEnabled(breaklineMode->isEnabled());
    minimumBreakline->setVisible(minimumBreaklineMode->currentIndex() == 1);
    minimumBreakline->setEnabled(breaklineMode->isEnabled());
    maxBreaklines->setEnabled(breaklineMode->isEnabled());
    m_refineTerrain->setEnabled(m_hasTerrain);
    alignTerrain->setEnabled(m_hasTerrain);
    terrainMode->setEnabled(m_hasTerrain);
    reference->setEnabled(m_hasTerrain);
    findChild<QCheckBox *>("meshReadTerrainDirectBox")
        ->setEnabled(m_hasTerrain && reference->currentIndex() == 1);
    m_referenceNote->setText(
        !m_hasTerrain ? tr("Select a terrain raster in Sources to use these settings.")
        : reference->currentIndex() == 0
            ? tr("Averaging target: %1%2. The actual pixel averaging scale is reported after generation.")
                  .arg(averagingMode->currentIndex() ? averaging->value() : minimum->value())
                  .arg(minimum->suffix())
            : tr("Compare against full-resolution DEM samples."));
}

mesh::ResolutionSettings MeshResolutionWidget::settings() const
{
    mesh::ResolutionSettings r;
    auto value = [&](QComboBox *mode, QDoubleSpinBox *spin) -> std::optional<double> {
        return mode->currentIndex() == 1 ? std::optional<double>(spin->value() * m_toSI)
                                         : std::nullopt;
    };
    r.coarsenPasses = coarsenEnabled->isChecked() ? coarsenPasses->value() : 0;
    r.minimumM = minimum->value() * m_toSI;
    r.maximumM = value(maximumMode, maximum);
    r.featureM = value(featureMode, feature);
    r.toleranceM = value(toleranceMode, tolerance);
    r.breaklineToleranceM = value(breaklineMode, breaklineTolerance);
    r.averagingM = value(averagingMode, averaging);
    r.minimumBreaklineLengthM = value(minimumBreaklineMode, minimumBreakline);
    r.maxBreaklines = maxBreaklines->value();
    r.transition = transition->value();
    r.refineFeatures = refineFeatures->isChecked();
    r.alignTerrain = alignTerrain->isChecked();
    r.terrain = mesh::ResolutionSettings::Terrain(terrainMode->currentIndex());
    r.reference = reference->currentIndex() == 0 ? mesh::ResolutionSettings::Reference::Averaged
                  : findChild<QCheckBox *>("meshReadTerrainDirectBox")->isChecked()
                      ? mesh::ResolutionSettings::Reference::Direct
                      : mesh::ResolutionSettings::Reference::Full;
    return r;
}

void MeshResolutionWidget::setSettings(const mesh::ResolutionSettings &r)
{
    coarsenPasses->setValue(r.coarsenPasses > 0 ? r.coarsenPasses : 3);
    coarsenEnabled->setChecked(r.coarsenPasses > 0);
    coarsenPasses->setEnabled(r.coarsenPasses > 0);
    minimum->setValue(r.minimumM / m_toSI);
    auto set = [&](QComboBox *c, QDoubleSpinBox *s, std::optional<double> v, double fallback) {
        s->setValue(v.value_or(fallback) / m_toSI);
        c->setCurrentIndex(v ? 1 : 0);
    };
    set(maximumMode, maximum, r.maximumM, 10 * r.minimumM);
    set(featureMode, feature, r.featureM, r.minimumM);
    set(toleranceMode, tolerance, r.toleranceM, .1);
    set(breaklineMode, breaklineTolerance, r.breaklineToleranceM, .1);
    set(averagingMode, averaging, r.averagingM, r.minimumM);
    set(minimumBreaklineMode, minimumBreakline, r.minimumBreaklineLengthM, 4 * r.minimumM);
    maxBreaklines->setValue(r.maxBreaklines);
    transition->setValue(r.transition);
    refineFeatures->setChecked(r.refineFeatures);
    alignTerrain->setChecked(r.alignTerrain);
    terrainMode->setCurrentIndex(int(r.terrain));
    m_refineTerrain->setChecked(r.terrain != mesh::ResolutionSettings::Terrain::Off);
    // Direct reading is retained for old projects in Advanced; it is equivalent
    // to full DEM resolution and never changes the reference values.
    reference->setCurrentIndex(r.reference == mesh::ResolutionSettings::Reference::Averaged ? 0
                                                                                            : 1);
    findChild<QCheckBox *>("meshReadTerrainDirectBox")
        ->setChecked(r.reference == mesh::ResolutionSettings::Reference::Direct);
    sync();
}

void MeshResolutionWidget::setUnits(double metresPerUnit, const QString &label)
{
    const auto r = settings();
    m_toSI = metresPerUnit;
    for (auto *s :
         {minimum, maximum, feature, tolerance, breaklineTolerance, averaging, minimumBreakline})
        s->setSuffix(" " + label);
    setSettings(r);
}
void MeshResolutionWidget::setTerrainAvailable(bool available)
{
    m_hasTerrain = available;
    sync();
}
