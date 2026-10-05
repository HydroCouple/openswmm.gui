/*!
 * \file   lidcontrolprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM LID control ([LID_CONTROLS]).
 *
 * Owned by a project-scoped LidControlRegistry. Engine I/O walks `swmm_lid_*`
 * from openswmm_infrastructure.h: add(id,type) + set_surface / set_soil /
 * set_storage / set_drain. The engine exposes setters but **no getters**, so
 * existing layer values cannot be pre-loaded; a `dirty` flag lets the registry
 * write only new or user-edited controls (see LidControlRegistry).
 *
 * LID type codes: 0=BIO_CELL, 1=RAIN_GARDEN, 2=GREEN_ROOF, 3=INFIL_TRENCH,
 * 4=PERM_PAVEMENT, 5=RAIN_BARREL, 6=ROOFTOP_DISCONN, 7=VEGETATIVE_SWALE.
 */
#ifndef OPENSWMMVIS_LID_LIDCONTROLPROVIDER_H
#define OPENSWMMVIS_LID_LIDCONTROLPROVIDER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::lid {

struct LidLayerTreatment {
    int layer = 1;
    QString pollutant;
    double removal = 0.0, decay = 0.0;
    QString expression;
};
class LidControlProvider : public QObject
{
    Q_OBJECT

public:
    explicit LidControlProvider(QString name, QObject *parent = nullptr);
    ~LidControlProvider() override;

    QString name() const noexcept { return m_name; }
    int     type() const noexcept { return m_type; }   ///< 0..7

    const QVector<SWMM_LidNodeLayer>& nodeLayers() const { return m_nodeLayers; }
    void setNodeLayers(QVector<SWMM_LidNodeLayer> layers);
    SWMM_LidRichardsOptions flowOptions{0, 8, 1.e-7, 1.e-5, 30.0};
    QVector<SWMM_LidRichardsMaterial> retention;
    void setNodeFlow(SWMM_LidRichardsOptions options, QVector<SWMM_LidRichardsMaterial> materials);
    QVector<LidLayerTreatment> treatments;


    // Surface layer.
    double surfStorage()   const noexcept { return m_surfStorage; }
    double surfRoughness() const noexcept { return m_surfRoughness; }
    double surfSlope()     const noexcept { return m_surfSlope; }
    // Soil layer.
    double soilThick()    const noexcept { return m_soilThick; }
    double soilPorosity() const noexcept { return m_soilPorosity; }
    double soilFc()       const noexcept { return m_soilFc; }
    double soilWp()       const noexcept { return m_soilWp; }
    double soilKsat()     const noexcept { return m_soilKsat; }
    double soilKslope()   const noexcept { return m_soilKslope; }
    // Storage layer.
    double storThick()    const noexcept { return m_storThick; }
    double storVoidFrac() const noexcept { return m_storVoidFrac; }
    double storKsat()     const noexcept { return m_storKsat; }
    // Drain.
    double drainCoeff()  const noexcept { return m_drainCoeff; }
    double drainExpon()  const noexcept { return m_drainExpon; }
    double drainOffset() const noexcept { return m_drainOffset; }

    bool dirty() const noexcept { return m_dirty; }
    void clearDirty() noexcept { m_dirty = false; }

    void setName(QString newName);
    void setType(int v);

    void setSurfStorage(double v);
    void setSurfRoughness(double v);
    void setSurfSlope(double v);
    void setSoilThick(double v);
    void setSoilPorosity(double v);
    void setSoilFc(double v);
    void setSoilWp(double v);
    void setSoilKsat(double v);
    void setSoilKslope(double v);
    void setStorThick(double v);
    void setStorVoidFrac(double v);
    void setStorKsat(double v);
    void setDrainCoeff(double v);
    void setDrainExpon(double v);
    void setDrainOffset(double v);

signals:
    void nameChanged(QString prev, QString now);
    void paramsChanged();

private:
    QString m_name;
    int     m_type = 0;

    double m_surfStorage = 0.0, m_surfRoughness = 0.0, m_surfSlope = 0.0;
    double m_soilThick = 0.0, m_soilPorosity = 0.0, m_soilFc = 0.0,
           m_soilWp = 0.0, m_soilKsat = 0.0, m_soilKslope = 0.0;
    double m_storThick = 0.0, m_storVoidFrac = 0.0, m_storKsat = 0.0;
    double m_drainCoeff = 0.0, m_drainExpon = 0.0, m_drainOffset = 0.0;

    QVector<SWMM_LidNodeLayer> m_nodeLayers;
    bool m_dirty = false;
};

} // namespace openswmmvis::lid

#endif // OPENSWMMVIS_LID_LIDCONTROLPROVIDER_H
