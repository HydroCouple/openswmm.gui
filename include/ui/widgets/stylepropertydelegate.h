/*!
 * \file   stylepropertydelegate.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice D1-a (MESH_SUBLAYER_CLASSIFICATION_UI_PLAN) — factory for the
 *         QPropertyItemDelegate used by the sublayer style grids.
 *
 *         Returns a QPropertyItemDelegate already extended with the
 *         ClassificationSchemeCellEditor for ClassificationScheme-typed
 *         properties. The base delegate already supplies named combos for
 *         enum Q_PROPERTYs (Q_ENUM), so installing this one editor fixes both
 *         the integer-enumerator fallback AND the classification popup.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_STYLEPROPERTYDELEGATE_H
#define OPENSWMMVIS_UI_WIDGETS_STYLEPROPERTYDELEGATE_H

class QObject;
class QPropertyItemDelegate;

namespace openswmmvis::ui {

/*! New a QPropertyItemDelegate, register the ClassificationScheme cell editor
 *  on it, and return it. Caller installs it via QTreeView::setItemDelegate. */
QPropertyItemDelegate *makeStyleDelegate(QObject *parent);

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_STYLEPROPERTYDELEGATE_H
