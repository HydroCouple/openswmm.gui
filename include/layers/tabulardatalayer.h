/**
 * @file      tabulardatalayer.h
 * @brief     Non-spatial tabular layer for CSV / TSV (and later XLSX) imports.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice Z.4 — Non-spatial tabular layer for CSV / TSV (and later
 * XLSX) imports.  Participates in the Layer Tree (so visibility /
 * order / properties round-trip through the standard pipe) but
 * has no scene representation — `render()` is a no-op.
 *
 * Consumers (today: the future Attribute Table integration; later:
 * join-to-spatial-layer analyses) read the rows as a list of
 * `QVariantMap`s keyed by column header.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef TABULARDATALAYER_H
#define TABULARDATALAYER_H

#include "layers/openswmmvislayer.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVector>

class TabularDataLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty nonspatial table participating in the layer tree.
     * @param[in] name Display name copied into the base layer.
     * @param[in] parent Optional workspace QObject owner. No file is opened or scene geometry allocated.
     */
    explicit TabularDataLayer(const QString &name = "Untitled Table",
                              OpenSWMMVisWorkspace *parent = nullptr);
    /**
     * @brief Release the in-memory row/header values through ordinary value/QObject ownership.
     */
    ~TabularDataLayer() override;

    /*! Source file path the data was loaded from (empty if rows
     *  were set programmatically).  Returned as an absolute path so the
     *  Properties window shows the full on-disk location.
     * @return Stored successful-load path interpreted as an absolute filesystem path, empty before loading; no canonicalization/existence check.
     */
    [[nodiscard]] QString sourcePath() const;

    /**
     * @return Absolute sourcePath() or an in-memory placeholder, by value without IO.
     */
    [[nodiscard]] QString sourceDescription() const override
    {
        const QString p = sourcePath();
        return p.isEmpty() ? tr("(in-memory)") : p;
    }

    /*! Column names in insertion order (== first-row headers).
     * @return Owned copy of the decoded first logical row, in original order; empty and duplicate column names are retained.
     */
    [[nodiscard]] QStringList columnHeaders() const { return m_headers; }

    /*! Snapshot of all rows.  Each row is a {header → value} map.
     * @return Owned row snapshots. Present tokens are strings or inferred integer/finite-double variants; missing trailing columns are absent keys. Duplicate header names use the last supplied token for that key.
     */
    [[nodiscard]] QVector<QVariantMap> rows() const { return m_rows; }

    /*! Total row count.
     * @return Current number of imported nonblank data rows, excluding the header.
     */
    [[nodiscard]] int rowCount() const { return m_rows.size(); }

    /*! Read access to one row by index.  Returns an empty map if
     *  out of range.
     * @param[in] idx Zero-based row ordinal.
     * @return Owned row copy, empty for negative/out-of-range indices; this accessor does not mutate table state.
     */
    [[nodiscard]] QVariantMap row(int idx) const;

    // ----- Loaders --------------------------------------------------------

    /*! Load CSV — RFC-4180-ish.  First row is treated as headers.
     *  Comma-separated; double-quote escaping supported.  TSV is
     *  the same path with a tab delimiter.  Returns true on success;
     *  on failure sets `*errorOut` if non-null and leaves the
     *  layer's previous contents intact.
     * @param[in] path Text file opened synchronously with QFile/QTextStream default text decoding.
     * @param[in] delimiter Field separator, normally comma or tab.
     * @param[out] errorOut Optional message assigned only on open/missing-header failure; success leaves old text intact.
     * @return True after complete parsed content replaces source path/headers/rows and dataLoaded() is emitted. False on open/missing-header failure with old data retained.
     * @note Parser is permissive: quoted fields can contain newlines, malformed/unclosed quotes are not rejected, lone carriage-return handling can consume the next character, and stream errors after a header have no separate failure result. Blank rows are skipped; extra tokens are discarded. Leading-zero identifiers and literal empty/NULL/dash text stay strings; otherwise integers, unsigned integers, then finite decimal/exponent values are inferred. GUI-thread mutation; no repaint or file watcher is created.
     */
    bool loadDelimited(const QString &path, QChar delimiter, QString *errorOut = nullptr);

    /*! Convenience: pick the delimiter from the file extension
     *  (`.csv` → `,`; `.tsv` → `\t`).  XLSX falls through to a
     *  not-yet-implemented stub (queued for QXlsx integration).
     * @param[in] path File whose case-insensitive extension selects tab for tsv/tab, comma for csv/unknown; xlsx returns false.
     * @param[out] errorOut Optional failure description; unchanged on success.
     * @return loadDelimited() result or false for unsupported xlsx, retaining prior data on that explicit failure. Parsing, replacement and dataLoaded() semantics follow loadDelimited().
     */
    bool loadFromFile(const QString &path, QString *errorOut = nullptr);

    // ----- OpenSWMMVisLayer overrides -------------------------------------

    /*! Non-spatial layer — paint is a no-op.
     * @param[in] painter Unused optional paint target.
     * @param[in] extent Unused map bounds.
     * @param[in] imageSize Unused output dimensions.
     * @param[in] srs Unused optional target CRS.
     * @note Does not paint or modify painter state; this is a nonspatial table.
     */
    void render(QPainter *painter, const MapExtent &extent, const QSize &imageSize,
                const SpatialReferenceSystem *srs) override
    {
        Q_UNUSED(painter);
        Q_UNUSED(extent);
        Q_UNUSED(imageSize);
        Q_UNUSED(srs);
    }

    /*! Non-spatial — no scene items.  The pure-virtual base needs
     *  an implementation; we just don't add anything to the
     *  scene.
     * @param[in] scene Unused optional scene; no items are added.
     * @param[in] canvasExtent Unused canvas bounds.
     * @param[in] canvasSRS Unused optional target CRS.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override
    {
        Q_UNUSED(scene);
        Q_UNUSED(canvasExtent);
        Q_UNUSED(canvasSRS);
    }

signals:
    /*! Emitted after a successful `loadFromFile` / `loadDelimited`.
     *  Consumers (Attribute Table panel) refresh their views.
     * @note Emitted once after successful table replacement, including a header-only file with zero rows; failures emit nothing. Observers can reread owned row/header snapshots.
     */
    void dataLoaded();

private:
    QString m_sourcePath;
    QStringList m_headers;
    QVector<QVariantMap> m_rows;
};

#endif // TABULARDATALAYER_H
