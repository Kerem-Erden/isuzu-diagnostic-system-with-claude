// Qt Test suite for SerialTransport WITHOUT hardware: port enumeration, CP210x recognition,
// open-failure path and lifecycle safety. No port is ever opened successfully here, so nothing is
// sent to any connected device. Real data flow over a COM port is verified manually (ESP32 bench).

#include <QSignalSpy>
#include <QTest>

#include "gateway/gateway_client.hpp"
#include "transport/serial_transport.hpp"
#include "transport/simulation_transport.hpp"

namespace {
// A name Windows will not have; opening it must fail quickly and cleanly.
const QString kMissingPort = QStringLiteral("COM_DOES_NOT_EXIST_ISUZU");
}

class SerialTransportTest : public QObject {
    Q_OBJECT
private slots:
    void recognisesCp210xIds() {
        static_assert(isCp210x(0x10C4, 0xEA60));
        QVERIFY(!isCp210x(0x1A86, 0x7523));  // CH340 (used in week 1 of the reference project)
        QVERIFY(!isCp210x(0x10C4, 0x0001));
    }

    void enumeratesPortsWithoutOpening() {
        const QList<SerialPortEntry> ports = availableSerialPorts();
        for (const SerialPortEntry& port : ports) {
            QVERIFY(!port.portName.isEmpty());
            QCOMPARE(port.likelyGateway, port.hasIds && isCp210x(port.vendorId, port.productId));
        }
    }

    void openFailureIsReportedAsClosed() {
        SerialTransport transport(kMissingPort);
        QSignalSpy closed(&transport, &Transport::closed);
        QSignalSpy errors(&transport, &Transport::errorOccurred);
        QVERIFY(transport.open());  // asynchronous: the result arrives as signals
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 3000);
        QVERIFY(closed.at(0).at(0).toString().startsWith(QStringLiteral("open failed")));
        QCOMPARE(errors.count(), 1);
        QVERIFY(!transport.isOpen());
        QTest::qWait(100);
        QCOMPARE(closed.count(), 1);  // exactly once
    }

    void clientReportsFailedConnectionNotLostConnection() {
        SerialTransport transport(kMissingPort);
        GatewayClient client(&transport);
        QSignalSpy failed(&client, &GatewayClient::connectionFailed);
        QSignalSpy lost(&client, &GatewayClient::connectionLost);
        client.connectToGateway();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QCOMPARE(lost.count(), 0);
        QCOMPARE(client.state(), GatewayClient::State::Disconnected);
    }

    void unopenedTransportIgnoresWriteAndClose() {
        SerialTransport transport(kMissingPort);
        QSignalSpy closed(&transport, &Transport::closed);
        transport.write("#REQ|1|PING*0000\n");
        transport.close();
        QTest::qWait(50);
        QCOMPARE(closed.count(), 0);
    }

    void destroyingDuringOpenAttemptIsSafe() {
        auto* transport = new SerialTransport(kMissingPort);
        transport->open();
        delete transport;  // must not crash or hang (worker thread stopped and joined)
        QTest::qWait(50);
    }

    void clientRefusesTransportSwitchWhileConnected() {
        SimulationTransport simulation;
        SerialTransport serial(kMissingPort);
        GatewayClient client(&simulation);
        client.connectToGateway();
        QTRY_COMPARE(client.state(), GatewayClient::State::Ready);
        QVERIFY(!client.setTransport(&serial));
        client.disconnectFromGateway();
        QTRY_COMPARE(client.state(), GatewayClient::State::Disconnected);
        QVERIFY(client.setTransport(&serial));
        QCOMPARE(client.transport(), &serial);
    }
};

QTEST_GUILESS_MAIN(SerialTransportTest)
#include "test_serial_transport.moc"
