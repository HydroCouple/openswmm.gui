#include "ui/widgets/commandpalettemodel.h"

#include "ui/actioncatalog.h"
#include "ui/actionregistry.h"

#include <QAction>
#include <QKeySequence>

#include <algorithm>

namespace openswmmvis::ui {

int fuzzyScore(const QString &pattern, const QString &candidate)
{
    if (pattern.isEmpty())
        return 0;
    const QString p = pattern.toCaseFolded();
    const QString c = candidate.toCaseFolded();

    int score = 0;
    int ci = 0;
    int lastMatch = -2;
    for (int pi = 0; pi < p.size(); ++pi) {
        const QChar ch = p.at(pi);
        int found = -1;
        for (; ci < c.size(); ++ci) {
            if (c.at(ci) == ch) {
                found = ci;
                break;
            }
        }
        if (found < 0)
            return -1;   // not a subsequence

        if (found == 0)
            score += 8;                                   // starts the string
        else if (!c.at(found - 1).isLetterOrNumber())
            score += 6;                                   // starts a word
        else if (found == lastMatch + 1)
            score += 4;                                   // consecutive run
        else
            score += 1;                                   // scattered hit
        lastMatch = found;
        ++ci;
    }
    // Tighter candidates rank higher on equal hit quality.
    score += qMax(0, 24 - candidate.size() / 2);
    return score;
}

CommandPaletteModel::CommandPaletteModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void CommandPaletteModel::reload()
{
    beginResetModel();
    mAll.clear();
    auto *registry = ActionRegistry::instance();
    const QStringList ids = registry->registeredIds();
    for (const QString &id : ids) {
        QAction *act = registry->action(id);
        if (!act || act->text().isEmpty())
            continue;
        const ActionCatalogEntry *entry = registry->catalogEntry(id);
        Row row;
        row.action = act;
        row.cleanText = act->text();
        row.cleanText.remove(QLatin1Char('&'));
        row.category = entry ? QString::fromLatin1(entry->category) : QString();
        mAll.append(row);
    }
    rebuildVisible();
    endResetModel();
}

void CommandPaletteModel::setFilterPattern(const QString &pattern)
{
    if (mPattern == pattern)
        return;
    beginResetModel();
    mPattern = pattern;
    rebuildVisible();
    endResetModel();
}

void CommandPaletteModel::rebuildVisible()
{
    mVisible.clear();
    if (mPattern.isEmpty()) {
        for (int i = 0; i < mAll.size(); ++i)
            mVisible.append(i);
        return;
    }
    QList<QPair<int, int>> scored;   // (score, index)
    for (int i = 0; i < mAll.size(); ++i) {
        // Match against "Category Text" so e.g. "panels layers" works.
        const int direct = fuzzyScore(mPattern, mAll[i].cleanText);
        const int withCategory = fuzzyScore(
            mPattern, mAll[i].category + QLatin1Char(' ') + mAll[i].cleanText);
        const int best = qMax(direct, withCategory);
        if (best >= 0)
            scored.append({best, i});
    }
    std::stable_sort(scored.begin(), scored.end(),
                     [](const QPair<int, int> &a, const QPair<int, int> &b) {
                         return a.first > b.first;
                     });
    for (const auto &pair : scored)
        mVisible.append(pair.second);
}

QAction *CommandPaletteModel::actionAt(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= mVisible.size())
        return nullptr;
    return mAll[mVisible[index.row()]].action;
}

int CommandPaletteModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : mVisible.size();
}

QVariant CommandPaletteModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= mVisible.size())
        return {};
    const Row &row = mAll[mVisible[index.row()]];
    switch (role) {
    case Qt::DisplayRole:
        return row.cleanText;
    case Qt::DecorationRole:
        return row.action->icon();
    case ShortcutRole:
        return row.action->shortcut().toString(QKeySequence::NativeText);
    case CategoryRole:
        return row.category;
    case EnabledRole:
        return row.action->isEnabled();
    default:
        return {};
    }
}

}   // namespace openswmmvis::ui
