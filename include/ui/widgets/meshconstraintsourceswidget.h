// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mesh/constraintsimplifier.h"
#include <QHash>
#include <QJsonArray>
#include <QWidget>

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;

// Source overrides are stored in physical SI units and keyed by stable layer/
// source identities. Unavailable sources retain their saved settings.
class MeshConstraintSourcesWidget : public QWidget
{
    Q_OBJECT
public:
    struct Source {
        QString id, label;
        bool authoredZ = false, preserveByDefault = false;
    };
    explicit MeshConstraintSourcesWidget(QWidget *parent = nullptr);
    void setSources(const QVector<Source> &sources);
    void setDefaults(const mesh::ConstraintSimplificationPolicy &physicalDefaults);
    void setUnits(double metresPerUnit, const QString &label);
    mesh::ConstraintPolicyOverride policy(const QString &id) const;
    QJsonArray toJson() const;
    void fromJson(const QJsonArray &json);
signals:
    void policiesChanged();
private:
    enum class Scale { Length, Angle, Fraction };
    struct Field { QCheckBox *override; QDoubleSpinBox *value; Scale scale; double displayed = 0; };
    QVector<Source> m_sources;
    QHash<QString, mesh::ConstraintPolicyOverride> m_policies;
    mesh::ConstraintSimplificationPolicy m_defaults;
    QVector<Field> m_fields;
    QComboBox *m_source = nullptr, *m_mode = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_reset = nullptr;
    bool m_loading = false;
    double m_toSI = 1;
    QString m_unitLabel = "m";
    void rebuildSources();
    void loadCurrent();
    void storeCurrent();
};
