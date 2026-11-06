/**
 * @file      profileattributetrackoptions.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Q_OBJECT facade for the attribute-tracks pane below the profile
 *         plot: which attributes are shown, how each is styled, and the
 *         pane-level chrome knobs.
 *
 * Same pattern as ProfilePlotOptions: every knob is a Q_PROPERTY with the
 * single `changed()` NOTIFY signal, so the toolbar attribute menu, the
 * QPropertyModel-backed Display Options tree, and the tracks widget itself
 * all edit/observe one instance and stay synchronized (CLAUDE.md §5.1) —
 * no view writes another view's state directly.
 *
 * Per-attribute state is a visibility bool + a pen for each of the 11
 * trackable attributes (6 node + 5 link). The generic accessors
 * (`isAttributeVisible` / `penFor` / …) are what programmatic consumers
 * use; the Q_PROPERTYs exist so QPropertyModel can edit the same state.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKOPTIONS_H
#define OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKOPTIONS_H

#include "plot/plotattribute.h"

#include <QHash>
#include <QObject>
#include <QPen>
#include <QString>
#include <QVector>

class QSettings;

class ProfileAttributeTrackOptions : public QObject
{
    Q_OBJECT

    // ── Pane chrome ─────────────────────────────────────────────────────
    Q_PROPERTY(int trackHeightPx READ trackHeightPx WRITE setTrackHeightPx NOTIFY changed)
    Q_PROPERTY(bool showTrackTitles READ showTrackTitles WRITE setShowTrackTitles NOTIFY changed)
    Q_PROPERTY(bool envelopesVisible READ envelopesVisible WRITE setEnvelopesVisible NOTIFY changed)
    Q_PROPERTY(double envelopeOpacity READ envelopeOpacity WRITE setEnvelopeOpacity NOTIFY changed)

    // ── Per-attribute visibility ────────────────────────────────────────
    Q_PROPERTY(bool nodeDepthVisible READ nodeDepthVisible WRITE setNodeDepthVisible NOTIFY changed)
    Q_PROPERTY(bool nodeHeadVisible READ nodeHeadVisible WRITE setNodeHeadVisible NOTIFY changed)
    Q_PROPERTY(
        bool nodeVolumeVisible READ nodeVolumeVisible WRITE setNodeVolumeVisible NOTIFY changed)
    Q_PROPERTY(bool nodeLateralInflowVisible READ nodeLateralInflowVisible WRITE
                   setNodeLateralInflowVisible NOTIFY changed)
    Q_PROPERTY(bool nodeTotalInflowVisible READ nodeTotalInflowVisible WRITE
                   setNodeTotalInflowVisible NOTIFY changed)
    Q_PROPERTY(bool nodeOverflowVisible READ nodeOverflowVisible WRITE setNodeOverflowVisible NOTIFY
                   changed)
    Q_PROPERTY(bool linkFlowVisible READ linkFlowVisible WRITE setLinkFlowVisible NOTIFY changed)
    Q_PROPERTY(bool linkDepthVisible READ linkDepthVisible WRITE setLinkDepthVisible NOTIFY changed)
    Q_PROPERTY(bool linkVelocityVisible READ linkVelocityVisible WRITE setLinkVelocityVisible NOTIFY
                   changed)
    Q_PROPERTY(
        bool linkVolumeVisible READ linkVolumeVisible WRITE setLinkVolumeVisible NOTIFY changed)
    Q_PROPERTY(bool linkCapacityVisible READ linkCapacityVisible WRITE setLinkCapacityVisible NOTIFY
                   changed)

    // ── Per-attribute pens (line color / width / style per track) ───────
    Q_PROPERTY(QPen nodeDepthPen READ nodeDepthPen WRITE setNodeDepthPen NOTIFY changed)
    Q_PROPERTY(QPen nodeHeadPen READ nodeHeadPen WRITE setNodeHeadPen NOTIFY changed)
    Q_PROPERTY(QPen nodeVolumePen READ nodeVolumePen WRITE setNodeVolumePen NOTIFY changed)
    Q_PROPERTY(QPen nodeLateralInflowPen READ nodeLateralInflowPen WRITE setNodeLateralInflowPen
                   NOTIFY changed)
    Q_PROPERTY(
        QPen nodeTotalInflowPen READ nodeTotalInflowPen WRITE setNodeTotalInflowPen NOTIFY changed)
    Q_PROPERTY(QPen nodeOverflowPen READ nodeOverflowPen WRITE setNodeOverflowPen NOTIFY changed)
    Q_PROPERTY(QPen linkFlowPen READ linkFlowPen WRITE setLinkFlowPen NOTIFY changed)
    Q_PROPERTY(QPen linkDepthPen READ linkDepthPen WRITE setLinkDepthPen NOTIFY changed)
    Q_PROPERTY(QPen linkVelocityPen READ linkVelocityPen WRITE setLinkVelocityPen NOTIFY changed)
    Q_PROPERTY(QPen linkVolumePen READ linkVolumePen WRITE setLinkVolumePen NOTIFY changed)
    Q_PROPERTY(QPen linkCapacityPen READ linkCapacityPen WRITE setLinkCapacityPen NOTIFY changed)

public:
    /**
     * @brief Create disabled node/link tracks with distinguishable per-attribute default pens.
     * @param[in] parent Optional QObject owner. Initial pens use 1.6 logical-pixel width; no changed() signal or settings IO occurs at construction.
     */
    explicit ProfileAttributeTrackOptions(QObject *parent = nullptr);

    // ── Generic accessors (the API the widget / menus actually use) ────
    /**
     * @param[in] a Raw fixed attribute key, not restricted to the canonical eleven by this accessor.
     * @return Stored visibility or false for an absent key; source availability is not checked.
     */
    [[nodiscard]] bool isAttributeVisible(openswmmvis::plot::PlotAttribute a) const;
    /**
     * @param[in] a Attribute key stored as its integer enum, without membership validation.
     * @param[in] on New visibility; equal effective visibility is a no-op.
     * @note Inserts a changed entry and emits changed(); no data fetch or settings persistence occurs. Unsupported keys can be stored but visibleAttributes() omits them.
     */
    void setAttributeVisible(openswmmvis::plot::PlotAttribute a, bool on);
    /**
     * @param[in] a Attribute key.
     * @return Value copy of stored pen or a 1.6-pixel seed-colour pen; unsupported attributes use a neutral grey seed. Querying does not insert an entry.
     */
    [[nodiscard]] QPen penFor(openswmmvis::plot::PlotAttribute a) const;
    /**
     * @param[in] a Raw attribute key; not membership-validated.
     * @param[in] pen Complete pen copied without colour/width/dash validation; logical-pixel width is used by the plot.
     * @note Compares against the stored map value (default QPen if absent), not the seeded penFor() fallback. A changed entry emits changed(); visibility is unaffected.
     */
    void setPenFor(openswmmvis::plot::PlotAttribute a, const QPen &pen);

    /*! Visible attributes in canonical order (node attrs then link attrs,
     *  each in plotattribute.h presentation order).
     * @return Owned list of visible canonical node-then-link attributes only; excludes species and any unsupported raw keys inserted through the generic setter.
     */
    [[nodiscard]] QVector<openswmmvis::plot::PlotAttribute> visibleAttributes() const;

    // ── Species tracks (Y2b-2 follow-up, amendment D-Y4) ────────────────
    // A species (pollutant or the reserved water-age column) is carried BY
    // NAME, never by enum — the run's live list resolves it at fetch time.
    // Each species can be tracked in node scope and link scope
    // independently (concentration at the path's nodes vs in its links),
    // so every accessor takes the scope alongside the name. State for a
    // species the current run doesn't carry is kept verbatim (the Y2b-3
    // save-then-reopen semantic): it simply isn't offered until a run
    // with that species is loaded again.

    /**
     * @brief Access name-keyed species style independently for node/link scope.
     * @param[in] species Exact case-sensitive name, not resolved to a numeric species index or validated against a source.
     * @param[in] nodeScope True selects node samples, false link samples.
     * @return Stored scoped visibility or false for an absent key. Does not check the currently loaded run.
     */
    [[nodiscard]] bool isSpeciesTrackVisible(const QString &species, bool nodeScope) const;
    /**
     * @brief Access name-keyed species style independently for node/link scope.
     * @param[in] species Exact case-sensitive name, not resolved to a numeric species index or validated against a source.
     * @param[in] nodeScope True selects node samples, false link samples.
     * @param[in] on Desired scoped visibility; empty species is ignored and equal effective visibility is a no-op.
     * @note Changed state emits changed(); unknown-to-current-run names are retained for later restoration.
     */
    void setSpeciesTrackVisible(const QString &species, bool nodeScope, bool on);
    /**
     * @brief Access name-keyed species style independently for node/link scope.
     * @param[in] species Exact case-sensitive name, not resolved to a numeric species index or validated against a source.
     * @param[in] nodeScope True selects node samples, false link samples.
     * @return Value copy of stored scoped pen, otherwise a deterministic name-checksum palette pen of width 1.6 logical pixels. Query does not create state.
     */
    [[nodiscard]] QPen speciesTrackPenFor(const QString &species, bool nodeScope) const;
    /**
     * @brief Access name-keyed species style independently for node/link scope.
     * @param[in] species Exact case-sensitive name, not resolved to a numeric species index or validated against a source.
     * @param[in] nodeScope True selects node samples, false link samples.
     * @param[in] pen Complete pen copied without validation; empty species is ignored.
     * @note Equal explicitly stored pen is a no-op; assigning a seed-equivalent fallback for the first time still stores/emits changed(). Visibility is unaffected.
     */
    void setSpeciesTrackPenFor(const QString &species, bool nodeScope, const QPen &pen);

    /*! Visible species tracks as (name, nodeScope) pairs, sorted by name
     *  then node-before-link — a deterministic display order regardless
     *  of toggle history.
     * @return Owned visible (exact name,nodeScope) pairs in case-sensitive name order with node before link. No loaded-run availability filtering occurs; keys are expected to use speciesKey()'s suffix convention.
     */
    [[nodiscard]] QVector<QPair<QString, bool>> visibleSpeciesTracks() const;

    /*! True when at least one attribute or species track is visible —
     *  drives whether the tracks pane is shown at all.
     * @return Whether either visibility map has any true entry, including a raw unsupported attribute key; may therefore be true while visibleAttributes() is empty.
     */
    [[nodiscard]] bool anyAttributeVisible() const;

    // ── Chrome ──────────────────────────────────────────────────────────
    /**
     * @brief Inspect track-pane chrome without reading result samples.
     * @return Minimum per-track height in logical pixels, initially 110; actual tracks can grow with the pane.
     */
    [[nodiscard]] int trackHeightPx() const { return m_trackHeightPx; }
    /**
     * @brief Inspect track-pane chrome without reading result samples.
     * @return Whether per-track quantity/scope titles should be painted.
     */
    [[nodiscard]] bool showTrackTitles() const { return m_showTrackTitles; }
    /**
     * @brief Inspect track-pane chrome without reading result samples.
     * @return Whether available min/max envelope bands should be painted.
     */
    [[nodiscard]] bool envelopesVisible() const { return m_envelopesVisible; }
    /**
     * @brief Inspect track-pane chrome without reading result samples.
     * @return Envelope alpha fraction, initially 0.25, used with each track pen colour.
     */
    [[nodiscard]] double envelopeOpacity() const { return m_envelopeOpacity; }
    /**
     * @brief Update per-pane track rendering policy.
     * @param[in] px Minimum logical-pixel height, clamped to [60,400].
     * @note Equality after normalization is a no-op; otherwise stores and emits changed() once. No result source/series data is mutated.
     */
    void setTrackHeightPx(int px);
    /**
     * @brief Update per-pane track rendering policy.
     * @param[in] on Whether track quantity titles are requested.
     * @note Equality after normalization is a no-op; otherwise stores and emits changed() once. No result source/series data is mutated.
     */
    void setShowTrackTitles(bool on);
    /**
     * @brief Update per-pane track rendering policy.
     * @param[in] on Whether min/max envelope bands are requested.
     * @note Equality after normalization is a no-op; otherwise stores and emits changed() once. No result source/series data is mutated.
     */
    void setEnvelopesVisible(bool on);
    /**
     * @brief Update per-pane track rendering policy.
     * @param[in] opacity01 Finite requested envelope alpha, clamped to [0,1]; NaN is not explicitly rejected.
     * @note Equality after normalization is a no-op; otherwise stores and emits changed() once. No result source/series data is mutated.
     */
    void setEnvelopeOpacity(double opacity01);

    // ── Persistence (mirrors SWMMResultsLayer::write/readProfileStyle) ──
    /*! Writes every property under the CURRENT group of \p s.
     * @param[in,out] s Settings whose current group receives every declared property and a replaced speciesTracks subgroup. Group nesting is restored on return.
     * @note Rewrites scoped species visibility/pens verbatim, including unavailable names, without clearing unrelated keys, calling sync() or reporting storage errors. Generic unsupported fixed keys are not declared properties and are not persisted.
     */
    void writeTo(QSettings &s) const;
    /*! Reads every stored property from the CURRENT group of \p s; missing
     *  keys keep their current values. Emits changed() once at the end.
     * @param[in,out] s Settings read beneath the current group; nested groups are restored and no settings values are written.
     * @note Missing property keys preserve current cache values, not necessarily construction defaults. Known properties use their setters via Qt metadata; write conversion errors are not surfaced. Species entries merge into existing maps without clearing missing keys, using QVariant conversions without schema validation. Signals are blocked during loading, then changed() is emitted once even when nothing differed (subject to an outer signal block).
     */
    void readFrom(QSettings &s);

    /*! Friendly row label for the QPropertyModel tree, mirroring
     *  ProfilePlotOptions::displayLabelFor.
     * @param[in] propertyName Exact property identifier.
     * @return Scope/quantity-friendly label, recognized pane label, or original key if unmapped. Does not inspect source availability.
     */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // Named getters/setters backing the Q_PROPERTYs — all forward to the
    // generic per-attribute maps. Written out longhand rather than macro-
    // generated: moc's handling of user macros around member declarations
    // is fragile, and 44 one-liners in the .cpp beat a moc mystery.

    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Depth above invert at path nodes visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool nodeDepthVisible() const;
    /**
     * @brief Toggle the canonical depth above invert at path nodes track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setNodeDepthVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Hydraulic head at path nodes visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool nodeHeadVisible() const;
    /**
     * @brief Toggle the canonical hydraulic head at path nodes track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setNodeHeadVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Stored volume at path nodes visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool nodeVolumeVisible() const;
    /**
     * @brief Toggle the canonical stored volume at path nodes track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setNodeVolumeVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Lateral inflow at path nodes visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool nodeLateralInflowVisible() const;
    /**
     * @brief Toggle the canonical lateral inflow at path nodes track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setNodeLateralInflowVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Total inflow at path nodes visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool nodeTotalInflowVisible() const;
    /**
     * @brief Toggle the canonical total inflow at path nodes track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setNodeTotalInflowVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Overflow at path nodes visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool nodeOverflowVisible() const;
    /**
     * @brief Toggle the canonical overflow at path nodes track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setNodeOverflowVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Flow at path links visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool linkFlowVisible() const;
    /**
     * @brief Toggle the canonical flow at path links track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setLinkFlowVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Water depth at path links visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool linkDepthVisible() const;
    /**
     * @brief Toggle the canonical water depth at path links track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setLinkDepthVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Velocity at path links visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool linkVelocityVisible() const;
    /**
     * @brief Toggle the canonical velocity at path links track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setLinkVelocityVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Stored volume at path links visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool linkVolumeVisible() const;
    /**
     * @brief Toggle the canonical stored volume at path links track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setLinkVolumeVisible(bool on);
    /**
     * @brief Inspect a canonical attribute's track visibility.
     * @return Dimensionless capacity ratio at path links visibility via isAttributeVisible(); this does not test whether samples exist.
     */
    [[nodiscard]] bool linkCapacityVisible() const;
    /**
     * @brief Toggle the canonical dimensionless capacity ratio at path links track.
     * @param[in] on Visibility passed to setAttributeVisible(); unchanged effective state is a no-op, otherwise changed() is emitted. No data fetch occurs here.
     */
    void setLinkCapacityVisible(bool on);

    /**
     * @brief Inspect rendering for depth above invert at path nodes.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen nodeDepthPen() const;
    /**
     * @brief Style the depth above invert at path nodes track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setNodeDepthPen(const QPen &p);
    /**
     * @brief Inspect rendering for hydraulic head at path nodes.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen nodeHeadPen() const;
    /**
     * @brief Style the hydraulic head at path nodes track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setNodeHeadPen(const QPen &p);
    /**
     * @brief Inspect rendering for stored volume at path nodes.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen nodeVolumePen() const;
    /**
     * @brief Style the stored volume at path nodes track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setNodeVolumePen(const QPen &p);
    /**
     * @brief Inspect rendering for lateral inflow at path nodes.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen nodeLateralInflowPen() const;
    /**
     * @brief Style the lateral inflow at path nodes track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setNodeLateralInflowPen(const QPen &p);
    /**
     * @brief Inspect rendering for total inflow at path nodes.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen nodeTotalInflowPen() const;
    /**
     * @brief Style the total inflow at path nodes track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setNodeTotalInflowPen(const QPen &p);
    /**
     * @brief Inspect rendering for overflow at path nodes.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen nodeOverflowPen() const;
    /**
     * @brief Style the overflow at path nodes track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setNodeOverflowPen(const QPen &p);
    /**
     * @brief Inspect rendering for flow at path links.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen linkFlowPen() const;
    /**
     * @brief Style the flow at path links track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setLinkFlowPen(const QPen &p);
    /**
     * @brief Inspect rendering for water depth at path links.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen linkDepthPen() const;
    /**
     * @brief Style the water depth at path links track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setLinkDepthPen(const QPen &p);
    /**
     * @brief Inspect rendering for velocity at path links.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen linkVelocityPen() const;
    /**
     * @brief Style the velocity at path links track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setLinkVelocityPen(const QPen &p);
    /**
     * @brief Inspect rendering for stored volume at path links.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen linkVolumePen() const;
    /**
     * @brief Style the stored volume at path links track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setLinkVolumePen(const QPen &p);
    /**
     * @brief Inspect rendering for dimensionless capacity ratio at path links.
     * @return Complete pen value via penFor(), including colour/alpha, logical-pixel width and dash/cap/join. Visibility and data units are independent.
     */
    [[nodiscard]] QPen linkCapacityPen() const;
    /**
     * @brief Style the dimensionless capacity ratio at path links track independently of visibility.
     * @param[in] p Complete pen passed to setPenFor(), without range/colour validation. Effective stored changes emit changed(); no persistence occurs.
     */
    void setLinkCapacityPen(const QPen &p);

signals:
    /*! One signal for every property — same contract as ProfilePlotOptions.
     * @note Emitted after effective setters or once after readFrom(); consumers decide whether to repaint, resample or resize.
     */
    void changed();

private:
    /*! Scope-qualified species key: `<name>@node` / `<name>@link`. The
     *  name is the identity (D-G1); '@' never appears in SWMM ids and '/'
     *  is avoided because QSettings treats it as a group separator.
     * @param[in] species Exact name copied without validation; caller should use valid SWMM identity tokens.
     * @param[in] nodeScope True appends the node-scope suffix, false the link-scope suffix.
     * @return Internal scope-qualified key. It is not a source species index; callers keep source-independent identity.
     */
    [[nodiscard]] static QString speciesKey(const QString &species, bool nodeScope);

    QHash<int, bool> m_visible; ///< key = int(PlotAttribute)
    QHash<int, QPen> m_pens;    ///< key = int(PlotAttribute)

    QHash<QString, bool> m_speciesVisible; ///< key = speciesKey()
    QHash<QString, QPen> m_speciesPens;    ///< key = speciesKey()

    int m_trackHeightPx = 110;
    bool m_showTrackTitles = true;
    bool m_envelopesVisible = true;
    double m_envelopeOpacity = 0.25; ///< 0..1 alpha applied to the pen color
};

#endif // OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKOPTIONS_H
