// Qt Test suite: GatewayClient against SimulationTransport (real gateway-core + SimulatedEcu).
// SIMULATION evidence only – no serial port, no ESP32, no vehicle.

#include <QSignalSpy>
#include <QTest>

#include "gateway/gateway_client.hpp"
#include "proto/crc16.hpp"
#include "transport/simulation_transport.hpp"

namespace {

/// Waits until a reply with @p id was emitted and returns it.
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
    return GatewayReply{};  // id 0 marks "not received"
}

struct Rig {
    explicit Rig(gw::EcuScenario scenario = gw::EcuScenario::Normal, gw::GatewayConfig config = {})
        : transport(nullptr, config), client(&transport), replies(&client, &GatewayClient::replyReceived) {
        transport.setScenario(scenario);
    }
    bool connect() {
        client.connectToGateway();
        return QTest::qWaitFor([this] { return client.state() == GatewayClient::State::Ready; }, 3000);
    }
    SimulationTransport transport;
    GatewayClient client;
    QSignalSpy replies;
};

}  // namespace

/// Answers HELLO like the BENCH self-test firmware does (source=BENCH).
class BenchFirmwareTransport final : public Transport {
public:
    QString description() const override { return QStringLiteral("fake bench"); }
    bool open() override { return open_ = true; }
    void close() override {
        if (!open_) return;
        open_ = false;
        QMetaObject::invokeMethod(this, [this] { emit closed(QStringLiteral("closed by user")); }, Qt::QueuedConnection);
    }
    bool isOpen() const override { return open_; }
    void write(const QByteArray& bytes) override {
        proto::FrameReader reader;
        reader.feed(bytes.constData(), static_cast<std::size_t>(bytes.size()), [this](std::string_view body) {
            const auto parsed = proto::parseBody(body);
            if (!parsed.ok()) return;
            proto::FrameWriter writer;
            proto::beginOk(writer, parsed.value().id).keyValue("proto", 1u).keyValue("source", "BENCH").keyValue("mode04", 0u);
            const std::string_view frame = writer.finish();
            const QByteArray reply(frame.data(), static_cast<qsizetype>(frame.size()));
            QMetaObject::invokeMethod(this, [this, reply] { emit bytesReceived(reply); }, Qt::QueuedConnection);
        });
    }

private:
    bool open_ = false;
};

class GatewayClientTest : public QObject {
    Q_OBJECT
private slots:
    void benchFirmwareIsRefusedForDiagnostics() {
        BenchFirmwareTransport transport;
        GatewayClient client(&transport);
        QSignalSpy failed(&client, &GatewayClient::connectionFailed);
        client.connectToGateway();
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.at(0).at(0).toString().contains(QStringLiteral("source=BENCH")));
        QTRY_COMPARE(client.state(), GatewayClient::State::Disconnected);
        QVERIFY(!transport.isOpen());
    }

    void handshakeReportsSimulationIdentity() {
        Rig rig;
        QSignalSpy identity(&rig.client, &GatewayClient::identityReady);
        QVERIFY(rig.connect());
        QCOMPARE(identity.count(), 1);
        const GatewayIdentity id = rig.client.identity();
        QVERIFY(id.isSimulation());
        QCOMPARE(id.protocolVersion, 1);
        QCOMPARE(id.requestId, QStringLiteral("7E0"));
        QCOMPARE(id.responseId, QStringLiteral("7E8"));
        QCOMPARE(id.bitrate, 500000u);
        QVERIFY(!id.mode04Available);
    }

    void readDtcEndToEnd() {
        Rig rig;
        QVERIFY(rig.connect());
        const quint32 id = rig.client.send(QStringLiteral("READ_DTC"));
        const GatewayReply reply = waitForReply(rig.replies, id);
        QCOMPARE(reply.id, id);
        QVERIFY2(reply.ok, qPrintable(reply.errorText()));
        QCOMPARE(reply.value(QStringLiteral("dtcs")), QStringLiteral("P1093,P0087,P3FFF"));
        QCOMPARE(reply.value(QStringLiteral("count")), QStringLiteral("3"));
    }

    void manyDtcsAreMergedFromParts() {
        Rig rig(gw::EcuScenario::ManyDtcs);
        QVERIFY(rig.connect());
        const quint32 id = rig.client.send(QStringLiteral("READ_DTC"));
        const GatewayReply reply = waitForReply(rig.replies, id);
        QVERIFY2(reply.ok, qPrintable(reply.errorText()));
        QCOMPARE(reply.value(QStringLiteral("dtcs")).split(QLatin1Char(',')).size(), 40);
        QVERIFY(reply.value(QStringLiteral("dtcs")).startsWith(QStringLiteral("P0100,P0101")));
    }

    void negativeResponseKeepsStageAndNrc() {
        Rig rig(gw::EcuScenario::NegativeResponse);
        QVERIFY(rig.connect());
        const GatewayReply reply = waitForReply(rig.replies, rig.client.send(QStringLiteral("READ_DTC")));
        QVERIFY(!reply.ok);
        QCOMPARE(reply.error.stage, diag::Stage::NegativeResponse);
        QCOMPARE(reply.error.detail, 0x22u);
        QCOMPARE(reply.errorText(), QStringLiteral("NRC/NEGATIVE_RESPONSE/22"));
    }

    void silentLinkTimesOutOnDesktopSide() {
        Rig rig;
        QVERIFY(rig.connect());
        rig.transport.setLinkMuted(true);
        const quint32 id = rig.client.send(QStringLiteral("READ_DTC"), {}, 300);
        const GatewayReply reply = waitForReply(rig.replies, id, 2000);
        QCOMPARE(reply.id, id);
        QCOMPARE(reply.error, diag::fail(diag::Stage::SerialTransport, diag::Code::Timeout));
    }

    void unplugFailsPendingRequestsAndReportsLoss() {
        Rig rig(gw::EcuScenario::Silent);  // keep the request pending
        QVERIFY(rig.connect());
        QSignalSpy lost(&rig.client, &GatewayClient::connectionLost);
        const quint32 id = rig.client.send(QStringLiteral("READ_PID"), {QStringLiteral("0C")});
        rig.transport.simulateUnplug();
        const GatewayReply reply = waitForReply(rig.replies, id);
        QCOMPARE(reply.error, diag::fail(diag::Stage::SerialTransport, diag::Code::Disconnected));
        QTRY_COMPARE(lost.count(), 1);
        QCOMPARE(lost.at(0).at(0).toString(), QStringLiteral("device removed"));
        QCOMPARE(rig.client.state(), GatewayClient::State::Disconnected);
    }

    void requestWhileDisconnectedFailsAsynchronously() {
        Rig rig;
        const quint32 id = rig.client.send(QStringLiteral("READ_DTC"));
        QCOMPARE(rig.replies.count(), 0);  // never synchronous
        const GatewayReply reply = waitForReply(rig.replies, id);
        QCOMPARE(reply.error.code, diag::Code::Disconnected);
    }

    void noiseAndUnknownResponsesAreSurvivable() {
        Rig rig;
        QVERIFY(rig.connect());
        QSignalSpy warnings(&rig.client, &GatewayClient::protocolWarning);
        rig.transport.injectToDesktop("I (12) boot: garbage\n#RES|1|OK*0000\n");
        rig.transport.injectToDesktop("#RES|999|OK|pong*" + QByteArray::number(
            proto::crc16CcittFalse("RES|999|OK|pong", 15), 16).toUpper().rightJustified(4, '0') + "\n");
        QTRY_VERIFY(warnings.count() >= 1);  // unknown id 999 reported, not crashed
        QVERIFY(rig.client.framingCounters().badChecksum >= 1);
        const GatewayReply reply = waitForReply(rig.replies, rig.client.send(QStringLiteral("PING")));
        QVERIFY(reply.ok);
    }

    void keepAliveHoldsTheGatewaySession() {
        gw::GatewayConfig config;
        config.sessionTimeoutMs = 600;
        Rig rig(gw::EcuScenario::Normal, config);
        rig.client.setKeepAliveInterval(200);
        QVERIFY(rig.connect());
        QTest::qWait(1500);  // > 2× session timeout
        QVERIFY(rig.transport.gateway()->sessionActive());
    }

    void gatewayRebootRepeatsHandshake() {
        Rig rig;
        QVERIFY(rig.connect());
        QSignalSpy identity(&rig.client, &GatewayClient::identityReady);
        proto::FrameWriter writer;
        proto::beginEvent(writer, 0, "BOOT").keyValue("proto", 1u).keyValue("source", "SIMULATION");
        const std::string_view boot = writer.finish();
        rig.transport.injectToDesktop(QByteArray(boot.data(), static_cast<qsizetype>(boot.size())));
        QTRY_COMPARE(identity.count(), 1);
        QCOMPARE(rig.client.state(), GatewayClient::State::Ready);
    }

    void liveEventsArrive() {
        Rig rig;
        QVERIFY(rig.connect());
        QSignalSpy events(&rig.client, &GatewayClient::eventReceived);
        QVERIFY(waitForReply(rig.replies, rig.client.send(QStringLiteral("LIVE_START"), {QStringLiteral("500")})).ok);
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (const auto& args : events) {
                    const auto event = args.at(0).value<GatewayEvent>();
                    if (event.kind == QLatin1String("LIVE") && event.fields.value(0) == QLatin1String("RPM")) return true;
                }
                return false;
            }(),
            3000);
    }
};

QTEST_GUILESS_MAIN(GatewayClientTest)
#include "test_gateway_client.moc"
