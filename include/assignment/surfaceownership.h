// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      surfaceownership.h
 * @brief     Inspection and editing of surface water ownership assignments.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_SURFACEOWNERSHIP_H
#define OPENSWMMVIS_SURFACEOWNERSHIP_H
#include <openswmm/engine/openswmm_surface_ownership.h>
#include <QVector>
#include <QString>
#include <functional>
class SWMM2DMeshLayer;
namespace openswmmvis::assignment
{
struct SurfaceOwnershipReview {
    QVector<int> before, after;
    QVector<SWMM_SurfaceOwnerObject> objects;
    QVector<SWMM_SurfaceOwnerShare> shares;
    QVector<double> meshWeatherArea;
    QString token, error;
    bool valid = false;
};
/**
 * @brief Read authored surface-owner records while the engine permits editing.
 * @param[in] engine Borrowed editing engine.
 * @param[out] rows Non-null destination for encoded owner records; may be resized before a failed fill read.
 * @param[out] error Non-null failure-message destination; unchanged on success.
 * @return True on a complete count/fill read; false on engine refusal.
 */
bool readSurfaceOwners(SWMM_Engine engine, QVector<int> *rows, QString *error);
/**
 * @brief Ask the engine to validate proposed ownership and compute object-area shares.
 * @param[in] engine Borrowed engine supplying current owners and mesh geometry.
 * @param[in] rows Proposed encoded ownership records passed to the engine unchanged.
 * @param[in] progress Optional synchronous (done,total) callback; true continues and false requests cancellation.
 * @return Prior/proposed records, computed shares and a token for stale-preview protection. Apply only when valid is true and the review diagnostics are acceptable.
 * @details Performs count and fill preview calls; engine changes between calls produce an error. Does not replace ownership records.
 */
SurfaceOwnershipReview previewSurfaceOwners(SWMM_Engine engine, const QVector<int> &rows,
                                            const std::function<bool(int, int)> &progress = {});
/**
 * @brief Replace ownership records using the engine-issued preview token.
 * @param[in] engine Borrowed editing engine.
 * @param[in] rows Reviewed encoded ownership records.
 * @param[in] token Token captured by previewSurfaceOwners(); stale model state is rejected by the engine.
 * @param[out] error Non-null failure-message destination; unchanged on success.
 * @return True when the engine accepts the replacement; false with a diagnostic otherwise.
 */
bool replaceSurfaceOwners(SWMM_Engine engine, const QVector<int> &rows, const QString &token,
                          QString *error);
/**
 * @brief Check that the GUI mesh and engine mesh describe the same cells before ownership review.
 * @param[in] engine Borrowed engine; nullptr is rejected.
 * @param[in] layer Borrowed active GUI mesh; nullptr is rejected.
 * @param[out] error Non-null failure-message destination; an empty matching mesh returns false without a new message.
 * @return True only for a nonempty mesh with matching vertex/cell counts, finite coordinates/elevations within tolerance, and identical ordered connectivity.
 * @details Reads GUI layer state; call on the GUI thread. No geometry or ownership is changed.
 */
bool surfaceOwnershipMeshMatches(SWMM_Engine engine, const SWMM2DMeshLayer *layer, QString *error);
}
#endif
