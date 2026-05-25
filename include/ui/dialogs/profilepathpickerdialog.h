/*!
 * \file   profilepathpickerdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BC's path-picker — modal dialog shown by
 *         OpenSWMMVisMapToolSelectProfile when ProfileRouter returns more
 *         than one candidate between the user-picked endpoints.
 *
 *         Lists each candidate with length / conduit count / non-conduit
 *         count / total invert drop.  Hovering or selecting a row emits
 *         `hoveredPathChanged(index)` so the host can promote that path on
 *         the map overlay; clicking OK confirms.
 */

#ifndef PROFILEPATHPICKERDIALOG_H
#define PROFILEPATHPICKERDIALOG_H

#include "plot/profilerouter.h"

#include <QDialog>
#include <QVector>

class QTableWidget;
class QDialogButtonBox;
class SWMMModelLayer;

class ProfilePathPickerDialog : public QDialog
{
    Q_OBJECT

public:
    ProfilePathPickerDialog(SWMMModelLayer *model,
                            const QVector<ProfileRouter::Path> &paths,
                            bool truncated = false,
                            QWidget *parent = nullptr);

    /*!
     * \brief Index (0-based) of the path the user accepted, or -1 when
     *        the dialog was cancelled.
     */
    [[nodiscard]] int selectedPathIndex() const;

signals:
    /*!
     * \brief Emitted whenever the focused/selected row changes — the host
     *        listens to update the map overlay's highlighted path.
     *        Index `-1` means no row is focused.
     */
    void hoveredPathChanged(int index);

    /*!
     * \brief Emitted on row double-click — the host listens to zoom the
     *        main map to that candidate path's extent.
     */
    void zoomToPathRequested(int index);

private slots:
    void onCurrentRowChanged(int currentRow, int previousRow);

private:
    void populateTable();
    void summarizePath(int pathIdx,
                       double &lengthOut,
                       int &conduitsOut,
                       int &nonConduitsOut,
                       double &dropOut) const;

    SWMMModelLayer                       *m_model = nullptr;
    QVector<ProfileRouter::Path>          m_paths;
    QTableWidget                         *m_table = nullptr;
    QDialogButtonBox                     *m_buttons = nullptr;
    int                                   m_selectedIdx = -1;
};

#endif // PROFILEPATHPICKERDIALOG_H
