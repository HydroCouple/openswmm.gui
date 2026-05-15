/*!
 * \file   licenseagreementdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Startup license agreement dialog. Shown at launch unless the user has
 * previously accepted and unchecked "Show on startup". Declining exits the
 * application immediately.
 */
#ifndef LICENSEAGREEMENTDIALOG_H
#define LICENSEAGREEMENTDIALOG_H

#include <QDialog>

class QCheckBox;
class QDialogButtonBox;
class QPlainTextEdit;

/*!
 * \class LicenseAgreementDialog
 * \brief Modal dialog presenting the GPLv3 license agreement on first launch.
 *
 * The dialog stores whether it should appear again via QSettings under
 * "SWMMVis/LicenseAgreement/showOnStartup". Call \c shouldShowOnStartup()
 * before constructing the dialog to skip it when the user opted out.
 */
class LicenseAgreementDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LicenseAgreementDialog(QWidget *parent = nullptr);
    ~LicenseAgreementDialog() override;

    /*!
     * \brief Returns true if the dialog should be shown at startup.
     *
     * Reads QSettings. Defaults to true (i.e., show on first run).
     */
    [[nodiscard]] static bool shouldShowOnStartup();

    /*!
     * \brief Persists the "show on startup" preference.
     * \param show  true = show dialog on next launch, false = suppress it.
     */
    static void setShowOnStartup(bool show);

private slots:
    void onAccepted();
    void onRejected();

private:
    void buildUi();
    void savePreference(bool show);

    QPlainTextEdit  *m_licenseText  = nullptr;
    QCheckBox       *m_showCheckBox = nullptr;
    QDialogButtonBox *m_buttons     = nullptr;
};

#endif // LICENSEAGREEMENTDIALOG_H
