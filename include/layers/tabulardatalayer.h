/*!
 * \file   tabulardatalayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice Z.4 — Non-spatial tabular layer for CSV / TSV (and later
 * XLSX) imports.  Participates in the Layer Tree (so visibility /
 * order / properties round-trip through the standard pipe) but
 * has no scene representation — `render()` is a no-op.
 *
 * Consumers (today: the future Attribute Table integration; later:
 * join-to-spatial-layer analyses) read the rows as a list of
 * `QVariantMap`s keyed by column header.
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
    explicit TabularDataLayer(const QString &name = "Untitled Table",
                                OpenSWMMVisWorkspace *parent = nullptr);
    ~TabularDataLayer() override;

    /*! Source file path the data was loaded from (empty if rows
     *  were set programmatically). */
    [[nodiscard]] QString sourcePath() const { return m_sourcePath; }

    /*! Column names in insertion order (== first-row headers). */
    [[nodiscard]] QStringList columnHeaders() const { return m_headers; }

    /*! Snapshot of all rows.  Each row is a {header → value} map. */
    [[nodiscard]] QVector<QVariantMap> rows() const { return m_rows; }

    /*! Total row count. */
    [[nodiscard]] int rowCount() const { return m_rows.size(); }

    /*! Read access to one row by index.  Returns an empty map if
     *  out of range. */
    [[nodiscard]] QVariantMap row(int idx) const;

    // ----- Loaders --------------------------------------------------------

    /*! Load CSV — RFC-4180-ish.  First row is treated as headers.
     *  Comma-separated; double-quote escaping supported.  TSV is
     *  the same path with a tab delimiter.  Returns true on success;
     *  on failure sets `*errorOut` if non-null and leaves the
     *  layer's previous contents intact. */
    bool loadDelimited(const QString &path, QChar delimiter,
                       QString *errorOut = nullptr);

    /*! Convenience: pick the delimiter from the file extension
     *  (`.csv` → `,`; `.tsv` → `\t`).  XLSX falls through to a
     *  not-yet-implemented stub (queued for QXlsx integration). */
    bool loadFromFile(const QString &path, QString *errorOut = nullptr);

    // ----- OpenSWMMVisLayer overrides -------------------------------------

    /*! Non-spatial layer — paint is a no-op. */
    void render(QPainter *painter,
                const MapExtent &extent,
                const QSize &imageSize,
                const SpatialReferenceSystem *srs) override
    {
        Q_UNUSED(painter); Q_UNUSED(extent);
        Q_UNUSED(imageSize); Q_UNUSED(srs);
    }

    /*! Non-spatial — no scene items.  The pure-virtual base needs
     *  an implementation; we just don't add anything to the
     *  scene. */
    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override
    {
        Q_UNUSED(scene); Q_UNUSED(canvasExtent); Q_UNUSED(canvasSRS);
    }

signals:
    /*! Emitted after a successful `loadFromFile` / `loadDelimited`.
     *  Consumers (Attribute Table panel) refresh their views. */
    void dataLoaded();

private:
    QString             m_sourcePath;
    QStringList         m_headers;
    QVector<QVariantMap> m_rows;
};

#endif // TABULARDATALAYER_H
