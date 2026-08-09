/*!
 * \file   newprojectdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "ui/dialogs/newprojectdialog.h"

#include "core/preferencesmanager.h"
#include "ui/dialogs/crsselectiondialog.h"

#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

NewProjectDialog::NewProjectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New SWMM Project"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("NewProjectDialog"));
    resize(420, 360);
    buildUi();
    seedDefaults();
}

void NewProjectDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    auto *header = new QLabel(
        tr("Create a new SWMM model with the options below. "
           "After clicking OK the project opens as an untitled window — "
           "use File → Save As to give it a real path."),
        this);
    header->setWordWrap(true);
    outer->addWidget(header);

    auto *form = new QFormLayout;
    outer->addLayout(form);

    m_nameEdit = new QLineEdit(this);
    form->addRow(tr("Project &name"), m_nameEdit);

    m_flowUnitsCombo = new QComboBox(this);
    m_flowUnitsCombo->addItems({QStringLiteral("CFS"), QStringLiteral("GPM"),
                                 QStringLiteral("MGD"), QStringLiteral("CMS"),
                                 QStringLiteral("LPS"), QStringLiteral("MLD")});
    form->addRow(tr("Flow &units"), m_flowUnitsCombo);

    m_infiltrationCombo = new QComboBox(this);
    m_infiltrationCombo->addItems({QStringLiteral("HORTON"),
                                    QStringLiteral("MODIFIED_HORTON"),
                                    QStringLiteral("GREEN_AMPT"),
                                    QStringLiteral("MODIFIED_GREEN_AMPT"),
                                    QStringLiteral("CURVE_NUMBER")});
    form->addRow(tr("&Infiltration model"), m_infiltrationCombo);

    m_routingCombo = new QComboBox(this);
    m_routingCombo->addItems({QStringLiteral("STEADY"),
                               QStringLiteral("KINWAVE"),
                               QStringLiteral("DYNWAVE"),
                               QStringLiteral("FV")});
    form->addRow(tr("Flow &routing"), m_routingCombo);

    m_startEdit = new QDateTimeEdit(this);
    m_startEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_startEdit->setCalendarPopup(true);
    form->addRow(tr("&Start"), m_startEdit);

    m_endEdit = new QDateTimeEdit(this);
    m_endEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    m_endEdit->setCalendarPopup(true);
    form->addRow(tr("&End"), m_endEdit);

    auto *crsRow = new QHBoxLayout;
    m_crsEdit = new QLineEdit(this);
    m_crsEdit->setReadOnly(true);
    m_crsBtn = new QPushButton(tr("Choose…"), this);
    crsRow->addWidget(m_crsEdit, 1);
    crsRow->addWidget(m_crsBtn);
    form->addRow(tr("&CRS"), crsRow);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                     this);
    outer->addWidget(bb);

    connect(m_crsBtn, &QPushButton::clicked, this, &NewProjectDialog::onChooseCrs);
    connect(bb, &QDialogButtonBox::accepted, this, &NewProjectDialog::onAccept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void NewProjectDialog::seedDefaults()
{
    auto *prefs = PreferencesManager::instance();
    const auto  sim = prefs->simulationDefaults();

    m_nameEdit->setText(QStringLiteral("Untitled"));
    m_flowUnitsCombo->setCurrentText(sim.flowUnits);
    m_infiltrationCombo->setCurrentText(sim.infiltrationModel);
    m_routingCombo->setCurrentText(sim.flowRouting);

    // Today at midnight → start + 24 h (matches File→New silent path).
    const QDateTime start(QDate::currentDate(), QTime(0, 0));
    m_startEdit->setDateTime(start);
    m_endEdit->setDateTime(start.addSecs(24 * 3600));

    if (!prefs->defaultCrsAuthority().isEmpty() && prefs->defaultCrsCode() > 0)
    {
        const QString code = QStringLiteral("%1:%2")
                                 .arg(prefs->defaultCrsAuthority())
                                 .arg(prefs->defaultCrsCode());
        m_crsEdit->setText(code);
    }
    else
    {
        m_crsEdit->setText(QStringLiteral("EPSG:4326"));
    }
}

void NewProjectDialog::onChooseCrs()
{
    CRSSelectionDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
    {
        const QString code = dlg.selectedAuthCode();
        if (!code.isEmpty())
            m_crsEdit->setText(code);
    }
}

void NewProjectDialog::onAccept()
{
    if (m_endEdit->dateTime() <= m_startEdit->dateTime())
    {
        QMessageBox::warning(this, tr("Invalid simulation window"),
            tr("End must be after Start."));
        return;
    }
    m_result.name              = m_nameEdit->text().trimmed();
    if (m_result.name.isEmpty())
        m_result.name = QStringLiteral("Untitled");
    m_result.flowUnits         = m_flowUnitsCombo->currentText();
    m_result.infiltrationModel = m_infiltrationCombo->currentText();
    m_result.flowRouting       = m_routingCombo->currentText();
    m_result.startDateTime     = m_startEdit->dateTime();
    m_result.endDateTime       = m_endEdit->dateTime();
    m_result.crsAuthCode       = m_crsEdit->text().trimmed();
    accept();
}
