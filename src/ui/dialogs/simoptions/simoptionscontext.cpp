/*!
 * \file   simoptionscontext.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/simoptionscontext.h"

#include "layers/swmmmodellayer.h"
#include "ui/dialogs/simulationoptionsdialog.h"

namespace openswmmvis::ui
{

SimOptionsContext::SimOptionsContext(SWMM_Engine e, SWMMModelLayer *layer,
                                     SWMMVisProjectWindow *pw,
                                     EngineCapabilities caps)
    : engine_(e), layer_(layer), pw_(pw), caps_(std::move(caps))
{
}

QString SimOptionsContext::option(const char *key, const QString &fallback) const
{
    if (!engine_) return fallback;
    char buf[256] = {};
    if (swmm_options_get(engine_, key, buf, sizeof(buf)) == 0)
        return QString::fromUtf8(buf).trimmed();
    return fallback;
}

int SimOptionsContext::writeIfChanged(const char *key, const QString &newVal)
{
    // Recorded whether or not it is written: the reachability test compares
    // this list against the set of tagged editors, and a key that happened to
    // be unchanged this pass still needs an editor behind it.
    written_ << QString::fromLatin1(key);

    if (SimulationOptionsDialog::optionValueEquals(option(key), newVal))
        return 0;

    // Prefer the layer's setOption: it emits optionsChanged(), which the main
    // window and status bar listen to, so per-key writes live-sync instead of
    // depending on a post-hoc refresh. Fall back to the raw engine API when no
    // layer is bound (engine-only tests).
    if (layer_)
        return layer_->setOption(QByteArray(key), newVal) ? 1 : 0;

    if (!engine_) return 0;
    const QByteArray v = newVal.toUtf8();
    return swmm_options_set(engine_, key, v.constData()) == 0 ? 1 : 0;
}

void SimOptionsContext::recordWrittenKey(const char *key)
{
    written_ << QString::fromLatin1(key);
}

void SimOptionsContext::beginWritePass()
{
    written_.clear();
}

} // namespace openswmmvis::ui
