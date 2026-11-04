#include "ui/properties/usageeditbutton.h"
#include "ui/panels/usagetablemodel.h"
#include "layers/swmmmodellayer.h"
#include <QHBoxLayout>
#include <QPushButton>
UsageEditButton::UsageEditButton(QWidget *parent) : QWidget(parent), m_button(new QPushButton(this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_button);
    setFocusProxy(m_button);
    connect(m_button, &QPushButton::clicked, this, [this] {
        if (m_owner && m_owner->engine() == m_ref.engine)
            openUsageEditor(m_ref, window());
    });
    setValue({});
}
void UsageEditButton::setValue(const UsageEditRef &ref)
{
    m_ref = ref;
    m_owner = ref.layer;
    m_button->setText(ref.summary.isEmpty() ? tr("Edit…") : tr("%1 — Edit…").arg(ref.summary));
}
void registerUsageEditRefConverter()
{
    static const bool registered = QMetaType::registerConverter<UsageEditRef, QString>(
        [](const UsageEditRef &r) { return r.summary; });
    Q_UNUSED(registered);
}
