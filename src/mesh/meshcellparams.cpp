/*!
 * \file   meshcellparams.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshcellparams.h"

#include <QCoreApplication>

#include <cmath>
#include <limits>

namespace mesh {

const QVector<CellParamSpec> &cellParamSpecs()
{
    static const QVector<CellParamSpec> specs = [] {
        const auto tr = [](const char *s) {
            return QCoreApplication::translate("MeshCellParams", s);
        };
        QVector<CellParamSpec> v;

        // ---- Live: round-tripped through [2D_TRIANGLES] --------------------
        v.append({"mannings", tr("Manning's n"), QStringLiteral("n="),
                  /*lengthUnit=*/false, 0.001, 1.0, 0.001, 0.035, 4,
                  /*enabled=*/true,
                  tr("Surface roughness of the selected cells "
                     "([2D_TRIANGLES] MANNINGS_N).")});
        v.append({"initDepth", tr("Initial Depth"), QStringLiteral("d="),
                  /*lengthUnit=*/true, 0.0, 1000.0, 0.05, 0.0, 4,
                  /*enabled=*/true,
                  tr("Standing water depth at the start of the run "
                     "([2D_TRIANGLES] INIT_DEPTH). 0 starts the cell dry.")});

        // ---- Pending: 2D two-zone groundwater ------------------------------
        // Shown greyed so the roadmap is visible in every selector. Keys and
        // ranges follow the engine's draft [2D_AQUIFER] design
        // (plans/TWO_ZONE_GROUNDWATER_FV_INTEGRATION_PLAN.md); applyCellParam
        // refuses them until the engine and MeshTriangle carry the fields.
        const QString pending =
            tr("Requires 2D groundwater engine support (not yet available).");
        v.append({"gw.Ks", tr("Saturated Conductivity (Ks)"), {},
                  false, 1e-9, 1.0, 1e-6, 1e-5, 8, false, pending});
        v.append({"gw.zs", tr("Aquifer Thickness (zs)"), {},
                  true, 0.0, 1000.0, 0.1, 2.0, 4, false, pending});
        v.append({"gw.thetaS", tr("Porosity (theta_s)"), {},
                  false, 0.01, 1.0, 0.01, 0.4, 3, false, pending});
        v.append({"gw.hu0", tr("Initial Unsaturated Depth (hu)"), {},
                  true, 0.0, 1000.0, 0.05, 0.0, 4, false, pending});
        v.append({"gw.hg0", tr("Initial Saturated Depth (hg)"), {},
                  true, 0.0, 1000.0, 0.05, 0.0, 4, false, pending});
        return v;
    }();
    return specs;
}

const CellParamSpec *cellParamSpec(const QByteArray &key)
{
    for (const CellParamSpec &s : cellParamSpecs())
        if (s.key == key) return &s;
    return nullptr;
}

QString cellParamLabel(const QByteArray &key, const QString &depthUnitLabel)
{
    const CellParamSpec *s = cellParamSpec(key);
    if (!s) return QString::fromUtf8(key);
    if (!s->lengthUnit || depthUnitLabel.isEmpty()) return s->label;
    return QStringLiteral("%1 (%2)").arg(s->label, depthUnitLabel);
}

double cellParamValue(const MeshResult &mesh, int tri, const QByteArray &key)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    if (tri < 0 || tri >= mesh.triangles.size()) return nan;
    const MeshTriangle &t = mesh.triangles[tri];
    if (key == "mannings")  return t.mannings;
    if (key == "initDepth") return t.initDepth;
    return nan;   // unknown or engine-pending key carries no stored value
}

} // namespace mesh
