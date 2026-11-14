/**
 * @file      basemaphttpheaderswidget.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Compact widget for editing basemap HTTP headers.
 *
 * @details
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
    /**
     * @brief Create an empty Referer field and optional extra-header table.
     * @param[in] parent Optional QWidget owner; use on the GUI thread.
     */
    explicit BasemapHttpHeadersWidget(QWidget *parent = nullptr);

    /*! \brief Returns the current header map including the Referer field.
     * @return Trimmed map: nonempty Referer is stored as lowercase referer, blank extra keys are skipped, and later exact duplicate keys overwrite earlier values. Header syntax/case equivalence are not validated.
     */
    [[nodiscard]] BasemapHttpHeaders headers() const;

    /*! \brief Populates the widget from \p headers.
     * @param[in] headers Header mapping copied into controls; exact lowercase referer populates the special field, and all case variants of that key are omitted from the table.
     * @note Expands the extras group when rows exist; does not automatically collapse it for an empty mapping. No application/network state is changed.
     */
    void setHeaders(const BasemapHttpHeaders &headers);

    /*! \brief Clears all fields.
     * @note Also unchecks the extras group. This edits only widget state.
     */
    void clear();

private slots:
    /**
     * @brief Append empty key/value cells and start editing the key.
     */
    void onAddRow();
    /**
     * @brief Remove all currently selected table rows from bottom to top, deduplicating overlapping ranges.
     */
    void onRemoveRow();

private:
    QLineEdit *m_refererEdit = nullptr;
    QGroupBox *m_extrasGroup = nullptr;
    QTableWidget *m_table = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
};

#endif // BASEMAPHTTPHEADERSWIDGET_H
