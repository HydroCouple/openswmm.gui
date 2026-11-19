/**
 * @file      crschangedialog.cpp
 * @brief     modal that asks the user how to apply a CRS change to a model with already-loaded
 *            geometry: reproject the stored coordinates, just re-render in the new CRS, or cancel.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/crschangedialog.h"
#include "ui/theme/themehelpers.h"

#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

CRSChangeDialog::CRSChangeDialog(const QString &oldAuth, const QString &newAuth, bool sourceIsLocal,
                                 QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Change Coordinate Reference System"));
    // Iteration 2 (D3) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("CRSChangeDialog"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    auto *summary = new QLabel(tr("<b>From:</b> %1<br><b>To:</b> %2")
                                   .arg(oldAuth.isEmpty() ? tr("(none)") : oldAuth,
                                        newAuth.isEmpty() ? tr("(none)") : newAuth),
                               this);
    summary->setTextFormat(Qt::RichText);
    layout->addWidget(summary);

    auto *group = new QGroupBox(tr("How should the change be applied?"), this);
    auto *gl = new QVBoxLayout(group);

    m_radioAssign = new QRadioButton(tr("Assign CRS without changing coordinates"), group);
    m_radioAssign->setToolTip(tr("Use when the stored coordinates already use the selected CRS. "
                                 "The numbers are kept unchanged; save to persist the CRS."));
    gl->addWidget(m_radioAssign);

    m_radioReproject = new QRadioButton(tr("Reproject stored coordinates"), group);
    m_radioReproject->setToolTip(
        tr("Permanently transforms every node, link vertex, and subcatchment "
           "polygon coordinate from the source CRS to the target CRS. "
           "Marks the project dirty; persists on next save."));
    gl->addWidget(m_radioReproject);

    auto *reprojDesc =
        new QLabel(tr("    Use when the new CRS is the correct one for the data."), group);
    reprojDesc->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    gl->addWidget(reprojDesc);

    m_radioRenderOnly = new QRadioButton(tr("Re-render only (display in new CRS)"), group);
    m_radioRenderOnly->setToolTip(
        tr("Keeps stored coordinates unchanged. The canvas reprojects on the "
           "fly for display. Use when the source CRS is correct and you just "
           "want a different visualization."));
    gl->addWidget(m_radioRenderOnly);

    auto *renderDesc = new QLabel(tr("    Use when the source CRS is correct."), group);
    renderDesc->setStyleSheet(openswmmvis::ui::theme::hintStyle());
    gl->addWidget(renderDesc);

    if (sourceIsLocal) {
        m_radioReproject->setEnabled(false);
        m_radioRenderOnly->setEnabled(false);
        renderDesc->setText(tr("    A local grid has no geographic transform. Assign the CRS "
                               "that describes the existing coordinates first."));
        m_radioAssign->setChecked(true);
    } else {
        m_radioReproject->setChecked(true); // preserve the default for georeferenced data
    }

    layout->addWidget(group);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    layout->addWidget(m_buttonBox);

    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &CRSChangeDialog::onAccept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void CRSChangeDialog::onAccept()
{
    if (m_radioAssign->isChecked())
        m_choice = Assign;
    else if (m_radioReproject->isChecked())
        m_choice = Reproject;
    else if (m_radioRenderOnly->isChecked())
        m_choice = RenderOnly;
    else
        m_choice = Cancel;
    accept();
}
