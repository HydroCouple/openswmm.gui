/*!
 * \file   stylepropertydelegate.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice D1-a — makeStyleDelegate() implementation.
 */
#include "ui/widgets/stylepropertydelegate.h"

#include "ui/widgets/classificationschemecelleditor.h"

#include "render/classificationscheme.h"

#include <qpropertyitemdelegate.h>

#include <QItemEditorFactory>

namespace openswmmvis::ui {

QPropertyItemDelegate *makeStyleDelegate(QObject *parent)
{
    auto *d = new QPropertyItemDelegate(parent);
    d->registerCustomTypeEditorCreator(
        QMetaType::Type(qMetaTypeId<OpenSWMM::Render::ClassificationScheme>()),
        new QStandardItemEditorCreator<ClassificationSchemeCellEditor>());
    return d;
}

} // namespace openswmmvis::ui
