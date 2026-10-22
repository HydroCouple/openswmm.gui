/**
 * @file      pathbrowsedelegate.h
 * @brief     Reusable QStyledItemDelegate that renders a {QLineEdit + "…" browse button} composite
 *            editor for a path column.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Reusable QStyledItemDelegate that renders a {QLineEdit + "…" browse
 * button} composite editor for a path column. Used by the Simulation
 * Options dialog's hot-start-saves and [PLUGINS] tables, and any other
 * MVC list / table where a row maps to a file path.
 *
 * Each instance carries a fixed dialog title, file-filter string, and
 * open/save mode — pass these in via the constructor (or the matching
 * setters) so a single class can serve multiple columns with different
 * file pickers.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PATHBROWSEDELEGATE_H
#define PATHBROWSEDELEGATE_H

#include <QString>
#include <QStyledItemDelegate>

class PathBrowseDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum Mode { OpenFile, SaveFile };

    /**
     * @param[in] parent Optional QObject owner; delegate must outlive editors because their
     *   callbacks capture raw this.
     * @brief Construct OpenFile delegate with empty title/filter/placeholder/anchor, no file
     *   picker/model write. GUI-thread only.
     */
    explicit PathBrowseDelegate(QObject *parent = nullptr);
    /**
     * @param[in] mode Copied open/save selection, any nonSave enum follows open branch.
     * @param[in] title Moved picker title, empty future fallback Choose file.
     * @param[in] filter Moved chooser filter, empty All Files fallback.
     * @param[in] placeholder Moved editor hint, empty none.
     * @param[in] parent Optional QObject owner, must preserve delegate until all editors destroyed.
     * @brief Store picker configuration only, no editor/file/model mutation.
     */
    PathBrowseDelegate(Mode mode, QString title, QString filter, QString placeholder = {},
                       QObject *parent = nullptr);

    /**
     * @param[in] m Raw mode, SaveFile exact chooses save branch, other open.
     * @note Store only, no model/file write or notifications. Affects subsequent browse clicks
     *   including already-created editor.
     */
    void setMode(Mode m) { m_mode = m; }
    /**
     * @param[in] t Copied title, empty fallback.
     * @note Store only, no model/file write or notifications. Existing button tooltip remains
     *   construction-time value, browse caption uses current title.
     */
    void setDialogTitle(const QString &t) { m_title = t; }
    /**
     * @param[in] f Copied chooser name filter, empty fallback.
     * @note Store only, no model/file write or notifications. Future/current editor browse clicks
     *   read current filter.
     */
    void setFilter(const QString &f) { m_filter = f; }
    /**
     * @param[in] p Copied hint text.
     * @note Store only, no model/file write or notifications. Only editors created later get new
     *   placeholder, no repaint/update existing controls.
     */
    void setPlaceholder(const QString &p) { m_placeholder = p; }

    /*! Slice IO-11c — directory the editor normalises picked paths against.
     *  Empty (the default) preserves legacy behaviour of storing whatever
     *  QFileDialog returned, verbatim. When set, a browse outcome under
     *  a compatible filesystem root is rewritten to its relative form before being committed
     *  to the model, so the engine slot ends up with a portable token
     *  instead of a host-specific path, including parent-directory (..) tokens.
     * @param[in] dir Copied raw directory path, empty disables relative rewriting; no
     *   existence/canonicalization validation.
     * @note Store only, no model/file write or notifications. Open chooser resolves current text
     *   against anchor; picked path may be relativized with .. even OUTSIDE anchor when QDir
     *   relativeFilePath returns relative. Save chooser initial path does not resolve anchor. Typed
     *   paths bypass normalization.
     */
    void setProjectAnchor(const QString &dir) { m_anchor = dir; }
    /**
     * @return Copied stored raw anchor string, no resolved/canonical path, existence check or
     *   ownership transfer.
     */
    [[nodiscard]] QString projectAnchor() const { return m_anchor; }

    // QStyledItemDelegate ------------------------------------------------------
    /**
     * @param[in] parent Optional QWidget owner for returned composite, caller/view manages
     *   lifetime.
     * @param[in] opt Unused style option, implementation unnamed.
     * @param[in] idx Unused cell index, implementation unnamed.
     * @return New composite line edit+browse button with line focus proxy, no immediate model
     *   read/write.
     * @note Browse reads CURRENT raw delegate configuration; accepted nonempty path rewrites
     *   relative whenever QDir can (including .. outside anchor), updates line then emits
     *   commitData(host). Cancel preserves. Open uses persistent FileOpenDialog classified filter,
     *   Save native chooser. EditingFinished also emits commitData; no closeEditor emitted or
     *   validation/trim on commit. Raw delegate this must outlive callbacks/modal chooser; host
     *   guards sender lifetime, source/model owner handles commit.
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &opt,
                          const QModelIndex &idx) const override;
    /**
     * @param[in] editor Optional borrowed composite; find child by fixed line-edit objectName, no
     *   match no-op.
     * @param[in] idx Current source cell EditRole toString.
     * @note Block line signals and hydrate raw text unchanged, no anchor resolve/normalization,
     *   file validation or model write.
     */
    void setEditorData(QWidget *editor, const QModelIndex &idx) const override;
    /**
     * @param[in] editor Optional borrowed composite, no matching named line edit no-op.
     * @param[in] model Optional mutable destination, null no-op.
     * @param[in] idx Current destination forwarded unchanged.
     * @note Submit raw UNTRIMMED line text via EditRole, ignore setter result, no existence
     *   validation/anchor conversion/own undo or file write. Source model owns
     *   normalization/notifications.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &idx) const override;

private:
    Mode m_mode = OpenFile;
    QString m_title;
    QString m_filter;
    QString m_placeholder;
    QString m_anchor; ///< IO-11c: relativise picked paths against this dir.
};

#endif // PATHBROWSEDELEGATE_H
