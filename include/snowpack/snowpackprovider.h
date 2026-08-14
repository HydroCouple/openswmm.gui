/*!
 * \file   snowpackprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM snow pack ([SNOWPACKS]).
 *
 * One SnowpackProvider per `[SNOWPACKS]` entry. Owned by a project-scoped
 * SnowpackRegistry. Mirrors AquiferProvider: the engine exposes snow-pack
 * parameters as four grouped get/set calls (PLOWABLE, IMPERVIOUS, PERVIOUS,
 * REMOVAL), so the twenty-seven scalars are stored in an index-addressed array
 * whose order matches Param (0..ParamCount-1). The optional REMOVAL
 * destination subcatchment is a separate string field. The provider stays
 * engine-agnostic — the registry maps index ranges to the grouped engine calls.
 */
#ifndef OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H
#define OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::snowpack {

class SnowpackProvider : public QObject
{
    Q_OBJECT

public:
    /*! Field order: the three 7-value surface groups in engine argument order
     *  (cmin, cmax, tbase, fwfrac, sd0, fw0, last), then the six REMOVAL
     *  values. `Last` is the plowable fraction for PLOWABLE and the 100%-cover
     *  depth for IMPERVIOUS / PERVIOUS. */
    enum Param {
        PlowableCmin = 0,
        PlowableCmax,
        PlowableTbase,
        PlowableFwFrac,
        PlowableSd0,
        PlowableFw0,
        PlowableLast,
        ImperviousCmin,
        ImperviousCmax,
        ImperviousTbase,
        ImperviousFwFrac,
        ImperviousSd0,
        ImperviousFw0,
        ImperviousLast,
        PerviousCmin,
        PerviousCmax,
        PerviousTbase,
        PerviousFwFrac,
        PerviousSd0,
        PerviousFw0,
        PerviousLast,
        RemovalDsnow,
        RemovalFOut,
        RemovalFImp,
        RemovalFPerv,
        RemovalFImelt,
        RemovalFSubcatch,
        ParamCount
    };

    explicit SnowpackProvider(QString name, QObject *parent = nullptr);
    ~SnowpackProvider() override;

    QString name() const noexcept { return m_name; }

    /*! \returns parameter \p k (0..ParamCount-1), or 0 if out of range. */
    double param(int k) const noexcept;

    /*! \returns destination subcatchment for the RemovalFSubcatch fraction. */
    QString removalSubcatch() const noexcept { return m_removalSubcatch; }

    void setName(QString newName);
    void setParam(int k, double v);
    void setRemovalSubcatch(QString name);

signals:
    void nameChanged(QString prev, QString now);
    void paramsChanged();

private:
    QString m_name;
    QString m_removalSubcatch;
    double  m_param[ParamCount] = {};
};

} // namespace openswmmvis::snowpack

#endif // OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H
