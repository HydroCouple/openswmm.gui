/**
 * @file      tableclipboard.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Implementation of the editor-table clipboard helper (see header).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/util/tableclipboard.h"

#include <QAbstractItemDelegate>
#include <QAction>
#include <QCheckBox>
#include <QChildEvent>
#include <QClipboard>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTableWidget>
#include <QTextEdit>
#include <QToolTip>
#include <QUndoStack>
#include <QValidator>

#include <algorithm>
#include <cmath>
#include <optional>

namespace openswmmvis::ui {

namespace {

/** @brief Number in C or system-locale notation; nullopt when \a s is not one. */
std::optional<double> parseNumber(const QString &s)
{
    bool ok = false;
    double v = s.toDouble(&ok);
    if (!ok)
        v = QLocale().toDouble(s, &ok);
    if (!ok || !std::isfinite(v))
        return std::nullopt;
    return v;
}

/** @brief Spreadsheet and SWMM spellings of a boolean. */
std::optional<bool> parseBool(const QString &s)
{
    const QString t = s.trimmed().toLower();
    if (t == QLatin1String("1") || t == QLatin1String("true") || t == QLatin1String("yes") ||
        t == QLatin1String("on") || t == QLatin1String("x") || t == QLatin1String("checked"))
        return true;
    if (t == QLatin1String("0") || t == QLatin1String("false") || t == QLatin1String("no") ||
        t == QLatin1String("off") || t == QLatin1String("unchecked"))
        return false;
    return std::nullopt;
}

/** @brief Excel-compatible TSV quoting for a cell holding tabs, breaks or quotes. */
QString quoteCell(QString s)
{
    if (!s.contains(QLatin1Char('\t')) && !s.contains(QLatin1Char('\n')) &&
        !s.contains(QLatin1Char('\r')) && !s.contains(QLatin1Char('"')))
        return s;
    s.replace(QLatin1String("\""), QLatin1String("\"\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}

/** @brief Header titles compare without case or line-break differences. */
QString normalizedLabel(const QString &s)
{
    return s.simplified().toLower();
}

/** @brief The value-carrying widget of a cell widget or editor (itself or a child). */
QWidget *editorWidget(QWidget *cell)
{
    const auto isEditor = [](QWidget *w) {
        return qobject_cast<QComboBox *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
               qobject_cast<QCheckBox *>(w) || qobject_cast<QLineEdit *>(w);
    };
    if (!cell || isEditor(cell))
        return cell;
    // findChildren is depth-first in creation order, so a combo box is found
    // before the line edit inside it.
    const auto children = cell->findChildren<QWidget *>();
    for (QWidget *w : children)
        if (isEditor(w))
            return w;
    return nullptr;
}

/** @brief Widgets that edit text and so have their own copy / paste. */
bool editsText(QObject *o)
{
    if (qobject_cast<QLineEdit *>(o) || qobject_cast<QAbstractSpinBox *>(o) ||
        qobject_cast<QTextEdit *>(o) || qobject_cast<QPlainTextEdit *>(o))
        return true;
    auto *combo = qobject_cast<QComboBox *>(o);
    return combo && combo->isEditable();
}

/** @brief A text editor with selected text, which its own Copy would take. */
bool hasSelectedText(QObject *o)
{
    if (auto *text = qobject_cast<QTextEdit *>(o))
        return text->textCursor().hasSelection();
    if (auto *plain = qobject_cast<QPlainTextEdit *>(o))
        return plain->textCursor().hasSelection();
    auto *line = qobject_cast<QLineEdit *>(o);
    if (!line) // spin boxes and editable combos edit through a child line edit
        if (auto *w = qobject_cast<QWidget *>(o))
            line = w->findChild<QLineEdit *>();
    return line && line->hasSelectedText();
}

/** @brief Clipboard text spanning more than one cell. */
bool clipboardHoldsBlock()
{
    const QString text = QGuiApplication::clipboard()->text().trimmed();
    return text.contains(QLatin1Char('\t')) || text.contains(QLatin1Char('\n'));
}

bool isNumericType(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
    case QMetaType::Short:
    case QMetaType::UShort:
        return true;
    default:
        return false;
    }
}

bool isFloatType(const QVariant &v)
{
    return v.typeId() == QMetaType::Double || v.typeId() == QMetaType::Float;
}

/** @brief Date/time text as Excel, ISO and the editor's own format write it. */
QDateTime parseDateTime(const QString &token, const QString &editorFormat)
{
    const QString formats[] = {
        editorFormat,
        QStringLiteral("yyyy-MM-dd HH:mm:ss"),
        QStringLiteral("yyyy-MM-dd HH:mm"),
        QStringLiteral("yyyy-MM-dd"),
        QStringLiteral("M/d/yyyy H:mm:ss"),
        QStringLiteral("M/d/yyyy H:mm"),
        QStringLiteral("M/d/yyyy"),
    };
    for (const QString &f : formats) {
        if (f.isEmpty())
            continue;
        const QDateTime t = QDateTime::fromString(token, f);
        if (t.isValid())
            return t;
    }
    return QDateTime::fromString(token, Qt::ISODate);
}

enum class SetResult { Changed, Unchanged, Rejected, NotAnEditor };

/**
 * @brief Sets value widget \a w from pasted text.
 * @param userEdit Also emit the signals a user's change sends (activated,
 *        editingFinished, clicked, textEdited), so dialogs that only listen
 *        to those see the paste.
 */
SetResult setWidgetValue(QWidget *w, const QString &token, bool userEdit)
{
    if (auto *combo = qobject_cast<QComboBox *>(w)) {
        int i = combo->findText(token, Qt::MatchFixedString);
        for (int k = 0; i < 0 && k < combo->count(); ++k)
            if (combo->itemData(k).toString().compare(token, Qt::CaseInsensitive) == 0)
                i = k;
        if (i < 0) {
            if (!combo->isEditable())
                return SetResult::Rejected;
            if (combo->currentText() == token)
                return SetResult::Unchanged;
            combo->setCurrentText(token);
            return SetResult::Changed;
        }
        if (i == combo->currentIndex())
            return SetResult::Unchanged;
        combo->setCurrentIndex(i);
        if (userEdit) {
            emit combo->activated(i);
            emit combo->textActivated(combo->itemText(i));
        }
        return SetResult::Changed;
    }
    if (auto *dbl = qobject_cast<QDoubleSpinBox *>(w)) {
        double v = dbl->minimum();
        if (dbl->specialValueText().isEmpty() ||
            token.compare(dbl->specialValueText(), Qt::CaseInsensitive) != 0) {
            const auto n = parseNumber(token);
            if (!n || *n < dbl->minimum() || *n > dbl->maximum())
                return SetResult::Rejected;
            v = *n;
        }
        if (dbl->value() == v)
            return SetResult::Unchanged;
        dbl->setValue(v);
        if (userEdit)
            emit dbl->editingFinished();
        return SetResult::Changed;
    }
    if (auto *spin = qobject_cast<QSpinBox *>(w)) {
        int v = spin->minimum();
        if (spin->specialValueText().isEmpty() ||
            token.compare(spin->specialValueText(), Qt::CaseInsensitive) != 0) {
            const auto n = parseNumber(token);
            if (!n || *n != std::floor(*n) || *n < spin->minimum() || *n > spin->maximum())
                return SetResult::Rejected;
            v = static_cast<int>(*n);
        }
        if (spin->value() == v)
            return SetResult::Unchanged;
        spin->setValue(v);
        if (userEdit)
            emit spin->editingFinished();
        return SetResult::Changed;
    }
    if (auto *dt = qobject_cast<QDateTimeEdit *>(w)) {
        QDateTime t = dt->minimumDateTime();
        if (dt->specialValueText().isEmpty() ||
            token.compare(dt->specialValueText(), Qt::CaseInsensitive) != 0) {
            t = parseDateTime(token, dt->displayFormat());
            if (!t.isValid() || t < dt->minimumDateTime() || t > dt->maximumDateTime())
                return SetResult::Rejected;
        }
        if (dt->dateTime() == t)
            return SetResult::Unchanged;
        dt->setDateTime(t);
        if (userEdit)
            emit dt->editingFinished();
        return SetResult::Changed;
    }
    if (auto *check = qobject_cast<QCheckBox *>(w)) {
        const auto b = parseBool(token);
        if (!b)
            return SetResult::Rejected;
        if (check->isChecked() == *b)
            return SetResult::Unchanged;
        check->setChecked(*b);
        if (userEdit)
            emit check->clicked(*b);
        return SetResult::Changed;
    }
    if (auto *edit = qobject_cast<QLineEdit *>(w)) {
        if (edit->isReadOnly())
            return SetResult::Rejected;
        QString t = token;
        if (const QValidator *validator = edit->validator()) {
            int pos = 0;
            if (validator->validate(t, pos) != QValidator::Acceptable) {
                validator->fixup(t);
                pos = 0;
                if (validator->validate(t, pos) != QValidator::Acceptable)
                    return SetResult::Rejected;
            }
        }
        if (edit->text() == t)
            return SetResult::Unchanged;
        edit->setText(t);
        edit->setModified(true); // as typing does; some delegates commit only then
        if (userEdit) {
            emit edit->textEdited(t);
            emit edit->editingFinished();
        }
        return SetResult::Changed;
    }
    return SetResult::NotAnEditor;
}

/** @brief The cell already holds \a token (same number, or same text). */
bool holdsValue(const QAbstractItemModel *model, const QModelIndex &index, const QString &token)
{
    const QVariant edit = model->data(index, Qt::EditRole);
    const auto n = parseNumber(token);
    if (n && isNumericType(edit))
        return std::abs(edit.toDouble() - *n) <= 1e-12 * std::max(1.0, std::abs(*n));
    return edit.toString().compare(token, Qt::CaseInsensitive) == 0 ||
           model->data(index, Qt::DisplayRole).toString().compare(token, Qt::CaseInsensitive) == 0;
}

} // namespace

TableClipboard::TableClipboard(QTableView *view, Options options)
    : QObject(view), m_view(view), m_options(std::move(options))
{
    m_copy = new QAction(tr("Copy"), view);
    m_copy->setShortcut(QKeySequence::Copy);
    m_copy->setShortcutContext(Qt::WidgetShortcut);
    m_copy->setToolTip(tr("Copy the selected rows as tab-separated text"));
    connect(m_copy, &QAction::triggered, this, &TableClipboard::copyToClipboard);

    QList<QAction *> actions{m_copy};
    if (!m_options.copyOnly) {
        m_paste = new QAction(tr("Paste"), view);
        m_paste->setShortcut(QKeySequence::Paste);
        m_paste->setShortcutContext(Qt::WidgetShortcut);
        m_paste->setToolTip(tr("Paste rows copied from Excel or a tab/comma-separated file"));
        connect(m_paste, &QAction::triggered, this, &TableClipboard::pasteFromClipboard);
        actions << m_paste;
    }

    view->addActions(actions);
    if (view->contextMenuPolicy() == Qt::DefaultContextMenu)
        view->setContextMenuPolicy(Qt::ActionsContextMenu);
    if (QHeaderView *rows = view->verticalHeader();
        rows && rows->contextMenuPolicy() == Qt::DefaultContextMenu) {
        rows->addActions(actions);
        rows->setContextMenuPolicy(Qt::ActionsContextMenu);
    }

    // The keys are claimed in eventFilter (see file comment). Cell widgets
    // live in the viewport, so it is watched for new ones.
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
    const auto existing = view->viewport()->children();
    for (QObject *child : existing)
        watchTree(child);
}

TableClipboard *TableClipboard::install(QTableView *view, Options options)
{
    if (!view)
        return nullptr;
    return new TableClipboard(view, std::move(options));
}

void TableClipboard::watchTree(QObject *object)
{
    if (!object || !object->isWidgetType())
        return;
    object->installEventFilter(this);
    const auto children = object->children();
    for (QObject *child : children)
        watchTree(child);
}

bool TableClipboard::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view)
        return false;
    if (event->type() == QEvent::ChildAdded && watched == m_view->viewport()) {
        watchTree(static_cast<QChildEvent *>(event)->child());
        return false;
    }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress)
        return false;
    auto *key = static_cast<QKeyEvent *>(event);
    const bool copy = key->matches(QKeySequence::Copy);
    const bool paste = !copy && m_paste && key->matches(QKeySequence::Paste);
    if (!copy && !paste)
        return false;
    // A text editor in a cell keeps the keys while a cell is being edited,
    // for Copy of its selected text, and for Paste of a single value.
    if (watched != m_view && editsText(watched) &&
        (isOpenCellEditor(watched) || (copy ? hasSelectedText(watched) : !clipboardHoldsBlock())))
        return false;
    // Accepting the override keeps any window-level Copy/Paste shortcut from
    // also matching; the key press that follows is handled here.
    event->accept();
    if (event->type() == QEvent::KeyPress)
        (copy ? m_copy : m_paste)->trigger();
    return true;
}

bool TableClipboard::isOpenCellEditor(QObject *object) const
{
    // The editor of a cell being edited sits directly in the viewport and,
    // unlike cell widgets and persistent editors, is not persistent.
    auto *w = qobject_cast<QWidget *>(object);
    while (w && w->parentWidget() != m_view->viewport())
        w = w->parentWidget();
    if (!w)
        return false;
    const QModelIndex index = m_view->indexAt(w->geometry().center());
    return index.isValid() && m_view->indexWidget(index) == w &&
           !m_view->isPersistentEditorOpen(index);
}

QList<QStringList> TableClipboard::parseDelimited(const QString &text)
{
    QList<QStringList> rows;
    if (text.isEmpty())
        return rows;

    // Semicolons separate cells where commas are decimal points.
    QChar delim = QLatin1Char('\t');
    if (!text.contains(delim)) {
        delim = QLatin1Char(',');
        bool quoted = false;
        for (const QChar ch : text) {
            if (ch == QLatin1Char('"'))
                quoted = !quoted;
            else if (!quoted && (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')))
                break;
            else if (!quoted && ch == QLatin1Char(';')) {
                delim = QLatin1Char(';');
                break;
            }
        }
    }

    QStringList row;
    QString cell;
    bool inQuotes = false;
    bool cellStarted = false; // a quote only opens a quoted cell at its start
    const int n = static_cast<int>(text.size());
    for (int i = 0; i < n; ++i) {
        const QChar ch = text.at(i);
        if (inQuotes) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < n && text.at(i + 1) == QLatin1Char('"')) {
                    cell += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cell += ch;
            }
            continue;
        }
        if (ch == QLatin1Char('"') && !cellStarted) {
            inQuotes = true;
            cellStarted = true;
        } else if (ch == delim) {
            row << cell;
            cell.clear();
            cellStarted = false;
        } else if (ch == QLatin1Char('\r') || ch == QLatin1Char('\n')) {
            if (ch == QLatin1Char('\r') && i + 1 < n && text.at(i + 1) == QLatin1Char('\n'))
                ++i;
            row << cell;
            rows << row;
            row.clear();
            cell.clear();
            cellStarted = false;
        } else {
            cell += ch;
            cellStarted = true;
        }
    }
    if (cellStarted || !row.isEmpty() || !cell.isEmpty()) {
        row << cell;
        rows << row;
    }
    return rows;
}

QList<int> TableClipboard::visibleColumns() const
{
    QList<int> cols;
    if (!m_view || !m_view->model())
        return cols;
    const QHeaderView *header = m_view->horizontalHeader();
    const int count = m_view->model()->columnCount(m_view->rootIndex());
    for (int v = 0; v < count; ++v) {
        const int c = header ? header->logicalIndex(v) : v;
        if (c >= 0 && !m_view->isColumnHidden(c))
            cols << c;
    }
    return cols;
}

QString TableClipboard::headerLabel(int column) const
{
    return m_view->model()->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString().simplified();
}

bool TableClipboard::hasTextHeader(const QList<int> &columns) const
{
    // Models without titles report section numbers; those are not a header.
    if (!m_view->horizontalHeader() || m_view->horizontalHeader()->isHidden())
        return false;
    for (int c : columns) {
        const QString label = headerLabel(c);
        if (!label.isEmpty() && !parseNumber(label))
            return true;
    }
    return false;
}

bool TableClipboard::isHeaderRow(const QList<QStringList> &rows,
                                 const QList<int> &targetColumns) const
{
    const QStringList &first = rows.first();
    // The table's own titles (what Copy writes).
    if (hasTextHeader(targetColumns)) {
        int matches = 0;
        bool mismatch = false;
        for (int c = 0; c < first.size() && c < targetColumns.size(); ++c) {
            const QString token = normalizedLabel(first[c]);
            if (token.isEmpty())
                continue;
            if (token == normalizedLabel(headerLabel(targetColumns[c])))
                ++matches;
            else
                mismatch = true;
        }
        if (matches > 0 && !mismatch)
            return true;
    }
    // Any titles: a text-only line above a line with numbers under them.
    if (rows.size() < 2)
        return false;
    bool anyText = false;
    for (const QString &cell : first) {
        if (cell.trimmed().isEmpty())
            continue;
        if (parseNumber(cell.trimmed()))
            return false;
        anyText = true;
    }
    for (int c = 0; c < rows[1].size() && c < first.size(); ++c)
        if (!first[c].trimmed().isEmpty() && parseNumber(rows[1][c].trimmed()))
            return anyText;
    return false;
}

bool TableClipboard::isDelegateEditor(const QModelIndex &index) const
{
    // Qt records setCellWidget() widgets as persistent editors too. A
    // QTableWidget's cell widgets hold the cell's value; a model view's open
    // editors belong to its delegate and commit through it.
    return !qobject_cast<QTableWidget *>(m_view.data()) && m_view->isPersistentEditorOpen(index);
}

bool TableClipboard::isCheckOnly(const QModelIndex &index) const
{
    const QAbstractItemModel *model = m_view->model();
    return (model->flags(index) & Qt::ItemIsUserCheckable) &&
           model->data(index, Qt::CheckStateRole).isValid() &&
           model->data(index, Qt::DisplayRole).toString().isEmpty();
}

QString TableClipboard::cellText(const QModelIndex &index) const
{
    if (!isDelegateEditor(index)) {
        if (QWidget *w = editorWidget(m_view->indexWidget(index))) {
            if (auto *combo = qobject_cast<QComboBox *>(w))
                return combo->currentText();
            if (auto *dbl = qobject_cast<QDoubleSpinBox *>(w))
                return QString::number(dbl->value(), 'g', 15);
            if (auto *spin = qobject_cast<QSpinBox *>(w))
                return QString::number(spin->value());
            if (auto *check = qobject_cast<QCheckBox *>(w))
                return check->isChecked() ? QStringLiteral("1") : QStringLiteral("0");
            if (auto *edit = qobject_cast<QLineEdit *>(w))
                return edit->text();
            if (auto *other = qobject_cast<QAbstractSpinBox *>(w)) // dates and the like
                return other->text();
        }
    }

    const QAbstractItemModel *model = m_view->model();
    const Qt::ItemFlags flags = model->flags(index);
    const QVariant display = model->data(index, Qt::DisplayRole);
    if (isCheckOnly(index))
        return model->data(index, Qt::CheckStateRole).toInt() == Qt::Checked
                   ? QStringLiteral("1")
                   : QStringLiteral("0");

    const QVariant edit = model->data(index, Qt::EditRole);
    // Full precision where the display rounds.
    if (isFloatType(edit))
        return QString::number(edit.toDouble(), 'g', 15);
    // An editable cell with no edit value shows a placeholder ("Mixed",
    // "(none)", "Inherited"); leaving it empty keeps a paste from writing it.
    if (!edit.isValid())
        return (flags & Qt::ItemIsEditable) ? QString() : display.toString();
    // Text is what the cell's editor takes; other values (choice codes,
    // dates) read best as displayed.
    if (edit.typeId() == QMetaType::QString)
        return edit.toString();
    return display.toString();
}

QString TableClipboard::selectionText() const
{
    if (!m_view || !m_view->model())
        return {};
    const QAbstractItemModel *model = m_view->model();
    const QModelIndex root = m_view->rootIndex();
    const QList<int> cols = visibleColumns();
    if (cols.isEmpty())
        return {};

    QList<int> rows;
    QSet<int> selCols;
    if (const auto *sm = m_view->selectionModel(); sm && sm->hasSelection()) {
        const QModelIndexList sel = sm->selectedIndexes();
        for (const QModelIndex &idx : sel) {
            if (m_view->isRowHidden(idx.row()) || m_view->isColumnHidden(idx.column()))
                continue;
            rows << idx.row();
            selCols.insert(idx.column());
        }
    }
    QList<int> outCols;
    if (rows.isEmpty()) {
        for (int r = 0; r < model->rowCount(root); ++r)
            if (!m_view->isRowHidden(r))
                rows << r;
        outCols = cols;
    } else {
        for (int c : cols)
            if (selCols.contains(c))
                outCols << c;
    }
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    if (rows.isEmpty() || outCols.isEmpty())
        return {};

    if (rows.size() == 1 && outCols.size() == 1)
        return cellText(model->index(rows.first(), outCols.first(), root));

    QStringList lines;
    lines.reserve(rows.size() + 1);
    if (hasTextHeader(outCols)) {
        QStringList header;
        for (int c : outCols)
            header << quoteCell(headerLabel(c));
        lines << header.join(QLatin1Char('\t'));
    }
    for (int r : rows) {
        QStringList cells;
        cells.reserve(outCols.size());
        for (int c : outCols)
            cells << quoteCell(cellText(model->index(r, c, root)));
        lines << cells.join(QLatin1Char('\t'));
    }
    return lines.join(QLatin1Char('\n'));
}

bool TableClipboard::isEditable(const QModelIndex &index) const
{
    const Qt::ItemFlags flags = m_view->model()->flags(index);
    return (flags & Qt::ItemIsEnabled) && (flags & Qt::ItemIsEditable) &&
           m_view->editTriggers() != QAbstractItemView::NoEditTriggers;
}

TableClipboard::CellResult TableClipboard::writeCell(const QModelIndex &index, const QString &token)
{
    QWidget *cell = m_view->indexWidget(index);
    const bool persistent = cell && isDelegateEditor(index);
    if (cell && !persistent) {
        QWidget *w = editorWidget(cell);
        if (!w || !w->isEnabled())
            return CellResult::ReadOnly;
        switch (setWidgetValue(w, token, /*userEdit=*/true)) {
        case SetResult::Changed:
        case SetResult::Unchanged: return CellResult::Written;
        case SetResult::Rejected: return CellResult::Rejected;
        case SetResult::NotAnEditor: return CellResult::ReadOnly;
        }
    }

    QAbstractItemModel *model = m_view->model();
    const Qt::ItemFlags flags = model->flags(index);
    if (!(flags & Qt::ItemIsEnabled))
        return CellResult::ReadOnly;
    if (isCheckOnly(index)) {
        const auto b = parseBool(token);
        if (!b)
            return CellResult::Rejected;
        return model->setData(index, *b ? Qt::Checked : Qt::Unchecked, Qt::CheckStateRole)
                   ? CellResult::Written
                   : CellResult::Rejected;
    }
    if (!isEditable(index))
        return CellResult::ReadOnly;
    return writeThroughDelegate(index, token, persistent ? cell : nullptr);
}

TableClipboard::CellResult TableClipboard::writeThroughDelegate(const QModelIndex &index,
                                                                const QString &token,
                                                                QWidget *openEditor)
{
    QAbstractItemModel *model = m_view->model();
    QAbstractItemDelegate *delegate = m_view->itemDelegateForIndex(index);
    if (!delegate)
        return writePlain(index, token);

    QWidget *editor = openEditor;
    const bool temporary = !editor;
    if (temporary) {
        QStyleOptionViewItem option;
        option.rect = m_view->visualRect(index);
        editor = delegate->createEditor(m_view->viewport(), option, index);
        if (!editor)
            return writePlain(index, token);
        editor->hide();
        // A pasted number keeps its precision; the editor's decimals are a
        // display choice.
        if (auto *dbl = qobject_cast<QDoubleSpinBox *>(editorWidget(editor)))
            dbl->setDecimals(std::max(dbl->decimals(), 15));
        delegate->setEditorData(editor, index);
    }
    const auto dispose = [&] {
        if (temporary)
            delegate->destroyEditor(editor, index);
    };

    QWidget *w = editorWidget(editor);
    if (!w) { // expression editors, color pickers, ...
        dispose();
        return writePlain(index, token);
    }
    const int row = index.row();
    const int column = index.column();
    const QModelIndex parent = index.parent();
    const QVariant before = model->data(index, Qt::EditRole);
    // Set the value as a user would, signals included: some delegates only
    // commit what the user "touched". A temporary editor is not registered
    // with the view, so its commitData/closeEditor signals change nothing.
    switch (setWidgetValue(w, token, /*userEdit=*/true)) {
    case SetResult::NotAnEditor:
        dispose();
        return writePlain(index, token);
    case SetResult::Rejected:
        dispose();
        return CellResult::Rejected;
    case SetResult::Unchanged:
        dispose();
        return CellResult::Written;
    case SetResult::Changed:
        break;
    }
    // The model may have been reset by an edit; look the cell up again. A
    // persistent editor's delegate may already have committed on the signal.
    QModelIndex now = model->index(row, column, parent);
    if (model->data(now, Qt::EditRole) == before) {
        delegate->setModelData(editor, model, now);
        now = model->index(row, column, parent);
    }
    dispose();
    if (model->data(now, Qt::EditRole) != before || holdsValue(model, now, token))
        return CellResult::Written;
    return CellResult::Rejected;
}

TableClipboard::CellResult TableClipboard::writePlain(const QModelIndex &index, const QString &token)
{
    QAbstractItemModel *model = m_view->model();
    QVariant value = token;
    const QVariant current = model->data(index, Qt::EditRole);
    if (isNumericType(current)) {
        const auto v = parseNumber(token);
        if (!v || (!isFloatType(current) && *v != std::floor(*v)))
            return CellResult::Rejected;
        value = *v;
        if (!value.convert(current.metaType()))
            return CellResult::Rejected;
    } else if (current.typeId() == QMetaType::Bool) {
        const auto b = parseBool(token);
        if (!b)
            return CellResult::Rejected;
        value = *b;
    }
    return model->setData(index, value, Qt::EditRole) ? CellResult::Written
                                                      : CellResult::Rejected;
}

bool TableClipboard::pasteText(const QString &text)
{
    if (!m_view || !m_view->model() || m_options.copyOnly)
        return false;
    QAbstractItemModel *model = m_view->model();
    const QModelIndex root = m_view->rootIndex();

    QList<QStringList> rows = parseDelimited(text);
    if (rows.isEmpty()) {
        notify(tr("The clipboard holds no table data."));
        return false;
    }
    const QList<int> cols = visibleColumns();
    if (cols.isEmpty())
        return false;
    int width = 0;
    for (const QStringList &r : std::as_const(rows))
        width = std::max(width, static_cast<int>(r.size()));

    // ── Anchor: top selected row, and the column the block starts in ──────
    // Cells are kept as row/column numbers: a dialog may rebuild its table
    // after each edit, which would invalidate model indexes.
    QList<QPair<int, int>> selected;
    if (const auto *sm = m_view->selectionModel(); sm && sm->hasSelection()) {
        const QModelIndexList sel = sm->selectedIndexes();
        for (const QModelIndex &idx : sel)
            if (!m_view->isRowHidden(idx.row()) && !m_view->isColumnHidden(idx.column()))
                selected.append({idx.row(), idx.column()});
    }

    int anchorRow = 0;
    int anchorPos = 0; // position within cols (visual order)
    if (!selected.isEmpty()) {
        QSet<int> selCols;
        anchorRow = selected.first().first;
        for (const auto &cell : std::as_const(selected)) {
            anchorRow = std::min(anchorRow, cell.first);
            selCols.insert(cell.second);
        }
        bool wholeRows = true;
        for (int c : cols)
            wholeRows = wholeRows && selCols.contains(c);
        if (!wholeRows) {
            for (int p = 0; p < cols.size(); ++p)
                if (selCols.contains(cols[p])) {
                    anchorPos = p;
                    break;
                }
        } else if (width < cols.size()) {
            // A block narrower than the table skips leading read-only columns
            // (row labels, IDs), so values copied from Excel line up.
            for (int p = 0; p < cols.size(); ++p) {
                const QModelIndex idx = model->index(anchorRow, cols[p], root);
                if (m_view->indexWidget(idx) || isEditable(idx) || isCheckOnly(idx)) {
                    anchorPos = p;
                    break;
                }
            }
        }
    } else if (const QModelIndex cur = m_view->currentIndex(); cur.isValid()) {
        anchorRow = cur.row();
        anchorPos = std::max(0, static_cast<int>(cols.indexOf(cur.column())));
    } else if (m_options.appendRow) {
        anchorRow = model->rowCount(root);
    }

    if (isHeaderRow(rows, cols.mid(anchorPos, width)))
        rows.removeFirst();
    if (rows.isEmpty()) {
        notify(tr("The clipboard holds only a header line."));
        return false;
    }

    const bool fill = rows.size() == 1 && rows.first().size() == 1 && selected.size() > 1;
    if (m_options.pasteRows && !fill) {
        QString message;
        const int written = m_options.pasteRows(anchorRow, cols.value(anchorPos, -1), rows, &message);
        if (written >= 0) {
            notify(message);
            return written > 0;
        }
    }

    // ── Write ─────────────────────────────────────────────────────────────
    // Sorting would move rows while their cells are written.
    const bool sorting = m_view->isSortingEnabled();
    if (sorting)
        m_view->setSortingEnabled(false);
    auto *proxy = qobject_cast<QSortFilterProxyModel *>(model);
    const bool dynamicSort = proxy && proxy->dynamicSortFilter();
    if (dynamicSort)
        proxy->setDynamicSortFilter(false);
    if (m_options.undoStack)
        m_options.undoStack->beginMacro(tr("Paste rows"));

    int written = 0, readOnly = 0, rejected = 0, outside = 0, rowsMissing = 0;
    QSet<int> touchedRows;
    const auto record = [&](CellResult r, int row) {
        switch (r) {
        case CellResult::Written:
            ++written;
            touchedRows.insert(row);
            break;
        case CellResult::ReadOnly: ++readOnly; break;
        case CellResult::Rejected: ++rejected; break;
        }
    };

    if (fill) {
        const QString token = rows.first().first().trimmed();
        for (const auto &cell : std::as_const(selected))
            if (cell.first < model->rowCount(root))
                record(writeCell(model->index(cell.first, cell.second, root), token), cell.first);
    } else {
        for (int r = 0; r < rows.size(); ++r) {
            const int row = anchorRow + r;
            bool haveRow = row < model->rowCount(root);
            while (!haveRow && m_options.appendRow) {
                const int before = model->rowCount(root);
                if (!m_options.appendRow() || model->rowCount(root) <= before)
                    break;
                haveRow = row < model->rowCount(root);
            }
            if (!haveRow) {
                rowsMissing = static_cast<int>(rows.size()) - r;
                break;
            }
            const QStringList &cells = rows[r];
            for (int c = 0; c < cells.size(); ++c) {
                const QString token = cells[c].trimmed();
                if (token.isEmpty())
                    continue;
                const int pos = anchorPos + c;
                if (pos >= cols.size()) {
                    ++outside;
                    continue;
                }
                record(writeCell(model->index(row, cols[pos], root), token), row);
            }
        }
    }

    if (m_options.undoStack)
        m_options.undoStack->endMacro();
    if (dynamicSort)
        proxy->setDynamicSortFilter(true);
    if (sorting)
        m_view->setSortingEnabled(true);

    QStringList parts;
    parts << tr("Pasted %n row(s).", nullptr, static_cast<int>(touchedRows.size()));
    if (readOnly)
        parts << tr("%n read-only cell(s) skipped.", nullptr, readOnly);
    if (rejected)
        parts << tr("%n value(s) rejected.", nullptr, rejected);
    if (outside)
        parts << tr("%n value(s) beyond the last column dropped.", nullptr, outside);
    if (rowsMissing)
        parts << tr("%n row(s) did not fit in the table.", nullptr, rowsMissing);
    notify(parts.join(QLatin1Char(' ')));
    return written > 0;
}

void TableClipboard::copyToClipboard()
{
    const QString text = selectionText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    notify(tr("Copied the selection to the clipboard."));
}

void TableClipboard::pasteFromClipboard()
{
    pasteText(QGuiApplication::clipboard()->text());
}

void TableClipboard::notify(const QString &message)
{
    m_lastMessage = message;
    if (!m_view)
        return;
    if (auto *status = m_view->window()->findChild<QStatusBar *>()) {
        status->showMessage(message, 4000);
        return;
    }
    QToolTip::showText(m_view->viewport()->mapToGlobal(QPoint(8, 8)), message, m_view);
}

} // namespace openswmmvis::ui
