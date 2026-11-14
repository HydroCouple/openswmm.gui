/**
 * @file      simoptionscontext.cpp
 * @brief     The one thing every Simulation Options page is handed.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/simoptions/simoptionscontext.h"

#include "layers/swmmmodellayer.h"
#include "core/enginestring.h"
#include <QMetaProperty>
#include <algorithm>
#include "ui/dialogs/simulationoptionsdialog.h"

namespace openswmmvis::ui
{

SimOptionsContext::SimOptionsContext(SWMM_Engine e, SWMMModelLayer *layer, SWMMVisProjectWindow *pw,
                                     EngineCapabilities caps)
    : engine_(e), layer_(layer), pw_(pw), caps_(std::move(caps))
{
}

QString SimOptionsContext::option(const char *key, const QString &fallback) const
{
    if (!engine_)
        return fallback;
    return openswmmvis::readEngineString([&](char *buf, int size) {
        return swmm_options_get(engine_, key, buf, size);
    }, fallback).trimmed();
}

int SimOptionsContext::writeIfChanged(const char *key, const QString &newVal, const QWidget *owner)
{
    // Recorded whether or not it is written: the reachability test compares
    // this list against the set of tagged editors, and a key that happened to
    // be unchanged this pass still needs an editor behind it.
    written_ << QString::fromLatin1(key);

    // Preserve the exact imported value until its editor changes. Numeric
    // equality must also distinguish real edits below display precision.
    const QString desired = valueForWrite(key, newVal, owner);
    const QString previous = option(key);
    bool a = false, b = false;
    const double oldNumber = previous.toDouble(&a), newNumber = desired.toDouble(&b);
    if ((a && b) ? oldNumber == newNumber : previous == desired)
        return 0;

    // Prefer the layer's setOption: it emits optionsChanged(), which the main
    // window and status bar listen to, so per-key writes live-sync instead of
    // depending on a post-hoc refresh. Fall back to the raw engine API when no
    // layer is bound (engine-only tests).
    if (layer_)
        return layer_->setOption(QByteArray(key), desired) ? 1 : 0;

    if (!engine_)
        return 0;
    const QByteArray v = desired.toUtf8();
    return swmm_options_set(engine_, key, v.constData()) == 0 ? 1 : 0;
}

void SimOptionsContext::recordWrittenKey(const char *key)
{
    written_ << QString::fromLatin1(key);
}

void SimOptionsContext::captureEditors(QWidget *root)
{
    for (auto it = baselines_.begin(); it != baselines_.end(); ++it) {
        auto &entries = it.value();
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
            return !entry.first || root == entry.first || root->isAncestorOf(entry.first);
        }), entries.end());
    }
    for (auto *widget : root->findChildren<QWidget *>()) {
        const auto property = widget->metaObject()->userProperty();
        if (!property.isValid()) continue;
        const auto keys = widget->property("optionKey").toString().split(',', Qt::SkipEmptyParts);
        for (const auto &key : keys) {
            baselines_[key].append({widget, property.read(widget)});
            const auto utf = key.toUtf8();
            const QString missing = QStringLiteral("__missing_engine_option__");
            const auto common = option(utf.constData(), missing);
            if (common != missing) original_[key] = common;
            if (engine_) {
                const auto ext = openswmmvis::readEngineString([&](char *b, int n) {
                    return swmm_options_get_ext(engine_, utf.constData(), b, n);
                }, missing);
                if (ext != missing) originalExtended_[key] = ext;
            }
        }
    }
}

QString SimOptionsContext::valueForWrite(const char *key, const QString &value,
                                         const QWidget *owner, bool extended) const
{
    const auto &original = extended ? originalExtended_ : original_;
    const auto it = original.constFind(QString::fromLatin1(key));
    return !editorChanged(key, owner) && it != original.cend() ? *it : value;
}

bool SimOptionsContext::editorChanged(const char *key, const QWidget *owner) const
{
    const auto found = baselines_.constFind(QString::fromLatin1(key));
    if (found == baselines_.cend()) return true;
    bool matched = false;
    for (const auto &entry : *found) {
        if (!entry.first || (owner && owner != entry.first && !owner->isAncestorOf(entry.first))) continue;
        matched = true;
        if (entry.first->metaObject()->userProperty().read(entry.first) != entry.second) return true;
    }
    return !matched;
}

void SimOptionsContext::beginWritePass()
{
    written_.clear();
}

} // namespace openswmmvis::ui
