/*!
 * \file   seriesstyleeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  QPropertyModel-backed per-series style editor.
 *
 * Replaces the original hand-rolled form widget with a `QTreeView` driven
 * by `QPropertyModel` over a `SeriesStyleObject`. Editors for every visual
 * attribute (colour pickers, font dialogs, enum combos for dash/cap/join,
 * etc.) come from the QPropertyModel item-delegate machinery — no
 * per-control wiring is needed here.
 *
 * Public API is unchanged from the prior version:
 *   - `setStyle(const SeriesStyle&)` replaces the displayed style
 *   - `style() const` returns the current edited style
 *   - `styleChanged(SeriesStyle)` fires on every edit (live preview)
 *
 * Hosts pair this with a small modal dialog (Ok/Cancel) or dock it
 * permanently into a multi-series dialog as a side panel.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H

#include "plot/seriesstyle.h"

#include <QWidget>

class QTreeView;
class QPropertyModel;

namespace openswmmvis::plot { class SeriesStyleObject; }

namespace openswmmvis::ui {

class SeriesStyleEditor : public QWidget
{
    Q_OBJECT
public:
    explicit SeriesStyleEditor(QWidget *parent = nullptr);
    ~SeriesStyleEditor() override;

    /*! \brief Replace the displayed style. Refreshes the property tree. */
    void setStyle(const openswmmvis::plot::SeriesStyle& style);

    /*! \brief Current style as edited by the user. */
    [[nodiscard]] openswmmvis::plot::SeriesStyle style() const;

    /*! \brief Underlying QObject — exposed for tests / advanced wiring. */
    openswmmvis::plot::SeriesStyleObject *styleObject() const { return m_obj; }

signals:
    /*! \brief Emitted whenever any field changes (live preview hook). */
    void styleChanged(const openswmmvis::plot::SeriesStyle& style);

private:
    void buildUi();

    openswmmvis::plot::SeriesStyleObject *m_obj   = nullptr;
    QPropertyModel                       *m_model = nullptr;
    QTreeView                            *m_tree  = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H
