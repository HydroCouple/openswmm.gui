/*!
 * \file   culvertcodecombobox.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 0 of docs/ATTRIBUTE_EDITOR_WIRING_PLAN_2026-06-04.md — inline
 * combobox editor for the conduit "Culvert Code" Property Browser row.
 * Registered against the `CulvertCodeRef` metatype via
 * `QPropertyItemDelegate::registerCustomTypeEditorCreator`.
 *
 * Apply-as-you-go (same contract the removed LinkCompoundEditDialog
 * culvert page honoured): picking an item writes immediately through
 * `SWMMModelLayer::applyLinkCulvertCode` (or the bare engine setter
 * when no layer is bound, e.g. in tests), then emits `valueChanged()`
 * so the delegate's setModelData round-trip refreshes the row.
 */

#ifndef CULVERTCODECOMBOBOX_H
#define CULVERTCODECOMBOBOX_H

#include <QComboBox>

#include "ui/properties/culvertcoderef.h"

class CulvertCodeComboBox : public QComboBox
{
    Q_OBJECT
    /*! USER true so QStyledItemDelegate uses this property for the
     *  setEditorData / setModelData round-trip. */
    Q_PROPERTY(CulvertCodeRef value
               READ value WRITE setValue USER true NOTIFY valueChanged)

public:
    explicit CulvertCodeComboBox(QWidget *parent = nullptr);

    [[nodiscard]] CulvertCodeRef value() const { return m_ref; }
    void setValue(const CulvertCodeRef &ref);

signals:
    void valueChanged();

private slots:
    void onCurrentIndexChanged(int index);

private:
    CulvertCodeRef m_ref;
    /*! Re-entrancy guard — `setValue` selects the current code
     *  programmatically; the index-changed slot bails while set so the
     *  engine doesn't see a phantom write. */
    bool           m_suppressApply = false;
};

#endif // CULVERTCODECOMBOBOX_H
