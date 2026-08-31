/*!
 * \file   test_wfsservice.cpp
 * \license GPL-3.0-or-later
 * \brief  Connecting to a Web Feature Service and taking a collection off it.
 *
 * \details
 * The first tests this program has ever had for one of its OGC services —
 * WMS, WMTS and WCS have none. They talk to an HTTP server running inside
 * the test process on the loopback interface, so nothing here reaches the
 * internet: a test that depended on a public server would fail for reasons
 * having nothing to do with this code.
 *
 * What the service says is a real saved capabilities document from a
 * national feature register, so the awkward parts are the ones a real
 * server actually has: collections published in a projected national grid
 * by default, and millions of features behind each of them.
 */

#include "layers/wfslayer.h"
#include "ui/dialogs/addbasemapdialog.h"

#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUrlQuery>

namespace {

QByteArray fixture(const QString &name)
{
    const QByteArray dir = qgetenv("SWMMVIS_GUI_TEST_DATA");
    QFile file(QString::fromUtf8(dir) + QStringLiteral("/ogc/") + name);

    if (!file.open(QIODevice::ReadOnly)) return {};

    return file.readAll();
}

/*!
 * \brief A feature service inside this process, on the loopback interface.
 */
class LocalWfs : public QTcpServer
{
public:
    LocalWfs() { listen(QHostAddress::LocalHost, 0); }

    [[nodiscard]] QString endpoint() const
    {
        return QStringLiteral("http://127.0.0.1:%1/wfs").arg(serverPort());
    }

    void answerFeaturesWith(const QByteArray &body) { m_features = body; }

    //! Answer the capabilities request with something that is not a WFS.
    void refuseCapabilities() { m_isWfs = false; }

    [[nodiscard]] QString lastRequest() const { return m_lastRequest; }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);

        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [this, socket]() { onReadyRead(socket); });
        QObject::connect(socket, &QTcpSocket::disconnected, socket,
                         [socket]() { socket->deleteLater(); });
    }

private:
    void onReadyRead(QTcpSocket *socket)
    {
        m_buffers[socket].append(socket->readAll());

        if (!m_buffers[socket].contains("\r\n\r\n")) return;

        const QByteArray head = m_buffers.take(socket);
        const QString target =
            QString::fromUtf8(head.split('\n').value(0).split(' ').value(1));

        m_lastRequest = target;

        const QString request =
            QUrlQuery(QUrl(target).query()).queryItemValue(
                QStringLiteral("REQUEST"));

        QByteArray body;
        QByteArray type = "application/xml";

        if (request == QLatin1String("GetFeature")) {
            body = m_features;
            type = "application/json";
        } else if (m_isWfs) {
            body = fixture(QStringLiteral("wfs-2.0.0-pdok.xml"));

            // The document says where GetFeature requests go, and a client
            // that reads it sends them there rather than back to whatever
            // address the capabilities came from. The saved one names the
            // service it was captured from, so this stand-in has to claim
            // the address it is actually listening on.
            body.replace("https://service.pdok.nl/kadaster/bag/wfs/v2_0",
                         endpoint().toUtf8());
        } else {
            body = "<?xml version=\"1.0\"?><ows:ExceptionReport "
                   "xmlns:ows=\"http://www.opengis.net/ows/1.1\">"
                   "<ows:Exception><ows:ExceptionText>Service WFS is not "
                   "supported</ows:ExceptionText></ows:Exception>"
                   "</ows:ExceptionReport>";
        }

        QByteArray answer = "HTTP/1.1 200 OK\r\n";
        answer += "Content-Type: " + type + "\r\n";
        answer += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        answer += "Connection: close\r\n\r\n";
        answer += body;

        socket->write(answer);
        socket->flush();
        socket->disconnectFromHost();
    }

    bool m_isWfs = true;
    QByteArray m_features;
    QString m_lastRequest;
    QMap<QTcpSocket *, QByteArray> m_buffers;
};

//! A GeoJSON answer of the shape a WFS returns.
QByteArray catchments()
{
    return R"({"type":"FeatureCollection",
               "crs":{"type":"name",
                      "properties":{"name":"urn:ogc:def:crs:EPSG::4326"}},
               "features":[
                 {"type":"Feature","properties":{"name":"Upper"},
                  "geometry":{"type":"Polygon",
                              "coordinates":[[[4,51],[5,51],[5,52],[4,52],[4,51]]]}},
                 {"type":"Feature","properties":{"name":"Lower"},
                  "geometry":{"type":"Polygon",
                              "coordinates":[[[5,52],[6,52],[6,53],[5,53],[5,52]]]}}]})";
}

bool waitFor(const std::function<bool()> &done, int milliseconds = 4000)
{
    QElapsedTimer timer;
    timer.start();

    while (!done() && timer.elapsed() < milliseconds)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

    return done();
}

} // namespace

class TestWfsService : public QObject
{
    Q_OBJECT

private slots:
    void anAddressIsEnoughToListWhatTheServiceHolds();
    void choosingACollectionFetchesItOverTheGroundInView();
    void aCollectionHoldingNothingThereSaysSoAndStaysOpen();
    void anAddressThatIsNotAFeatureServiceSaysWhatItSaid();
    void aLayerKeepsItsFeaturesReadableForItsWholeLife();
};

void TestWfsService::anAddressIsEnoughToListWhatTheServiceHolds()
{
    LocalWfs server;

    AddBasemapDialog dialog;

    dialog.setInitialTab(AddBasemapDialog::Wfs);
    dialog.findChild<QLineEdit *>(QStringLiteral("wfsUrlEdit"))
        ->setText(server.endpoint());
    dialog.findChild<QPushButton *>(QStringLiteral("wfsConnectButton"))
        ->click();

    auto *list = dialog.findChild<QListWidget *>(
        QStringLiteral("wfsCollectionList"));

    QVERIFY2(waitFor([&] { return list->count() > 0; }),
             qPrintable(dialog.wfsStatus()));

    // The user said nothing about the version or what it holds; both are
    // answers, not questions.
    QCOMPARE(list->count(), 5);
    // The service names itself, and that name is what the user is shown --
    // not the address they typed.
    QVERIFY2(dialog.wfsStatus().contains(QStringLiteral("BAG WFS")),
             qPrintable(dialog.wfsStatus()));

    // Something readable is already chosen, so Add is available without
    // hunting through the list first.
    QVERIFY(dialog.findChild<QDialogButtonBox *>()
                ->button(QDialogButtonBox::Ok)
                ->isEnabled());
}

void TestWfsService::choosingACollectionFetchesItOverTheGroundInView()
{
    LocalWfs server;
    server.answerFeaturesWith(catchments());

    AddBasemapDialog dialog;

    dialog.setInitialTab(AddBasemapDialog::Wfs);

    // What the map is looking at: one catchment's worth, not a country's.
    dialog.setPreferredExtent(QRectF(QPointF(4.0, 51.0), QPointF(6.0, 53.0)));
    dialog.findChild<QLineEdit *>(QStringLiteral("wfsUrlEdit"))
        ->setText(server.endpoint());
    dialog.findChild<QPushButton *>(QStringLiteral("wfsConnectButton"))
        ->click();

    auto *list = dialog.findChild<QListWidget *>(
        QStringLiteral("wfsCollectionList"));

    QVERIFY(waitFor([&] { return list->count() > 0; }));

    list->setCurrentRow(0);
    dialog.findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Ok)
        ->click();

    // Accepted only once the features are in hand, because a request that
    // is well formed can still come back holding nothing.
    QVERIFY2(waitFor([&] { return dialog.result() == QDialog::Accepted; }),
             qPrintable(dialog.wfsStatus()));

    const QUrlQuery query(QUrl(server.lastRequest()).query());

    QCOMPARE(query.queryItemValue(QStringLiteral("REQUEST")),
             QStringLiteral("GetFeature"));

    // A national register asked without a limit answers with every feature
    // it holds, and this one holds millions.
    QVERIFY(!query.queryItemValue(QStringLiteral("COUNT")).isEmpty());

    // Latitude first: EPSG:4326 under WFS 2.0.0 is written in the
    // authority's own axis order.
    QVERIFY2(query.queryItemValue(QStringLiteral("BBOX"))
                 .startsWith(QStringLiteral("51")),
             qPrintable(query.queryItemValue(QStringLiteral("BBOX"))));

    // GeoJSON, because this collection offers it and it needs no schema.
    QVERIFY(query.queryItemValue(QStringLiteral("OUTPUTFORMAT"))
                .contains(QStringLiteral("json")));

    std::unique_ptr<WFSLayer> layer(
        qobject_cast<WFSLayer *>(dialog.createLayer(nullptr)));

    QVERIFY(layer != nullptr);
    QCOMPARE(layer->featureCount(), 2);
    QCOMPARE(layer->typeName(), QStringLiteral("bag:pand"));

    // The layer says where it came from, not where GDAL was handed the
    // bytes — /vsimem names nothing a user could open.
    QVERIFY(!layer->sourceDescription().contains(QStringLiteral("vsimem")));
    QVERIFY(layer->sourceDescription().contains(QStringLiteral("bag:pand")));
}

void TestWfsService::aCollectionHoldingNothingThereSaysSoAndStaysOpen()
{
    LocalWfs server;
    server.answerFeaturesWith(
        R"({"type":"FeatureCollection","features":[]})");

    AddBasemapDialog dialog;

    dialog.setInitialTab(AddBasemapDialog::Wfs);
    dialog.findChild<QLineEdit *>(QStringLiteral("wfsUrlEdit"))
        ->setText(server.endpoint());
    dialog.findChild<QPushButton *>(QStringLiteral("wfsConnectButton"))
        ->click();

    auto *list = dialog.findChild<QListWidget *>(
        QStringLiteral("wfsCollectionList"));

    QVERIFY(waitFor([&] { return list->count() > 0; }));

    list->setCurrentRow(0);
    dialog.findChild<QDialogButtonBox *>()
        ->button(QDialogButtonBox::Ok)
        ->click();

    QVERIFY2(waitFor([&] {
                 return dialog.wfsStatus().contains(QStringLiteral("no features"));
             }),
             qPrintable(dialog.wfsStatus()));

    // Said where the user is looking, rather than after the dialog has
    // closed on an empty layer — and they can pick another without
    // starting again.
    QVERIFY(dialog.result() != QDialog::Accepted);
    QVERIFY(dialog.createLayer(nullptr) == nullptr);
    QVERIFY(dialog.findChild<QDialogButtonBox *>()
                ->button(QDialogButtonBox::Ok)
                ->isEnabled());
}

void TestWfsService::anAddressThatIsNotAFeatureServiceSaysWhatItSaid()
{
    LocalWfs server;
    server.refuseCapabilities();

    AddBasemapDialog dialog;

    dialog.setInitialTab(AddBasemapDialog::Wfs);
    dialog.findChild<QLineEdit *>(QStringLiteral("wfsUrlEdit"))
        ->setText(server.endpoint());
    dialog.findChild<QPushButton *>(QStringLiteral("wfsConnectButton"))
        ->click();

    QVERIFY(waitFor([&] {
        return !dialog.wfsStatus().contains(QStringLiteral("Asking"));
    }));

    // The server said why, and that is more use than this program's guess.
    QVERIFY2(dialog.wfsStatus().contains(QStringLiteral("not supported")),
             qPrintable(dialog.wfsStatus()));
    QCOMPARE(dialog.findChild<QListWidget *>(
                 QStringLiteral("wfsCollectionList"))
                 ->count(),
             0);

    // And nothing typed that is not an address is fetched at all.
    AddBasemapDialog nowhere;
    nowhere.setInitialTab(AddBasemapDialog::Wfs);
    nowhere.findChild<QLineEdit *>(QStringLiteral("wfsUrlEdit"))
        ->setText(QStringLiteral("this is not a url"));
    nowhere.findChild<QPushButton *>(QStringLiteral("wfsConnectButton"))
        ->click();

    QVERIFY(nowhere.wfsStatus().contains(QStringLiteral("not a web address")));
}

void TestWfsService::aLayerKeepsItsFeaturesReadableForItsWholeLife()
{
    // Unlike a layer that reads everything into memory once, this one keeps
    // its dataset open and reads features from it per paint. The bytes it
    // was handed therefore have to outlive the call that handed them over
    // — and a second layer must not disturb the first.
    WFSLayer first(QStringLiteral("First"));
    QString message;

    QVERIFY2(first.adoptResponse(catchments(), message), qPrintable(message));

    WFSLayer second(QStringLiteral("Second"));

    QVERIFY(second.adoptResponse(
        R"({"type":"FeatureCollection","features":[
             {"type":"Feature","properties":{},
              "geometry":{"type":"Point","coordinates":[5,52]}}]})",
        message));

    QCOMPARE(second.featureCount(), 1);

    // Still readable, with the second layer alive and holding its own.
    QCOMPARE(first.featureCount(), 2);

    // A refusal replaces nothing: what was already open stays open.
    QVERIFY(!first.adoptResponse("<html>Sign in</html>", message));
    QCOMPARE(first.featureCount(), 2);

    // And a service that refuses does so in the body, under an HTTP 200 as
    // often as not — so the answer is read as a document before being
    // called unreadable, and what it says reaches the user.
    QVERIFY(!first.adoptResponse(
        "<?xml version=\"1.0\"?><ows:ExceptionReport "
        "xmlns:ows=\"http://www.opengis.net/ows/1.1\"><ows:Exception>"
        "<ows:ExceptionText>Unknown typeName bag:nonesuch</ows:ExceptionText>"
        "</ows:Exception></ows:ExceptionReport>",
        message));
    QVERIFY2(message.contains(QStringLiteral("bag:nonesuch")),
             qPrintable(message));
}

QTEST_MAIN(TestWfsService)
#include "test_wfsservice.moc"
