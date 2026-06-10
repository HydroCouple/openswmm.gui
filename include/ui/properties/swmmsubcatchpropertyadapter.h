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

#include "ui/properties/userflagseditref.h"   // USER_FLAGS Phase 4

class SWMMModelLayer;

class SWMMSubcatchPropertyAdapter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name        READ name  WRITE setName)
    /*! Slice TA — free-form `[TAGS]` label. Direct engine read/write
     *  (mirror of node/link tag wiring); the existing `changed()`
     *  signal + AttributePanel.objectEdited fan-out is sufficient for
     *  two-way sync with the Attribute Table since tag changes don't
     *  affect map symbology or attribute-table layout. */
    Q_PROPERTY(QString tag         READ tag         WRITE setTag         NOTIFY changed)
    Q_PROPERTY(double  area        READ area        WRITE setArea        NOTIFY changed)
    Q_PROPERTY(double  width       READ width       WRITE setWidth       NOTIFY changed)
    Q_PROPERTY(double  slope       READ slope       WRITE setSlope       NOTIFY changed)
    Q_PROPERTY(double  impervPct   READ impervPct   WRITE setImpervPct   NOTIFY changed)
    Q_PROPERTY(double  nImperv     READ nImperv     WRITE setNImperv     NOTIFY changed)
    Q_PROPERTY(double  nPerv       READ nPerv       WRITE setNPerv       NOTIFY changed)
    Q_PROPERTY(double  dsImperv    READ dsImperv    WRITE setDsImperv    NOTIFY changed)
    Q_PROPERTY(double  dsPerv      READ dsPerv      WRITE setDsPerv      NOTIFY changed)
    /*! Phase 4 of docs/USER_FLAGS_UI_PLAN_2026-06-03.md — per-object
     *  user-flag assignments row (see SWMMNodePropertyAdapter). */
    Q_PROPERTY(UserFlagsEditRef userFlags
               READ userFlagsRef WRITE setUserFlagsRef NOTIFY changed)

public:
    SWMMSubcatchPropertyAdapter(SWMM_Engine engine, QString name,
                                  QObject *parent = nullptr);

    /*! USER_FLAGS Phase 4 — bind the owning layer so userFlagsRef() can
     *  reach the shared UserFlagsModel (mirror of
     *  SWMMNodePropertyAdapter::setModelLayer; nullptr-safe). */
    void setModelLayer(SWMMModelLayer *layer) { m_layer = layer; }
    [[nodiscard]] SWMMModelLayer *modelLayer() const { return m_layer; }

    [[nodiscard]] UserFlagsEditRef userFlagsRef() const;

    [[nodiscard]] QString name() const { return m_name; }
    [[nodiscard]] QString tag()  const;

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
    void setName(const QString &newName);
    void setTag(const QString &t);
    void setArea(double v);
    void setWidth(double v);
    void setSlope(double v);
    void setImpervPct(double v);
    void setNImperv(double v);
    void setNPerv(double v);
    void setDsImperv(double v);
    void setDsPerv(double v);

    void setUserFlagsRef(const UserFlagsEditRef &) { emit changed(); }

    /*! Round-4 follow-up 2026-05-12 — see SWMMNodePropertyAdapter::refresh. */
    void refresh() { emit changed(); }

    /*! See SWMMNodePropertyAdapter::updateStoredName. */
    void updateStoredName(const QString &newName) { m_name = newName; }

signals:
    void changed();
    void renameRequested(const QString &oldName, const QString &newName);
    /*! See SWMMNodePropertyAdapter::displayLabelsChanged. */
    void displayLabelsChanged();

private:
    [[nodiscard]] int idx() const;
    SWMM_Engine     m_engine;
    QString         m_name;
    SWMMModelLayer *m_layer = nullptr;   ///< USER_FLAGS Phase 4 — borrow.
};

#endif // SWMMSUBCATCHPROPERTYADAPTER_H
