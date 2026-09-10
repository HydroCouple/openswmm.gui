/*!
 * \file   enginecapabilities.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * One-shot engine capability probe for the Simulation Options dialog.
 *
 * Replaces the in-place `setEnabled()` calls of
 * `SimulationOptionsDialog::applyEngineConstraints()`. Probed once in the
 * dialog constructor and never re-probed: the engine's option surface does not
 * change while a model is open.
 *
 * Each field maps 1:1 onto one `if` block of the original
 * `applyEngineConstraints()`, and each carries a comment naming the block it
 * came from so the port stays auditable (HANDOFF §2.1).
 */
#ifndef ENGINECAPABILITIES_H
#define ENGINECAPABILITIES_H

#include <QString>

#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::ui
{

/*!
 * \brief What this engine build accepts, probed once.
 *
 * The C ABI is string-keyed and unchanged across builds, so a capability is
 * only detectable by asking the engine for one of its keys — never by the
 * version string alone. `legacy` is the one exception: a 5.x engine is
 * identified by version because it predates the whole option surface.
 */
struct EngineCapabilities
{
    QString engineVersion;   ///< As handed to the dialog, e.g. "6.0.0".

    /// engineVersion starts with "5." — the legacy solver. Gates the four
    /// legacy-only blocks below and short-circuits every probe.
    bool legacy = false;

    // ---- probed keys: one field per `if` block of applyEngineConstraints ----
    bool fv          = false;  ///< probe FV_CFL              (FLOW_ROUTING FV item)
    bool transport   = false;  ///< probe QUALITY_SOLVER      (solver combo, outfall backflow,
                               ///<                            ARD + LARD groups, water age, heat)
    bool tpa         = false;  ///< probe TPA_CELERITY        (SURCHARGE_METHOD TPA item + celerity)
    bool fvPressure  = false;  ///< probe FV_PRESSURE_CLOSURE (FV pressure-closure combo)
    bool uf          = false;  ///< probe UNSTEADY_FRICTION   (was: m_ufSupported)
    bool signedHeads = false;  ///< probe REPORT_SIGNED_HEADS (signed-heads check)

    // ---- legacy-only blocks: false on a 5.x engine, true otherwise ----
    bool dynSlot       = false;  ///< SURCHARGE_METHOD DYNAMIC_SLOT item
    bool semiImplicit  = false;  ///< NODE_CONTINUITY SEMI_IMPLICIT item
    bool andersonAccel = false;  ///< ANDERSON_ACCEL check box
    bool pluginWriters = false;  ///< writers group + [PLUGINS] table

    bool has2D = false;  ///< OPENSWMM_HAS_2D && the engine reports a 2D surface.
};

/*!
 * \brief Probe \a e once for everything the dialog gates on.
 * \param e       Engine handle; may be null (every capability reads false).
 * \param version Engine version string as passed to the dialog.
 */
EngineCapabilities probeEngineCapabilities(SWMM_Engine e, const QString &version);

} // namespace openswmmvis::ui

#endif // ENGINECAPABILITIES_H
