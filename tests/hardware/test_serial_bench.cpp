// OPT-IN bench test: desktop GatewayClient + SerialTransport against a real ESP32 running the
// SIMULATION firmware profile. Runs only when ISUZU_BENCH_PORT is set (e.g. "COM5"); otherwise every
// case is skipped, so CI and normal `ctest` never touch hardware.
//
// It refuses to continue if the gateway reports source=VEHICLE: this test must never cause CAN
// transmissions on a vehicle.

#include <QSignalSpy>
#include <QTest>

#include "gateway/gateway_client.hpp"
#include "transport/serial_transport.hpp"

namespace {

GatewayReply waitForReply(QSignalSpy& spy, quint32 id, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        for (const QList<QVariant>& args : spy) {
            const auto reply = args.at(0).value<GatewayReply>();
            if (reply.id == id) return reply;
        }
        QTest::qWait(5);
    }
    return GatewayReply{};
}

}  // namespace

class SerialBenchTest : public QObject {
    Q_OBJECT
private:
    QString port_;

private slots:
    void initTestCase() {
        port_ = qEnvironmentVariable("ISUZU_BENCH_PORT");
        if (port_.isEmpty()) QSKIP("ISUZU_BENCH_PORT not set – hardware bench test skipped");
    }

    void simulationFirmwareEndToEnd() {
        SerialTransport transport(port_);
        GatewayClient client(&transport);
        QSignalSpy replies(&client, &GatewayClient::replyReceived);
        QSignalSpy events(&client, &GatewayClient::eventReceived);
        client.connectToGateway();
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), GatewayClient::State::Ready, 5000);
        if (!client.identity().isSimulation()) {
            client.disconnectFromGateway();
            QFAIL("gateway is not the SIMULATION profile – aborting, no CAN requests sent");
        }
        qInfo("identity: fw %s build %s source %s", qPrintable(client.identity().firmwareVersion),
              qPrintable(client.identity().buildId), qPrintable(client.identity().source));

        const GatewayReply dtc = waitForReply(replies, client.send(QStringLiteral("READ_DTC")));
        QVERIFY2(dtc.ok, qPrintable(dtc.errorText()));
        QCOMPARE(dtc.value(QStringLiteral("dtcs")), QStringLiteral("P1093,P0087,P3FFF"));

        QVERIFY(waitForReply(replies, client.send(QStringLiteral("LIVE_START"), {QStringLiteral("500")})).ok);
        QTest::qWait(1200);
        QVERIFY(waitForReply(replies, client.send(QStringLiteral("LIVE_STOP"))).ok);
        int live = 0;
        for (const auto& args : events) {
            if (args.at(0).value<GatewayEvent>().kind == QLatin1String("LIVE")) ++live;
        }
        qInfo("live samples in 1.2 s: %d", live);
        QVERIFY(live >= 8);

        // Keep-alive must hold the gateway session past its 5 s timeout.
        QTest::qWait(6000);
        const GatewayReply status = waitForReply(replies, client.send(QStringLiteral("STATUS")));
        QVERIFY(status.ok);
        QCOMPARE(status.value(QStringLiteral("tx")), QStringLiteral("0"));  // idle → listen-only
        QCOMPARE(client.state(), GatewayClient::State::Ready);
        qInfo("framing: frames=%u crc=%u malformed=%u noise=%u", client.framingCounters().frames,
              client.framingCounters().badChecksum, client.framingCounters().malformed,
              client.framingCounters().noiseBytes);

        QSignalSpy lost(&client, &GatewayClient::connectionLost);
        client.disconnectFromGateway();
        QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, 3000);
    }
};

QTEST_GUILESS_MAIN(SerialBenchTest)
#include "test_serial_bench.moc"
