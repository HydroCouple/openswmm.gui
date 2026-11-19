/**
 * @file      markershapeeditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QPropertyModel editor for OpenSWMM::Render::MarkerShape — a
 *         combo-box whose 13 entries each carry an icon rendered by
 *         drawMarkerShape() plus the enum name as the label.
 *
 *         Registered with QPropertyItemDelegate via
 *         registerCustomTypeEditorCreator() so any Q_PROPERTY typed as
 *         MarkerShape picks up the iconified editor automatically.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_MARKERSHAPEEDITOR_H
#define OPENSWMMVIS_UI_MARKERSHAPEEDITOR_H

#include <qcustomeditors.h>

class QComboBox;

namespace openswmmvis::ui
{

class MarkerShapeEditor : public QBasePropertyItemEditor
{
    Q_OBJECT
public:
    /**
     * @brief Create a property-delegate combo with icons for every registered MarkerShape enum
     * entry.
     * @param parent Optional Qt widget owner; caller retains ownership when parentless.
     * Uses meta-enum key order/names or thirteen hard-coded fallback entries if registration
     * is absent. Sets focus proxy and emits inherited valueChanged(this) on every actual
     * selection change, including unblocked programmatic setValue calls. No target-property
     * write or undo is performed directly by this widget.
     */
    explicit MarkerShapeEditor(QWidget *parent);
    /**
     * @brief Destroy the combo/layout through normal QWidget ownership.
     * Does not apply/undo a value, close a model or own any target style object.
     */
    ~MarkerShapeEditor() override;

    /**
     * @brief Select a listed MarkerShape from a copied QVariant conversion without blocking
     * notifications.
     * @param value Borrowed enum-convertible value, otherwise integer-convertible fallback;
     * unknown/unconvertible/unlisted values leave selection unchanged. No reference retained.
     * A changed index emits inherited valueChanged(this); same index is Qt's usual no-op.
     * Does not add unknown enum entries or directly write a target property.
     */
    void setValue(const QVariant &value) override;
    /**
     * @brief Read current combo item data as a detached property value.
     * @return Owned QVariant normally containing MarkerShape, invalid if combo/index absent.
     * No property application, ownership transfer or signal is generated.
     */
    QVariant getValue() const override;

private:
    QComboBox *m_combo = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MARKERSHAPEEDITOR_H
