/**
 * @file      draftmarkproxymodel.h
 * @brief     Marks the rows of a data-object list whose object holds unapplied
 *            changes in its editor (Apply / Revert plan, carried item).
 *
 * An identity proxy between an editor's list model and its view: same rows,
 * same data, plus an italic font, a tooltip and an accessible description on
 * the rows the editor reports as holding a draft. The text is unchanged, so
 * name lookups and filters keep working.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_UI_MODELS_DRAFTMARKPROXYMODEL_H
#define OPENSWMMVIS_UI_MODELS_DRAFTMARKPROXYMODEL_H

#include <QAbstractItemView>
#include <QCoreApplication>
#include <QFont>
#include <QIdentityProxyModel>
#include <QVector>

#include <functional>

namespace openswmmvis::ui
{

class DraftMarkProxyModel : public QIdentityProxyModel
{
public:
    using QIdentityProxyModel::QIdentityProxyModel;

    /*! \p hasDraft(sourceRow) says whether that row's object holds unapplied changes. */
    void setHasDraft(std::function<bool(int sourceRow)> hasDraft) { m_hasDraft = std::move(hasDraft); }

    /*! Re-read the marks (after drafts changed). */
    void refreshMarks()
    {
        const int n = rowCount();
        if (n > 0)
            emit dataChanged(index(0, 0), index(n - 1, 0),
                             {Qt::FontRole, Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if ((role == Qt::FontRole || role == Qt::ToolTipRole ||
             role == Qt::AccessibleDescriptionRole) &&
            index.isValid() && m_hasDraft && m_hasDraft(mapToSource(index).row())) {
            if (role == Qt::FontRole) {
                QFont font = QIdentityProxyModel::data(index, role).value<QFont>();
                font.setItalic(true);
                return font;
            }
            return QCoreApplication::translate("DraftMarkProxyModel",
                                               "Unapplied changes: Apply writes them to the model");
        }
        return QIdentityProxyModel::data(index, role);
    }

private:
    std::function<bool(int)> m_hasDraft;
};

/*! \p index of the model behind \p view's proxies, as the view's own index;
 *  invalid when \p view does not show that model. */
inline QModelIndex viewIndexOf(const QAbstractItemView *view, const QModelIndex &index)
{
    QVector<const QAbstractProxyModel *> chain;
    const QAbstractItemModel *m = view ? view->model() : nullptr;
    while (m && m != index.model()) {
        const auto *proxy = qobject_cast<const QAbstractProxyModel *>(m);
        if (!proxy)
            return {};
        chain.prepend(proxy);
        m = proxy->sourceModel();
    }
    if (!m)
        return {};
    QModelIndex mapped = index;
    for (const QAbstractProxyModel *proxy : std::as_const(chain))
        mapped = proxy->mapFromSource(mapped);
    return mapped;
}

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_MODELS_DRAFTMARKPROXYMODEL_H
