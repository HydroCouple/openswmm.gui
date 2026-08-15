/*!
 * \file   basemaphttpheaderswidget.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Compact widget for editing basemap HTTP headers.
 *
 * \details
 * Layout:
 *   Referer: [____________________]
 *   ▼ Additional headers
 *     ┌─────────────────┬─────────────────┐
 *     │ Header          │ Value           │
 *     ├─────────────────┼─────────────────┤
 *     │ User-Agent      │ OpenSWMM/1.0    │
 *     └─────────────────┴─────────────────┘
 *     [Add row]  [Remove row]
 *
 * The "Additional headers" group is collapsible.  The Referer field is
 * always visible as it is the most commonly needed header.
 */
#ifndef BASEMAPHTTPHEADERSWIDGET_H
#define BASEMAPHTTPHEADERSWIDGET_H

#include "connections/basemapconnection.h"

#include <QWidget>

class QLineEdit;
class QGroupBox;
class QTableWidget;
class QPushButton;

class BasemapHttpHeadersWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BasemapHttpHeadersWidget(QWidget *parent = nullptr);

    /*! \brief Returns the current header map including the Referer field. */
    [[nodiscard]] BasemapHttpHeaders headers() const;

    /*! \brief Populates the widget from \p headers. */
    void setHeaders(const BasemapHttpHeaders &headers);

    /*! \brief Clears all fields. */
    void clear();

private slots:
    void onAddRow();
    void onRemoveRow();

private:
    QLineEdit    *m_refererEdit  = nullptr;
    QGroupBox    *m_extrasGroup  = nullptr;
    QTableWidget *m_table        = nullptr;
    QPushButton  *m_addBtn       = nullptr;
    QPushButton  *m_removeBtn    = nullptr;
};

#endif // BASEMAPHTTPHEADERSWIDGET_H
