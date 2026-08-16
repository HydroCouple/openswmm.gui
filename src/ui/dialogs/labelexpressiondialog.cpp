/*!
 * \file   labelexpressiondialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See labelexpressiondialog.h for the contract.
 */
#include "ui/dialogs/labelexpressiondialog.h"

#include "ui/theme/themehelpers.h"

#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using OpenSWMM::Render::AttributeField;

namespace {

constexpr int kTokenRole = Qt::UserRole + 1;

/*! Groups the field list the way the picker presents it: values that change
 *  per animation frame are worth separating from static model properties,
 *  because a label bound to a dynamic value re-resolves every tick. */
QString sectionFor(const AttributeField &f)
{
    return f.isDynamic ? QObject::tr("Result values (per time step)")
                       : QObject::tr("Model properties (static)");
}

/*! A stand-in value so the preview shows a realistic shape even when the
 *  caller supplied no sample feature. */
QString placeholderFor(const AttributeField &f)
{
    switch (f.type) {
    case QMetaType::QString: return QObject::tr("text");
    case QMetaType::Int:
    case QMetaType::LongLong: return QStringLiteral("42");
    default: return QStringLiteral("1.23");
    }
}

} // namespace

LabelExpressionDialog::LabelExpressionDialog(
    const QVector<AttributeField> &fields,
    const QString &initial,
    QWidget *parent)
    : QDialog(parent), m_fields(fields)
{
    setWindowTitle(tr("Label Expression Builder"));
    setObjectName(QStringLiteral("LabelExpressionDialog"));
    setModal(true);
    resize(720, 480);

    auto *root = new QVBoxLayout(this);

    auto *split = new QSplitter(Qt::Horizontal, this);

    // ── Field list ─────────────────────────────────────────────────────
    auto *fieldBox = new QGroupBox(tr("Fields"), split);
    auto *fieldLay = new QVBoxLayout(fieldBox);
    m_fieldTree = new QTreeWidget(fieldBox);
    m_fieldTree->setHeaderLabels({tr("Field"), tr("Unit")});
    m_fieldTree->setRootIsDecorated(true);
    m_fieldTree->header()->setStretchLastSection(false);
    m_fieldTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fieldTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    fieldLay->addWidget(m_fieldTree);

    auto *insertHint = new QLabel(
        tr("Double-click a field to insert its token."), fieldBox);
    insertHint->setStyleSheet(theme::hintStyle());
    insertHint->setWordWrap(true);
    fieldLay->addWidget(insertHint);
    split->addWidget(fieldBox);

    // "{name}" is always available and is not an attribute, so it heads the
    // tree as its own entry rather than sitting under a data section.
    auto *nameItem = new QTreeWidgetItem(m_fieldTree,
                                         {QStringLiteral("{name}"), QString()});
    nameItem->setData(0, kTokenRole, QStringLiteral("name"));
    nameItem->setToolTip(0, tr("The element's ID — always available."));

    QHash<QString, QTreeWidgetItem *> sections;
    for (const AttributeField &f : m_fields) {
        const QString sec = sectionFor(f);
        QTreeWidgetItem *parentItem = sections.value(sec);
        if (!parentItem) {
            parentItem = new QTreeWidgetItem(m_fieldTree, {sec, QString()});
            parentItem->setFlags(parentItem->flags() & ~Qt::ItemIsSelectable);
            QFont bold = parentItem->font(0);
            bold.setBold(true);
            parentItem->setFont(0, bold);
            parentItem->setExpanded(true);
            sections.insert(sec, parentItem);
        }
        auto *item = new QTreeWidgetItem(
            parentItem, {QStringLiteral("{%1}").arg(f.name), f.unit});
        item->setData(0, kTokenRole, f.name);
        if (!f.displayName.isEmpty() && f.displayName != f.name)
            item->setToolTip(0, f.displayName);
    }
    m_fieldTree->expandAll();

    // ── Editor + syntax help + preview ─────────────────────────────────
    auto *rightPane = new QWidget(split);
    auto *rightLay  = new QVBoxLayout(rightPane);
    rightLay->setContentsMargins(0, 0, 0, 0);

    rightLay->addWidget(new QLabel(tr("Expression"), rightPane));
    m_edit = new QPlainTextEdit(rightPane);
    m_edit->setPlainText(initial);
    m_edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_edit->setTabChangesFocus(true);
    rightLay->addWidget(m_edit, 1);

    auto *help = new QLabel(
        tr("Literal text is kept as typed. <b>{token}</b> is replaced with "
           "that field's value for each feature; <b>{name}</b> is the element "
           "ID. An unknown token resolves to nothing, so the label simply "
           "comes out short.<br>Example: <tt>{name}: {depth} m</tt>"),
        rightPane);
    help->setWordWrap(true);
    help->setTextFormat(Qt::RichText);
    help->setStyleSheet(theme::hintStyle());
    rightLay->addWidget(help);

    auto *previewBox = new QGroupBox(tr("Preview"), rightPane);
    auto *previewLay = new QVBoxLayout(previewBox);
    m_preview = new QLabel(previewBox);
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    previewLay->addWidget(m_preview);
    m_status = new QLabel(previewBox);
    m_status->setWordWrap(true);
    previewLay->addWidget(m_status);
    rightLay->addWidget(previewBox);

    split->addWidget(rightPane);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    root->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_edit, &QPlainTextEdit::textChanged,
            this, &LabelExpressionDialog::refreshPreview);
    connect(m_fieldTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) {
                const QString tok = item->data(0, kTokenRole).toString();
                if (!tok.isEmpty()) insertToken(tok);
            });

    refreshPreview();
}

QString LabelExpressionDialog::expression() const
{
    return m_edit->toPlainText();
}

void LabelExpressionDialog::setSampleFeature(const QString &name,
                                             const QVariantMap &attrs)
{
    m_sampleName  = name;
    m_sampleAttrs = attrs;
    refreshPreview();
}

void LabelExpressionDialog::insertToken(const QString &token)
{
    m_edit->insertPlainText(QStringLiteral("{%1}").arg(token));
    m_edit->setFocus();
}

QString LabelExpressionDialog::resolve(const QString &expr,
                                       const QString &name,
                                       const QVariantMap &attrs)
{
    QString out;
    out.reserve(expr.size());
    int i = 0;
    while (i < expr.size()) {
        if (expr[i] == QLatin1Char('{')) {
            const int close = expr.indexOf(QLatin1Char('}'), i + 1);
            if (close > i) {
                const QString token = expr.mid(i + 1, close - i - 1);
                out += (token == QLatin1String("name"))
                           ? name
                           : attrs.value(token).toString();
                i = close + 1;
                continue;
            }
        }
        out += expr[i];
        ++i;
    }
    return out;
}

QStringList LabelExpressionDialog::unknownTokens(
    const QString &expr, const QVector<AttributeField> &fields)
{
    QSet<QString> known{QStringLiteral("name")};
    for (const AttributeField &f : fields)
        known.insert(f.name);

    QStringList bad;
    int i = 0;
    while (i < expr.size()) {
        if (expr[i] == QLatin1Char('{')) {
            const int close = expr.indexOf(QLatin1Char('}'), i + 1);
            if (close > i) {
                const QString token = expr.mid(i + 1, close - i - 1);
                if (!known.contains(token) && !bad.contains(token))
                    bad << token;
                i = close + 1;
                continue;
            }
        }
        ++i;
    }
    return bad;
}

void LabelExpressionDialog::refreshPreview()
{
    const QString expr = m_edit->toPlainText();

    // Sample values: the caller's feature when it gave one, otherwise a
    // per-type stand-in so the preview still shows the label's shape.
    QString     name  = m_sampleName;
    QVariantMap attrs = m_sampleAttrs;
    const bool synthetic = attrs.isEmpty();
    if (name.isEmpty()) name = tr("J1");
    if (synthetic)
        for (const AttributeField &f : m_fields)
            attrs.insert(f.name, placeholderFor(f));

    const QString resolved = resolve(expr, name, attrs);
    m_preview->setText(resolved.isEmpty()
                           ? tr("<i>(empty — nothing would be drawn)</i>")
                           : resolved.toHtmlEscaped());

    const QStringList bad = unknownTokens(expr, m_fields);
    if (!bad.isEmpty()) {
        m_status->setText(
            tr("<b>Unknown field(s):</b> %1 — these resolve to nothing.")
                .arg(bad.join(QStringLiteral(", ")).toHtmlEscaped()));
        m_status->setStyleSheet(QStringLiteral("color: palette(link-visited);"));
    } else if (synthetic) {
        m_status->setText(tr("Sample values shown; actual labels use each "
                             "feature's own data."));
        m_status->setStyleSheet(theme::hintStyle());
    } else {
        m_status->setText(tr("Previewing feature \"%1\".").arg(name));
        m_status->setStyleSheet(theme::hintStyle());
    }
}

} // namespace openswmmvis::ui
