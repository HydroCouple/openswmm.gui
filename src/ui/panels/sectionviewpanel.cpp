/*!
 * \file   sectionviewpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/panels/sectionviewpanel.h"

#include "core/unitsystem.h"
#include "layers/swmmmodellayer.h"
#include "selection/selectionmanager.h"
#include "ui/sectionview/sectionmodelbuilders.h"
#include "ui/sectionview/sectionpreviewwidget.h"

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>

#include <QComboBox>
#include <QSignalBlocker>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace openswmmvis::ui {

using sectionview::DiagramUnits;
using sectionview::SectionDiagramModel;
using sectionview::SectionPreviewWidget;

namespace {

/*! Unit context for the builders, read from the active project's UnitSystem.
 *  Falls back to SI when no project is bound (tests, empty app). */
DiagramUnits currentUnits()
{
    DiagramUnits u;
    if (auto *us = UnitSystem::instance()) {
        u.si          = us->isSI();
        u.lengthLabel = us->lengthLabel();
    }
    return u;
}

} // namespace

SectionViewPanel::SectionViewPanel(QWidget *parent)
    : QDockWidget(tr("Section View"), parent)
{
    setObjectName(QStringLiteral("dockWidgetSectionView"));
    buildUi();
    clearSelection();
}

SectionViewPanel::~SectionViewPanel() = default;

void SectionViewPanel::buildUi()
{
    auto *central = new QWidget(this);
    auto *vlay    = new QVBoxLayout(central);
    vlay->setContentsMargins(4, 4, 4, 4);
    vlay->setSpacing(4);

    // Mode row — Section / Profile for links; hidden for everything else.
    auto *modeRow = new QHBoxLayout;
    modeRow->setSpacing(4);

    m_sectionBtn = new QToolButton(central);
    m_sectionBtn->setText(tr("&Section"));
    m_sectionBtn->setCheckable(true);
    m_sectionBtn->setChecked(true);
    m_sectionBtn->setToolTip(
        tr("Show the cross-section (1:1 true shape by default; change with "
           "the V:H control)."));
    m_sectionBtn->setAccessibleName(tr("Show cross-section"));

    m_profileBtn = new QToolButton(central);
    m_profileBtn->setText(tr("&Profile"));
    m_profileBtn->setCheckable(true);
    m_profileBtn->setToolTip(
        tr("Show the longitudinal profile between the end nodes "
           "(10:1 vertical exaggeration by default)."));
    m_profileBtn->setAccessibleName(tr("Show profile"));

    modeRow->addWidget(m_sectionBtn);
    modeRow->addWidget(m_profileBtn);
    modeRow->addStretch(1);

    // Link scale (V:H), held per mode. A cross-section is drawn at 1:1 by
    // default because any other ratio distorts the barrel's true shape; a
    // profile is drawn at 10:1 because a 0.25 % invert slope is invisible at
    // true scale. Auto is still offered: it fills the pane and STATES the
    // ratio it landed on, so the stretch is never silent. Nodes always fill
    // and carry no control.
    m_veLabel = new QLabel(tr("V:&H"), central);
    m_veCombo = new QComboBox(central);
    m_veCombo->setToolTip(
        tr("Scale of the link drawing (V:H), remembered per mode. 1:1 draws "
           "the true shape (the section default); 10:1 is the profile "
           "default; Auto fills the pane and states the achieved ratio on "
           "the drawing."));
    m_veCombo->setAccessibleName(tr("Vertical exaggeration"));
    m_veLabel->setBuddy(m_veCombo);
    m_veCombo->addItem(tr("Auto"), 0.0);
    for (double ve : { 1.0, 2.0, 5.0, 10.0, 20.0, 50.0 })
        m_veCombo->addItem(tr("%1:1").arg(ve, 0, 'g', 3), ve);
    syncVeCombo();

    modeRow->addWidget(m_veLabel);
    modeRow->addWidget(m_veCombo);

    vlay->addLayout(modeRow);

    m_preview = new SectionPreviewWidget(central);
    m_preview->setPlaceholderText(
        tr("Select a node or link on the map to see its section."));
    vlay->addWidget(m_preview, 1);

    setWidget(central);

    connect(m_sectionBtn, &QToolButton::clicked,
            this, [this]() { setMode(Mode::Section); });
    connect(m_profileBtn, &QToolButton::clicked,
            this, [this]() { setMode(Mode::Profile); });
    connect(m_veCombo, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i < 0) return;
        setVerticalExaggeration(m_veCombo->itemData(i).toDouble());
    });
}

void SectionViewPanel::setProject(SWMMModelLayer *layer)
{
    if (m_layer == layer) return;

    if (m_layer)
        disconnect(m_layer, &SWMMModelLayer::attributeChanged,
                   this, &SectionViewPanel::onObjectEditedExternally);

    m_layer = layer;

    if (m_layer)
        connect(m_layer, &SWMMModelLayer::attributeChanged,
                this, &SectionViewPanel::onObjectEditedExternally,
                Qt::UniqueConnection);

    clearSelection();
}

void SectionViewPanel::setMode(Mode mode)
{
    if (m_mode == mode) {
        updateModeButtons();
        return;
    }
    m_mode = mode;
    updateModeButtons();
    // Each mode carries its own ratio, so the combo has to follow the mode.
    syncVeCombo();
    refresh();
    // Section and profile are different drawings — carrying zoom across would
    // land the user somewhere arbitrary.
    m_preview->zoomToExtents();
}

void SectionViewPanel::updateModeButtons()
{
    const bool isLink = (m_objectType == SWMMObjectRef::Link);
    // Nodes have only one drawing, so the toggle would be a dead control.
    m_sectionBtn->setVisible(isLink);
    m_profileBtn->setVisible(isLink);
    m_sectionBtn->setChecked(isLink && m_mode == Mode::Section);
    m_profileBtn->setChecked(isLink && m_mode == Mode::Profile);

    // The scale control governs LINK drawings in both modes (SVX). Nodes
    // always fill the pane, so for them it would be a dead control.
    m_veLabel->setVisible(isLink);
    m_veCombo->setVisible(isLink);
}

void SectionViewPanel::setVerticalExaggeration(double ve)
{
    double &slot = (m_mode == Mode::Profile) ? m_profileVe : m_sectionVe;
    if (qFuzzyCompare(slot + 1.0, ve + 1.0)) return;
    slot = ve;

    // Keep the combo honest when the value is set programmatically — the same
    // state edited through two surfaces has to stay in step (CLAUDE.md §5.1).
    syncVeCombo();
    refresh();
}

void SectionViewPanel::syncVeCombo()
{
    if (!m_veCombo) return;
    const int idx = m_veCombo->findData(verticalExaggeration());
    if (idx < 0 || idx == m_veCombo->currentIndex()) return;
    // Signals blocked so this doesn't bounce straight back into
    // setVerticalExaggeration and overwrite the other mode's value.
    const QSignalBlocker block(m_veCombo);
    m_veCombo->setCurrentIndex(idx);
}

void SectionViewPanel::clearSelection()
{
    m_objectType = SWMMObjectRef::Unknown;
    m_objectName.clear();
    updateModeButtons();
    m_preview->setModel(SectionDiagramModel{});
}

void SectionViewPanel::showObject(int objectType, const QString &name)
{
    const bool subjectChanged =
        (objectType != m_objectType) || (name != m_objectName);

    m_objectType = objectType;
    m_objectName = name;
    updateModeButtons();
    refresh();

    // A different object is a different drawing at a different scale, so the
    // previous zoom means nothing. Re-selecting the SAME object (e.g. a
    // selection signal re-fired after an edit) leaves the view alone.
    if (subjectChanged) m_preview->zoomToExtents();
}

void SectionViewPanel::onObjectEditedExternally(const QString &name)
{
    // A rename arrives as the NEW name, which won't match the displayed one;
    // that case is covered by the selection change that follows it.
    if (!m_objectName.isEmpty() && name == m_objectName)
        refresh();
}

void SectionViewPanel::refresh()
{
    if (!m_preview) return;

    SWMM_Engine engine = m_layer ? m_layer->engine() : nullptr;
    if (!engine || m_objectName.isEmpty()
        || m_objectType == SWMMObjectRef::Unknown)
    {
        m_preview->setModel(SectionDiagramModel{});
        return;
    }

    const DiagramUnits units = currentUnits();
    const QByteArray   utf8  = m_objectName.toUtf8();

    switch (m_objectType) {
    case SWMMObjectRef::Link: {
        const int idx = swmm_link_index(engine, utf8.constData());
        if (idx < 0) { clearSelection(); return; }
        if (m_mode == Mode::Profile) {
            SectionDiagramModel m = sectionview::buildLinkProfile(engine, idx, units);
            if (m_profileVe > 0.0) {
                // An explicit ratio (10:1 by default) overrides the builder's
                // automatic, which would otherwise pick a smaller value for a
                // reach that is already steep.
                m.verticalExaggeration = m_profileVe;
            } else {
                // SVX fill-canvas default: drop the builder's aspect target
                // and cap so the fit stretches to the pane; the achieved
                // ratio is still stated on the drawing.
                m.targetDrawnAspect       = 0.0;
                m.maxVerticalExaggeration = 0.0;
            }
            m_preview->setModel(std::move(m));
        } else {
            SectionDiagramModel m = sectionview::buildLinkSection(engine, idx, units);
            // A cross-section is drawn at 1:1 by default: stretching it to
            // fill a non-square pane misreports the barrel's shape, which is
            // the whole point of the drawing. The combo can still pin another
            // ratio, or Auto (0) to fill the pane; either way the achieved
            // V:H is STATED on the drawing, so the stretch is never silent.
            m.uniformScale            = false;
            m.verticalExaggeration    = m_sectionVe;              // 0 = fill
            m.targetDrawnAspect       = 0.0;
            m.maxVerticalExaggeration = 0.0;
            m.annotateExaggeration    = true;
            m_preview->setModel(std::move(m));
        }
        break;
    }
    case SWMMObjectRef::Node: {
        const int idx = swmm_node_index(engine, utf8.constData());
        if (idx < 0) { clearSelection(); return; }
        m_preview->setModel(sectionview::buildNodeProfile(engine, idx, units));
        break;
    }
    default: {
        // Subcatchments, gages and the non-spatial data objects have no
        // section drawing; say so rather than leaving the last one up.
        SectionDiagramModel m;
        m.title     = m_objectName;
        m.emptyText = tr("No section view for this object type.");
        m_preview->setModel(m);
        break;
    }
    }
}

} // namespace openswmmvis::ui
