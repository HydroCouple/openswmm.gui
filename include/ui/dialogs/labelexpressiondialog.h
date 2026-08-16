/*!
 * \file   labelexpressiondialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Expression builder for label templates.
 *
 *         The label template language is deliberately small: literal text
 *         with `{token}` placeholders, where `{name}` is the element id and
 *         every other token is an attribute lookup (see the resolver in
 *         swmmlayeritem.cpp). Small as it is, typing it blind is unforgiving
 *         — a misspelt token is not an error, it silently substitutes an
 *         empty string, so the label just comes out short and nothing says
 *         why.
 *
 *         This dialog closes that loop: the available fields are browsable
 *         and insertable, unknown tokens are called out as you type, and a
 *         live preview resolves the template against a sample feature so
 *         you can see the finished label before committing it.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_LABELEXPRESSIONDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LABELEXPRESSIONDIALOG_H

#include "render/iattributeprovider.h"

#include <QDialog>
#include <QString>
#include <QVariantMap>
#include <QVector>

class QDialogButtonBox;
class QLabel;
class QPlainTextEdit;
class QTreeWidget;

namespace openswmmvis::ui {

class LabelExpressionDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param fields  Themeable attributes for the sublayer being labelled;
     *                these become the insertable token list.
     * \param initial The expression to seed the editor with.
     */
    LabelExpressionDialog(
        const QVector<OpenSWMM::Render::AttributeField> &fields,
        const QString &initial,
        QWidget *parent = nullptr);

    /*! The edited template. */
    [[nodiscard]] QString expression() const;

    /*!
     * \brief Sample values used by the preview, keyed by attribute name.
     *
     *        Optional: without it the preview substitutes each token's own
     *        placeholder value so the shape of the label is still visible.
     */
    void setSampleFeature(const QString &name, const QVariantMap &attrs);

    // ── Pure logic, exposed for testing ────────────────────────────────

    /*! Resolves \p expr the same way the paint path does: `{name}` → \p name,
     *  any other `{token}` → `attrs[token]` as a string, unmatched braces
     *  kept literally. Mirrors swmmlayeritem.cpp's resolveExpression. */
    [[nodiscard]] static QString resolve(const QString &expr,
                                         const QString &name,
                                         const QVariantMap &attrs);

    /*! Every `{token}` in \p expr that is neither "name" nor a known field.
     *  Empty means the template references nothing that would resolve blank. */
    [[nodiscard]] static QStringList unknownTokens(
        const QString &expr,
        const QVector<OpenSWMM::Render::AttributeField> &fields);

private:
    void insertToken(const QString &token);
    void refreshPreview();

    QVector<OpenSWMM::Render::AttributeField> m_fields;
    QString      m_sampleName;
    QVariantMap  m_sampleAttrs;

    QPlainTextEdit   *m_edit      = nullptr;
    QTreeWidget      *m_fieldTree = nullptr;
    QLabel           *m_preview   = nullptr;
    QLabel           *m_status    = nullptr;
    QDialogButtonBox *m_buttons   = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LABELEXPRESSIONDIALOG_H
