/**
 * @file      configurationsession.cpp
 * @brief     Restoration of editor-owned model configuration sessions.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/configurationsession.h"

#include "layers/swmmmodellayer.h"
#include "ui/dialogs/climatologydialog.h"
#include "ui/dialogs/heatconfigdialog.h"

#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_gw_transport.h>

#include <QMessageBox>
#include <QUndoStack>

namespace OpenSWMMVis
{

namespace
{

// A pushed command that removes itself discards the redo entries above it.
class DiscardRedo final : public QUndoCommand
{
public:
    /**
     * @brief Mark this throwaway command obsolete during push so QUndoStack discards the canceled segment's redo branch.
     */
    void redo() override { setObsolete(true); }
};

} // namespace

QString configurationIdentity(SWMM_Engine engine, const QString &anchor,
                              const QStringList &optionKeys)
{
    if (!engine)
        return {};
    QString id = HeatConfigDialog::configurationSignature(engine) + QChar(0x1e) +
                 ClimatologyDialog::configurationSignature(engine, anchor);
    for (const auto &key : optionKeys) {
        // Base options use their own getters; groundwater transport has its
        // dedicated getter because its values are not generic extensions.
        const QByteArray name = key.toUtf8();
        char value[1024] = {};
        if (key.startsWith(QStringLiteral("GW_TRANSPORT:"))) {
            const QByteArray transportName =
                key.mid(QStringLiteral("GW_TRANSPORT:").size()).toUtf8();
            if (swmm_gw_transport_option_get(engine, transportName.constData(), value,
                                             sizeof value) != SWMM_OK)
                value[0] = '\0';
        } else if (swmm_options_get(engine, name.constData(), value, sizeof value) != SWMM_OK &&
                   swmm_options_get_ext(engine, name.constData(), value, sizeof value) != SWMM_OK) {
            value[0] = '\0';
        }
        id += QChar(0x1e) + key + QLatin1Char('=') + QString::fromUtf8(value);
    }
    return id;
}

bool pushConfigurationUndo(QUndoStack *stack, SWMMModelLayer *layer, const QString &text,
                           ConfigSnapshot before, QString beforeIdentity, const QString &anchor,
                           const QStringList &optionKeys, std::function<void(QString)> failed)
{
    const SWMM_Engine engine = layer ? layer->engine() : nullptr;
    if (!stack || !engine || !before)
        return false;
    ConfigSnapshot after(swmm_config_snapshot_create(engine));
    if (!after)
        return false;
    const QString afterIdentity = configurationIdentity(engine, anchor, optionKeys);
    if (beforeIdentity.isEmpty()) {
        if (swmm_config_snapshot_restore(engine, before.get()) != SWMM_OK)
            return false;
        beforeIdentity = configurationIdentity(engine, anchor, optionKeys);
        if (swmm_config_snapshot_restore(engine, after.get()) != SWMM_OK)
            return false;
    }
    if (beforeIdentity == afterIdentity)
        return false;
    QPointer<SWMMModelLayer> model(layer);
    const QStringList changed = optionKeys.isEmpty() ? QStringList{QStringLiteral("CLIMATE"),
                                                                   QStringLiteral("HEAT_TRANSPORT")}
                                                     : optionKeys;
    stack->push(new ConfigurationUndo(
        text, layer, engine, std::move(before), std::move(after), beforeIdentity, afterIdentity,
        [model] { return model ? model->engine() : nullptr; },
        [anchor, optionKeys](SWMM_Engine e) {
            return configurationIdentity(e, anchor, optionKeys);
        },
        [model, changed] {
            if (model) {
                model->markEdited();
                emit model->optionsChanged(changed);
            }
        },
        failed ? std::move(failed)
               : [](const QString
                        &error) { QMessageBox::warning(nullptr, QObject::tr("Undo"), error); }));
    return true;
}

ConfigurationSession::ConfigurationSession(SWMMModelLayer *layer, QUndoStack *stack, QString text,
                                           QString anchor)
    : layer_(layer), stack_(stack), text_(std::move(text)), anchor_(std::move(anchor))
{
}

ConfigurationSession::~ConfigurationSession()
{
    cancel();
}

void ConfigurationSession::openSegment()
{
    if (open_ || !stack_)
        return;
    stack_->beginMacro(text_);
    open_ = true;
}

void ConfigurationSession::commit(ConfigSnapshot before, const QStringList &optionKeys)
{
    if (stack_ && layer_)
        pushConfigurationUndo(stack_, layer_, text_, std::move(before), {}, anchor_, optionKeys,
                              failed_);
    if (!open_ || !stack_)
        return;
    stack_->endMacro();
    open_ = false;
    // A segment in which nothing was pushed leaves an empty entry; drop it.
    const auto *macro = stack_->command(stack_->index() - 1);
    if (macro && macro->childCount() == 0) {
        stack_->undo();
        stack_->push(new DiscardRedo);
    }
}

void ConfigurationSession::abandon()
{
    if (!open_ || !stack_) {
        open_ = false;
        return;
    }
    stack_->endMacro();
    open_ = false;
    // Model closure may interrupt a nested editor before it has committed
    // anything. Do not leave the empty macro created by openSegment() in the
    // user's Undo history.
    const auto *macro = stack_->command(stack_->index() - 1);
    if (macro && macro->childCount() == 0) {
        stack_->undo();
        stack_->push(new DiscardRedo);
    }
}

bool ConfigurationSession::cancel()
{
    if (!open_ || !stack_)
        return false;
    stack_->endMacro();
    open_ = false;
    const auto *macro = stack_->command(stack_->index() - 1);
    const bool edits = macro && macro->childCount() > 0;
    stack_->undo();
    stack_->push(new DiscardRedo);
    return edits;
}

} // namespace OpenSWMMVis
