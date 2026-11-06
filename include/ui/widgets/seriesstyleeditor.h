/**
 * @file      seriesstyleeditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     QPropertyModel-backed per-series style editor.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H

#include "plot/seriesstyle.h"

#include <QWidget>

class QTreeView;
class QPropertyModel;

namespace openswmmvis::plot
{
class SeriesStyleObject;
}

namespace openswmmvis::ui
{

class SeriesStyleEditor : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create a widget-owned SeriesStyleObject and property tree with live change forwarding.
     * @param[in] parent Optional QWidget owner; edits occur on the GUI thread.
     */
    explicit SeriesStyleEditor(QWidget *parent = nullptr);
    /**
     * @brief Destroy the editor and its owned style/property/view objects.
     */
    ~SeriesStyleEditor() override;

    /*! \brief Replace the displayed style. Refreshes the property tree.
     * @param[in] style Value copied through SeriesStyleObject::setStyle(), then property values are refreshed.
     * @note This method does not block style-object notifications; host signal behavior follows that setter.
     */
    void setStyle(const openswmmvis::plot::SeriesStyle &style);

    /*! \brief Current style as edited by the user.
     * @return Current style copy, or a default value if the internal object is unavailable.
     */
    [[nodiscard]] openswmmvis::plot::SeriesStyle style() const;

    /*! \brief Seed the spec-level legend override field. Empty = auto.
     * @param[in] override Spec-level label text; empty restores automatic legend text. Writes through the style object and refreshes property values without blocking notifications.
     */
    void setLegendOverride(const QString &override);

    /*! \brief Current legend override as edited by the user.
     * @return Current override copy; empty means automatic or an unavailable internal object.
     */
    [[nodiscard]] QString legendOverride() const;

    /*! \brief Underlying QObject — exposed for tests / advanced wiring.
     * @return Borrowed editor-owned object; caller must not delete it independently.
     */
    openswmmvis::plot::SeriesStyleObject *styleObject() const { return m_obj; }

signals:
    /*! \brief Emitted whenever any style field changes (live preview hook).
     * @param[in] style Complete current aggregate style forwarded from the shared style object.
     */
    void styleChanged(const openswmmvis::plot::SeriesStyle &style);

    /*! \brief Emitted when the spec-level legend override is edited.
     * @param[in] override Current spec-level override, including empty for automatic text.
     */
    void legendOverrideChanged(const QString &override);

private:
    /**
     * @brief Build the owned property tree/model/delegate over the existing style object and expand all rows.
     */
    void buildUi();

    openswmmvis::plot::SeriesStyleObject *m_obj = nullptr;
    QPropertyModel *m_model = nullptr;
    QTreeView *m_tree = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_SERIESSTYLEEDITOR_H
