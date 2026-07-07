/*!
 * \file   annotationstyledialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/annotationstyledialog.h"

#include "layers/annotationtextitem.h"

#include <qpropertymodel.h>
#include <qpropertyitemdelegate.h>

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

AnnotationStyleDialog::AnnotationStyleDialog(AnnotationTextItem *item,
                                             QWidget *parent)
    : QDialog(parent)
    , m_item(item)
{
    setWindowTitle(tr("Add Text Annotation"));
    resize(420, 540);
    buildUi();
}

AnnotationStyleDialog::~AnnotationStyleDialog() = default;

void AnnotationStyleDialog::setEditMode(bool edit)
{
    setWindowTitle(edit ? tr("Edit Text Annotation")
                        : tr("Add Text Annotation"));
    if (auto *ok = m_btns ? m_btns->button(QDialogButtonBox::Ok) : nullptr)
        ok->setText(edit ? tr("Apply") : tr("Add"));
}

void AnnotationStyleDialog::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // Quick text field — the single most-edited attribute lives outside the
    // tree so the user can type the label without expanding any group.
    auto *textRow = new QHBoxLayout;
    textRow->addWidget(new QLabel(tr("Text:"), this));
    m_text = new QLineEdit(m_item ? m_item->text() : QString(), this);
    m_text->setPlaceholderText(tr("Annotation text"));
    textRow->addWidget(m_text, 1);
    root->addLayout(textRow);

    // Property tree — every style attribute is exposed here. QPropertyModel
    // auto-resolves an editor for QColor (colour picker), QFont (font
    // dialog), bool (checkbox), and numeric types (spin boxes); no
    // per-field delegate wiring is needed.
    m_tree = new QTreeView(this);
    m_tree->setAlternatingRowColors(true);
    m_tree->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(false);
    if (m_item) {
        m_model = new QPropertyModel(m_item, this);
        m_tree->setModel(m_model);
        // QPropertyItemDelegate is what binds QColor → QColorDialog,
        // QFont → QFontDialog, bool → checkbox editor, etc. Without it the
        // tree falls back to plain text editors and the user can't drive a
        // colour picker from the row.
        m_tree->setItemDelegate(new QPropertyItemDelegate(this));
        m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        m_tree->header()->setStretchLastSection(true);
        m_tree->header()->setMinimumSectionSize(90);  // stop columns collapsing on a narrow dialog
        m_tree->expandAll();
    }
    root->addWidget(m_tree, 1);

    // Keep the quick text field in sync with the tree's text row — both
    // ultimately write back to the same Q_PROPERTY, so a typed change in
    // the line edit needs to round-trip into the model, and a change made
    // via the tree needs to refresh the line edit.
    if (m_item) {
        connect(m_text, &QLineEdit::textChanged, this, [this](const QString &t) {
            if (m_item->text() == t) return;
            m_item->setText(t);
            if (m_model) m_model->refreshValues();
        });
        connect(m_item, &AnnotationTextItem::textChanged,
                m_text, [this](const QString &t) {
            if (m_text->text() == t) return;
            QSignalBlocker block(m_text);
            m_text->setText(t);
        });
    }

    m_btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    if (auto *ok = m_btns->button(QDialogButtonBox::Ok))
        ok->setText(tr("Add"));
    connect(m_btns, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(m_btns);
}
