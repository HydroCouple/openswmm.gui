// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modellabelslayer.cpp
 * @brief     The model's [LABELS] as a map text layer whose edits are saved in the model (G-01).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "layers/modellabelslayer.h"

#include "layers/annotationlayer.h"
#include "layers/annotationtextitem.h"
#include "layers/swmmmodellayer.h"
#include "ui/util/usernotice.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSignalBlocker>

namespace openswmmvis::layers
{

using project::ModelLabel;

namespace
{
QString defaultFamily()
{
    static const QString family = AnnotationTextItem().font().family();
    return family;
}
} // namespace

QList<ModelLabel> modelLabelsOf(const OpenSWMMVisAnnotationLayer &layer)
{
    QList<ModelLabel> labels;
    for (const AnnotationTextItem *item : layer.annotations()) {
        const QFont font = item->font();
        labels.append({item->x(), item->y(), item->text(), layer.modelAnchor(item->id()),
                       font.family(), font.pointSizeF() > 0 ? font.pointSizeF() : 10.0,
                       font.bold(), font.italic()});
    }
    return labels;
}

void setModelLabels(OpenSWMMVisAnnotationLayer &layer, const QList<ModelLabel> &labels)
{
    {
        // One change notice for the whole replacement, as fromJson() gives.
        const QSignalBlocker block(&layer);
        for (const AnnotationTextItem *item : layer.annotations())
            layer.removeAnnotation(item->id());
        int index = 0;
        for (const ModelLabel &label : labels) {
            auto *item = new AnnotationTextItem;
            item->setId(QStringLiteral("model-label-%1").arg(++index));
            item->setText(label.text);
            item->setX(label.x);
            item->setY(label.y);
            QFont font = item->font();
            if (!label.fontFamily.isEmpty())
                font.setFamily(label.fontFamily);
            font.setPointSizeF(label.fontSize);
            font.setBold(label.bold);
            font.setItalic(label.italic);
            item->setFont(font);
            if (!layer.addAnnotation(item))
                delete item;
            else
                layer.setModelAnchor(item->id(), label.anchor);
        }
    }
    emit layer.annotationsChanged();
    emit layer.repaintRequested();
}

bool sameModelLabels(const QList<ModelLabel> &a, const QList<ModelLabel> &b)
{
    if (a.size() != b.size())
        return false;
    const auto family = [](const QString &name) { return name.isEmpty() ? defaultFamily() : name; };
    for (qsizetype i = 0; i < a.size(); ++i) {
        ModelLabel left = a[i], right = b[i];
        left.fontFamily = family(left.fontFamily);
        right.fontFamily = family(right.fontFamily);
        if (!(left == right))
            return false;
    }
    return true;
}

OpenSWMMVisAnnotationLayer *createModelLabelsLayer(SWMMModelLayer &model, QStringList *warnings)
{
    const QList<ModelLabel> labels =
        project::parseLabels(project::readAnnotation(model.engine(), "LABELS"), warnings);
    if (labels.isEmpty())
        return nullptr;
    auto *layer = new OpenSWMMVisAnnotationLayer(
        QCoreApplication::translate("ModelLabelsLayer", "Model labels"));
    layer->setModelSection(QString::fromLatin1(kModelLabelsSection));
    setModelLabels(*layer, labels);
    return layer;
}

void bindModelLabels(OpenSWMMVisAnnotationLayer *layer, SWMMModelLayer *model)
{
    if (!layer || !model)
        return;
    const QPointer<OpenSWMMVisAnnotationLayer> guardedLayer(layer);
    const QPointer<SWMMModelLayer> guardedModel(model);
    QObject::connect(layer, &OpenSWMMVisAnnotationLayer::annotationsChanged, model,
                     [guardedLayer, guardedModel] {
                         if (!guardedLayer || !guardedModel || !guardedModel->engine())
                             return;
                         const SWMM_Engine engine = guardedModel->engine();
                         const QList<ModelLabel> labels = modelLabelsOf(*guardedLayer);
                         if (sameModelLabels(
                                 project::parseLabels(project::readAnnotation(engine, "LABELS")),
                                 labels))
                             return;
                         QString error;
                         if (!project::writeAnnotation(engine, "LABELS", project::labelsText(labels),
                                                       &error)) {
                             openswmmvis::ui::notifyUser(
                                 nullptr, openswmmvis::ui::NoticeSeverity::Error,
                                 QCoreApplication::translate("ModelLabelsLayer",
                                                             "Map labels were not saved in the "
                                                             "model: %1")
                                     .arg(error));
                             return;
                         }
                         guardedModel->markEdited();
                     });
}

bool applySavedModelLabels(OpenSWMMVisAnnotationLayer *live, const QJsonObject &saved)
{
    OpenSWMMVisAnnotationLayer copy;
    copy.restoreLayer(saved);
    const QList<ModelLabel> savedLabels = modelLabelsOf(copy);
    if (!live)
        return savedLabels.isEmpty();
    live->restoreLayerId(copy.layerId());
    live->setName(copy.name());
    live->setVisible(copy.isVisible());
    live->setOpacity(copy.opacity());
    live->setAutoLayout(copy.autoLayout());
    if (!sameModelLabels(savedLabels, modelLabelsOf(*live)))
        return false;
    // Anchors first: the replacement's change notice must already see them.
    for (const AnnotationTextItem *item : copy.annotations())
        live->setModelAnchor(item->id(), copy.modelAnchor(item->id()));
    live->fromJson(copy.toJson());
    return true;
}

OpenSWMMVisAnnotationLayer *findModelLabelsLayer(const QList<OpenSWMMVisLayer *> &layers)
{
    for (OpenSWMMVisLayer *base : layers)
        if (auto *layer = qobject_cast<OpenSWMMVisAnnotationLayer *>(base))
            if (layer->modelSection() == QLatin1String(kModelLabelsSection))
                return layer;
    return nullptr;
}

} // namespace openswmmvis::layers
