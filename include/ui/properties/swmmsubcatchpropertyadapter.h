/*!
 * \file   swmmsubcatchpropertyadapter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AG.3 — Property-tree adapter for SWMM subcatchments.
 */

#ifndef SWMMSUBCATCHPROPERTYADAPTER_H
#define SWMMSUBCATCHPROPERTYADAPTER_H

#include <QObject>
#include <QString>

#include <openswmm/engine/openswmm_engine.h>

class SWMMSubcatchPropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name        READ name)
    Q_PROPERTY(double  area        READ area        WRITE setArea        NOTIFY changed)
    Q_PROPERTY(double  width       READ width       WRITE setWidth       NOTIFY changed)
    Q_PROPERTY(double  slope       READ slope       WRITE setSlope       NOTIFY changed)
    Q_PROPERTY(double  impervPct   READ impervPct   WRITE setImpervPct   NOTIFY changed)
    Q_PROPERTY(double  nImperv     READ nImperv     WRITE setNImperv     NOTIFY changed)
    Q_PROPERTY(double  nPerv       READ nPerv       WRITE setNPerv       NOTIFY changed)
    Q_PROPERTY(double  dsImperv    READ dsImperv    WRITE setDsImperv    NOTIFY changed)
    Q_PROPERTY(double  dsPerv      READ dsPerv      WRITE setDsPerv      NOTIFY changed)

public:
    SWMMSubcatchPropertyAdapter(SWMM_Engine engine, QString name,
                                  QObject *parent = nullptr);

    [[nodiscard]] QString name() const { return m_name; }

    [[nodiscard]] double area()      const;
    [[nodiscard]] double width()     const;
    [[nodiscard]] double slope()     const;
    [[nodiscard]] double impervPct() const;
    [[nodiscard]] double nImperv()   const;
    [[nodiscard]] double nPerv()     const;
    [[nodiscard]] double dsImperv()  const;
    [[nodiscard]] double dsPerv()    const;

    /*! See SWMMNodePropertyAdapter::displayLabelFor. */
    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

public slots:
    void setArea(double v);
    void setWidth(double v);
    void setSlope(double v);
    void setImpervPct(double v);
    void setNImperv(double v);
    void setNPerv(double v);
    void setDsImperv(double v);
    void setDsPerv(double v);

    /*! Round-4 follow-up 2026-05-12 — see SWMMNodePropertyAdapter::refresh. */
    void refresh() { emit changed(); }

signals:
    void changed();
    /*! See SWMMNodePropertyAdapter::displayLabelsChanged. */
    void displayLabelsChanged();

private:
    [[nodiscard]] int idx() const;
    SWMM_Engine m_engine;
    QString     m_name;
};

#endif // SWMMSUBCATCHPROPERTYADAPTER_H
