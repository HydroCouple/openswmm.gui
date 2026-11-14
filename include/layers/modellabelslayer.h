// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      modellabelslayer.h
 * @brief     The model's [LABELS] as a map text layer whose edits are saved in the model (G-01).
 * @details   The text layer is the single edit path: the existing text tools, styles and undo
 *            change its items, and every change that alters what [LABELS] can hold (text,
 *            position, font family, size, bold, italic) is written to the engine at once. Colors,
 *            halos and other styling stay in the project file. A change that leaves the labels as
 *            [LABELS] already holds them writes nothing, so an unedited section keeps its text.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include "project/modelannotations.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

class OpenSWMMVisAnnotationLayer;
class OpenSWMMVisLayer;
class SWMMModelLayer;

namespace openswmmvis::layers
{

/// Value of OpenSWMMVisAnnotationLayer::modelSection() for the model labels layer.
inline constexpr char kModelLabelsSection[] = "LABELS";

/** @return The labels @p layer holds, in item order, as [LABELS] rows. */
QList<project::ModelLabel> modelLabelsOf(const OpenSWMMVisAnnotationLayer &layer);

/** @brief Replace @p layer's items with @p labels; writes nothing to the model. */
void setModelLabels(OpenSWMMVisAnnotationLayer &layer, const QList<project::ModelLabel> &labels);

/**
 * @return Whether @p a and @p b hold the same labels. A label without a font matches one in the
 *   default text font, which is how such a label is shown.
 */
bool sameModelLabels(const QList<project::ModelLabel> &a, const QList<project::ModelLabel> &b);

/**
 * @brief A "Model labels" text layer for the model's [LABELS], in the model's map units.
 * @param[out] warnings Rows of [LABELS] that could not be read.
 * @return A new unparented layer, or nullptr when the model has no labels.
 */
OpenSWMMVisAnnotationLayer *createModelLabelsLayer(SWMMModelLayer &model, QStringList *warnings);

/**
 * @brief Write @p layer's labels to @p model's [LABELS] whenever they change.
 * @details Connected to annotationsChanged(); a refused write goes to the Message Log.
 */
void bindModelLabels(OpenSWMMVisAnnotationLayer *layer, SWMMModelLayer *model);

/**
 * @brief Apply a project's saved copy of the model labels layer to the live one.
 * @details Display settings (name, visibility, opacity, auto layout, layer id) always apply. The
 *   saved items, with their colors and halos, replace the live ones only when they hold the same
 *   labels as the model's [LABELS], so the model decides what the labels say.
 * @param[in,out] live The model labels layer built from the model, or nullptr when it has none.
 * @param[in] saved The layer's entry in the project file (layerJson()).
 * @return False when the saved labels differ from the model's (the model's are kept).
 */
bool applySavedModelLabels(OpenSWMMVisAnnotationLayer *live, const QJsonObject &saved);

/** @return The model labels layer among @p layers, or nullptr. */
OpenSWMMVisAnnotationLayer *findModelLabelsLayer(const QList<OpenSWMMVisLayer *> &layers);

} // namespace openswmmvis::layers
