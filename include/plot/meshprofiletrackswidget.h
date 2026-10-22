/**
 * @file      meshprofiletrackswidget.h
 * @brief     Mesh Profile Tracks Widget interface and supporting types.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MESH_PROFILE_TRACKS_WIDGET_H
#define MESH_PROFILE_TRACKS_WIDGET_H
#include "plot/profilesection.h"
#include "plot/profilesectionseries.h"
#include <QAbstractTableModel>
#include <QWidget>

// Long-form scientific samples: independent source intervals are never forced
// onto another mesh's stations. Qt's native table accessibility exposes values.
class MeshProfileSamplesModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty read-only long-form sample table.
     * @param[in] parent Optional QObject owner; all later section/definition data is copied.
     */
    explicit MeshProfileSamplesModel(QObject *parent = nullptr) : QAbstractTableModel(parent) {}
    /**
     * @brief Reset the table with terrain/current-WSE plus all additional sampled rows.
     * @param[in] section Section snapshot copied without validation; additional hidden series are included in the table too.
     * @param[in] definition Copied units/identity metadata for builtInSeries() and headers.
     * @note Emits standard model reset notifications. One table row is produced per series point; independent source station arrays are never resampled onto another mesh.
     */
    void setSection(const ProfileSection::Section &section,
                    const ProfileSection::Definition &definition);
    /**
     * @param[in] parent Invalid for root rows; any valid parent yields zero.
     * @return Total flattened point rows across built-in and additional series, or zero for child requests.
     */
    int rowCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] parent Invalid for root columns; any valid parent yields zero.
     * @return Ten scientific metadata/value columns at root, zero for children.
     */
    int columnCount(const QModelIndex &parent = {}) const override;
    /**
     * @param[in] index Valid row/column in this model; foreign-model indices are not explicitly rejected.
     * @param[in] role Display/accessibility text returns formatted metadata, EditRole returns native doubles/times for supported columns, UserRole returns raw chainage for any column, and tooltip/accessible-description returns the series summary.
     * @return Owning QVariant, invalid for invalid/out-of-range rows/unsupported roles or columns. Values display only when status is Valid and value finite; otherwise em dash/invalid EditRole, while status retains its own label. Map Y is negative scene Y.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @param[in] section Zero-based row/column.
     * @param[in] orientation Vertical returns section+1; horizontal selects one of the ten labels, including declared horizontal units for distance.
     * @param[in] role Only DisplayRole/AccessibleTextRole are supported.
     * @return Label/row number or invalid QVariant for unsupported roles/out-of-range horizontal sections.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    ProfileSection::Section m_section;
    ProfileSection::Definition m_definition;
    QVector<QPair<int, int>> m_rows;
};

class MeshProfileTracksWidget : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create an empty focusable scalar-track pane with an accessible shared station cursor.
     * @param[in] parent Optional QWidget owner; no layers/sources are owned or fetched. Initial horizontal range is [0,1], cursor -1 and minimum height 150 pixels.
     */
    explicit MeshProfileTracksWidget(QWidget *parent = nullptr);
    /**
     * @param[in] section Complete sampled snapshot copied without validation. Only visible Scalar-role series draw tracks, including error rows.
     * @note Uses first/last primary sample chainages for the horizontal range when present, widening a degenerate range by one. Empty samples preserve prior bounds. Refreshes accessibility/minimum height and repaints without emitting cursorChainageChanged(); current cursor is retained.
     */
    void setSection(const ProfileSection::Section &section);
    /**
     * @param[in] range Horizontal left/right values in section chainage units; Y extents are ignored.
     * @note Rejects nonfinite endpoints or nonpositive width; otherwise replaces X range and repaints without signaling or data-extent clamping.
     */
    void setViewRange(const QRectF &range); // horizontal range shared with elevation plot
    /**
     * @param[in] chainage Finite coordinate in section horizontal units, not snapped/clamped to a station or visible range. Nonfinite is ignored.
     * @note Stores/repaints and sends an accessibility value change even for equality; does not emit cursorChainageChanged(), allowing the host to synchronize without an echo. Negative values suppress station text but may still be stored.
     */
    void setCursorChainage(double chainage);
    /**
     * @brief Count visible scalar result definitions rather than successful sample arrays.
     * @return Number of visible Scalar-role series; error/empty rows still count and reserve track space.
     */
    int trackCount() const;
signals:
    /**
     * @brief Notify local mouse/keyboard movement of the shared cursor.
     * @param[in] chainage New stored coordinate; mouse positions clamp to visible X range while keyboard uses source stations, potentially outside it. Programmatic setters do not emit.
     */
    void cursorChainageChanged(double chainage);

protected:
    /**
     * @brief Paint one independently scaled track per visible scalar series and its shared station cursor.
     * @note Qt's borrowed paint-event pointer is unused. Draws only finite Valid points, breaking at missing/status gaps and breakBefore. Custom ranges override finite data extrema; constant ranges expand. Unavailable/error rows show status text instead of curves. No fetching/resampling occurs.
     */
    void paintEvent(QPaintEvent *) override;
    /**
     * @brief Move the shared cursor to a left-click's horizontal fraction and announce it.
     * @param[in,out] event Borrowed Qt mouse event. Left-button X is clamped to the plot width independent of Y/track availability, then accepted; other buttons delegate to QWidget.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @brief Navigate sorted unique station chainages with Left/Right/Home/End.
     * @param[in,out] event Borrowed Qt key event. Navigation uses points from all additional series, including hidden/non-scalar/error rows; no built-in stations are added automatically. Empty/other-key cases delegate to QWidget.
     * @note Handled keys update accessibility and emit cursorChainageChanged() even at a retained endpoint, then accept. Input station chainages are expected finite.
     */
    void keyPressEvent(QKeyEvent *event) override;

private:
    ProfileSection::Section m_section;
    double m_xMin = 0, m_xMax = 1, m_cursor = -1;
    /**
     * @brief Replace the accessible description with cursor/navigation text and visible scalar-series provenance.
     * @note Does not emit the cursor signal or sample any source. Called after section/cursor changes.
     */
    void refreshAccessibleDescription();
};
#endif
