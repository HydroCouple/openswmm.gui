/*!
 * \file   istyleeditorwidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/istyleeditorwidget.h"

#include <QMetaObject>

namespace openswmmvis::ui {

StyleEditorRegistry &StyleEditorRegistry::instance()
{
    static StyleEditorRegistry s_instance;
    return s_instance;
}

void StyleEditorRegistry::registerFactory(const QString &className,
                                           StyleEditorRegistry::Factory factory)
{
    m_entries.push_back({className, std::move(factory)});
}

IStyleEditorWidget *StyleEditorRegistry::createEditorFor(
    QObject *propertyObject, QWidget *parent) const
{
    if (!propertyObject) return nullptr;

    // Walk the metaobject chain looking for the first registered class.
    // Most-derived class wins because we walk from propertyObject->metaObject()
    // up its superClass() chain.
    //
    // SE.1 — match the registered key against BOTH the fully-qualified
    // className and its unqualified tail. Qt reports namespaced classes as
    // e.g. "OpenSWMM::Render::PointSymbolStyleAdapter", but the
    // REGISTER_STYLE_EDITOR macro can only stringize an unqualified token
    // ("PointSymbolStyleAdapter") because the key is pasted into a C++
    // identifier. Without the tail match, every namespaced editor
    // registration silently never matched and the dialog fell back to the
    // generic grid for all of them.
    const QMetaObject *mo = propertyObject->metaObject();
    while (mo) {
        const QString name = QString::fromLatin1(mo->className());
        const int sep = name.lastIndexOf(QStringLiteral("::"));
        const QString tail = (sep >= 0) ? name.mid(sep + 2) : name;
        for (const auto &entry : m_entries) {
            if (entry.className == name || entry.className == tail)
                return entry.factory(propertyObject, parent);
        }
        mo = mo->superClass();
    }
    return nullptr;
}

} // namespace openswmmvis::ui
