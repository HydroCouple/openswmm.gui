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

namespace {

/*! Editor configuration for one named infiltration parameter. Ranges are
 *  generous on purpose: the values are in PROJECT units (in/hr or mm/hr,
 *  in or mm — user decision 2026-08-20) and the GUI performs no conversion,
 *  so one spec has to cover both unit systems. */
struct InfilParamUi
{
    const char *key;
    const char *label;
    double      min;
    double      max;
    double      step;
    double      defaultValue;
    int         decimals;
    const char *tooltip;
};

/*! One entry per mesh::infilParamKeys() key, in that order. Keys shared by
 *  several methods (infil.dryTime is Horton's slot 3 AND Curve Number's
 *  slot 2) appear once — the method-dependent slot mapping lives in
 *  mesh::infilParamKey/infilSlotForKey, not here. */
const InfilParamUi kInfilParamUi[] = {
    {"infil.f0",      QT_TRANSLATE_NOOP("MeshCellParams", "Max. Infil. Rate (f0)"),
     0.0, 1000.0, 0.1, 3.0, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Horton maximum infiltration rate, in project rate units (in/hr or "
         "mm/hr). [2D_INFILTRATION] P1.")},
    {"infil.fmin",    QT_TRANSLATE_NOOP("MeshCellParams", "Min. Infil. Rate (fmin)"),
     0.0, 1000.0, 0.1, 0.5, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Horton minimum (saturated) infiltration rate, in project rate units. "
         "[2D_INFILTRATION] P2.")},
    {"infil.decay",   QT_TRANSLATE_NOOP("MeshCellParams", "Decay Constant (1/hr)"),
     0.0, 100.0, 0.1, 4.0, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Horton decay constant in 1/hr. [2D_INFILTRATION] P3.")},
    {"infil.dryTime", QT_TRANSLATE_NOOP("MeshCellParams", "Drying Time (days)"),
     0.0, 1000.0, 0.5, 7.0, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Days for a fully saturated soil to dry out. Horton reads it from "
         "P4, Curve Number from P3.")},
    {"infil.Fmax",    QT_TRANSLATE_NOOP("MeshCellParams", "Max. Infil. Volume (Fmax)"),
     0.0, 1.0e6, 1.0, 0.0, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Modified Horton maximum infiltration volume, in project depth units "
         "(0 = unlimited). [2D_INFILTRATION] P5.")},
    {"infil.suction", QT_TRANSLATE_NOOP("MeshCellParams", "Suction Head"),
     0.0, 1.0e4, 0.1, 3.5, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Green-Ampt soil capillary suction, in project depth units. "
         "[2D_INFILTRATION] P1.")},
    {"infil.Ks",      QT_TRANSLATE_NOOP("MeshCellParams", "Conductivity (Ks)"),
     0.0, 1000.0, 0.01, 0.5, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Green-Ampt saturated hydraulic conductivity, in project rate units. "
         "[2D_INFILTRATION] P2.")},
    {"infil.IMD",     QT_TRANSLATE_NOOP("MeshCellParams", "Initial Deficit (IMD)"),
     0.0, 1.0, 0.01, 0.26, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Green-Ampt initial soil moisture deficit as a fraction in 0..1. "
         "[2D_INFILTRATION] P3.")},
    {"infil.CN",      QT_TRANSLATE_NOOP("MeshCellParams", "Curve Number (CN)"),
     1.0, 100.0, 1.0, 80.0, 2,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "SCS curve number in 1..100. [2D_INFILTRATION] P1.")},
    {"infil.rate",    QT_TRANSLATE_NOOP("MeshCellParams", "Infiltration Rate"),
     0.0, 1000.0, 0.1, 0.5, 4,
     QT_TRANSLATE_NOOP("MeshCellParams",
         "Constant infiltration rate, in project rate units. "
         "[2D_INFILTRATION] P1.")},
};

} // namespace

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

        // ---- Live: per-cell infiltration (GUI plan §3.5(2), phase GG0b) ----
        // Values resolve through mesh::resolveInfil, so a cell inheriting from
        // its region tag reads back the region's numbers; an edit materialises
        // a per-cell [2D_INFILTRATION] override. Parameter columns are masked
        // per row by the resolved METHOD (mesh::infilUsesParam) — a cell whose
        // method does not use a slot renders "—" and refuses edits.
        {
            CellParamSpec m;
            m.key        = QByteArrayLiteral("infil.method");
            m.label      = tr("Infiltration Method");
            m.kind       = CellParamSpec::Kind::Enum;
            m.enumLabels = infilMethodLabels();
            m.min        = double(int(InfilMethod::None));
            m.max        = double(int(InfilMethod::Constant));
            m.step       = 1.0;
            m.defaultValue = double(int(InfilMethod::None));
            m.decimals   = 0;
            m.enabled    = true;
            m.tooltip    = tr("Per-cell infiltration model "
                              "([2D_INFILTRATION] METHOD). Inherited from the "
                              "cell's region tag until it is set here.");
            v.append(m);
        }
        for (const InfilParamUi &p : kInfilParamUi) {
            CellParamSpec s;
            s.key          = QByteArray(p.key);
            s.label        = tr(p.label);
            s.min          = p.min;
            s.max          = p.max;
            s.step         = p.step;
            s.defaultValue = p.defaultValue;
            s.decimals     = p.decimals;
            s.enabled      = true;
            s.tooltip      = tr(p.tooltip);
            v.append(s);
        }

        // ---- Pending: 2D two-zone groundwater ------------------------------
        // Shown greyed so the roadmap is visible in every selector. Keys and
        // ranges follow the engine's draft [2D_AQUIFER] design
        // (plans/TWO_ZONE_GROUNDWATER_EXPLICIT_LTS_PLAN_2026-08-15.md);
        // applyCellParam refuses them until the engine and MeshTriangle carry
        // the fields.
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
    if (key.startsWith("infil.")) {
        // Resolved, not raw: a cell with no per-cell override reads back its
        // region tag's (or the '*' row's) numbers, which is what the engine
        // will run. resolveInfil never mutates the mesh.
        const ResolvedInfil r = resolveInfil(mesh, tri);
        if (key == "infil.method") return double(int(r.row.method));
        const int slot = infilSlotForKey(r.row.method, key);
        return slot < 0 ? nan : r.row.p[slot];  // NaN = this method has no such slot
    }
    return nan;   // unknown or engine-pending key carries no stored value
}

} // namespace mesh
