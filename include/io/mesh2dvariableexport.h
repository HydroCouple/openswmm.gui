/**
 * @file      mesh2dvariableexport.h
 * @brief     Export of native-unit mesh result slices with validity and provenance.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_IO_MESH2DVARIABLEEXPORT_H
#define OPENSWMMVIS_IO_MESH2DVARIABLEEXPORT_H

#include <QString>

class IMesh2DSource;

namespace openswmmvis::io
{

// Export one native-unit cell slice with semantic identity and validity.
// Resolves the key against the source's current catalog; refuses unknown units
// and source-file aliases. Existing destinations survive any detected failure.
/**
 * @brief Publish one native-unit cell slice with semantic identity, validity, and provenance.
 * @param[in,out] source Borrowed source accessed synchronously on its owning thread; must remain stable throughout acquisition and publication.
 * @param[in] key Unique nonempty key resolved against the current variable catalog.
 * @param[in] frame Zero-based report frame; ignored for static/envelope values but retained as requested-frame provenance.
 * @param[in] path Nonempty CSV destination distinct from the result source, including filesystem aliases.
 * @param[out] error Optional diagnostic cleared on entry, set on validation, read, source-change, or publication failure.
 * @return True after atomic QSaveFile commit; false preserves a preexisting destination.
 * @details Requires declared units and cell-aligned values/status. Missing values have an empty numeric field; waterless/not-applicable raw values remain with explicit status. Coordinates and values are not converted.
 */
bool exportMesh2DVariableCsv(IMesh2DSource &source, const QString &key, int frame,
                             const QString &path, QString *error = nullptr);

} // namespace openswmmvis::io
#endif
