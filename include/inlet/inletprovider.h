/*!
 * \file   inletprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM inlet design ([INLETS]).
 *
 * One InletProvider per `[INLETS]` entry. Owned by a project-scoped
 * InletRegistry. Engine I/O walks `swmm_inlet_*` from openswmm_infrastructure.h
 * (add(id,type) + set_params(length,width,grateType,openArea,splashVeloc)).
 *
 * The engine exposes setters but **no getters** for inlet parameters, so
 * existing values cannot be pre-loaded. To avoid clobbering untouched
 * definitions, the provider carries a `dirty` flag — the registry only writes
 * new providers or ones the user actually edited (see InletRegistry).
 */
#ifndef OPENSWMMVIS_INLET_INLETPROVIDER_H
#define OPENSWMMVIS_INLET_INLETPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::inlet {

class InletProvider : public QObject
{
    Q_OBJECT

public:
    explicit InletProvider(QString name, QObject *parent = nullptr);
    ~InletProvider() override;

    QString name() const noexcept { return m_name; }

    QString type()        const noexcept { return m_type; }       ///< GRATE/CURB/SLOTTED/CUSTOM
    double  length()      const noexcept { return m_length; }
    double  width()       const noexcept { return m_width; }
    QString grateType()   const noexcept { return m_grateType; }  ///< e.g. "P-50", "GENERIC"
    double  openArea()    const noexcept { return m_openArea; }   ///< 0..1
    double  splashVeloc() const noexcept { return m_splashVeloc; }

    bool dirty() const noexcept { return m_dirty; }
    void clearDirty() noexcept { m_dirty = false; }

    void setName(QString newName);
    void setType(QString v);
    void setLength(double v);
    void setWidth(double v);
    void setGrateType(QString v);
    void setOpenArea(double v);
    void setSplashVeloc(double v);

signals:
    void nameChanged(QString prev, QString now);
    void paramsChanged();

private:
    QString m_name;
    QString m_type        = QStringLiteral("GRATE");
    double  m_length      = 0.0;
    double  m_width       = 0.0;
    QString m_grateType   = QStringLiteral("GENERIC");
    double  m_openArea    = 0.0;
    double  m_splashVeloc = 0.0;
    bool    m_dirty       = false;
};

} // namespace openswmmvis::inlet

#endif // OPENSWMMVIS_INLET_INLETPROVIDER_H
