/*!
 * \file   transectpropertybag.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — QObject Q_PROPERTY surface backing the
 *         TransectEditorDialog middle-pane property tree (QPropertyModel).
 *
 * Bidirectional wiring: on bind(), the bag pulls current values from the
 * bound TransectProvider; setter slots push back to the provider via the
 * SWMMModelLayer apply helpers (if a layer is bound) or directly on the
 * provider (test mode). Property changes flow back through the provider's
 * signals → bag's `setX(currentValue)` which is a no-op if the value did
 * not change, breaking the feedback loop.
 *
 * Exposed groups (the displayLabelFor() prefix drives section headers in
 * the QPropertyModel tree):
 *
 *   Roughness             — nLeftBank, nRightBank, nChannel
 *   Bank Stations         — xLeftBank, xRightBank
 *   Encroachment Stations — xLeftEncroachment, xRightEncroachment
 *   Modifiers             — stationMultiplier, elevationOffset, meanderFactor
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_TRANSECTPROPERTYBAG_H
#define OPENSWMMVIS_UI_DIALOGS_TRANSECTPROPERTYBAG_H

#include <QObject>
#include <QPointer>
#include <QString>

class SWMMModelLayer;

namespace openswmmvis::transect { class TransectProvider; }

namespace openswmmvis::ui {

class TransectPropertyBag : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double nLeftBank          READ nLeftBank          WRITE setNLeftBank          NOTIFY nLeftBankChanged)
    Q_PROPERTY(double nRightBank         READ nRightBank         WRITE setNRightBank         NOTIFY nRightBankChanged)
    Q_PROPERTY(double nChannel           READ nChannel           WRITE setNChannel           NOTIFY nChannelChanged)

    Q_PROPERTY(double xLeftBank          READ xLeftBank          WRITE setXLeftBank          NOTIFY xLeftBankChanged)
    Q_PROPERTY(double xRightBank         READ xRightBank         WRITE setXRightBank         NOTIFY xRightBankChanged)

    Q_PROPERTY(double xLeftEncroachment  READ xLeftEncroachment  WRITE setXLeftEncroachment  NOTIFY xLeftEncroachmentChanged)
    Q_PROPERTY(double xRightEncroachment READ xRightEncroachment WRITE setXRightEncroachment NOTIFY xRightEncroachmentChanged)

    Q_PROPERTY(double stationMultiplier  READ stationMultiplier  WRITE setStationMultiplier  NOTIFY stationMultiplierChanged)
    Q_PROPERTY(double elevationOffset    READ elevationOffset    WRITE setElevationOffset    NOTIFY elevationOffsetChanged)
    Q_PROPERTY(double meanderFactor      READ meanderFactor      WRITE setMeanderFactor      NOTIFY meanderFactorChanged)

public:
    explicit TransectPropertyBag(QObject *parent = nullptr);

    void bind(openswmmvis::transect::TransectProvider *p,
              SWMMModelLayer *layer = nullptr);
    openswmmvis::transect::TransectProvider *provider() const noexcept;

    Q_INVOKABLE QString displayLabelFor(const QString &property) const;

    double nLeftBank()          const noexcept { return m_nLeft; }
    double nRightBank()         const noexcept { return m_nRight; }
    double nChannel()           const noexcept { return m_nChannel; }
    double xLeftBank()          const noexcept { return m_xLeftBank; }
    double xRightBank()         const noexcept { return m_xRightBank; }
    double xLeftEncroachment()  const noexcept { return m_xLeftEnc; }
    double xRightEncroachment() const noexcept { return m_xRightEnc; }
    double stationMultiplier()  const noexcept { return m_xFactor; }
    double elevationOffset()    const noexcept { return m_yFactor; }
    double meanderFactor()      const noexcept { return m_lengthFactor; }

public slots:
    void setNLeftBank(double v);
    void setNRightBank(double v);
    void setNChannel(double v);
    void setXLeftBank(double v);
    void setXRightBank(double v);
    void setXLeftEncroachment(double v);
    void setXRightEncroachment(double v);
    void setStationMultiplier(double v);
    void setElevationOffset(double v);
    void setMeanderFactor(double v);

signals:
    void nLeftBankChanged(double);
    void nRightBankChanged(double);
    void nChannelChanged(double);
    void xLeftBankChanged(double);
    void xRightBankChanged(double);
    void xLeftEncroachmentChanged(double);
    void xRightEncroachmentChanged(double);
    void stationMultiplierChanged(double);
    void elevationOffsetChanged(double);
    void meanderFactorChanged(double);

private slots:
    void onProviderRoughnessChanged_();
    void onProviderBankStationsChanged_();
    void onProviderEncroachmentChanged_();
    void onProviderModifiersChanged_();

private:
    void refreshFromProvider_();
    void pushRoughness_();
    void pushBank_();
    void pushEncroach_();
    void pushModifiers_();

    QPointer<openswmmvis::transect::TransectProvider> m_provider;
    QPointer<SWMMModelLayer>                          m_layer;

    double m_nLeft       = 0.0;
    double m_nRight      = 0.0;
    double m_nChannel    = 0.0;
    double m_xLeftBank   = 0.0;
    double m_xRightBank  = 0.0;
    double m_xLeftEnc    = 0.0;
    double m_xRightEnc   = 0.0;
    double m_xFactor     = 1.0;
    double m_yFactor     = 0.0;
    double m_lengthFactor = 1.0;

    bool m_suppressPush = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TRANSECTPROPERTYBAG_H
