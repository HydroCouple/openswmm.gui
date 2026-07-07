/*!
 * \file   aquiferprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM aquifer ([AQUIFERS]).
 *
 * One AquiferProvider per `[AQUIFERS]` entry. Owned by a project-scoped
 * AquiferRegistry. Mirrors PollutantProvider. The engine exposes aquifer
 * parameters through a uniform param-code API (swmm_aquifer_get/set_param),
 * so the twelve fields are stored in an index-addressed array whose order
 * matches SWMM_AquiferParam (0..ParamCount-1). The provider stays engine-
 * agnostic — the registry maps indices to engine codes (identity).
 */
#ifndef OPENSWMMVIS_AQUIFER_AQUIFERPROVIDER_H
#define OPENSWMMVIS_AQUIFER_AQUIFERPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::aquifer {

class AquiferProvider : public QObject
{
    Q_OBJECT

public:
    /*! Field order mirrors SWMM_AquiferParam in openswmm_subcatchments.h. */
    enum Param {
        Porosity = 0,
        WiltingPoint,
        FieldCapacity,
        Conductivity,
        ConductSlope,
        TensionSlope,
        UpperEvapFrac,
        LowerEvapDepth,
        LowerLossCoeff,
        BottomElev,
        WaterTableElev,
        UpperMoisture,
        ParamCount
    };

    explicit AquiferProvider(QString name, QObject *parent = nullptr);
    ~AquiferProvider() override;

    QString name() const noexcept { return m_name; }

    /*! \returns parameter \p k (0..ParamCount-1), or 0 if out of range. */
    double param(int k) const noexcept;

    void setName(QString newName);
    void setParam(int k, double v);

signals:
    void nameChanged(QString prev, QString now);
    void paramsChanged();

private:
    QString m_name;
    double  m_param[ParamCount] = {};
};

} // namespace openswmmvis::aquifer

#endif // OPENSWMMVIS_AQUIFER_AQUIFERPROVIDER_H
