/*!
 * \file   classificationschemecelleditor.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice D1-a — ClassificationSchemeCellEditor implementation.
 */
#include "ui/widgets/classificationschemecelleditor.h"

#include "ui/dialogs/editors/classificationbindings.h"
#include "ui/widgets/classificationeditor.h"

#include "render/intervalbinner.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using OpenSWMM::Render::BinMethod;
using OpenSWMM::Render::ClassificationScheme;

namespace {

// Human-readable method name for the read-only summary label.
QString methodName(BinMethod m)
{
    switch (m) {
    case BinMethod::EqualInterval: return ClassificationSchemeCellEditor::tr("Equal interval");
    case BinMethod::Quantile:      return ClassificationSchemeCellEditor::tr("Quantile");
    case BinMethod::Manual:        return ClassificationSchemeCellEditor::tr("Manual");
    case BinMethod::NaturalBreaks: return ClassificationSchemeCellEditor::tr("Natural breaks");
    case BinMethod::StdDev:        return ClassificationSchemeCellEditor::tr("Std. deviation");
    case BinMethod::Logarithmic:   return ClassificationSchemeCellEditor::tr("Logarithmic");
    case BinMethod::Exponential:   return ClassificationSchemeCellEditor::tr("Exponential");
    }
    return ClassificationSchemeCellEditor::tr("Equal interval");
}

} // namespace

ClassificationSchemeCellEditor::ClassificationSchemeCellEditor(QWidget *parent)
    : QWidget(parent)
{
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);

    m_summary = new QLabel(this);
    m_editBtn = new QPushButton(tr("Edit…"), this);
    m_editBtn->setAutoDefault(false);

    row->addWidget(m_summary, 1);
    row->addWidget(m_editBtn, 0);

    connect(m_editBtn, &QPushButton::clicked, this,
            &ClassificationSchemeCellEditor::openDialog);

    updateSummary();
}

void ClassificationSchemeCellEditor::setScheme(const ClassificationScheme &s)
{
    m_scheme = s;
    updateSummary();
}

void ClassificationSchemeCellEditor::updateSummary()
{
    if (!m_summary) return;
    const QString ramp = m_scheme.rampName().isEmpty()
                             ? tr("custom")
                             : m_scheme.rampName();
    m_summary->setText(tr("%1 · %2 classes · %3")
                           .arg(methodName(m_scheme.method()))
                           .arg(m_scheme.classCount())
                           .arg(ramp));
}

void ClassificationSchemeCellEditor::openDialog()
{
    // Restore point for Cancel — the binding mutates m_scheme live.
    const ClassificationScheme before = m_scheme;

    auto *dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Classification"));
    auto *lay = new QVBoxLayout(dlg);

    // In-memory binding over a local copy of m_scheme. Empty sample provider
    // → the editor degrades to equal spacing for the table preview; range
    // comes from the scheme's own min/max (static layers bin per draw).
    auto *binding = new SublayerSchemeBinding(
        [this] { return m_scheme; },
        [this](const ClassificationScheme &s) { m_scheme = s; },
        [] { return QVector<double>{}; },
        [this] {
            const double lo = m_scheme.rangeMin();
            const double hi = m_scheme.rangeMax();
            return qMakePair(lo, hi > lo ? hi : lo + 1.0);
        },
        /*supportsContinuousMode=*/true,
        /*supportsRangeModes=*/false);

    lay->addWidget(new ClassificationEditor(binding, /*ownBinding=*/true, dlg));

    auto *bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    connect(bb, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    lay->addWidget(bb);

    if (dlg->exec() == QDialog::Accepted) {
        updateSummary();
        emit schemeChanged();
    } else {
        m_scheme = before;   // roll back the live edits
    }
    dlg->deleteLater();
}

} // namespace openswmmvis::ui
