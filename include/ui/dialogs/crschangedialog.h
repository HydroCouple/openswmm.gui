/*!
 * \file   crschangedialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Phase 0.7 — modal that asks the user how to apply a CRS change to a model
 * with already-loaded geometry: reproject the stored coordinates, just
 * re-render in the new CRS, or cancel.
 */
#ifndef CRSCHANGEDIALOG_H
#define CRSCHANGEDIALOG_H

#include <QDialog>
#include <QString>

class QRadioButton;
class QLabel;
class QDialogButtonBox;

/*!
 * \class CRSChangeDialog
 * \brief Three-choice CRS change confirmation.
 *
 * Displayed by SWMMVis::onCRSButtonClicked when the active project has a
 * non-empty network and the user selects a different CRS.
 */
class CRSChangeDialog : public QDialog
{
    Q_OBJECT

public:
    enum Choice {
        Cancel     = 0,
        Reproject  = 1,  ///< Permanently rewrite stored coordinates via OGR transform.
        RenderOnly = 2,  ///< Keep stored coordinates; only the canvas display CRS changes.
    };

    /*!
     * \param oldAuth   Authority of the model's current CRS (e.g. "EPSG:6595").
     * \param newAuth   Authority of the newly-selected CRS.
     * \param sourceIsLocal If true, recommend Reproject — re-render needs a real
     *                      source CRS to build a transform.
     */
    explicit CRSChangeDialog(const QString &oldAuth,
                             const QString &newAuth,
                             bool sourceIsLocal = false,
                             QWidget *parent = nullptr);

    Choice choice() const { return m_choice; }

private slots:
    void onAccept();

private:
    Choice            m_choice          = Cancel;
    QRadioButton     *m_radioReproject  = nullptr;
    QRadioButton     *m_radioRenderOnly = nullptr;
    QDialogButtonBox *m_buttonBox       = nullptr;
};

#endif // CRSCHANGEDIALOG_H
