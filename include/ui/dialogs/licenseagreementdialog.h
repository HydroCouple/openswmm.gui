/**
 * @file      licenseagreementdialog.h
 * @brief     Startup license agreement dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Startup license agreement dialog. Shown at launch unless the user has
 * previously accepted and unchecked "Show on startup". Declining exits the
 * application immediately.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Build a modal GPL notice with Show on startup initially checked, independent of stored
     *   preference.
     * @note Does not call shouldShowOnStartup to decide whether to appear and does not start an
     *   event loop or exit the application. Caller handles rejected startup result.
     */
    explicit LicenseAgreementDialog(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls without persisting their current checkbox state.
     */
    ~LicenseAgreementDialog() override;

    /*!
     * \brief Returns true if the dialog should be shown at startup.
     *
     * Reads QSettings. Defaults to true (i.e., show on first run).
     * @return Current default-application QSettings SWMMVis/LicenseAgreement/showOnStartup
     *   converted to bool, default true. No accepted-license/version record is stored or checked.
     */
    [[nodiscard]] static bool shouldShowOnStartup();

    /*!
     * \brief Persists the "show on startup" preference.
     * \param show  true = show dialog on next launch, false = suppress it.
     * @note Write the default-application QSettings key and call sync immediately; storage errors
     *   ignored, no signal or live-dialog checkbox update. This preference alone does not
     *   constitute a separate recorded acceptance state.
     */
    static void setShowOnStartup(bool show);

private slots:
    /**
     * @brief Persist the current checkbox with immediate settings sync, then accept.
     * @note Persists true as well as false; settings failures do not block acceptance. No
     *   application exit, source/model edit or independent acceptance-version record.
     */
    void onAccepted();
    /**
     * @brief Reject without writing preferences; the startup caller decides whether to exit.
     * @note Existing preference remains unchanged, including a previously stored false.
     *   Window-manager rejection also follows QDialog behavior rather than saving checkbox state.
     */
    void onRejected();

private:
    /**
     * @brief Create owned read-only GPL summary, initially checked startup preference and
     *   agree/exit-role buttons, then fit size.
     * @note Displays compiled translated summary, not a license-file/network fetch. Constructor
     *   connects button actions afterward; helper does not read or write settings.
     */
    void buildUi();
    /**
     * @param[in] show Exact next-startup boolean persisted, without inspecting checkbox or prior
     *   acceptance.
     * @brief Write default-application settings key and sync immediately; ignore storage errors. No
     *   live UI or application lifecycle action.
     */
    void savePreference(bool show);

    QPlainTextEdit *m_licenseText = nullptr;
    QCheckBox *m_showCheckBox = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

#endif // LICENSEAGREEMENTDIALOG_H
