// Qt Test: LiveDataService over the simulated gateway (real gateway-core polling + SimulatedEcu).
// SIMULATION evidence only.

#include <QSignalSpy>
#include <QTest>

#include "gateway/gateway_client.hpp"
#include "services/live_data_service.hpp"
#include "transport/simulation_transport.hpp"

using State = LiveParameter::State;

namespace {

struct LiveRig {
    explicit LiveRig(gw::EcuScenario scenario) : client(&transport), live(&client) {
        transport.setScenario(scenario);
        client.connectToGateway();
        ready = QTest::qWaitFor([this] { return client.state() == GatewayClient::State::Ready; }, 3000);
    }
    const LiveParameter& param(const QString& key) const {
        for (const LiveParameter& p : live.parameters()) {
            if (p.key == key) return p;
        }
        static const LiveParameter none;
        return none;
    }
    bool allIn(State state) const {
        for (const LiveParameter& p : live.parameters()) {
            if (p.state != state) return false;
        }
        return true;
    }
    SimulationTransport transport;
    GatewayClient client;
    LiveDataService live;
    bool ready = false;
};

}  // namespace

class LiveDataTest : public QObject {
    Q_OBJECT
private slots:
    void exposesTheFiveMvpParameters() {
        LiveRig rig(gw::EcuScenario::Normal);
        QCOMPARE(rig.live.parameters().size(), 5);
        QVERIFY(rig.allIn(State::NoData));
        QCOMPARE(rig.param(QStringLiteral("COOLANT_TEMP")).unit, QStringLiteral("°C"));
        QCOMPARE(rig.param(QStringLiteral("MODULE_VOLTAGE")).displayName, QStringLiteral("Control module voltage"));
        QCOMPARE(rig.param(QStringLiteral("RPM")).pid, quint8(0x0C));
    }

    void startMakesAllParametersFreshWithSource() {
        LiveRig rig(gw::EcuScenario::Normal);
        QVERIFY(rig.ready);
        QSignalSpy started(&rig.live, &LiveDataService::started);
        rig.live.start(500);
        QTRY_COMPARE(started.count(), 1);
        QVERIFY(rig.live.running());
        QTRY_VERIFY_WITH_TIMEOUT(rig.allIn(State::Fresh), 3000);
        const LiveParameter& rpm = rig.param(QStringLiteral("RPM"));
        QVERIFY(rpm.hasValue);
        QVERIFY(rpm.value >= 750.0 && rpm.value <= 2500.0);  // SimulatedEcu RPM profile
        QCOMPARE(rpm.source, QStringLiteral("SIMULATION"));
        QVERIFY(rpm.gatewayTimeMs > 0);
    }

    void stoppedValuesBecomeStaleButKeepTheirValue() {
        LiveRig rig(gw::EcuScenario::Normal);
        QVERIFY(rig.ready);
        rig.live.setStaleAfterMs(400);
        QSignalSpy stopped(&rig.live, &LiveDataService::stopped);
        rig.live.start(500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.allIn(State::Fresh), 3000);
        rig.live.stop();
        QTRY_COMPARE(stopped.count(), 1);
        QCOMPARE(stopped.at(0).at(0).toString(), QStringLiteral("requested"));
        QTRY_VERIFY_WITH_TIMEOUT(rig.allIn(State::Stale), 2000);
        QVERIFY(rig.param(QStringLiteral("SPEED")).hasValue);
    }

    void unsupportedPidIsMarkedAndOthersContinue() {
        LiveRig rig(gw::EcuScenario::UnsupportedVoltage);
        QVERIFY(rig.ready);
        rig.live.start(500);
        QTRY_COMPARE_WITH_TIMEOUT(rig.param(QStringLiteral("MODULE_VOLTAGE")).state, State::Unsupported, 3000);
        QCOMPARE(rig.param(QStringLiteral("MODULE_VOLTAGE")).lastError.detail, 0x31u);
        QTRY_COMPARE_WITH_TIMEOUT(rig.param(QStringLiteral("RPM")).state, State::Fresh, 3000);
        QTest::qWait(1200);
        QVERIFY(rig.live.running());
        QCOMPARE(rig.param(QStringLiteral("MODULE_VOLTAGE")).state, State::Unsupported);  // not retried
    }

    void silentEcuGivesErrorsAndGatewayStopsPolling() {
        LiveRig rig(gw::EcuScenario::Silent);
        QVERIFY(rig.ready);
        QSignalSpy stopped(&rig.live, &LiveDataService::stopped);
        rig.live.start(500);
        QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 4000);
        QCOMPARE(stopped.at(0).at(0).toString(), QStringLiteral("no_response"));
        QVERIFY(!rig.live.running());
        const LiveParameter& first = rig.live.parameters().first();
        QCOMPARE(first.state, State::Error);
        QCOMPARE(first.lastError.stage, diag::Stage::RxTimeout);
        QVERIFY(!first.hasValue);  // an error never invents a value
    }

    void negativeResponseIsErrorNotUnsupported() {
        LiveRig rig(gw::EcuScenario::NegativeResponse);  // NRC 0x22 conditionsNotCorrect
        QVERIFY(rig.ready);
        rig.live.start(500);
        QTRY_COMPARE_WITH_TIMEOUT(rig.param(QStringLiteral("ENGINE_LOAD")).state, State::Error, 3000);
        QCOMPARE(rig.param(QStringLiteral("ENGINE_LOAD")).lastError.detail, 0x22u);
    }

    void disconnectStopsAndAgesImmediately() {
        LiveRig rig(gw::EcuScenario::Normal);
        QVERIFY(rig.ready);
        QSignalSpy stopped(&rig.live, &LiveDataService::stopped);
        rig.live.start(500);
        QTRY_VERIFY_WITH_TIMEOUT(rig.allIn(State::Fresh), 3000);
        rig.transport.simulateUnplug();
        QTRY_COMPARE(stopped.count(), 1);
        QCOMPARE(stopped.at(0).at(0).toString(), QStringLiteral("disconnected"));
        QVERIFY(rig.allIn(State::Stale));  // no waiting for the stale timer
    }

    void startRejectedByGatewayIsReported() {
        LiveRig rig(gw::EcuScenario::Normal);
        QVERIFY(rig.ready);
        QSignalSpy failed(&rig.live, &LiveDataService::startFailed);
        rig.live.start(100);  // below the gateway minimum (500 ms)
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).value<diag::Error>().code, diag::Code::InvalidArgument);
        QVERIFY(!rig.live.running());
    }

    void startWhileDisconnectedFails() {
        SimulationTransport transport;
        GatewayClient client(&transport);
        LiveDataService live(&client);
        QSignalSpy failed(&live, &LiveDataService::startFailed);
        live.start(1000);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(failed.at(0).at(0).value<diag::Error>().code, diag::Code::Disconnected);
    }
};

QTEST_GUILESS_MAIN(LiveDataTest)
#include "test_live_data.moc"
