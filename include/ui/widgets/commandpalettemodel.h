#ifndef COMMANDPALETTEMODEL_H
#define COMMANDPALETTEMODEL_H

/*!
 * \file commandpalettemodel.h
 *
 * UI redesign P7 — list model behind the command palette: a filtered,
 * relevance-sorted view over ActionRegistry's registered actions. The
 * fuzzy scorer is a pure free function so tests can pin its ranking
 * behavior without widgets.
 */

#include <QAbstractListModel>
#include <QList>
#include <QString>

class QAction;

namespace openswmmvis::ui {

/*! Fuzzy relevance of \a pattern against \a candidate (case-insensitive).
 *  -1 = no subsequence match; higher is better. Prefix matches beat
 *  word-boundary subsequences beat scattered subsequences; shorter
 *  candidates win ties. */
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

    explicit CommandPaletteModel(QObject *parent = nullptr);

    /*! Rebuild the source rows from the registry (call before showing). */
    void reload();

    /*! Filter + sort by fuzzy relevance; empty pattern shows everything
     *  in catalog order. */
    void setFilterPattern(const QString &pattern);

    QAction *actionAt(const QModelIndex &index) const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

private:
    struct Row {
        QAction *action;
        QString  cleanText;   // ampersand-stripped
        QString  category;
    };

    void rebuildVisible();

    QList<Row> mAll;
    QList<int> mVisible;      // indexes into mAll, relevance-sorted
    QString    mPattern;
};

}   // namespace openswmmvis::ui

#endif // COMMANDPALETTEMODEL_H
