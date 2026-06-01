/*!
 * \file   stylemanagerdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Browse, apply, save, import, and export Rule Lists from a
 *         user-level style library (Slice Z.17c).
 *
 *         The library is a directory of `.swmm-rule.json` files; one
 *         file per saved Rule List. The dialog lists files in that
 *         directory, lets the user preview each one's contents,
 *         apply a selection to the active layer, save the active
 *         layer's current RuleList into the library under a new
 *         name, and import / export to arbitrary paths outside the
 *         library.
 *
 *         The library directory defaults to a writable per-user
 *         location (`QStandardPaths::AppLocalDataLocation` +
 *         "/styles"). The directory is created on first use.
 *
 *         "Active layer" is passed in by the caller. When null, the
 *         apply/save buttons are disabled but browse / preview /
 *         import / export still work — useful as a project-wide
 *         library editor when no layer is currently selected.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_STYLEMANAGERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STYLEMANAGERDIALOG_H

#include <QDialog>
#include <QString>

class OpenSWMMVisLayer;
class QDialogButtonBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

namespace openswmmvis::ui {

class StyleManagerDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \param activeLayer  Layer whose RuleList the Apply / Save Current
     *                      buttons target. May be nullptr — those buttons
     *                      then sit disabled.
     *  \param parent       Standard Qt parent. */
    explicit StyleManagerDialog(OpenSWMMVisLayer *activeLayer,
                                QWidget *parent = nullptr);
    ~StyleManagerDialog() override;

private slots:
    void onSelectionChanged();
    void onApply();
    void onSaveCurrent();
    void onImport();
    void onExport();
    void onDelete();
    void onOpenLibraryFolder();

private:
    /*! Ensure the library directory exists and is writable.
     *  Returns the absolute path; empty on failure. */
    [[nodiscard]] QString resolveLibraryDir() const;

    /*! Rescan the library directory and rebuild the list. Preserves
     *  the previously-selected file when possible. */
    void refreshLibrary();

    /*! Update the preview pane from the currently-selected list row. */
    void refreshPreview();

    /*! Convenience: full path to the .swmm-rule.json for the currently-
     *  selected row, or empty if no selection. */
    [[nodiscard]] QString currentFilePath() const;

    OpenSWMMVisLayer *m_layer       = nullptr;
    QString           m_libraryDir;

    // Left pane — library list + folder shortcut.
    QListWidget      *m_list        = nullptr;
    QLabel           *m_libDirLabel = nullptr;
    QPushButton      *m_openDirBtn  = nullptr;

    // Right pane — preview + action buttons.
    QPlainTextEdit   *m_preview     = nullptr;
    QPushButton      *m_applyBtn    = nullptr;
    QPushButton      *m_saveBtn     = nullptr;
    QPushButton      *m_importBtn   = nullptr;
    QPushButton      *m_exportBtn   = nullptr;
    QPushButton      *m_deleteBtn   = nullptr;

    QDialogButtonBox *m_btns        = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STYLEMANAGERDIALOG_H
