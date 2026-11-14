/**
 * @file      fieldwriter.h
 * @brief     Hook through which single-object property adapters send writes to the
 *            project's checked, undoable edit path.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Review C-01/E-07 (workplans/SPATIAL_EDIT_PATH_AND_DELETE_UNDO_PLAN_2026-11-12.md, Part A):
 * PropertiesPanel installs a FieldWriter on each spatial adapter. A setter offers its
 * Attribute Table field key first and writes the engine itself only when the writer
 * does not route that field.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PROPERTIES_FIELDWRITER_H
#define OPENSWMMVIS_UI_PROPERTIES_FIELDWRITER_H

#include <QString>
#include <QVariant>

#include <functional>

namespace openswmmvis::properties
{

/// Outcome of offering one field write to the shared edit path.
enum class FieldWrite {
    NotRouted, ///< The object's table has no such field; the adapter writes directly.
    Applied,   ///< Written through the checked command (one undo step).
    Refused,   ///< Rejected (lifecycle, validation or read-back); the reason was shown.
};

/// Arguments: object name, Attribute Table field key, value in the table's representation.
using FieldWriter =
    std::function<FieldWrite(const QString &objectName, const QString &fieldKey, const QVariant &value)>;

} // namespace openswmmvis::properties

#endif // OPENSWMMVIS_UI_PROPERTIES_FIELDWRITER_H
