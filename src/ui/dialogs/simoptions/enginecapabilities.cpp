/*!
 * \file   enginecapabilities.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/enginecapabilities.h"

namespace openswmmvis::ui
{

namespace
{
/*!
 * \brief `swmm_options_get`, trimmed. Empty when the engine has no such key.
 *
 * This is the probe: the C ABI is string-keyed and stable across builds, so a
 * feature that landed after this engine was compiled is only detectable by
 * asking for one of its keys and getting nothing back.
 */
QString probe(SWMM_Engine e, const char *key)
{
    if (!e) return {};
    char buf[256] = {};
    if (swmm_options_get(e, key, buf, sizeof(buf)) == 0)
        return QString::fromUtf8(buf).trimmed();
    return {};
}
} // namespace

EngineCapabilities probeEngineCapabilities(SWMM_Engine e, const QString &version)
{
    EngineCapabilities c;
    c.engineVersion = version;
    c.legacy        = version.startsWith(QLatin1String("5."));

    // A legacy engine predates the whole option surface, so every probe below
    // would come back empty anyway — short-circuit rather than ask eleven
    // times. This mirrors the original's `if (!legacy) return;` structure.
    if (c.legacy) return c;

    c.fv          = !probe(e, "FV_CFL").isEmpty();
    c.transport   = !probe(e, "QUALITY_SOLVER").isEmpty();
    c.tpa         = !probe(e, "TPA_CELERITY").isEmpty();
    c.fvPressure  = !probe(e, "FV_PRESSURE_CLOSURE").isEmpty();
    c.uf          = !probe(e, "UNSTEADY_FRICTION").isEmpty();
    c.signedHeads = !probe(e, "REPORT_SIGNED_HEADS").isEmpty();

    // The four legacy-only blocks: available on any non-legacy engine.
    c.dynSlot       = true;
    c.semiImplicit  = true;
    c.andersonAccel = true;
    c.pluginWriters = true;

#ifdef OPENSWMM_HAS_2D
    c.has2D = (e != nullptr);
#else
    c.has2D = false;
#endif

    return c;
}

} // namespace openswmmvis::ui
