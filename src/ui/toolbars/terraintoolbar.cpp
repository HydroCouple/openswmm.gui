/*!
 * \file   terraintoolbar.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/toolbars/terraintoolbar.h"
#include "core/unitsystem.h"
#include "layers/gisrasterlayer.h"
#include "layers/openswmmvislayer.h"
#include "map/mapcanvas.h"
#include "ui/toolbars/ribbongroup.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSizePolicy>

static const int kNoneIndex = 0;

TerrainToolbar::TerrainToolbar(const QString &title, QWidget *parent)
    : QToolBar(title, parent)
{
    setObjectName(QStringLiteral("toolBarTerrain"));
    setMovable(true);

    // Iteration 3 — the terrain controls live inside captioned ribbon
    // groups (its own contextual "Terrain" tab), not a flat toolbar
    // sliver: Active Terrain | Vertical Units | Invert Offsets, with a
    // trailing spacer left-packing the Fixed-width groups.
    using openswmmvis::ui::RibbonGroup;
    auto *groupTerrain = new RibbonGroup(tr("Active Terrain"), this);
    auto *groupUnits   = new RibbonGroup(tr("Vertical Units"), this);
    auto *groupOffsets = new RibbonGroup(tr("Invert Offsets"), this);

    // ── DEM selector ──────────────────────────────────────────────────────────
    m_terrainCombo = new QComboBox(this);
    m_terrainCombo->setToolTip(tr("Active terrain raster used for Z sampling and invert elevation estimation."));
    m_terrainCombo->setMinimumWidth(160);
    m_terrainCombo->setEnabled(false);
    m_terrainCombo->addItem(tr("(none)"), QVariant::fromValue<quintptr>(0));
    groupTerrain->addWidget(m_terrainCombo);

    // ── Vertical unit transformation chain: DEM unit → [factor] → model unit ──
    groupUnits->addWidget(new QLabel(tr("DEM:"), this));
    m_verticalUnitCombo = new QComboBox(this);
    m_verticalUnitCombo->setToolTip(
        tr("Vertical unit of the DEM elevation values.\n"
           "Auto-detected from the raster CRS metadata; override when the\n"
           "detection is wrong (common for 3DEP data whose raster CRS reports\n"
           "geographic lat/lon but whose Z values are in metres)."));
    m_verticalUnitCombo->addItem(tr("m (metres)"), QStringLiteral("m"));
    m_verticalUnitCombo->addItem(tr("ft (feet)"),  QStringLiteral("ft"));
    m_verticalUnitCombo->setEnabled(false);
    groupUnits->addWidget(m_verticalUnitCombo);

    // Conversion arrow: shows "m → ft" or "→ ft" etc.
    m_conversionLabel = new QLabel(this);
    m_conversionLabel->setMinimumWidth(60);
    groupUnits->addWidget(m_conversionLabel);

    // User-editable conversion factor (auto-populated from unit combos).
    groupUnits->addWidget(new QLabel(tr("\xc3\x97"), this));   // × symbol
    m_factorSpin = new QDoubleSpinBox(this);
    m_factorSpin->setRange(0.0001, 10000.0);
    m_factorSpin->setDecimals(6);
    m_factorSpin->setSingleStep(0.001);
    m_factorSpin->setValue(1.0);
    m_factorSpin->setMinimumWidth(90);
    m_factorSpin->setToolTip(
        tr("Multiplication factor applied to raw DEM elevation values to\n"
           "convert them into model vertical units.\n"
           "Auto-computed from the DEM unit and project flow units;\n"
           "override here when the auto-detected value is incorrect.\n"
           "ModelZ = DEM_Z \xc3\x97 factor"));
    m_factorSpin->setEnabled(false);
    groupUnits->addWidget(m_factorSpin);

    // ── Signed offsets in model vertical units ────────────────────────────────
    groupOffsets->addWidget(new QLabel(tr("Node Δ:"), this));   // Δ (delta)
    m_nodeOffsetSpin = new QDoubleSpinBox(this);
    m_nodeOffsetSpin->setRange(-1e6, 1e6);
    m_nodeOffsetSpin->setDecimals(3);
    m_nodeOffsetSpin->setSingleStep(0.1);
    m_nodeOffsetSpin->setValue(0.0);
    m_nodeOffsetSpin->setToolTip(
        tr("Signed offset added to the converted DEM elevation to obtain the\n"
           "node invert elevation (model vertical units).\n"
           "InvertElev = DEM_Z \xc3\x97 factor + Δ\n"
           "Use a negative value to place the invert below the ground surface."));
    m_nodeOffsetSpin->setEnabled(false);
    groupOffsets->addWidget(m_nodeOffsetSpin);
    m_nodeUnitLabel = new QLabel(this);
    m_nodeUnitLabel->setMinimumWidth(20);
    groupOffsets->addWidget(m_nodeUnitLabel);

    groupOffsets->addWidget(new QLabel(tr("Link Δ:"), this));   // Δ (delta)
    m_linkOffsetSpin = new QDoubleSpinBox(this);
    m_linkOffsetSpin->setRange(-1e6, 1e6);
    m_linkOffsetSpin->setDecimals(3);
    m_linkOffsetSpin->setSingleStep(0.1);
    m_linkOffsetSpin->setValue(0.0);
    m_linkOffsetSpin->setToolTip(
        tr("Signed offset added to the converted DEM elevation at each link\n"
           "endpoint.  Used to estimate upstream and downstream invert\n"
           "elevations and derive the conduit slope (model vertical units).\n"
           "InvertElev = DEM_Z \xc3\x97 factor + Δ"));
    m_linkOffsetSpin->setEnabled(false);
    groupOffsets->addWidget(m_linkOffsetSpin);
    m_linkUnitLabel = new QLabel(this);
    m_linkUnitLabel->setMinimumWidth(20);
    groupOffsets->addWidget(m_linkUnitLabel);

    // Mount the groups; leftover row width pools behind them.
    addWidget(groupTerrain);
    addWidget(groupUnits);
    addWidget(groupOffsets);
    {
        auto *spacer = new QWidget(this);
        spacer->setObjectName(QStringLiteral("ribbonBarSpacer"));
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        spacer->setMinimumSize(0, 0);
        addWidget(spacer);
    }

    // Initialise unit labels and keep them in sync with the active project.
    updateUnitLabels();
    connect(UnitSystem::instance(), &UnitSystem::unitsChanged,
            this, [this]() { updateUnitLabels(); });

    connect(m_terrainCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TerrainToolbar::onComboIndexChanged);

    connect(m_verticalUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
                updateUnitLabels();
                emit verticalUnitChanged(verticalUnit());
            });

    connect(m_factorSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) {
                // Factor was manually edited — notify consumers to re-read it.
                emit verticalUnitChanged(verticalUnit());
            });

    connect(m_nodeOffsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TerrainToolbar::nodeOffsetChanged);

    connect(m_linkOffsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TerrainToolbar::linkOffsetChanged);
}

void TerrainToolbar::autoDetectVerticalUnit()
{
    if (!m_verticalUnitCombo) return;
    GISRasterLayer *layer = activeTerrain();
    if (!layer) return;                      // no terrain — keep current selection

    const QString detected = layer->detectVerticalUnit();
    const int idx = m_verticalUnitCombo->findData(detected);
    if (idx >= 0) {
        QSignalBlocker b(m_verticalUnitCombo);
        m_verticalUnitCombo->setCurrentIndex(idx);
    }
}

void TerrainToolbar::updateUnitLabels()
{
    const QString modelUnit = UnitSystem::instance()->depthLabel();
    if (m_nodeUnitLabel) m_nodeUnitLabel->setText(modelUnit);
    if (m_linkUnitLabel) m_linkUnitLabel->setText(modelUnit);

    const QString demUnit = verticalUnit();
    if (m_conversionLabel) {
        const QString text = (demUnit == modelUnit)
            ? tr("→ %1").arg(modelUnit)
            : tr("%1 → %2").arg(demUnit, modelUnit);
        m_conversionLabel->setText(text);
    }

    // Auto-populate the factor spin from unit combo + project units.
    // Only overwrite when the user hasn't manually changed it (spin blocker).
    if (m_factorSpin) {
        const double rasterToSI = (demUnit == QLatin1String("ft")) ? 0.3048 : 1.0;
        const double modelToSI  = UnitSystem::instance()->isSI() ? 1.0 : 0.3048;
        QSignalBlocker b(m_factorSpin);
        m_factorSpin->setValue(rasterToSI / modelToSI);
    }
}

GISRasterLayer *TerrainToolbar::activeTerrain() const
{
    if (!m_terrainCombo || m_terrainCombo->currentIndex() == kNoneIndex)
        return nullptr;
    const quintptr ptr = m_terrainCombo->currentData().value<quintptr>();
    return reinterpret_cast<GISRasterLayer *>(ptr);
}

double TerrainToolbar::nodeOffset() const
{
    return m_nodeOffsetSpin ? m_nodeOffsetSpin->value() : 0.0;
}

double TerrainToolbar::linkOffset() const
{
    return m_linkOffsetSpin ? m_linkOffsetSpin->value() : 0.0;
}

QString TerrainToolbar::verticalUnit() const
{
    if (!m_verticalUnitCombo) return QStringLiteral("m");
    return m_verticalUnitCombo->currentData().toString();
}

double TerrainToolbar::verticalToModelFactor() const
{
    if (m_factorSpin) return m_factorSpin->value();
    // Fallback when spin is not yet constructed.
    const double rasterToSI = (verticalUnit() == QLatin1String("ft")) ? 0.3048 : 1.0;
    const double modelToSI  = UnitSystem::instance()->isSI() ? 1.0 : 0.3048;
    return rasterToSI / modelToSI;
}

void TerrainToolbar::rebindCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas) return;

    if (m_canvas) {
        disconnect(m_canvas, &MapCanvas::layerAdded,   this, &TerrainToolbar::onLayerAdded);
        disconnect(m_canvas, &MapCanvas::layerRemoved, this, &TerrainToolbar::onLayerRemoved);
    }

    m_canvas = canvas;

    const bool active = (m_canvas != nullptr);
    if (m_canvas) {
        connect(m_canvas, &MapCanvas::layerAdded,   this, &TerrainToolbar::onLayerAdded,   Qt::UniqueConnection);
        connect(m_canvas, &MapCanvas::layerRemoved, this, &TerrainToolbar::onLayerRemoved, Qt::UniqueConnection);
    }
    m_terrainCombo->setEnabled(active);
    m_verticalUnitCombo->setEnabled(active);
    m_factorSpin->setEnabled(active);
    m_nodeOffsetSpin->setEnabled(active);
    m_linkOffsetSpin->setEnabled(active);

    rebuildCombo();
}

void TerrainToolbar::restoreState(const QString &terrainLayerPath,
                                   double nodeOff,
                                   double linkOff,
                                   const QString &vertUnit)
{
    {
        QSignalBlocker b(m_nodeOffsetSpin);
        m_nodeOffsetSpin->setValue(nodeOff);
    }
    {
        QSignalBlocker b(m_linkOffsetSpin);
        m_linkOffsetSpin->setValue(linkOff);
    }

    if (terrainLayerPath.isEmpty()) {
        QSignalBlocker b(m_terrainCombo);
        m_terrainCombo->setCurrentIndex(kNoneIndex);
    } else {
        // Find the raster layer whose filePath matches the stored path.
        bool found = false;
        for (int i = 1; i < m_terrainCombo->count(); ++i) {
            const quintptr ptr = m_terrainCombo->itemData(i).value<quintptr>();
            auto *layer = reinterpret_cast<GISRasterLayer *>(ptr);
            if (layer && layer->filePath() == terrainLayerPath) {
                QSignalBlocker b(m_terrainCombo);
                m_terrainCombo->setCurrentIndex(i);
                found = true;
                break;
            }
        }
        if (!found) {
            QSignalBlocker b(m_terrainCombo);
            m_terrainCombo->setCurrentIndex(kNoneIndex);
        }
    }

    // Restore vertical unit; fall back to auto-detect if not saved.
    if (!vertUnit.isEmpty()) {
        const int idx = m_verticalUnitCombo->findData(vertUnit);
        if (idx >= 0) {
            QSignalBlocker b(m_verticalUnitCombo);
            m_verticalUnitCombo->setCurrentIndex(idx);
        }
    } else {
        autoDetectVerticalUnit();
    }
    updateUnitLabels();
}

void TerrainToolbar::onLayerAdded(OpenSWMMVisLayer *layer)
{
    if (!qobject_cast<GISRasterLayer *>(layer)) return;
    rebuildCombo();
}

void TerrainToolbar::onLayerRemoved(OpenSWMMVisLayer *layer)
{
    if (!qobject_cast<GISRasterLayer *>(layer)) return;

    // If the removed layer was active, reset to "(none)" before rebuilding.
    if (activeTerrain() == qobject_cast<GISRasterLayer *>(layer)) {
        QSignalBlocker b(m_terrainCombo);
        m_terrainCombo->setCurrentIndex(kNoneIndex);
        emit activeTerrainChanged(nullptr);
    }
    rebuildCombo();
}

void TerrainToolbar::onComboIndexChanged(int index)
{
    Q_UNUSED(index)
    autoDetectVerticalUnit(); // update unit combo to match new terrain
    updateUnitLabels();
    emit activeTerrainChanged(activeTerrain());
    emit verticalUnitChanged(verticalUnit());
}

void TerrainToolbar::rebuildCombo()
{
    GISRasterLayer *previous = activeTerrain();

    QSignalBlocker b(m_terrainCombo);
    m_terrainCombo->clear();
    m_terrainCombo->addItem(tr("(none)"), QVariant::fromValue<quintptr>(0));

    if (!m_canvas) return;

    int restoreIndex = kNoneIndex;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *raster = qobject_cast<GISRasterLayer *>(l);
        if (!raster) continue;
        m_terrainCombo->addItem(raster->name(),
                                QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(raster)));
        if (raster == previous)
            restoreIndex = m_terrainCombo->count() - 1;
    }

    m_terrainCombo->setCurrentIndex(restoreIndex);

    // Emit only if the active layer actually changed.
    GISRasterLayer *current = activeTerrain();
    if (current != previous)
        emit activeTerrainChanged(current);
}
