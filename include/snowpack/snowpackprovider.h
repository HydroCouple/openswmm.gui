/*!
 * \file   snowpackprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MVC model for a single SWMM snow pack ([SNOWPACKS]).
 *
 * One SnowpackProvider per `[SNOWPACKS]` entry. Owned by a project-scoped
 * SnowpackRegistry. The current engine exposes only add/count/index/id for
 * snow packs — there are no per-parameter getters/setters — so this provider
 * carries the identity only (name). Melt-coefficient parameters can be added
 * once the engine surfaces them.
 */
#ifndef OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H
#define OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H

#include <QObject>
#include <QString>

namespace openswmmvis::snowpack {

class SnowpackProvider : public QObject
{
    Q_OBJECT

public:
    explicit SnowpackProvider(QString name, QObject *parent = nullptr);
    ~SnowpackProvider() override;

    QString name() const noexcept { return m_name; }
    void setName(QString newName);

signals:
    void nameChanged(QString prev, QString now);

private:
    QString m_name;
};

} // namespace openswmmvis::snowpack

#endif // OPENSWMMVIS_SNOWPACK_SNOWPACKPROVIDER_H
