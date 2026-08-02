/*!
 * \file   licenseagreementdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/licenseagreementdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

static constexpr char kSettingsKey[] = "SWMMVis/LicenseAgreement/showOnStartup";

static const char kLicenseText[] =
    "SWMMVis is free software: you can redistribute it and/or modify "
    "it under the terms of the GNU General Public License as published by "
    "the Free Software Foundation, either version 3 of the License, or "
    "(at your option) any later version.\n\n"
    "This program is distributed in the hope that it will be useful, "
    "but WITHOUT ANY WARRANTY; without even the implied warranty of "
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the "
    "GNU General Public License for more details.\n\n"
    "You should have received a copy of the GNU General Public License "
    "along with this program. If not, see <https://www.gnu.org/licenses/>.";

LicenseAgreementDialog::LicenseAgreementDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("License Agreement"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("LicenseAgreementDialog"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    buildUi();

    connect(m_buttons, &QDialogButtonBox::accepted, this, &LicenseAgreementDialog::onAccepted);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &LicenseAgreementDialog::onRejected);
}

LicenseAgreementDialog::~LicenseAgreementDialog() = default;

bool LicenseAgreementDialog::shouldShowOnStartup()
{
    QSettings settings;
    return settings.value(kSettingsKey, true).toBool();
}

void LicenseAgreementDialog::setShowOnStartup(bool show)
{
    QSettings settings;
    settings.setValue(kSettingsKey, show);
    settings.sync();
}

void LicenseAgreementDialog::onAccepted()
{
    savePreference(m_showCheckBox->isChecked());
    accept();
}

void LicenseAgreementDialog::onRejected()
{
    // Do not persist the preference — next launch will prompt again.
    reject();
}

void LicenseAgreementDialog::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);

    auto *heading = new QLabel(
        tr("<b>SWMMVis — GNU General Public License v3</b>"), this);
    heading->setWordWrap(true);
    layout->addWidget(heading);

    m_licenseText = new QPlainTextEdit(this);
    m_licenseText->setReadOnly(true);
    m_licenseText->setPlainText(tr(kLicenseText));
    m_licenseText->setMinimumSize(560, 200);
    layout->addWidget(m_licenseText);

    m_showCheckBox = new QCheckBox(tr("Show this agreement on startup"), this);
    m_showCheckBox->setChecked(true);
    layout->addWidget(m_showCheckBox);

    m_buttons = new QDialogButtonBox(this);
    QPushButton *yesBtn = m_buttons->addButton(tr("Yes, I Agree"), QDialogButtonBox::AcceptRole);
    QPushButton *noBtn  = m_buttons->addButton(tr("No, Exit"),     QDialogButtonBox::RejectRole);
    Q_UNUSED(yesBtn)
    Q_UNUSED(noBtn)
    layout->addWidget(m_buttons);

    adjustSize();
}

void LicenseAgreementDialog::savePreference(bool show)
{
    QSettings settings;
    settings.setValue(kSettingsKey, show);
    settings.sync();
}
