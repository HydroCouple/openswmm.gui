#ifndef USAGEEDITBUTTON_H
#define USAGEEDITBUTTON_H
#include "ui/properties/usageeditref.h"
#include <QWidget>
class QPushButton;
class UsageEditButton : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(UsageEditRef value READ value WRITE setValue USER true)
public:
    explicit UsageEditButton(QWidget *parent = nullptr);
    UsageEditRef value() const { return m_ref; }
    void setValue(const UsageEditRef &ref);
signals:
    void valueChanged();

private:
    UsageEditRef m_ref;
    QPointer<SWMMModelLayer> m_owner;
    QPushButton *m_button;
};
#endif
