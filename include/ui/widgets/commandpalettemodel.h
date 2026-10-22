#ifndef COMMANDPALETTEMODEL_H
#define COMMANDPALETTEMODEL_H

/**
 * @file      commandpalettemodel.h
 * @brief     UI redesign P7 — list model behind the command palette: a filtered, relevance-sorted
 *            view over ActionRegistry's registered actions.
 *
 * UI redesign P7 — list model behind the command palette: a filtered,
 * relevance-sorted view over ActionRegistry's registered actions. The
 * fuzzy scorer is a pure free function so tests can pin its ranking
 * behavior without widgets.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QAbstractListModel>
#include <QList>
#include <QString>

class QAction;

namespace openswmmvis::ui
{

/*! Fuzzy relevance of \a pattern against \a candidate (case-insensitive).
 *  -1 = no subsequence match; higher is better. Prefix matches beat
 *  word-boundary subsequences beat scattered subsequences; shorter
 *  candidates win ties.
 * @param[in] pattern Case-folded subsequence to find; empty scores zero.
 * @param[in] candidate Candidate display text, case-folded for matching.
 * @return -1 when the pattern is not a subsequence; otherwise accumulated start/word/consecutive bonuses plus a short-candidate bonus. No locale sorting occurs.
 */
int fuzzyScore(const QString &pattern, const QString &candidate);

class CommandPaletteModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        ShortcutRole = Qt::UserRole + 1,
        CategoryRole,
        EnabledRole,
    };

    /**
     * @brief Create an empty registry-backed palette model; call reload() before showing it.
     * @param[in] parent Optional QObject owner; use on the GUI thread with registered actions.
     */
    explicit CommandPaletteModel(QObject *parent = nullptr);

    /*! Rebuild the source rows from the registry (call before showing).
     * @note Snapshots nonempty-text registered actions in registry order and strips all ampersands from display text. Retains raw borrowed QAction pointers, so registry/actions must outlive use of the snapshot. Resets model indexes.
     */
    void reload();

    /*! Filter + sort by fuzzy relevance; empty pattern shows everything
     *  in catalog order.
     * @param[in] pattern Case-insensitive subsequence query matched against both command text and category-plus-text.
     * @note A changed pattern resets indexes; equal scores preserve source order. Identical patterns are a no-op.
     */
    void setFilterPattern(const QString &pattern);

    /**
     * @brief Resolve a visible palette row to its registered action.
     * @param[in] index Existing nonnegative row index from this model.
     * @return Borrowed QAction pointer, or null for invalid/out-of-range indexes; no ownership transfer or enabled-state check.
     */
    QAction *actionAt(const QModelIndex &index) const;

    /**
     * @brief Count visible filtered commands at root.
     * @param[in] parent Invalid for root; valid parents have no rows.
     * @return Visible row count or zero for children.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Read cached command/category text and current action icon/shortcut/enabled state.
     * @param[in] index Existing nonnegative visible row from this model.
     * @param[in] role DisplayRole, DecorationRole, ShortcutRole (native text), CategoryRole or EnabledRole.
     * @return Requested value, or invalid QVariant for an invalid row/unsupported role. Action pointers must still be alive.
     */
    QVariant data(const QModelIndex &index, int role) const override;

private:
    struct Row {
        QAction *action;
        QString cleanText; // ampersand-stripped
        QString category;
    };

    /**
     * @brief Recompute visible source indexes, preserving source order without a filter and stable descending fuzzy relevance with one.
     * @note Caller surrounds externally visible changes with a model reset.
     */
    void rebuildVisible();

    QList<Row> mAll;
    QList<int> mVisible; // indexes into mAll, relevance-sorted
    QString mPattern;
};

} // namespace openswmmvis::ui

#endif // COMMANDPALETTEMODEL_H
