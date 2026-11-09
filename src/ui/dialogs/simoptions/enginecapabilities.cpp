/**
 * @file      enginecapabilities.cpp
 * @brief     One-shot engine capability probe for the Simulation Options dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/simoptions/enginecapabilities.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonParseError>
#include <vector>

namespace openswmmvis::ui
{

namespace
{
/**
 * @brief Read one trimmed fixed-buffer engine option for capability detection.
 * @param e Borrowed engine, permitted null.
 * @param key Borrowed non-null null-terminated option key when e is present.
 * @return Owned UTF-8 text from a 256-byte buffer, empty on null engine, failed
 * getter or blank successful value. Nonempty text means key availability, including
 * a disabled-looking value such as NO; no boolean interpretation or setter call.
 */
QString probe(SWMM_Engine e, const char *key)
{
    if (!e)
        return {};
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
    c.legacy = version.startsWith(QLatin1String("5."));

    // A legacy engine predates the whole option surface, so every probe below
    // would come back empty anyway — short-circuit rather than ask eleven
    // times. This mirrors the original's `if (!legacy) return;` structure.
    if (c.legacy)
        return c;

    // The C getter reports success even when its output is truncated. Retry
    // until the growing capability schema parses, or leave the tab gated.
    if (e) {
        std::vector<char> schema(8192, '\0');
        for (int attempt = 0; attempt < 5; ++attempt) {
            if (swmm_options_get(e, "FV_IMPLICIT_SCHEMA", schema.data(), schema.size()) != 0)
                break;
            QJsonParseError error;
            const auto parsed = QJsonDocument::fromJson(QByteArray(schema.data()), &error);
            if (error.error == QJsonParseError::NoError && parsed.isObject()) {
                c.implicitFvSchema = parsed.object();
                c.implicitFv = c.implicitFvSchema.value("version").toInt() == 1 &&
                               c.implicitFvSchema.value("native").toBool() &&
                               c.implicitFvSchema.value("time_integration")
                                   .toArray()
                                   .contains("BACKWARD_EULER");
                break;
            }
            schema.resize(schema.size() * 2, '\0');
        }
    }
    c.fv = !probe(e, "FV_CFL").isEmpty();
    c.transport = !probe(e, "QUALITY_SOLVER").isEmpty();
    c.tpa = !probe(e, "TPA_CELERITY").isEmpty();
    c.fvPressure = !probe(e, "FV_PRESSURE_CLOSURE").isEmpty();
    c.uf = !probe(e, "UNSTEADY_FRICTION").isEmpty();
    c.signedHeads = !probe(e, "REPORT_SIGNED_HEADS").isEmpty();

    // The four legacy-only blocks: available on any non-legacy engine.
    c.dynSlot = true;
    c.semiImplicit = true;
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
