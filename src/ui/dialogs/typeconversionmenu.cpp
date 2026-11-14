// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialogs/typeconversionflow.h"
#include "ui/dialogs/inletjunctionsetupdialog.h"
#include "layers/swmmmodellayer.h"

#include <QMenu>
#include <QMessageBox>
#include <algorithm>

namespace openswmmvis::ui
{
namespace
{
using Outcome = TypeConversionOutcome;

QString label(const TypeConversionRequest &request)
{
    return request.family == SWMMObjectRef::Node
               ? TypeConversionFlow::nodeTypeLabel(request.target)
               : TypeConversionFlow::linkTypeLabel(request.target);
}

struct Summary {
    int eligible = 0, converted = 0, same = 0, skipped = 0;
    QMap<QString, QStringList> groups;

    explicit Summary(const QList<Outcome> &rows)
    {
        for (const auto &row : rows) {
            QString group;
            switch (row.state) {
            case Outcome::Eligible:
                ++eligible;
                continue;
            case Outcome::AlreadyTarget:
                ++same;
                group = QObject::tr("Already this type");
                break;
            case Outcome::Skipped:
                ++skipped;
                group = row.reason;
                break;
            case Outcome::Converted:
                ++converted;
                group = QObject::tr("Converted");
                if (!row.cleared.isEmpty())
                    group += QObject::tr(" — cleared %1").arg(row.cleared.join(", "));
                if (!row.warnings.isEmpty())
                    group += " — " + row.warnings.join(" / ");
                break;
            }
            groups[group].append(row.object.name);
        }
    }
    QString details() const
    {
        QStringList lines;
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
            lines << QStringLiteral("%1 (%2)\n%3")
                         .arg(it.key())
                         .arg(it.value().size())
                         .arg(it.value().join(", "));
        return lines.join("\n\n");
    }
};

bool message(QWidget *parent, const QString &title, const QString &text, const QString &details,
             bool confirm = false)
{
    QMessageBox box(confirm ? QMessageBox::Question : QMessageBox::Information, title, text,
                    confirm ? QMessageBox::Yes | QMessageBox::No : QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    if (confirm)
        box.setDefaultButton(QMessageBox::No);
    if (!details.isEmpty())
        box.setDetailedText(details);
    return box.exec() == QMessageBox::Yes;
}

void run(QPointer<QWidget> parent, QPointer<SWMMModelLayer> layer, TypeConversionRequest request,
         int hidden)
{
    if (!layer || !layer->engine())
        return;
    const auto engine = layer->engine();
    bool sameEngine = true;
    QObject lifetime;
    QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &lifetime,
                     [&] { sameEngine = false; });
    const auto preview = layer->convertTypes(request, false);
    const Summary before(preview);
    const QString title = QObject::tr("Convert to %1").arg(label(request));
    QString prompt = QObject::tr("%1 eligible, %2 already this type, %3 skipped.")
                         .arg(before.eligible)
                         .arg(before.same)
                         .arg(before.skipped);
    if (hidden)
        prompt += QObject::tr("\nSelected rows hidden by the filter: %1.").arg(hidden);
    if (!before.eligible) {
        message(parent, title, prompt, before.details());
        return;
    }
    prompt += QObject::tr("\n\nConvert the eligible objects? Incompatible attributes will be lost. "
                          "This cannot be undone.");
    if (!message(parent, title, prompt, before.details(), true) || !layer || !sameEngine ||
        layer->engine() != engine)
        return;

    // Only apply to IDs and source types the user confirmed. A modeless editor can
    // change the model during confirmation. The final engine call rechecks dependencies.
    auto final = preview;
    const auto live = layer->convertTypes(request, false);
    request.objects.clear();
    QList<int> positions;
    for (qsizetype i = 0; i < preview.size(); ++i) {
        if (preview[i].state != Outcome::Eligible)
            continue;
        if (live[i].state != Outcome::Eligible) {
            final[i] = live[i];
        } else if (live[i].source != preview[i].source) {
            final[i].state = Outcome::Skipped;
            final[i].reason = QObject::tr("The source type changed after confirmation.");
        } else {
            positions.append(i);
            request.objects.append(preview[i].object);
        }
    }
    const auto applied = layer->convertTypes(request, true);
    for (qsizetype i = 0; i < applied.size(); ++i)
        final[positions[i]] = applied[i];
    const Summary after(final);
    message(parent, title,
            QObject::tr("%1 converted, %2 already this type, %3 skipped.")
                .arg(after.converted)
                .arg(after.same)
                .arg(after.skipped),
            after.details());
}
} // namespace

void TypeConversionFlow::addActions(QMenu *menu, SWMMModelLayer *model,
                                    TypeConversionRequest request, int hiddenCount)
{
    if (!model || !model->engine() || request.objects.isEmpty())
        return;
    std::sort(request.objects.begin(), request.objects.end(), [](const auto &a, const auto &b) {
        return a.objectType == b.objectType ? a.name < b.name : a.objectType < b.objectType;
    });
    request.objects.erase(std::unique(request.objects.begin(), request.objects.end()),
                          request.objects.end());
    QPointer<SWMMModelLayer> layer(model);
    QPointer<QWidget> parent(menu->parentWidget() ? menu->parentWidget()->window() : nullptr);
    // A context menu may itself be a temporary top-level window. Use the owning application window.
    while (qobject_cast<QMenu *>(parent.data()))
        parent = parent->parentWidget();
    if (parent)
        parent = parent->window();
    const auto engine = model->engine();
    QObject::connect(model, &SWMMModelLayer::engineAboutToClose, menu, [menu] {
        menu->setEnabled(false);
        menu->close();
    });
    menu->setToolTipsVisible(true);
    // Supply a placeholder so native menus can open the lazy submenu.
    menu->addAction(tr("Checking types…"))->setEnabled(false);
    QObject::connect(menu, &QMenu::aboutToShow, menu, [=]() mutable {
        menu->clear();
        if (!layer || layer->engine() != engine)
            return;
        QStringList unavailable;
        const bool nodes = request.family == SWMMObjectRef::Node;
        const int count = nodes && layer->engineSupportsInletJunctions() ? 6 : 5;
        for (int target = 0; target < count; ++target) {
            request.target = target;
            request.topologyOnly = nodes && target == kInletNodeType;
            const auto rows = layer->convertTypes(request, false);
            const Summary summary(rows);
            if (summary.same == rows.size())
                continue;
            auto *action = menu->addAction(label(request));
            action->setData(target);
            if (!summary.eligible) {
                action->setEnabled(false);
                const auto reasons = summary.groups.keys().join("\n");
                action->setToolTip(reasons);
                unavailable << label(request) + "\n" + summary.details();
                continue;
            }
            QObject::connect(action, &QAction::triggered, menu, [=]() mutable {
                if (!layer || layer->engine() != engine)
                    return;
                if (!request.topologyOnly) {
                    run(parent, layer, request, hiddenCount);
                    return;
                }
                QVector<int> exclude;
                if (request.objects.size() == 1)
                    exclude.append(layer->nodeIndex(request.objects.first().name));
                auto *setup = new InletJunctionSetupDialog(layer, exclude, parent);
                setup->setAttribute(Qt::WA_DeleteOnClose);
                QObject::connect(layer, &QObject::destroyed, setup, &QWidget::close);
                QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, setup,
                                 &QWidget::close);
                QObject::connect(setup, &QDialog::accepted, setup, [=]() mutable {
                    if (!layer || layer->engine() != engine)
                        return;
                    request.topologyOnly = false;
                    request.design = setup->inletDesign();
                    request.capture = setup->captureNode();
                    request.placement = setup->placement();
                    run(parent, layer, request, hiddenCount);
                });
                setup->show();
                setup->raise();
                setup->activateWindow();
            });
        }
        if (!unavailable.isEmpty()) {
            menu->addSeparator();
            auto *why = menu->addAction(tr("Show unavailable conversions…"));
            QObject::connect(why, &QAction::triggered, menu, [=] {
                message(parent, tr("Unavailable conversions"),
                        tr("No selected object qualifies for these targets."),
                        unavailable.join("\n\n"));
            });
        }
    });
}

} // namespace openswmmvis::ui
