/**
 * @file      meshpage.h
 * @brief     Simulation Options → Mesh.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Mesh.
 *
 * File management for 2D mesh configurations: which `.2dm` the engine reads
 * via `[2D_MESH_FILE]`, or the inline mesh embedded in the project `.inp`.
 *
 * Deliberately never gated on the 2D module: creating or selecting a mesh is
 * what turns the module on, so gating this page would be circular.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESHPAGE_H
#define MESHPAGE_H

#include <functional>

#include "ui/dialogs/simoptions/simoptionspage.h"

class QLabel;
class QListWidget;

namespace openswmmvis::ui
{

class MeshPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Build owned mesh-management controls and immediately refresh the disk inventory.
     * @param ctx Borrowed GUI-thread context that must outlive the page; dependencies
     * must remain valid during file operations, nested prompts and import completion.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Reads input-file references and sibling names during construction; no mesh write.
     */
    explicit MeshPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Mesh string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Refresh sibling mesh names and active/inline reference labels from disk.
     * Does not hydrate a deferred option draft or reload the live engine mesh.
     */
    void read() override;
    /**
     * @brief Contribute no deferred Apply operation for mesh actions.
     * @return Always zero. Set Active, Remove and completed imports have separate immediate
     * side effects; outer Cancel does not reverse their disk/engine changes.
     */
    int write() override;

    /**
     * @brief Replace the retained callback synchronizing the Models page's 2D intent draft.
     * @param setter Moved optional callable receiving true after successful reference
     * patching or false after removal classified as active. Empty skips synchronization.
     * Caller keeps captures live until replacement/page destruction; callbacks run
     * synchronously on the GUI thread and exceptions propagate. Assignment invokes
     * nothing and does not itself enable the solver, write IGNORE_2D or persist settings.
     */
    void set2DModuleSetter(std::function<void(bool)> setter);

private slots:
    /**
     * @brief Replace the owned list from readable sibling *.2dm names and input-file text.
     * Missing controls return immediately. Without a model path, shows unsaved/no-mesh
     * labels and no sibling inventory. Reads the input once as UTF-8; failure silently
     * looks empty. Inline detection requires both vertex and triangle substrings, even
     * inside comments. Active FILE token is parsed tolerantly from the first matching
     * mesh-reference section and matched by basename, without path/geometry validation.
     * Does not parse mesh topology, synchronize the engine or alter input files.
     */
    void refreshMeshList();
    /**
     * @brief Immediately patch the saved input's mesh reference from the current list item.
     * Missing layer/list returns; missing selection/path prompts. Inline selection clears
     * the reference, external selection writes its sibling path; patch failure reports
     * and stops. After success attempts MESH_FILE in the live engine without checking
     * setter status, calls the retained module setter with true and refreshes the list.
     * Disk and engine may diverge on refusal. Does not reload/validate mesh geometry,
     * create undo or defer disk edits to Apply; dependencies must survive nested prompts.
     */
    void onMeshSetActive();
    /**
     * @brief Confirm then delete the selected external sibling mesh file from disk.
     * Missing dependencies/selection/path return or prompt; synthetic inline rows cannot
     * be removed. Active classification uses filename substring matching against the
     * label, not a normalized reference comparison. Refusal or deletion error stops;
     * a missing file is treated as already removed. When classified active, attempts to
     * clear the live MESH_FILE without checking status and calls the module setter false.
     * Does not clear the input's on-disk reference, restore the file on Cancel, create
     * undo or reload geometry. Captured dependencies must survive the modal confirmation.
     */
    void onMeshRemove();
    /**
     * @brief Browse for a mesh and ask the project window to import it asynchronously.
     * No project or cancelled picker returns. The project performs copying/parsing/layer
     * adoption; one-shot meshImportFinished connection on this page handles the next
     * completion, without a request identifier. Its ok flag gates success, borrowed
     * message supplies failure text, and borrowed meshPath supplies a basename to find
     * and activate after refresh. Failure prompts only for nonempty text. Success can
     * patch the input through onMeshSetActive(). Dependencies must survive nested picker/
     * prompts and subsequent callbacks; destroyed page disconnects its receiver context.
     * Starting import does not itself prove copy, parse, activation or reference success.
     */
    void onMeshImport();

private:
    /**
     * @brief Create owned labels, single-selection inventory and immediate action buttons once.
     * Import enablement snapshots project-window presence at construction. Connects
     * Refresh/Set Active/Remove/Import to their handlers, then reads disk inventory.
     * No 2D module gate is imposed here and no deferred common-option draft is created.
     */
    void buildUi();

    QListWidget *m_meshList = nullptr;
    QLabel *m_meshDirLabel = nullptr;
    QLabel *m_meshActiveLabel = nullptr;
    std::function<void(bool)> m_set2DModuleEnabled;
};

} // namespace openswmmvis::ui

#endif // MESHPAGE_H
