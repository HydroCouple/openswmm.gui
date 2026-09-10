/*!
 * \file   spatialpage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/spatialpage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "layers/swmmmodellayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "ui/dialogs/crsselectiondialog.h"

namespace openswmmvis::ui
{

SpatialPage::SpatialPage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
}

QString SpatialPage::title() const
{
    return tr("Spatial & CRS");
}

void SpatialPage::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    auto *crsGroup = new QGroupBox(tr("Coordinate reference system"), this);
    auto *crsForm  = new QFormLayout(crsGroup);

    auto *crsRow = new QWidget(crsGroup);
    auto *crsRowLay = new QHBoxLayout(crsRow);
    crsRowLay->setContentsMargins(0, 0, 0, 0);
    m_crsLabel = new QLabel(tr("(unknown)"), crsRow);
    m_crsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_crsChangeButton = new QToolButton(crsRow);
    m_crsChangeButton->setText(tr("Change…"));
    m_crsChangeButton->setToolTip(tr("Open the CRS picker (writes to swmm_spatial_set_crs)."));
    m_crsDetectButton = new QToolButton(crsRow);
    m_crsDetectButton->setText(tr("Detect from coordinates"));
    m_crsDetectButton->setToolTip(tr(
        "Inspect the model's coordinate ranges and suggest EPSG:4326 if all\n"
        "coordinates fall within geographic bounds (±180° lon, ±85° lat)."));
    crsRowLay->addWidget(m_crsLabel, 1);
    crsRowLay->addWidget(m_crsChangeButton);
    crsRowLay->addWidget(m_crsDetectButton);
    crsForm->addRow(tr("Layer CRS:"), crsRow);

    m_extentLabel = new QLabel(tr("(extent unavailable)"), crsGroup);
    m_extentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_extentLabel->setWordWrap(true);
    crsForm->addRow(tr("Model extent:"), m_extentLabel);

    auto *note = new QLabel(
        tr("<i>Changing the CRS here updates the layer's stored CRS only. "
           "To permanently transform stored coordinates, use the canvas-CRS "
           "button on the status bar (Phase 0.7 reproject prompt).</i>"),
        crsGroup);
    note->setWordWrap(true);
    crsForm->addRow(note);

    vlay->addWidget(crsGroup);
    vlay->addStretch();

    // NOTE (T1, 2026-09-10): in the monolith these four statements sat AFTER
    // an unconditional `return page;` and had never run — the two buttons were
    // built, laid out and left permanently unconnected. Restored here because
    // the phase's own acceptance criterion is that the CRS pick path still
    // marks the dialog dirty, which is unreachable while the connect is dead.
    connect(m_crsChangeButton, &QToolButton::clicked,
            this, &SpatialPage::onPickCRS);
    connect(m_crsDetectButton, &QToolButton::clicked,
            this, &SpatialPage::onDetectCRS);

    if (!ctx_.modelLayer())
    {
        m_crsChangeButton->setEnabled(false);
        m_crsDetectButton->setEnabled(false);
    }
}

void SpatialPage::read()
{
    refreshSummary();
}

int SpatialPage::write()
{
    // Nothing to do: the CRS is written the moment it is picked (onPickCRS),
    // not deferred to the write pass, so this page contributes no keys.
    return 0;
}

void SpatialPage::refreshSummary()
{
    if (!m_crsLabel) return;

    if (SWMMModelLayer *layer = ctx_.modelLayer())
    {
        if (auto *srs = layer->srs())
        {
            const QString auth = srs->toAuthority();
            m_crsLabel->setText(auth.isEmpty() ? tr("(local)") : auth);
        }
        else
        {
            m_crsLabel->setText(tr("(none)"));
        }

        const MapExtent ext = layer->extent();
        if (ext.isValid())
        {
            m_extentLabel->setText(
                tr("X: [%1, %2]   Y: [%3, %4]")
                    .arg(ext.xMin(), 0, 'g', 8)
                    .arg(ext.xMax(), 0, 'g', 8)
                    .arg(ext.yMin(), 0, 'g', 8)
                    .arg(ext.yMax(), 0, 'g', 8));
        }
        else
        {
            m_extentLabel->setText(tr("(extent invalid / not yet computed)"));
        }
    }
    else
    {
        m_crsLabel->setText(tr("(no layer)"));
        m_extentLabel->setText(tr("(no layer)"));
    }
}

void SpatialPage::onPickCRS()
{
    SWMMModelLayer *layer = ctx_.modelLayer();
    if (!layer) return;
    CRSSelectionDialog dlg(this);
    dlg.setCurrentCRS(layer->srs());
    if (dlg.exec() != QDialog::Accepted) return;
    SpatialReferenceSystem *srs = dlg.selectedSRS();
    if (!srs) return;

    layer->setSRS(srs, true);
    // Also write the engine's CRS option so it round-trips through .inp save.
    ctx_.writeIfChanged("CRS", srs->toAuthority().isEmpty() ? srs->toWkt()
                                                            : srs->toAuthority());
    emit crsChanged();
    refreshSummary();
}

void SpatialPage::onDetectCRS()
{
    SWMMModelLayer *layer = ctx_.modelLayer();
    if (!layer) return;
    const MapExtent ext = layer->extent();
    if (!ext.isValid())
        return;

    const bool inGeographic =
        ext.xMin() >= -180.0 && ext.xMax() <=  180.0 &&
        ext.yMin() >=  -90.0 && ext.yMax() <=   90.0;

    if (inGeographic)
    {
        // Suggest EPSG:4326. Update the label as a hint; the user must press
        // Change… to actually apply (so detection is non-destructive).
        m_crsLabel->setText(tr("(suggested: EPSG:4326 — press Change… to apply)"));
    }
    else
    {
        m_crsLabel->setText(tr(
            "(coordinates exceed geographic bounds — pick a projected CRS)"));
    }
}

} // namespace openswmmvis::ui
