/**
 * @file      labelexpressiondialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Expression builder for label templates.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::ui
{

class LabelExpressionDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \param fields  Themeable attributes for the sublayer being labelled;
     *                these become the insertable token list.
     * \param initial The expression to seed the editor with.
     * @param[in] parent Optional QWidget owner; caller manages parentless modal dialog.
     * @brief Copy schema/initial text and build static/dynamic token tree plus live advisory
     *   preview on GUI thread.
     * @note name token always offered; field unit/display metadata shown without converting values.
     *   Unknown tokens or empty expression never disable OK. No source provider retained,
     *   label/style write or automatic sample lookup.
     */
    LabelExpressionDialog(const QVector<OpenSWMM::Render::AttributeField> &fields,
                          const QString &initial, QWidget *parent = nullptr);

    /*! The edited template.
     * @return Current plain-text template copy exactly as edited, including
     *   whitespace/newlines/unknown tokens. No acceptance check, trimming, validation or source
     *   write; caller applies only after accepted result.
 */
    [[nodiscard]] QString expression() const;

    /*!
     * \brief Sample values used by the preview, keyed by attribute name.
     *
     *        Optional: without it the preview substitutes each token's own
     *        placeholder value so the shape of the label is still visible.
     * @param[in] name Copied sample ID; empty falls back to translated J1 even when attributes
     *   supplied.
     * @param[in] attrs Copied sample values keyed by exact token; EMPTY map triggers synthetic
     *   per-field placeholders, not an explicitly empty feature.
     * @brief Replace preview sample and refresh immediately; does not alter template, schema or
     *   real feature. Missing tokens in a nonempty supplied map resolve empty.
     */
    void setSampleFeature(const QString &name, const QVariantMap &attrs);

    // ── Pure logic, exposed for testing ────────────────────────────────

    /*! Resolves \p expr the same way the paint path does: `{name}` → \p name,
     *  any other `{token}` → `attrs[token]` as a string, unmatched braces
     *  kept literally. Mirrors swmmlayeritem.cpp's resolveExpression.
     * @param[in] expr Raw literal template with brace placeholders; scanned left to right.
     * @param[in] name Exact replacement for lowercase name token, independent of attributes.
     * @param[in] attrs Read-only exact case-sensitive token-to-QVariant map; values converted with
     *   toString, missing/invalid becomes empty.
     * @return New resolved text. Opening brace consumes through FIRST subsequent closing brace,
     *   including empty/nested-looking token; no escaping, recursion, expression evaluation, unit
     *   formatting or whitespace trim. Unmatched braces remain literal and replacements are not
     *   scanned again. Pure value operation.
 */
    [[nodiscard]] static QString resolve(const QString &expr, const QString &name,
                                         const QVariantMap &attrs);

    /*! Every `{token}` in \p expr that is neither "name" nor a known field.
     *  Empty means the template references nothing that would resolve blank. */
    [[nodiscard]] static QStringList
    /**
     * @param[in] expr Raw template scanned with same first-closing-brace rule as resolve.
     * @param[in] fields Known exact case-sensitive attribute names; lowercase name is implicitly
     *   known regardless of schema.
     * @return Distinct unknown tokens in first occurrence order, including empty token if
     *   unrecognized; unmatched braces ignored. Does not inspect sample map or determine whether
     *   known values are actually populated. Pure operation, no syntax rejection.
     */
    unknownTokens(const QString &expr, const QVector<OpenSWMM::Render::AttributeField> &fields);

private:
    /**
     * @param[in] token Raw unescaped identifier wrapped in braces, no validation, case/whitespace
     *   normalization or deduplication.
     * @brief Insert at editor cursor/replacing selection, focus editor and let textChanged refresh
     *   preview. No automatic spacing, acceptance or label model mutation.
     */
    void insertToken(const QString &token);
    /**
     * @brief Resolve current editor text from copied sample, display escaped preview and advisory
     *   unknown-field status.
     * @note Empty sample map generates per-type placeholders; blank sample name uses J1. Unknown
     *   detection consults schema rather than sample values, so a populated unknown token may still
     *   be warned and a known missing value may remain blank without warning. Empty resolved text
     *   displays explanatory italic message. Never blocks OK or writes source/style.
     */
    void refreshPreview();

    QVector<OpenSWMM::Render::AttributeField> m_fields;
    QString m_sampleName;
    QVariantMap m_sampleAttrs;

    QPlainTextEdit *m_edit = nullptr;
    QTreeWidget *m_fieldTree = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_status = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LABELEXPRESSIONDIALOG_H
