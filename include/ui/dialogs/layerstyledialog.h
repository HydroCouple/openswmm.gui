/**
 * @file      layerstyledialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Unified, multitab styling dialog for every OpenSWMMVisLayer.
 *
 *         Slice X.1 — refactored to a QGIS-aligned fixed tab set with a
 *         horizontal tabs (`tabPosition = North`).  The dialog hosts six
 *         tabs in canonical QGIS order — Information / Source / Symbology
 *         / Labels / Rendering / Metadata — and shows only the ones the
 *         current layer type supports per `layerCapabilities()`.
 *
 *         Symbology/labels/basemap edits preview live; name, visibility and
 *         opacity remain drafts until Apply/OK. Apply commits a new Cancel
 *         checkpoint; Cancel restores later style/subject previews. CRS
 *         selection writes immediately and is outside these snapshots.
 *
 *         Entry points:
 *           - Layer-tree right-click → Properties        (any layer)
 *           - Legend dock right-click → Edit Style       (sublayer rows)
 *           - Kind-row right-click → Properties          (focused on kind)
 *
 *         The optional \p initialRoutingId focuses the Symbology tab on
 *         the matching kind / sublayer when launched from a sub-row
 *         context menu.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_LAYERSTYLEDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LAYERSTYLEDIALOG_H

#include <QDialog>
#include <QFlags>
#include <QJsonObject>
#include <QPointer>
#include <QString>

#include <memory>
#include <vector>

class QLineEdit;
class QLabel;
class QToolButton;
class QCheckBox;
class QComboBox;
class QGroupBox;
class QSlider;
class QSpinBox;
class QPlainTextEdit;
class QTableWidget;
class QTabWidget;

class OpenSWMMVisLayer;
class QUndoStack; // #36 — optional app-level undo for symbology edits

namespace openswmmvis::ui
{

class ILayerStyleSubject;

/*!
 * \enum  LayerCapability
 * \brief Which tabs apply to a given layer type.
 *
 *        Information / Rendering / Metadata are universal (every layer
 *        type has them).  Source applies when the layer has an editable
 *        CRS or filter expression.  Symbology and Labels are gated per
 *        the §X.3.1 table.  The dialog skips any tab whose capability
 *        flag is false — no empty placeholders.
 */
enum class LayerCapability : unsigned int {
    Information = 1u << 0,
    Source = 1u << 1,
    Symbology = 1u << 2,
    Labels = 1u << 3,
    Rendering = 1u << 4,
    Metadata = 1u << 5,
    // VS.5 — the deferred-feature tabs (Temporal / Mask / AuxStorage /
    // Joins / Diagrams) were removed from the dialog for simplicity. Their
    // capability bits are retired; the standalone tab widget classes are
    // scheduled for stale-file cleanup in VS.11.
};
Q_DECLARE_FLAGS(LayerCapabilities, LayerCapability)

/*! Return the capability mask for a layer based on its layerType + class. */
/**
 * @brief Compute supported main tabs from the layer type.
 * @param[in] layer Nullable borrowed layer, queried synchronously without mutation.
 * @return Information, Rendering and Metadata always; Source for CRS/source-bearing types,
 * Symbology for model/results/vector/raster/2D kinds, Labels for feature-bearing kinds. Null yields
 * only universal flags.
 */
[[nodiscard]] LayerCapabilities layerCapabilities(OpenSWMMVisLayer *layer);

// ---------------------------------------------------------------------------

class LayerStyleDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @brief Build a GUI-thread layer properties session with live style previews and Apply/Cancel
     * checkpoints.
     * @param[in,out] layer Nullable borrowed layer, guarded by QPointer; its persistent
     * adapters/styles remain layer-owned. Destruction invalidates the target, clears wrappers,
     * disables/rejects the dialog. The dialog owns only subject wrappers and Qt child editors.
     * @param[in] initialRoutingId Optional exact kind/sublayer routing ID, consumed to focus
     * Symbology and a matching nested editor. Unknown IDs leave the available default page.
     * @param[in] parent Optional Qt owner of this dialog.
     * @param[in,out] undoStack Optional borrowed app undo stack, QPointer-guarded; accepted changes
     * push one session command. Stack destruction cancels and disables the session.
     * Captures subject/full-style/open-time undo baselines after editor construction.
     * Name/visibility/opacity remain drafts; most style controls preview live. Apply advances
     * Cancel checkpoints; import remains reversible preview. CRS writes immediately and is not
     * captured by the style/general rollback. Null target can display universal tabs, but Apply/OK
     * do nothing.
     */
    explicit LayerStyleDialog(OpenSWMMVisLayer *layer, QString initialRoutingId = {},
                              QWidget *parent = nullptr,
                              QUndoStack *undoStack = nullptr); // #36 — optional
    /**
     * @brief Destroy owned subject wrappers, Qt controls and connections without deleting the
     * layer/undo stack.
     * Destruction itself does not run Cancel; callers use reject/Cancel to restore an unfinished
     * preview.
     */
    ~LayerStyleDialog() override;

public slots:
    /**
     * @brief Route virtual acceptance through draft validation and session recording, rather than
     * bypassing onAccept.
     */
    void accept() override;
    /**
     * @brief Route virtual rejection through Cancel checkpoint restoration and retained
     * Apply-session undo recording.
     */
    void reject() override;

private slots:
    /**
     * @brief Commit valid draft general fields and advance subject/full-style Cancel baselines
     * without closing.
     * No-op for finished/layerless sessions or an invalid classification draft. Rehydrates general
     * widgets and rollback values, retaining original session undo baselines so earlier Apply
     * changes remain undoable after closing.
     */
    void onApply();
    /**
     * @brief Commit valid general drafts, record a changed session on the surviving undo stack,
     * mark finished and accept.
     * Invalid classification focuses its editor and keeps the dialog open; finished/layerless
     * sessions are ignored.
     */
    void onAccept();
    /**
     * @brief Restore live style/subject previews to the latest Apply/open checkpoint and reject
     * once.
     * Only calls full-style restoration when serialized state differs, then reacquires/restores
     * subject identities. Unapplied general drafts are discarded by closing; prior Apply changes
     * remain and can be recorded as one undo command from the original baseline. CRS lies outside
     * these snapshots. A finished session is ignored.
     */
    void onCancel();
    /**
     * @brief Run a modal CRS picker and apply its newly allocated selection immediately with layer
     * ownership.
     * Cancellation/null selection is a no-op; updates authority text/pending string after
     * selection. CRS is outside the style/general Cancel and undo snapshots; no geometry
     * transformation is requested here.
     */
    void onPickCRS();
    /**
     * @brief Synchronize the opacity spin draft without recursive signals or layer writes.
     * @param[in] v Percentage from the [0,100] slider; the receiving spin enforces its range.
     */
    void onOpacitySliderChanged(int v);
    /**
     * @brief Synchronize the opacity slider draft without recursive signals or layer writes.
     * @param[in] v Percentage from the [0,100] spin; the receiving slider enforces its range.
     */
    void onOpacitySpinChanged(int v);
    /*! Slice X.23 — Import / Export buttons in the dialog bar.  Round-
     *  trip the active layer's full styling via the StyleFileIO helper
     *  (native .swmm-style.json + minimal QGIS .qml import). */
    /**
     * @brief Prompt for native/QGIS style import and apply it as live session preview.
     * Rechecks target/session after the file dialog; shows import errors/warnings and reacquires
     * subject wrappers on success. Keeps original Cancel/undo baselines, does not write general
     * drafts, and does not rebuild existing tabs.
     */
    void onImportStyle();
    /**
     * @brief Prompt for a native style destination and export the current live layer style.
     * Rechecks target/session after the file dialog, reports writer failure or success/warnings.
     * Does not commit draft name/visibility/opacity or advance Cancel/undo baselines.
     */
    void onExportStyle();

    /*! Slice X.26 — refresh the Basemap-adjustments group from the
     *  layer's current `basemapRenderParams()` (e.g. after style
     *  import or another view mutated it). */
    /**
     * @brief Read supported basemap parameters into controls under signal blockers.
     * No-op without built controls or a recognized surviving basemap; slider displays round/clamp
     * brightness/saturation percentages and contrast offset without writing back.
     */
    void onBasemapRenderParamsChanged();

private:
    /**
     * @brief Append Qt-owned main tabs in capability order: Information, Source, Symbology, Labels,
     * Rendering, Metadata.
     * Called once after tab host/capability initialization; unsupported tabs are omitted.
     */
    void buildTabs();
    /**
     * @brief Append a scrolling Identification/Properties page with a draft name and read-only
     * type/summary fields.
     * Requires the initialized main tab host; values are hydrated by readFromLayer.
     */
    void buildInformationTab();
    /**
     * @brief Append a scrolling source-description/CRS page and connect the modal CRS action.
     * Requires the initialized main tab host; source text is read-only, CRS selection applies live
     * rather than buffering a transformation.
     */
    void buildSourceTab();
    /**
     * @brief Append the type-specific live style editor page using borrowed persistent adapters.
     * Model/results use the kind tree; 2D results/mesh use specialized panels; vector prefers its
     * symbol adapter, raster uses its renderer panel, and other types use SymbologyTab. Qt owns new
     * controls; layer-owned styles/adapters must survive their active callbacks.
     */
    void buildSymbologyTab();
    /**
     * @brief Append a scrolling live LabelsTab bound to the guarded layer and owned by the tab
     * host.
     */
    void buildLabelsTab();
    /**
     * @brief Append draft visibility/percent-opacity controls and applicable live basemap
     * adjustments.
     * Basemap brightness/saturation sliders map [-100,100] to [-1,1], contrast to [0,2], with
     * bilinear/nearest selection and identity reset; normal setters repaint. Hydrates before
     * connecting and subscribes to external basemap parameter changes.
     */
    void buildRenderingTab();
    /**
     * @brief Append a scrolling read-only metadata table with manual Refresh.
     * Mesh layers additionally offer background overview rebuild, gating its button until
     * overviewBuildFinished. Does not start a rebuild during construction or subscribe metadata to
     * a timer.
     */
    void buildMetadataTab();
    /**
     * @brief Hydrate general/source/summary controls from a surviving layer and capture their
     * display rollback values.
     * Blocks draft-widget signals, rounds only displayed opacity to percent, clears pending CRS
     * text and refreshes metadata. Does not reconstruct symbology widgets or replace
     * full-style/subject undo baselines.
     */
    void readFromLayer();
    // Rebuild the Metadata tab's key/value table from the common block plus
    // the active layer's extendedMetadata(). Also used by the Refresh button
    // so a live results layer's growing counts can be re-read on demand.
    /**
     * @brief Replace table-owned cells with common layer values and extendedMetadata rows.
     * No-op without table/layer. Opacity is displayed as rounded percent, extent coordinates retain
     * layer units, and full values become tooltips; a manual snapshot without polling.
     */
    void populateMetadata();
    /**
     * @brief Apply only name/visibility/opacity drafts changed relative to their last hydrated
     * display.
     * Preserves untouched external edits and precise opacity hidden by percentage rounding. Records
     * the first overwritten value and latest applied value per changed general field for session
     * undo; no-op without a layer.
     */
    void writeGeneralRenderingToLayer();
    /**
     * @brief Replace Cancel subject snapshots by reacquiring current layer subjects using stable
     * routing IDs/title fallback and 2D water policy.
     */
    void snapshotSubjects();
    /**
     * @brief Push one undo command when full style, subject state or net general edits differ from
     * open-time baselines.
     * Requires surviving layer/undo stack, drops net-zero general changes and captures current
     * subjects afresh; command is stack-owned and skips its first redo because the preview is
     * already applied. Does not itself finish the session.
     */
    void recordAcceptedSession();
    /**
     * @brief Check every descendant classification editor and focus the first invalid draft.
     * @return True when all found drafts are valid (including none), false after focusing the first
     * invalid editor; no model changes.
     */
    bool validClassificationDrafts() const;
    /**
     * @brief Clear the dead layer reference and owned wrappers, mark finished, disable and reject
     * directly.
     * Avoids restoration/undo against a destroyed target; Qt child controls are subsequently
     * destroyed with the dialog.
     */
    void invalidateTarget();
    /**
     * @brief Reacquire current subjects and restore matching Cancel snapshots by stable key,
     * including 2D water display policy.
     * No-op without the target; does not reuse wrappers that structural style restoration may have
     * invalidated.
     */
    void restoreSubjectsFromSnapshot();
    /**
     * @brief Reveal Symbology for a nonempty requested routing ID, comparing translated tab
     * captions without mnemonic ampersands.
     * First lets the 2D results panel select an additional result; otherwise matches tooltips in
     * the first nested tab widget. Kind-tree routing is handled during its construction;
     * absent/unknown IDs leave defaults.
     */
    void focusInitialSubject();

    QPointer<OpenSWMMVisLayer> m_layer;
    LayerCapabilities m_caps;

    // Subject wrappers are owned by the dialog; the property objects they
    // point at are the layer's PERSISTENT adapters (adapter-ownership
    // refactor), so every panel edits the same instances the snapshots
    // cover. Rendered into the Symbology / Labels tabs as appropriate.
    std::vector<std::unique_ptr<ILayerStyleSubject>> m_subjects;
    QJsonObject m_subjectSnapshots;
    // Full renderer-state baseline for Cancel (kind renderers, layer
    // renderer, label config) via StyleFileIO::styleToJson — the part the
    // per-subject snapshots don't cover. Refreshed on Apply; import is a preview.
    QJsonObject m_styleSnapshot;

    // #36 — app-level undo. m_undoBaseline (+ m_undoStyleBaseline) is the
    // state at dialog open; on OK, if it changed, push an
    // EditLayerStyleCommand(before,after) onto m_undoStack so the symbology
    // edit is undoable after the dialog closes. Null stack → no command
    // (back-compat for callers without one).
    QPointer<QUndoStack> m_undoStack;
    QJsonObject m_undoBaseline;
    QJsonObject m_undoStyleBaseline;

    QJsonObject m_undoGeneralBefore;
    QJsonObject m_undoGeneralAfter;
    bool m_sessionFinished = false;

    QString m_initialRoutingId;

    // Main horizontal tab bar (tabPosition = North).
    QTabWidget *m_tabs = nullptr;

    // Information tab widgets.
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_typeLabel = nullptr;
    QPlainTextEdit *m_infoText = nullptr;

    // Source tab widgets.
    QLabel *m_sourceLabel = nullptr; // populated in readFromLayer()
    QLabel *m_crsLabel = nullptr;
    QToolButton *m_crsButton = nullptr;
    QString m_pendingCRSAuthority;

    // Rendering tab widgets.
    QCheckBox *m_visibleBox = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QSpinBox *m_opacitySpin = nullptr;

    // Slice X.26 — basemap render adjustments (only created when the
    // active layer is a basemap kind: XYZ / WMTS / WMS / WCS).
    QGroupBox *m_basemapAdjustBox = nullptr;
    QSlider *m_brightnessSlider = nullptr;
    QSlider *m_contrastSlider = nullptr;
    QSlider *m_saturationSlider = nullptr;
    QComboBox *m_resamplingCombo = nullptr;

    // Metadata tab — read-only Name/Value table (common block + the layer's
    // type-specific extendedMetadata()).
    QTableWidget *m_metadataTable = nullptr;

    // General / Rendering snapshot for Cancel rollback.
    QString m_snapshotName;
    bool m_snapshotVisible = true;
    double m_snapshotOpacity = 1.0;
};

} // namespace openswmmvis::ui

Q_DECLARE_OPERATORS_FOR_FLAGS(openswmmvis::ui::LayerCapabilities)

#endif // OPENSWMMVIS_UI_DIALOGS_LAYERSTYLEDIALOG_H
