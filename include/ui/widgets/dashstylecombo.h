/*!
 * \file   dashstylecombo.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Combo box rendering each Qt::PenStyle as the icon of its row,
 *         mirroring QGIS's "Stroke style" picker.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_DASHSTYLECOMBO_H
#define OPENSWMMVIS_UI_WIDGETS_DASHSTYLECOMBO_H

#include <QComboBox>
#include <Qt>

namespace openswmmvis::ui {

class DashStyleCombo : public QComboBox
{
    Q_OBJECT
public:
    explicit DashStyleCombo(QWidget *parent = nullptr);

    [[nodiscard]] Qt::PenStyle penStyle() const;
    void setPenStyle(Qt::PenStyle s);

signals:
    void penStyleChanged(Qt::PenStyle s);

private:
    static QIcon iconFor(Qt::PenStyle s, int wPx = 60, int hPx = 18);
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_DASHSTYLECOMBO_H
