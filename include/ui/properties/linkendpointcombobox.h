/**
 * @file linkendpointcombobox.h
 * @brief Searchable node picker shared by property and attribute editors.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#ifndef LINKENDPOINTCOMBOBOX_H
#define LINKENDPOINTCOMBOBOX_H

#include <QComboBox>
#include "ui/properties/linkendpointref.h"

class LinkEndpointComboBox : public QComboBox
{
    Q_OBJECT
    Q_PROPERTY(LinkEndpointRef value READ value WRITE setValue USER true NOTIFY valueChanged)
public:
    explicit LinkEndpointComboBox(QWidget *parent = nullptr);
    LinkEndpointRef value() const;
    void setValue(const LinkEndpointRef &ref);

signals:
    void valueChanged();

private:
    LinkEndpointRef m_ref;
    void finishEditing();
};

#endif // LINKENDPOINTCOMBOBOX_H
