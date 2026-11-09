/**
 * @file      enginecapabilities.h
 * @brief     One-shot engine capability probe for the Simulation Options dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef ENGINECAPABILITIES_H
#define ENGINECAPABILITIES_H

#include <QString>
#include <QJsonObject>

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
struct EngineCapabilities {
    QString engineVersion; ///< As handed to the dialog, e.g. "6.0.0".

    /// engineVersion starts with "5." — the legacy solver. Gates the four
    /// legacy-only blocks below and short-circuits every probe.
    bool legacy = false;

    // ---- probed keys: one field per `if` block of applyEngineConstraints ----
    QJsonObject implicitFvSchema; ///< Read-only engine contract; empty on older builds.
    bool implicitFv = false;
    bool fv = false;          ///< probe FV_CFL              (FLOW_ROUTING FV item)
    bool transport = false;   ///< probe QUALITY_SOLVER      (solver combo, outfall backflow,
                              ///<                            ARD + LARD groups, water age, heat)
    bool tpa = false;         ///< probe TPA_CELERITY        (SURCHARGE_METHOD TPA item + celerity)
    bool fvPressure = false;  ///< probe FV_PRESSURE_CLOSURE (FV pressure-closure combo)
    bool uf = false;          ///< probe UNSTEADY_FRICTION   (was: m_ufSupported)
    bool signedHeads = false; ///< probe REPORT_SIGNED_HEADS (signed-heads check)

    // ---- legacy-only blocks: false on a 5.x engine, true otherwise ----
    bool dynSlot = false;       ///< SURCHARGE_METHOD DYNAMIC_SLOT item
    bool semiImplicit = false;  ///< NODE_CONTINUITY SEMI_IMPLICIT item
    bool andersonAccel = false; ///< ANDERSON_ACCEL check box
    bool pluginWriters = false; ///< writers group + [PLUGINS] table

    bool has2D = false; ///< OPENSWMM_HAS_2D and a non-null engine handle; no mesh query.
};

/**
 * @brief Snapshot the option-editor capability gates for one engine/version pair.
 * @param e Borrowed valid engine during this read, permitted null.
 * @param version Copied version text; an exact leading 5. marks legacy and
 * short-circuits all probes without trimming or version parsing.
 * @return Owned capability value. Nonlegacy ordinary feature flags require a
 * nonempty successful option read. Implicit-FV support requires parsed schema
 * version one, native true and BACKWARD_EULER integration; parsing retries five
 * buffers from 8 through 128 KiB. Parsed schema is retained even when unsupported.
 * For nonlegacy versions the four legacy-exclusion flags are true even with a
 * null handle. has2D checks only build-time 2D support and a non-null handle, not
 * runtime mesh existence. Does not mutate options, probe again later or own the engine.
 */
EngineCapabilities probeEngineCapabilities(SWMM_Engine e, const QString &version);

} // namespace openswmmvis::ui

#endif // ENGINECAPABILITIES_H
