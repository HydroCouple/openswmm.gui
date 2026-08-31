/*!
 * \file   wfslayer.cpp
 * \license GPL-3.0-or-later
 */

#include "layers/wfslayer.h"

#include <hydrocoupleogc/wfscapabilities.h>

#include <QUuid>

#include <cpl_vsi.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

namespace {

/*!
 * \brief What the service said when it would not answer.
 *
 * A WFS refuses in the body, under an HTTP 200 as often as not, so a
 * response that will not open has to be read as a document before it is
 * reported as gibberish.
 */
QString refusalText(const QByteArray &body)
{
    const HydroCouple::Ogc::WfsCapabilities report =
        HydroCouple::Ogc::parseWfsCapabilities(body);

    return report.ok ? QString() : report.message;
}

} // namespace

WFSLayer::WFSLayer(const QString &name, OpenSWMMVisWorkspace *parent)
    : GISVectorLayer(QString(), QString(), parent)
{
    setName(name);
}

WFSLayer::~WFSLayer()
{
    // The dataset must close before the bytes under it go away.
    closeDataset();

    if (!m_vsiPath.isEmpty())
        VSIUnlink(m_vsiPath.toUtf8().constData());
}

QString WFSLayer::sourceDescription() const
{
    if (m_serviceUrl.isEmpty())
        return m_typeName;

    return m_typeName.isEmpty()
               ? m_serviceUrl
               : QStringLiteral("%1 — %2").arg(m_typeName, m_serviceUrl);
}

bool WFSLayer::adoptResponse(const QByteArray &body, QString &message)
{
    if (body.isEmpty()) {
        message = tr("The service sent an empty response.");
        return false;
    }

    GDALAllRegister();

    // Unique per layer: the dataset stays open over these bytes for the
    // layer's lifetime, so two layers sharing one name would have the
    // second's open file pulled out from under the first.
    const QString path =
        QStringLiteral("/vsimem/swmmvis-wfs-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));

    // The buffer is copied rather than borrowed: `body` is the caller's and
    // will not outlive this call, but the dataset reads from it for as long
    // as the layer is alive.
    VSILFILE *file = VSIFileFromMemBuffer(
        path.toUtf8().constData(),
        reinterpret_cast<GByte *>(CPLMalloc(static_cast<size_t>(body.size()))),
        body.size(), TRUE);

    if (!file) {
        message = tr("The response could not be read.");
        return false;
    }

    VSIFSeekL(file, 0, SEEK_SET);
    VSIFWriteL(body.constData(), 1, static_cast<size_t>(body.size()), file);
    VSIFCloseL(file);

    auto *dataset = static_cast<GDALDataset *>(
        GDALOpenEx(path.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr,
                   nullptr));

    if (!dataset || dataset->GetLayerCount() == 0) {
        const QString said = refusalText(body);

        message = said.isEmpty()
                      ? tr("The service's answer was not features this "
                           "program can read.")
                      : said;

        if (dataset)
            GDALClose(dataset);

        VSIUnlink(path.toUtf8().constData());

        return false;
    }

    const QString layerName = QString::fromUtf8(dataset->GetLayer(0)->GetName());
    const long long count = dataset->GetLayer(0)->GetFeatureCount();

    GDALClose(dataset);

    if (count == 0) {
        // Not a failure of this program: the collection may simply hold
        // nothing over the ground that was asked about, and saying so is
        // more use than an error.
        message = tr("The service returned no features over that ground.");
        VSIUnlink(path.toUtf8().constData());

        return false;
    }

    // Whatever was open before is replaced, and its bytes released with it.
    const QString previous = m_vsiPath;

    m_vsiPath = path;
    openDataset(path, layerName);

    if (!previous.isEmpty())
        VSIUnlink(previous.toUtf8().constData());

    return true;
}
