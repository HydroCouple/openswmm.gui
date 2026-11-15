/**
 * @file      propertyrows.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Shared behavior for property grids (QTreeView over a QPropertyModel): a font row
 *            shows its parts (family, size, bold, italic, …) when selected, and rows re-read
 *            an object that changed elsewhere.
 *
 *         Used by the Properties panel and by the legend and date/time Properties dialogs,
 *         which can show the same style object at once.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PROPERTIES_PROPERTYROWS_H
#define OPENSWMMVIS_UI_PROPERTIES_PROPERTYROWS_H

class QPropertyModel;
class QTreeView;

namespace openswmmvis
{

/**
 * @brief Let font rows open and close, and open a font row when it becomes the current row.
 * @param tree Property grid; call after its model is set (the selection model is replaced by
 *   setModel()).
 */
void showFontPartsOnSelection(QTreeView *tree);

/**
 * @brief Close every font row of @p tree, so only the selected font shows its parts. Call after
 *   expandAll().
 * @param tree Property grid.
 */
void collapseFontRows(QTreeView *tree);

/**
 * @brief Re-read every row from the shown object after it changed elsewhere, including the parts
 *   of font rows (which otherwise keep the values they were opened with).
 * @param model Property model; null does nothing. Writes each font back unchanged to refresh its
 *   parts, so the object's setters must ignore a value equal to the current one.
 */
void refreshPropertyRows(QPropertyModel *model);

} // namespace openswmmvis

#endif // OPENSWMMVIS_UI_PROPERTIES_PROPERTYROWS_H
