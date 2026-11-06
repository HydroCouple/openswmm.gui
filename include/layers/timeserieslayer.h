/**
 * @file timeserieslayer.h
 * @brief Native persistent observation collection, optional station map and feature associations.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include "layers/gisvectorlayer.h"
#include "timeseries/timeseriesdataset.h"
#include "selection/selectionmanager.h"
#include <QJsonArray>
#include <QPointer>
#include <memory>

class MapCanvas;
class QWidget;

/**
 * @brief GUI-owned observation layer with read-only, independently owned source samples.
 * @details GISVectorLayer supplies ordinary station markers, labels and FID selection. The
 * optional station table never owns observations: an empty/nonspatial table is valid. Native
 * series selection has a separate namespace from model time-series and GIS feature identities.
 * All layer methods use the GUI thread; sample readers reopen their own handles on workers.
 */
class TimeSeriesLayer final : public GISVectorLayer
{
    Q_OBJECT
public:
    /** @brief Construct an unopened collection; no engine, plugin or file is required.
     * @param[in] parent Optional workspace owner. */
    explicit TimeSeriesLayer(OpenSWMMVisWorkspace *parent = nullptr);
    /** @brief Release presentation connections; source files are never deleted. */
    ~TimeSeriesLayer() override;

    /** @brief Validate and open metadata and the bounded station table.
     * @param[in] path Native GeoPackage path, retained even when unavailable.
     * @param[in] expectedDatasetId Nonempty expected UUID prevents accidental source substitution.
     * @param[out] error Failure detail, cleared on success.
     * @return True when metadata and station table are available. Missing sources remain representable.
     * @note Metadata-only IO is synchronous; import/sample jobs remain worker operations. */
    bool openTimeSeries(const QString &path, const QString &expectedDatasetId = {},
                        QString *error = nullptr);
    /** @return Immutable shared metadata snapshot, null while source is unavailable. */
    std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset> dataset() const
    {
        return m_data;
    }
    /** @return Absolute stored path, including unavailable sources. */
    QString sourcePath() const { return m_sourcePath; }
    /** @return Expected immutable dataset UUID, retained through source loss. */
    QString datasetId() const { return m_expectedId; }
    /** @return Current source failure text; empty after successful open. */
    QString sourceError() const { return m_sourceError; }
    /** @return Source path for project persistence and properties. */
    QString sourceDescription() const override { return m_sourcePath; }
    /** @return Collection identity/count/availability metadata without sample IO. */
    QVector<QPair<QString, QString>> extendedMetadata() const override;
    /** @return Stable series IDs in source order, without loading observations. */
    QStringList seriesIds() const;
    /** @param[in] id Series UUID.
     * @return Copied series metadata; empty id means not found. */
    OpenSWMM::TimeSeries::SeriesInfo seriesInfo(const QString &id) const;
    /** @param[in] id Series UUID.
     * @return Site/title/unit caption, preserving source identity in details. */
    QString seriesCaption(const QString &id) const;
    /** @return Current series selection in source order. */
    QStringList selectedSeriesIds() const;
    /** @brief Replace collection selection and synchronize owned markers and the project bus.
     * @param[in] ids Series identities; missing/duplicate IDs are ignored. */
    void setSelectedSeriesIds(const QStringList &ids);
    /** @brief Attach/detach the canvas and its selection/lifecycle signals.
     * @param[in] canvas Borrowed attached canvas, null when removed. */
    void bindCanvas(MapCanvas *canvas);
    /** @return Persisted feature links; source samples and model-assignment recipes are separate. */
    QJsonArray associations() const { return m_associations; }
    /** @brief Restore validated relationship records without an undo step.
     * @param[in] links Project-sidecar associations; malformed records are dropped. */
    void restoreAssociations(const QJsonArray &links);
    /** @brief Attach selected series to one currently selected persistent GIS/model feature.
     * @param[in] ids Existing series IDs.
     * @param[out] error Diagnostic when selection is missing/ambiguous/temporary.
     * @return True after one undoable relationship change; samples are unchanged. */
    bool attachToSelectedFeature(const QStringList &ids, QString *error = nullptr);
    /** @brief Remove relationships as one undoable presentation edit.
     * @param[in] ids Series IDs whose links should be removed. */
    void detachFeatures(const QStringList &ids);
    /** @param[in] id Series UUID.
     * @return User-facing linked/unresolved/unlinked status after current source validation. */
    QString associationStatus(const QString &id) const;
    /** @brief Find series attached to an exact live feature identity.
     * @param[in] feature Runtime GIS/model selection identity.
     * @param[in] targetLayer Optional owning model layer for non-GIS identities.
     * @return Matching series; unresolved links are never returned. */
    QStringList seriesForFeature(const SWMMObjectRef &feature,
                                 const OpenSWMMVisLayer *targetLayer = nullptr) const;
    /** @param[in] fids Owned station table IDs.
     * @return Corresponding series, each once in source order. */
    QStringList seriesForLocations(const QSet<long long> &fids) const;
    /** @brief Whether exact series have an owned station or a currently resolved feature link.
     * @param[in] ids Stable source-series identities; missing IDs are ignored.
     * @return True for at least one finite location in the attached canvas CRS. Owned stations
     * take precedence over linked model features; unlocated/unresolved series return false. */
    bool canZoomToLocations(const QStringList &ids) const;
    /** @brief Frame the requested source stations, falling back to links for unlocated series.
     * @param[in] ids Stable series identities, independent of the current map selection.
     * @return True after accepted canvas navigation; false for detached/nonspatial/unresolved
     * series. Uses CRS-aware point padding and never changes series selection or geometry. */
    bool zoomToLocations(const QStringList &ids);
    /** @brief Show the modeless native series/observation browser.
     * @param[in] parent Optional window parent.
     * @param[in] initialSeries Optional row selected on opening. */
    void showData(QWidget *parent = nullptr, const QString &initialSeries = {});
    /** @brief Request plotting, choosing series when more than one is available.
     * @param[in] ids Candidates, empty means all series.
     * @param[in] parent Optional chooser parent. */
    void requestPlot(const QStringList &ids = {}, QWidget *parent = nullptr);
    /** @brief Present a persistent-source locator; validates the expected dataset UUID.
     * @param[in] parent Optional dialog parent. */
    void locateSource(QWidget *parent = nullptr);
    /** @brief Build a qualified external-series selection token.
     * @param[in] seriesId Stable series UUID.
     * @return ObservedSeries selection ref, independent of model series names. */
    SWMMObjectRef seriesRef(const QString &seriesId) const;
    /** @brief Interpret a selection token belonging to this layer.
     * @param[in] ref Candidate token.
     * @return Existing series ID, empty for foreign/missing identity. */
    QString seriesIdForRef(const SWMMObjectRef &ref) const;

signals:
    /** @brief Dataset became available/unavailable; old metadata pointers remain immutable snapshots. */
    void datasetChanged();
    /** @brief Shared selected series changed; subscribers must not mutate source samples.
     * @param[in] ids Stable IDs in source order. */
    void seriesSelectionChanged(const QStringList &ids);
    /** @brief Project relationships changed; persist sidecar metadata and mark project dirty. */
    void associationsChanged();
    /** @brief Referenced-layer availability changed; no authored relationship changed. */
    void associationResolutionChanged();
    /** @brief Native controller should plot these series from this layer.
     * @param[in] ids Stable dataset series IDs. */
    void plotRequested(const QStringList &ids);
    /** @brief Native controller should prepare an explicit model-owned copy.
     * @param[in] id One stable series ID. */
    void useInModelRequested(const QString &id);

private:
    /** @brief Resolve source locations to the current canvas without changing selection.
     * @param[in] ids Requested stable series identities.
     * @return Finite ordered bounds, possibly a point/line; nonfinite bounds mean no location. */
    MapExtent locationsExtent(const QStringList &ids) const;
    /** @brief Synchronize runtime selection without recursive bus notifications. */
    void synchronizeSelection();
    /** @brief Apply links for undo/redo; source samples remain unchanged.
     * @param[in] links Replacement project relationship value. */
    void applyAssociations(const QJsonArray &links);
    /** @brief Resolve one relationship only against its original source identity.
     * @param[in] link Validated sidecar value.
     * @param[out] target Optional resolved feature reference.
     * @return True for a current unambiguous live target. */
    bool resolveAssociation(const QJsonObject &link, SWMMObjectRef *target = nullptr) const;
    QString m_sourcePath; ///< Absolute native package path, retained when the file is unavailable.
    QString m_expectedId; ///< Dataset UUID required when reopening or locating a moved package.
    QString m_sourceError; ///< Last open diagnostic displayed by the unavailable-layer placeholder.
    std::shared_ptr<const OpenSWMM::TimeSeries::TimeSeriesDataset> m_data; ///< Shared immutable metadata; null while unavailable.
    QHash<QString, qsizetype>
        m_seriesIndex; ///< Constant-time stable identity lookup into immutable metadata.
    QSet<QString> m_selected; ///< Selected source series UUIDs, independent of map feature ordinals.
    QSet<SWMMObjectRef>
        m_linkHighlights; ///< Only highlights introduced by this layer, not user-owned selections.
    QHash<long long, QString> m_locationFids; ///< Runtime station FIDs mapped to persistent location UUIDs.
    QJsonArray m_associations; ///< Authored project-sidecar feature links; never owns source observations.
    QPointer<MapCanvas> m_canvas; ///< Borrowed canvas whose undo stack and current layers resolve associations.
    QPointer<SelectionManager> m_selection; ///< Borrowed project selection bus; null for unattached collections.
    QPointer<QWidget> m_browser; ///< Optional modeless browser; guarded because it deletes itself on close.
    QList<QMetaObject::Connection> m_connections; ///< Canvas/model/selection connections disconnected when the layer is detached.
    bool m_syncing = false; ///< Prevents recursive marker, browser, and project selection notifications.
};

/** @brief One native plot hit; guarded layer plus copied series identities. */
struct TimeSeriesMapHit {
    QPointer<TimeSeriesLayer> layer; ///< Borrowed eligible collection; becomes null if the layer is removed and destroyed.
    QStringList seriesIds; ///< Copied stable series identities eligible at the picked map position.
};

/** @brief Find visible owned station or linked-feature series at a canvas location.
 * @param[in] canvas Borrowed current canvas.
 * @param[in] x Canvas-map X.
 * @param[in] y Canvas-map Y.
 * @param[in] tolerance Canvas-map pick radius.
 * @return Eligible collections in topmost order; no selection or plot mutation. */
QList<TimeSeriesMapHit> timeSeriesHitsAt(MapCanvas *canvas, double x, double y, double tolerance);
