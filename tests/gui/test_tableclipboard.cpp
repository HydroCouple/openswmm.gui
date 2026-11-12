/**
 * @file      test_tableclipboard.cpp
 * @brief     QtTest: TableClipboard copies selected table rows as TSV and
 *            pastes TSV / CSV blocks into plain cells, cell widgets and
 *            numeric model cells, growing the table through the dialog's
 *            appendRow callback.
 *
 * @see ui/util/tableclipboard.h
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/util/tableclipboard.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QStandardItemModel>
#include <QTableView>
#include <QTableWidget>
#include <QTest>
#include <QUndoStack>

using openswmmvis::ui::TableClipboard;

namespace {

/** @brief Name (read-only) | Depth | Width table with \a rows rows. */
QTableWidget *makeTable(int rows, QWidget *parent = nullptr)
{
    auto *t = new QTableWidget(rows, 3, parent);
    t->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Depth\n(ft)"),
                                  QStringLiteral("Width")});
    for (int r = 0; r < rows; ++r) {
        auto *name = new QTableWidgetItem(QStringLiteral("R%1").arg(r + 1));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        t->setItem(r, 0, name);
        t->setItem(r, 1, new QTableWidgetItem(QString::number(r + 1)));
        t->setItem(r, 2, new QTableWidgetItem(QString::number((r + 1) * 10)));
    }
    return t;
}

QString text(QTableWidget *t, int r, int c)
{
    return t->item(r, c) ? t->item(r, c)->text() : QString();
}

/** @brief One "Level" column storing a code 0..2 and showing its label. */
class CodeModel : public QAbstractTableModel
{
public:
    static QStringList labels() { return {QStringLiteral("Low"), QStringLiteral("Mid"), QStringLiteral("High")}; }
    QList<int> codes{1, 1};
    int rowCount(const QModelIndex & = {}) const override { return int(codes.size()); }
    int columnCount(const QModelIndex & = {}) const override { return 1; }
    QVariant data(const QModelIndex &i, int role) const override
    {
        if (role == Qt::DisplayRole)
            return labels().at(codes[i.row()]);
        if (role == Qt::EditRole)
            return codes[i.row()];
        return {};
    }
    QVariant headerData(int, Qt::Orientation o, int role) const override
    {
        return o == Qt::Horizontal && role == Qt::DisplayRole ? QVariant(QStringLiteral("Level"))
                                                              : QVariant();
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override
    {
        return QAbstractTableModel::flags(i) | Qt::ItemIsEditable;
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override
    {
        bool ok = false;
        const int code = v.toInt(&ok);
        if (role != Qt::EditRole || !ok || code < 0 || code > 2)
            return false;
        codes[i.row()] = code;
        emit dataChanged(i, i);
        return true;
    }
};

/** @brief Edits CodeModel through a combo of labels, writing the code. */
class CodeDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *combo = new QComboBox(parent);
        combo->addItems(CodeModel::labels());
        return combo;
    }
    void setEditorData(QWidget *editor, const QModelIndex &i) const override
    {
        static_cast<QComboBox *>(editor)->setCurrentIndex(i.data(Qt::EditRole).toInt());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *m, const QModelIndex &i) const override
    {
        m->setData(i, static_cast<QComboBox *>(editor)->currentIndex(), Qt::EditRole);
    }
};

} // namespace

/** @brief Commits a line edit only when the user modified it (as the
 *  inherited-properties editor does). */
class ModifiedOnlyDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        return new QLineEdit(parent);
    }
    void setEditorData(QWidget *editor, const QModelIndex &i) const override
    {
        auto *line = static_cast<QLineEdit *>(editor);
        line->setText(i.data(Qt::EditRole).toString());
        line->setModified(false);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *m, const QModelIndex &i) const override
    {
        auto *line = static_cast<QLineEdit *>(editor);
        if (line->isModified())
            m->setData(i, line->text(), Qt::EditRole);
    }
};

class TestTableClipboard : public QObject
{
    Q_OBJECT

private slots:
    void parseHandlesExcelCsvAndQuotes();
    void copySelectedRowsWithHeader();
    void copySingleCellIsBareValue();
    void copySkipsHiddenColumns();
    void pasteOverwritesFromTopSelectedRow();
    void pasteSkipsMatchingHeader();
    void pasteSkipsTextHeaderOverNumbers();
    void pasteNarrowBlockStartsAtFirstEditableColumn();
    void pasteFullRowRoundTrips();
    void pasteAppendsRowsThroughCallback();
    void pasteWithoutCallbackReportsRowsThatDidNotFit();
    void pasteSingleValueFillsSelection();
    void pasteRespectsNoEditTriggers();
    void pasteIntoCellWidgets();
    void pasteConvertsNumericModelValues();
    void pasteIsOneUndoStep();
    void pasteMapsLabelsThroughComboDelegate();
    void pasteThroughPersistentEditor();
    void pasteCommitsThroughModifiedOnlyDelegate();
    void pasteKeepsSortedRowsInPlace();
    void pasteRowsHookWritesWholeBlock();
    void shortcutsCopyAndPaste();
    void shortcutsClaimedFromNonTextCellWidgets();
    void copyOnlyTableRefusesPaste();
};

void TestTableClipboard::parseHandlesExcelCsvAndQuotes()
{
    // Excel TSV with a trailing CRLF.
    auto rows = TableClipboard::parseDelimited(QStringLiteral("a\tb\r\n1\t2\r\n"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[0], (QStringList{"a", "b"}));
    QCOMPARE(rows[1], (QStringList{"1", "2"}));

    // CSV with a quoted comma, doubled quote and line break inside a cell.
    rows = TableClipboard::parseDelimited(
        QStringLiteral("name,note\n\"Main, St\",\"say \"\"hi\"\"\nthere\"\n"));
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows[1], (QStringList{"Main, St", "say \"hi\"\nthere"}));

    // Semicolon CSV (comma-decimal locales).
    rows = TableClipboard::parseDelimited(QStringLiteral("1,5;2,5\n3;4"));
    QCOMPARE(rows[0], (QStringList{"1,5", "2,5"}));

    // Tabs win over commas: "1,234" stays one cell.
    rows = TableClipboard::parseDelimited(QStringLiteral("1,234\t5"));
    QCOMPARE(rows[0], (QStringList{"1,234", "5"}));

    // Empty trailing cell is kept; empty text gives no rows.
    rows = TableClipboard::parseDelimited(QStringLiteral("1\t\n"));
    QCOMPARE(rows[0], (QStringList{"1", ""}));
    QVERIFY(TableClipboard::parseDelimited(QString()).isEmpty());
}

void TestTableClipboard::copySelectedRowsWithHeader()
{
    QScopedPointer<QTableWidget> t(makeTable(4));
    auto *clip = TableClipboard::install(t.data());
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->selectRow(1);
    t->selectionModel()->select(t->model()->index(3, 0),
                                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(clip->selectionText(),
             QStringLiteral("Name\tDepth (ft)\tWidth\nR2\t2\t20\nR4\t4\t40"));

    // Nothing selected copies the whole table.
    t->clearSelection();
    QCOMPARE(clip->selectionText().count('\n'), 4);
}

void TestTableClipboard::copySingleCellIsBareValue()
{
    QScopedPointer<QTableWidget> t(makeTable(2));
    auto *clip = TableClipboard::install(t.data());
    t->setCurrentCell(1, 2);
    QCOMPARE(clip->selectionText(), QStringLiteral("20"));

    // A cell holding a tab is quoted so Excel keeps it in one cell.
    t->item(1, 2)->setText(QStringLiteral("a\tb"));
    t->selectRow(1);
    QVERIFY(clip->selectionText().endsWith(QStringLiteral("\"a\tb\"")));
}

void TestTableClipboard::copySkipsHiddenColumns()
{
    QScopedPointer<QTableWidget> t(makeTable(2));
    auto *clip = TableClipboard::install(t.data());
    t->setColumnHidden(1, true);
    t->selectRow(0);
    QCOMPARE(clip->selectionText(), QStringLiteral("Name\tWidth\nR1\t10"));
}

void TestTableClipboard::pasteOverwritesFromTopSelectedRow()
{
    QScopedPointer<QTableWidget> t(makeTable(4));
    auto *clip = TableClipboard::install(t.data());
    t->setCurrentCell(1, 1);
    QVERIFY(clip->pasteText(QStringLiteral("7\t70\n8\t80\n")));
    QCOMPARE(text(t.data(), 1, 1), QStringLiteral("7"));
    QCOMPARE(text(t.data(), 1, 2), QStringLiteral("70"));
    QCOMPARE(text(t.data(), 2, 1), QStringLiteral("8"));
    QCOMPARE(text(t.data(), 2, 2), QStringLiteral("80"));
    QCOMPARE(text(t.data(), 0, 1), QStringLiteral("1"));
    QCOMPARE(text(t.data(), 3, 1), QStringLiteral("4"));
    QVERIFY(clip->lastMessage().contains(QStringLiteral("2 row")));

    // Comma-separated text pastes the same way.
    t->setCurrentCell(3, 1);
    QVERIFY(clip->pasteText(QStringLiteral("9,90")));
    QCOMPARE(text(t.data(), 3, 2), QStringLiteral("90"));
}

void TestTableClipboard::pasteSkipsMatchingHeader()
{
    QScopedPointer<QTableWidget> t(makeTable(3));
    auto *clip = TableClipboard::install(t.data());
    t->setCurrentCell(0, 1);
    QVERIFY(clip->pasteText(QStringLiteral("depth (FT)\twidth\n5\t50")));
    QCOMPARE(text(t.data(), 0, 1), QStringLiteral("5"));
    QCOMPARE(text(t.data(), 1, 1), QStringLiteral("2"));
}

void TestTableClipboard::pasteSkipsTextHeaderOverNumbers()
{
    QScopedPointer<QTableWidget> t(makeTable(3));
    auto *clip = TableClipboard::install(t.data());
    t->setCurrentCell(0, 1);
    QVERIFY(clip->pasteText(QStringLiteral("D [m]\tW [m]\n5\t50")));
    QCOMPARE(text(t.data(), 0, 1), QStringLiteral("5"));
    QCOMPARE(text(t.data(), 1, 1), QStringLiteral("2"));
}

void TestTableClipboard::pasteNarrowBlockStartsAtFirstEditableColumn()
{
    QScopedPointer<QTableWidget> t(makeTable(3));
    auto *clip = TableClipboard::install(t.data());
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->selectRow(1);
    QVERIFY(clip->pasteText(QStringLiteral("6\t60")));
    QCOMPARE(text(t.data(), 1, 0), QStringLiteral("R2"));
    QCOMPARE(text(t.data(), 1, 1), QStringLiteral("6"));
    QCOMPARE(text(t.data(), 1, 2), QStringLiteral("60"));
}

void TestTableClipboard::pasteFullRowRoundTrips()
{
    QScopedPointer<QTableWidget> t(makeTable(3));
    auto *clip = TableClipboard::install(t.data());
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->selectRow(0);
    const QString copied = clip->selectionText();
    t->selectRow(2);
    QVERIFY(clip->pasteText(copied));
    QCOMPARE(text(t.data(), 2, 0), QStringLiteral("R3")); // read-only name kept
    QCOMPARE(text(t.data(), 2, 1), QStringLiteral("1"));
    QCOMPARE(text(t.data(), 2, 2), QStringLiteral("10"));
    QVERIFY(clip->lastMessage().contains(QStringLiteral("read-only")));
}

void TestTableClipboard::pasteAppendsRowsThroughCallback()
{
    QScopedPointer<QTableWidget> t(makeTable(1));
    TableClipboard::Options opts;
    QTableWidget *tw = t.data();
    int calls = 0;
    opts.appendRow = [tw, &calls] {
        ++calls;
        const int r = tw->rowCount();
        tw->insertRow(r);
        auto *name = new QTableWidgetItem(QStringLiteral("New%1").arg(r));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        tw->setItem(r, 0, name);
        return true;
    };
    auto *clip = TableClipboard::install(tw, opts);
    tw->setCurrentCell(0, 1);
    QVERIFY(clip->pasteText(QStringLiteral("1\t2\n3\t4\n5\t6")));
    QCOMPARE(calls, 2);
    QCOMPARE(tw->rowCount(), 3);
    QCOMPARE(text(tw, 2, 1), QStringLiteral("5"));
    QCOMPARE(text(tw, 2, 2), QStringLiteral("6"));

    // An empty table with nothing selected appends after the last row.
    tw->setRowCount(0);
    tw->clearSelection();
    tw->setCurrentIndex(QModelIndex());
    QVERIFY(clip->pasteText(QStringLiteral("R\t1\t2")));
    QCOMPARE(tw->rowCount(), 1);
    QCOMPARE(text(tw, 0, 2), QStringLiteral("2"));
}

void TestTableClipboard::pasteWithoutCallbackReportsRowsThatDidNotFit()
{
    QScopedPointer<QTableWidget> t(makeTable(2));
    auto *clip = TableClipboard::install(t.data());
    t->setCurrentCell(1, 1);
    QVERIFY(clip->pasteText(QStringLiteral("5\n6\n7")));
    QCOMPARE(t->rowCount(), 2);
    QCOMPARE(text(t.data(), 1, 1), QStringLiteral("5"));
    QVERIFY(clip->lastMessage().contains(QStringLiteral("2 row(s) did not fit")));
}

void TestTableClipboard::pasteSingleValueFillsSelection()
{
    QScopedPointer<QTableWidget> t(makeTable(4));
    auto *clip = TableClipboard::install(t.data());
    t->setSelectionMode(QAbstractItemView::ExtendedSelection);
    t->setRangeSelected(QTableWidgetSelectionRange(0, 2, 2, 2), true);
    QVERIFY(clip->pasteText(QStringLiteral("99\n")));
    for (int r = 0; r < 3; ++r)
        QCOMPARE(text(t.data(), r, 2), QStringLiteral("99"));
    QCOMPARE(text(t.data(), 3, 2), QStringLiteral("40"));
}

void TestTableClipboard::pasteRespectsNoEditTriggers()
{
    QScopedPointer<QTableWidget> t(makeTable(2));
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *clip = TableClipboard::install(t.data());
    t->setCurrentCell(0, 1);
    QVERIFY(!clip->pasteText(QStringLiteral("5")));
    QCOMPARE(text(t.data(), 0, 1), QStringLiteral("1"));
}

void TestTableClipboard::pasteIntoCellWidgets()
{
    QScopedPointer<QTableWidget> t(new QTableWidget(2, 3));
    t->setHorizontalHeaderLabels({QStringLiteral("Type"), QStringLiteral("Value"),
                                  QStringLiteral("On")});
    // The cells start at -1 / unchecked so the copied 0 / 1 cells below
    // show up as changes.
    for (int r = 0; r < 2; ++r) {
        auto *combo = new QComboBox;
        combo->addItem(QStringLiteral("CONCEN"), QStringLiteral("C"));
        combo->addItem(QStringLiteral("MASS"), QStringLiteral("M"));
        t->setCellWidget(r, 0, combo);
        auto *spin = new QDoubleSpinBox;
        spin->setRange(-1.0, 100.0);
        spin->setDecimals(3);
        spin->setValue(-1.0);
        t->setCellWidget(r, 1, spin);
        // A check box wrapped in a centering container, as dialogs do.
        auto *box = new QWidget;
        auto *check = new QCheckBox(box);
        Q_UNUSED(check);
        t->setCellWidget(r, 2, box);
    }
    auto *clip = TableClipboard::install(t.data());
    auto *combo0 = qobject_cast<QComboBox *>(t->cellWidget(0, 0));
    int activated = -1;
    QObject::connect(combo0, &QComboBox::activated, combo0, [&](int i) { activated = i; });

    t->setCurrentCell(0, 0);
    QVERIFY(clip->pasteText(QStringLiteral("mass\t2.5\tTRUE\nC\t101\tno")));
    QCOMPARE(combo0->currentIndex(), 1);
    QCOMPARE(activated, 1);
    QCOMPARE(qobject_cast<QDoubleSpinBox *>(t->cellWidget(0, 1))->value(), 2.5);
    QVERIFY(t->cellWidget(0, 2)->findChild<QCheckBox *>()->isChecked());
    // Second row: combo matched by item data, spin value out of range rejected.
    QCOMPARE(qobject_cast<QComboBox *>(t->cellWidget(1, 0))->currentIndex(), 0);
    QCOMPARE(qobject_cast<QDoubleSpinBox *>(t->cellWidget(1, 1))->value(), -1.0);
    QVERIFY(clip->lastMessage().contains(QStringLiteral("1 value(s) rejected")));

    // Copy reads the widgets back.
    t->selectRow(0);
    QCOMPARE(clip->selectionText(), QStringLiteral("Type\tValue\tOn\nMASS\t2.5\t1"));
}

void TestTableClipboard::pasteConvertsNumericModelValues()
{
    QStandardItemModel model(2, 2);
    model.setHorizontalHeaderLabels({QStringLiteral("X"), QStringLiteral("Count")});
    for (int r = 0; r < 2; ++r) {
        model.setData(model.index(r, 0), 0.5 * r, Qt::EditRole);
        model.setData(model.index(r, 1), r, Qt::EditRole);
    }
    QTableView view;
    view.setModel(&model);
    auto *clip = TableClipboard::install(&view);

    view.setCurrentIndex(model.index(0, 0));
    QVERIFY(clip->pasteText(QStringLiteral("1.25\t7\nabc\t2.5")));
    QCOMPARE(model.data(model.index(0, 0), Qt::EditRole).typeId(), QMetaType::Double);
    QCOMPARE(model.data(model.index(0, 0), Qt::EditRole).toDouble(), 1.25);
    QCOMPARE(model.data(model.index(0, 1), Qt::EditRole).toInt(), 7);
    // Text into a number cell and a fraction into a count cell are rejected.
    QCOMPARE(model.data(model.index(1, 0), Qt::EditRole).toDouble(), 0.5);
    QCOMPARE(model.data(model.index(1, 1), Qt::EditRole).toInt(), 1);
    QVERIFY(clip->lastMessage().contains(QStringLiteral("2 value(s) rejected")));

    // Copy keeps full precision.
    model.setData(model.index(0, 0), 0.1 + 0.2, Qt::EditRole);
    view.setCurrentIndex(model.index(0, 0));
    view.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect);
    QCOMPARE(clip->selectionText(), QStringLiteral("0.3"));
}

void TestTableClipboard::pasteIsOneUndoStep()
{
    // A model that records each edit on an undo stack, as provider-backed
    // editor models do.
    QStandardItemModel model(3, 1);
    model.setHorizontalHeaderLabels({QStringLiteral("V")});
    QUndoStack stack;
    QObject::connect(&model, &QStandardItemModel::itemChanged, &stack, [&stack](QStandardItem *) {
        stack.push(new QUndoCommand(QStringLiteral("edit")));
    });
    QTableView view;
    view.setModel(&model);
    TableClipboard::Options opts;
    opts.undoStack = &stack;
    auto *clip = TableClipboard::install(&view, opts);
    view.setCurrentIndex(model.index(0, 0));
    QVERIFY(clip->pasteText(QStringLiteral("1\n2\n3")));
    QCOMPARE(stack.count(), 1);
}

void TestTableClipboard::pasteMapsLabelsThroughComboDelegate()
{
    CodeModel model;
    QTableView view;
    view.setModel(&model);
    view.setItemDelegate(new CodeDelegate(&view));
    auto *clip = TableClipboard::install(&view);

    // The label is what the user sees and what Copy writes.
    view.selectionModel()->select(model.index(0, 0), QItemSelectionModel::ClearAndSelect);
    QCOMPARE(clip->selectionText(), QStringLiteral("Mid"));

    // A pasted label is stored as the code the combo maps it to.
    view.selectionModel()->clearSelection();
    view.setCurrentIndex(model.index(0, 0));
    QVERIFY(clip->pasteText(QStringLiteral("high\nLow")));
    QCOMPARE(model.codes, (QList<int>{2, 0}));

    // A label the combo does not offer is rejected, not stored as 0.
    view.setCurrentIndex(model.index(0, 0));
    QVERIFY(!clip->pasteText(QStringLiteral("Bogus")));
    QCOMPARE(model.codes.first(), 2);
    QVERIFY(clip->lastMessage().contains(QStringLiteral("1 value(s) rejected")));

    // Pasting the value a cell already holds is accepted.
    QVERIFY(clip->pasteText(QStringLiteral("High")));
}

void TestTableClipboard::pasteThroughPersistentEditor()
{
    CodeModel model;
    QTableView view;
    view.setModel(&model);
    view.setItemDelegate(new CodeDelegate(&view));
    view.openPersistentEditor(model.index(0, 0));
    view.openPersistentEditor(model.index(1, 0));
    auto *clip = TableClipboard::install(&view);

    view.setCurrentIndex(model.index(0, 0));
    QVERIFY(clip->pasteText(QStringLiteral("Low\nHigh")));
    QCOMPARE(model.codes, (QList<int>{0, 2}));
    QVERIFY(view.isPersistentEditorOpen(model.index(0, 0)));
    // Copy reads the model, not the editor widget.
    view.selectionModel()->select(model.index(1, 0), QItemSelectionModel::ClearAndSelect);
    QCOMPARE(clip->selectionText(), QStringLiteral("High"));
}

void TestTableClipboard::pasteCommitsThroughModifiedOnlyDelegate()
{
    QStandardItemModel model(2, 1);
    model.setHorizontalHeaderLabels({QStringLiteral("Note")});
    QTableView view;
    view.setModel(&model);
    view.setItemDelegate(new ModifiedOnlyDelegate(&view));
    auto *clip = TableClipboard::install(&view);
    view.setCurrentIndex(model.index(0, 0));
    QVERIFY(clip->pasteText(QStringLiteral("a\nb")));
    QCOMPARE(model.index(0, 0).data().toString(), QStringLiteral("a"));
    QCOMPARE(model.index(1, 0).data().toString(), QStringLiteral("b"));
}

void TestTableClipboard::pasteKeepsSortedRowsInPlace()
{
    QStandardItemModel source(3, 2);
    source.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Value")});
    const QStringList names{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    for (int r = 0; r < 3; ++r) {
        source.setItem(r, 0, new QStandardItem(names[r]));
        auto *v = new QStandardItem;
        v->setData(double(r + 1), Qt::EditRole);
        source.setItem(r, 1, v);
    }
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QTableView view;
    view.setModel(&proxy);
    view.setSortingEnabled(true);
    view.sortByColumn(1, Qt::AscendingOrder);
    auto *clip = TableClipboard::install(&view);

    // Rows A, B, C on screen get 30, 20, 10 — each write would otherwise
    // re-sort the rows under the paste.
    view.setCurrentIndex(proxy.index(0, 1));
    QVERIFY(clip->pasteText(QStringLiteral("30\n20\n10")));
    const auto valueOf = [&](const QString &name) {
        return source.findItems(name).first()->index().siblingAtColumn(1).data().toDouble();
    };
    QCOMPARE(valueOf(QStringLiteral("A")), 30.0);
    QCOMPARE(valueOf(QStringLiteral("B")), 20.0);
    QCOMPARE(valueOf(QStringLiteral("C")), 10.0);
    QVERIFY(proxy.dynamicSortFilter());
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("C")); // re-sorted after
}

void TestTableClipboard::pasteRowsHookWritesWholeBlock()
{
    QScopedPointer<QTableWidget> t(makeTable(3));
    TableClipboard::Options opts;
    int gotRow = -1, gotColumn = -1;
    QList<QStringList> gotRows;
    opts.pasteRows = [&](int row, int column, const QList<QStringList> &rows, QString *message) {
        gotRow = row;
        gotColumn = column;
        gotRows = rows;
        *message = QStringLiteral("hooked");
        return int(rows.size());
    };
    auto *clip = TableClipboard::install(t.data(), opts);
    t->setCurrentCell(1, 1);
    QVERIFY(clip->pasteText(QStringLiteral("Depth (ft)\tWidth\n5\t50\n6\t60")));
    QCOMPARE(gotRow, 1);
    QCOMPARE(gotColumn, 1);
    QCOMPARE(gotRows.size(), 2); // header removed
    QCOMPARE(clip->lastMessage(), QStringLiteral("hooked"));
    QCOMPARE(text(t.data(), 1, 1), QStringLiteral("2")); // the hook owns the write
}

void TestTableClipboard::shortcutsCopyAndPaste()
{
    QWidget window;
    auto *t = makeTable(3, &window);
    auto *clip = TableClipboard::install(t);
    QCOMPARE(t->contextMenuPolicy(), Qt::ActionsContextMenu);
    QVERIFY(t->actions().contains(clip->copyAction()));
    QVERIFY(t->actions().contains(clip->pasteAction()));

    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    t->setFocus();
    t->selectRow(2);
    QApplication::clipboard()->clear();
    QTest::keySequence(t, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("Name\tDepth (ft)\tWidth\nR3\t3\t30"));

    QApplication::clipboard()->setText(QStringLiteral("11\t111"));
    t->clearSelection();
    t->setCurrentCell(0, 1);
    QTest::keySequence(t, QKeySequence::Paste);
    QCOMPARE(text(t, 0, 1), QStringLiteral("11"));
    QCOMPARE(text(t, 0, 2), QStringLiteral("111"));
}

void TestTableClipboard::shortcutsClaimedFromNonTextCellWidgets()
{
    QWidget window;
    auto *t = new QTableWidget(2, 2, &window);
    t->setHorizontalHeaderLabels({QStringLiteral("Kind"), QStringLiteral("Note")});
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("A"), QStringLiteral("B")});
    t->setCellWidget(0, 0, combo); // added before install
    TableClipboard::install(t);
    auto *edit = new QLineEdit(QStringLiteral("typed"));
    t->setCellWidget(0, 1, edit); // added after install
    t->setItem(1, 1, new QTableWidgetItem(QStringLiteral("n")));

    window.show();
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    // Focus in a combo: Ctrl/Cmd+C copies the table.
    t->selectRow(1);
    combo->setFocus();
    QApplication::clipboard()->setText(QStringLiteral("sentinel"));
    QTest::keySequence(combo, QKeySequence::Copy);
    QVERIFY(QApplication::clipboard()->text().startsWith(QStringLiteral("Kind\tNote")));

    // Focus in a line edit with selected text: Copy takes the text.
    edit->setFocus();
    edit->selectAll();
    QApplication::clipboard()->setText(QStringLiteral("sentinel"));
    QTest::keySequence(edit, QKeySequence::Copy);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("typed"));

    // ... with nothing selected, Copy takes the table's rows.
    edit->deselect();
    QTest::keySequence(edit, QKeySequence::Copy);
    QVERIFY(QApplication::clipboard()->text().startsWith(QStringLiteral("Kind\tNote")));

    // A single value pastes into the line edit; a block pastes into the table.
    // (Selection is set directly: the key clicks above leave modifier state
    // that would make setCurrentCell toggle.)
    t->selectionModel()->select(t->model()->index(1, 0),
                                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    edit->setFocus();
    edit->selectAll();
    QApplication::clipboard()->setText(QStringLiteral("one"));
    QTest::keySequence(edit, QKeySequence::Paste);
    QCOMPARE(edit->text(), QStringLiteral("one"));
    QCOMPARE(t->item(1, 1)->text(), QStringLiteral("n"));
    QApplication::clipboard()->setText(QStringLiteral("B\tblock"));
    QTest::keySequence(edit, QKeySequence::Paste);
    QCOMPARE(t->item(1, 1)->text(), QStringLiteral("block"));
    QCOMPARE(edit->text(), QStringLiteral("one"));
}

void TestTableClipboard::copyOnlyTableRefusesPaste()
{
    QScopedPointer<QTableWidget> t(makeTable(2));
    TableClipboard::Options opts;
    opts.copyOnly = true;
    auto *clip = TableClipboard::install(t.data(), opts);
    QVERIFY(clip->pasteAction() == nullptr);
    QCOMPARE(t->actions().size(), 1);
    t->selectRow(0);
    QCOMPARE(clip->selectionText(), QStringLiteral("Name\tDepth (ft)\tWidth\nR1\t1\t10"));
    t->setCurrentCell(0, 1);
    QVERIFY(!clip->pasteText(QStringLiteral("5")));
    QCOMPARE(text(t.data(), 0, 1), QStringLiteral("1"));
}

QTEST_MAIN(TestTableClipboard)
#include "test_tableclipboard.moc"
