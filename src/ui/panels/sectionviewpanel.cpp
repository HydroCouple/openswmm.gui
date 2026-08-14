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

#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

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
    m_sectionBtn->setToolTip(tr("Show the true-shape cross-section."));
    m_sectionBtn->setAccessibleName(tr("Show cross-section"));

    m_profileBtn = new QToolButton(central);
    m_profileBtn->setText(tr("&Profile"));
    m_profileBtn->setCheckable(true);
    m_profileBtn->setToolTip(
        tr("Show the longitudinal profile between the end nodes."));
    m_profileBtn->setAccessibleName(tr("Show profile"));

    modeRow->addWidget(m_sectionBtn);
    modeRow->addWidget(m_profileBtn);
    modeRow->addStretch(1);

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
    refresh();
}

void SectionViewPanel::updateModeButtons()
{
    const bool isLink = (m_objectType == SWMMObjectRef::Link);
    // Nodes have only one drawing, so the toggle would be a dead control.
    m_sectionBtn->setVisible(isLink);
    m_profileBtn->setVisible(isLink);
    m_sectionBtn->setChecked(isLink && m_mode == Mode::Section);
    m_profileBtn->setChecked(isLink && m_mode == Mode::Profile);
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
    m_objectType = objectType;
    m_objectName = name;
    updateModeButtons();
    refresh();
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
        m_preview->setModel(m_mode == Mode::Profile
            ? sectionview::buildLinkProfile(engine, idx, units)
            : sectionview::buildLinkSection(engine, idx, units));
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
