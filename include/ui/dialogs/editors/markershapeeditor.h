/*!
 * \file   markershapeeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QPropertyModel editor for OpenSWMM::Render::MarkerShape — a
 *         combo-box whose 13 entries each carry an icon rendered by
 *         drawMarkerShape() plus the enum name as the label.
 *
 *         Registered with QPropertyItemDelegate via
 *         registerCustomTypeEditorCreator() so any Q_PROPERTY typed as
 *         MarkerShape picks up the iconified editor automatically.
 */

#ifndef OPENSWMMVIS_UI_MARKERSHAPEEDITOR_H
#define OPENSWMMVIS_UI_MARKERSHAPEEDITOR_H

#include <qcustomeditors.h>

class QComboBox;

namespace openswmmvis::ui {

class MarkerShapeEditor : public QBasePropertyItemEditor
{
    Q_OBJECT
public:
    explicit MarkerShapeEditor(QWidget *parent);
    ~MarkerShapeEditor() override;

    void     setValue(const QVariant &value) override;
    QVariant getValue() const override;

private:
    QComboBox *m_combo = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MARKERSHAPEEDITOR_H
