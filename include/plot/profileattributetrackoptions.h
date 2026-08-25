/*!
 * \file   profileattributetrackoptions.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Q_OBJECT facade for the attribute-tracks pane below the profile
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
    Q_PROPERTY(int    trackHeightPx    READ trackHeightPx    WRITE setTrackHeightPx    NOTIFY changed)
    Q_PROPERTY(bool   showTrackTitles  READ showTrackTitles  WRITE setShowTrackTitles  NOTIFY changed)
    Q_PROPERTY(bool   envelopesVisible READ envelopesVisible WRITE setEnvelopesVisible NOTIFY changed)
    Q_PROPERTY(double envelopeOpacity  READ envelopeOpacity  WRITE setEnvelopeOpacity  NOTIFY changed)

    // ── Per-attribute visibility ────────────────────────────────────────
    Q_PROPERTY(bool nodeDepthVisible         READ nodeDepthVisible         WRITE setNodeDepthVisible         NOTIFY changed)
    Q_PROPERTY(bool nodeHeadVisible          READ nodeHeadVisible          WRITE setNodeHeadVisible          NOTIFY changed)
    Q_PROPERTY(bool nodeVolumeVisible        READ nodeVolumeVisible        WRITE setNodeVolumeVisible        NOTIFY changed)
    Q_PROPERTY(bool nodeLateralInflowVisible READ nodeLateralInflowVisible WRITE setNodeLateralInflowVisible NOTIFY changed)
    Q_PROPERTY(bool nodeTotalInflowVisible   READ nodeTotalInflowVisible   WRITE setNodeTotalInflowVisible   NOTIFY changed)
    Q_PROPERTY(bool nodeOverflowVisible      READ nodeOverflowVisible      WRITE setNodeOverflowVisible      NOTIFY changed)
    Q_PROPERTY(bool linkFlowVisible          READ linkFlowVisible          WRITE setLinkFlowVisible          NOTIFY changed)
    Q_PROPERTY(bool linkDepthVisible         READ linkDepthVisible         WRITE setLinkDepthVisible         NOTIFY changed)
    Q_PROPERTY(bool linkVelocityVisible      READ linkVelocityVisible      WRITE setLinkVelocityVisible      NOTIFY changed)
    Q_PROPERTY(bool linkVolumeVisible        READ linkVolumeVisible        WRITE setLinkVolumeVisible        NOTIFY changed)
    Q_PROPERTY(bool linkCapacityVisible      READ linkCapacityVisible      WRITE setLinkCapacityVisible      NOTIFY changed)

    // ── Per-attribute pens (line color / width / style per track) ───────
    Q_PROPERTY(QPen nodeDepthPen         READ nodeDepthPen         WRITE setNodeDepthPen         NOTIFY changed)
    Q_PROPERTY(QPen nodeHeadPen          READ nodeHeadPen          WRITE setNodeHeadPen          NOTIFY changed)
    Q_PROPERTY(QPen nodeVolumePen        READ nodeVolumePen        WRITE setNodeVolumePen        NOTIFY changed)
    Q_PROPERTY(QPen nodeLateralInflowPen READ nodeLateralInflowPen WRITE setNodeLateralInflowPen NOTIFY changed)
    Q_PROPERTY(QPen nodeTotalInflowPen   READ nodeTotalInflowPen   WRITE setNodeTotalInflowPen   NOTIFY changed)
    Q_PROPERTY(QPen nodeOverflowPen      READ nodeOverflowPen      WRITE setNodeOverflowPen      NOTIFY changed)
    Q_PROPERTY(QPen linkFlowPen          READ linkFlowPen          WRITE setLinkFlowPen          NOTIFY changed)
    Q_PROPERTY(QPen linkDepthPen         READ linkDepthPen         WRITE setLinkDepthPen         NOTIFY changed)
    Q_PROPERTY(QPen linkVelocityPen      READ linkVelocityPen      WRITE setLinkVelocityPen      NOTIFY changed)
    Q_PROPERTY(QPen linkVolumePen        READ linkVolumePen        WRITE setLinkVolumePen        NOTIFY changed)
    Q_PROPERTY(QPen linkCapacityPen      READ linkCapacityPen      WRITE setLinkCapacityPen      NOTIFY changed)

public:
    explicit ProfileAttributeTrackOptions(QObject *parent = nullptr);

    // ── Generic accessors (the API the widget / menus actually use) ────
    [[nodiscard]] bool isAttributeVisible(openswmmvis::plot::PlotAttribute a) const;
    void setAttributeVisible(openswmmvis::plot::PlotAttribute a, bool on);
    [[nodiscard]] QPen penFor(openswmmvis::plot::PlotAttribute a) const;
    void setPenFor(openswmmvis::plot::PlotAttribute a, const QPen &pen);

    /*! Visible attributes in canonical order (node attrs then link attrs,
     *  each in plotattribute.h presentation order). */
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

    [[nodiscard]] bool isSpeciesTrackVisible(const QString &species,
                                             bool nodeScope) const;
    void setSpeciesTrackVisible(const QString &species, bool nodeScope,
                                bool on);
    [[nodiscard]] QPen speciesTrackPenFor(const QString &species,
                                          bool nodeScope) const;
    void setSpeciesTrackPenFor(const QString &species, bool nodeScope,
                               const QPen &pen);

    /*! Visible species tracks as (name, nodeScope) pairs, sorted by name
     *  then node-before-link — a deterministic display order regardless
     *  of toggle history. */
    [[nodiscard]] QVector<QPair<QString, bool>> visibleSpeciesTracks() const;

    /*! True when at least one attribute or species track is visible —
     *  drives whether the tracks pane is shown at all. */
    [[nodiscard]] bool anyAttributeVisible() const;

    // ── Chrome ──────────────────────────────────────────────────────────
    [[nodiscard]] int    trackHeightPx()    const { return m_trackHeightPx; }
    [[nodiscard]] bool   showTrackTitles()  const { return m_showTrackTitles; }
    [[nodiscard]] bool   envelopesVisible() const { return m_envelopesVisible; }
    [[nodiscard]] double envelopeOpacity()  const { return m_envelopeOpacity; }
    void setTrackHeightPx(int px);
    void setShowTrackTitles(bool on);
    void setEnvelopesVisible(bool on);
    void setEnvelopeOpacity(double opacity01);

    // ── Persistence (mirrors SWMMResultsLayer::write/readProfileStyle) ──
    /*! Writes every property under the CURRENT group of \p s. */
    void writeTo(QSettings &s) const;
    /*! Reads every stored property from the CURRENT group of \p s; missing
     *  keys keep their defaults. Emits changed() once at the end. */
    void readFrom(QSettings &s);

    /*! Friendly row label for the QPropertyModel tree, mirroring
     *  ProfilePlotOptions::displayLabelFor. */
    Q_INVOKABLE QString displayLabelFor(const QString &propertyName) const;

    // Named getters/setters backing the Q_PROPERTYs — all forward to the
    // generic per-attribute maps. Written out longhand rather than macro-
    // generated: moc's handling of user macros around member declarations
    // is fragile, and 44 one-liners in the .cpp beat a moc mystery.

    [[nodiscard]] bool nodeDepthVisible() const;
    void setNodeDepthVisible(bool on);
    [[nodiscard]] bool nodeHeadVisible() const;
    void setNodeHeadVisible(bool on);
    [[nodiscard]] bool nodeVolumeVisible() const;
    void setNodeVolumeVisible(bool on);
    [[nodiscard]] bool nodeLateralInflowVisible() const;
    void setNodeLateralInflowVisible(bool on);
    [[nodiscard]] bool nodeTotalInflowVisible() const;
    void setNodeTotalInflowVisible(bool on);
    [[nodiscard]] bool nodeOverflowVisible() const;
    void setNodeOverflowVisible(bool on);
    [[nodiscard]] bool linkFlowVisible() const;
    void setLinkFlowVisible(bool on);
    [[nodiscard]] bool linkDepthVisible() const;
    void setLinkDepthVisible(bool on);
    [[nodiscard]] bool linkVelocityVisible() const;
    void setLinkVelocityVisible(bool on);
    [[nodiscard]] bool linkVolumeVisible() const;
    void setLinkVolumeVisible(bool on);
    [[nodiscard]] bool linkCapacityVisible() const;
    void setLinkCapacityVisible(bool on);

    [[nodiscard]] QPen nodeDepthPen() const;
    void setNodeDepthPen(const QPen &p);
    [[nodiscard]] QPen nodeHeadPen() const;
    void setNodeHeadPen(const QPen &p);
    [[nodiscard]] QPen nodeVolumePen() const;
    void setNodeVolumePen(const QPen &p);
    [[nodiscard]] QPen nodeLateralInflowPen() const;
    void setNodeLateralInflowPen(const QPen &p);
    [[nodiscard]] QPen nodeTotalInflowPen() const;
    void setNodeTotalInflowPen(const QPen &p);
    [[nodiscard]] QPen nodeOverflowPen() const;
    void setNodeOverflowPen(const QPen &p);
    [[nodiscard]] QPen linkFlowPen() const;
    void setLinkFlowPen(const QPen &p);
    [[nodiscard]] QPen linkDepthPen() const;
    void setLinkDepthPen(const QPen &p);
    [[nodiscard]] QPen linkVelocityPen() const;
    void setLinkVelocityPen(const QPen &p);
    [[nodiscard]] QPen linkVolumePen() const;
    void setLinkVolumePen(const QPen &p);
    [[nodiscard]] QPen linkCapacityPen() const;
    void setLinkCapacityPen(const QPen &p);

signals:
    /*! One signal for every property — same contract as ProfilePlotOptions. */
    void changed();

private:
    /*! Scope-qualified species key: `<name>@node` / `<name>@link`. The
     *  name is the identity (D-G1); '@' never appears in SWMM ids and '/'
     *  is avoided because QSettings treats it as a group separator. */
    [[nodiscard]] static QString speciesKey(const QString &species,
                                            bool nodeScope);

    QHash<int, bool> m_visible;   ///< key = int(PlotAttribute)
    QHash<int, QPen> m_pens;      ///< key = int(PlotAttribute)

    QHash<QString, bool> m_speciesVisible;   ///< key = speciesKey()
    QHash<QString, QPen> m_speciesPens;      ///< key = speciesKey()

    int    m_trackHeightPx    = 110;
    bool   m_showTrackTitles  = true;
    bool   m_envelopesVisible = true;
    double m_envelopeOpacity  = 0.25;   ///< 0..1 alpha applied to the pen color
};

#endif // OPENSWMMVIS_PLOT_PROFILEATTRIBUTETRACKOPTIONS_H
